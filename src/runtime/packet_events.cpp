#include "packet_events.h"

#include "client_commands.h"
#include "events.h"
#include "hook_engine.h"
#include "js/js_engine.h"
#include "live.h"
#include "log.h"
#include "native_string.h"
#include "players.h"
#include "runtime.h"

#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yoyo {
namespace {

constexpr size_t kPacketBaseSize = 48;
constexpr size_t kPacketHandlerOffset = 32;
constexpr int kPacketPlayerEquipment = 31;
constexpr int kPacketInteract = 33;
constexpr int kPacketPlayerAction = 36;
constexpr int kPacketSetHealth = 42;
constexpr int kPacketSetTime = 10;
constexpr int kPacketLevelEvent = 25;
constexpr int kPacketUpdateBlock = 21;
constexpr int kPacketRespawn = 45;
constexpr int kPacketDeathInfo = 189;
constexpr int kPacketContainerOpen = 46;
constexpr int kPacketContainerClose = 47;
constexpr int kPacketCommandRequest = 77;
constexpr int kPacketAvailableCommands = 76;
constexpr int kPacketPlayerSkin = 93;
constexpr int kPacketModalFormResponse = 101;
constexpr int kPacketEmote = 138;
constexpr int kPacketPlayerAuthInput = 144;

enum PlayerActionType : int {
    ActionStartDestroyBlock = 0,
    ActionAbortDestroyBlock = 1,
    ActionStopDestroyBlock = 2,
    ActionDropItem = 4,
    ActionStartSleeping = 5,
    ActionStopSleeping = 6,
    ActionRespawn = 7,
    ActionStartJump = 8,
    ActionStartSprinting = 9,
    ActionStopSprinting = 10,
    ActionStartSneaking = 11,
    ActionStopSneaking = 12,
    ActionCreativeDestroyBlock = 13,
    ActionHandledTeleport = 30,
    ActionStartFlying = 34,
    ActionStopFlying = 35,
    ActionStartUsingItem = 37,
    ActionInteractWithBlock = 25,
    ActionStartItemUseOn = 28,
    ActionStopItemUseOn = 29,
    ActionStartSwimming = 21,
    ActionStopSwimming = 22,
    ActionStartGliding = 15,
    ActionStopGliding = 16,
};

/* One block as reported by the server in an UpdateBlockPacket. */
struct BlockEntry {
    int x = 0;
    int y = 0;
    int z = 0;
    uint32_t runtime_id = 0;
    int flags = 0;
};

/* World state observed from outgoing packets; read-only, no engine access. */
struct WorldState {
    int time = -1;
    int weather = -1; /* 0 clear, 1 rain, 2 thunder */
    bool time_valid = false;
    bool weather_valid = false;
    /* Blocks the server has told this client about.  Bounded so a long session
     * cannot grow without limit. */
    std::unordered_map<uint64_t, BlockEntry> blocks;
};

std::mutex& world_mutex() {
    static std::mutex mutex;
    return mutex;
}

WorldState& world_state() {
    static WorldState state;
    return state;
}

struct PlayerState {
    bool initialized = false;
    bool sneaking = false;
    bool sprinting = false;
    bool flying = false;
    bool jumping = false;
    bool dead = false;
    int held_slot = -1;
    int health = -1;
};

std::mutex& state_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<void*, PlayerState>& player_states() {
    static std::unordered_map<void*, PlayerState> states;
    return states;
}

class PacketDispatcher {
public:
    virtual ~PacketDispatcher() = default;
    virtual void handle(const void* network_id, void* callback, void* packet_ref) const = 0;
};

/*
 * BDS calls IPacketHandlerDispatcher::handle after a packet has been read.
 * One wrapper per packet id is enough: the original dispatcher is shared by
 * all instances of that packet type, while the concrete packet is read from
 * the shared_ptr argument.
 */
class WrappedPacketHandler final : public PacketDispatcher {
public:
    int packet_id = 0;
    const PacketDispatcher* original = nullptr;

    void handle(const void* network_id, void* callback, void* packet_ref) const override {
        void* packet = nullptr;
        if (packet_ref != nullptr) {
            packet = *static_cast<void**>(packet_ref);
        }
        PlayerView player = player_for_network(network_id);
        bool cancelled = packet != nullptr && dispatch_packet(packet_id, packet, player);
        if (!cancelled && original != nullptr && original != this) {
            original->handle(network_id, callback, packet_ref);
        }
    }

    static PlayerView player_for_network(const void* network_id);
    static bool dispatch_packet(int packet_id, void* packet, PlayerView& player);
};

WrappedPacketHandler g_wrappers[512];
InlineHook g_create_packet_hook;
bool g_packet_hook_installed = false;

std::mutex& vtable_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::mutex& wrap_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<void*, int>& packet_vtables() {
    static std::unordered_map<void*, int> ids;
    return ids;
}

using SendPacketFn = void (*)(void*, void*);
SendPacketFn g_send_packet_original = nullptr;
bool g_send_packet_hook_installed = false;

#ifdef _WIN32
using CreatePacketFn = std::shared_ptr<void> (*)(int);
CreatePacketFn g_create_packet_original = nullptr;
#else
using CreatePacketFn = void (*)(void*, int);
CreatePacketFn g_create_packet_original = nullptr;
#endif

template <typename T>
T read_value(const void* base, size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof(value));
    return value;
}

bool read_block_pos(const void* payload, size_t offset, int& x, int& y, int& z) {
    x = read_value<int32_t>(payload, offset);
    y = read_value<int32_t>(payload, offset + 4);
    z = read_value<int32_t>(payload, offset + 8);
    if (x < -30000000 || x > 30000000 || z < -30000000 || z > 30000000 || y < -2048 || y > 2048) {
        x = y = z = 0;
        return false;
    }
    return true;
}

std::string read_string_at(const void* base, size_t offset) {
    auto view = read_native_string(static_cast<const unsigned char*>(base) + offset, native_abi());
    return std::string(view);
}

std::string uuid_hex(const void* data) {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string result;
    result.reserve(32);
    for (int i = 0; i < 16; ++i) {
        result.push_back(kHex[bytes[i] >> 4]);
        result.push_back(kHex[bytes[i] & 0x0F]);
    }
    return result;
}

struct FormResponse {
    std::string text;
    int cancel_reason = -1;
    bool has_text = false;
};

FormResponse read_form_response(const void* payload) {
    /*
     * ModalFormResponsePacket payload:
     *   +0  uint32 form_id
     *   +8  optional<string> response
     *   +8+sizeof(optional<string>) optional<uint8> cancel_reason
     *
     * Both MSVC and libc++ store the value before the engaged flag for
     * optional<T>, so the string starts at the object's base and the flag
     * sits just past the string.  optional<uint8> likewise has the value at
     * +0 and its flag at +1.
     */
    FormResponse out;
    const auto* base = static_cast<const unsigned char*>(payload);
    const size_t string_size = native_string_size(native_abi());
    const size_t optional_string_size = string_size + 8; /* value + flag + padding */

    const auto* response = base + 8;
    const bool response_engaged = response[string_size] != 0;
    if (response_engaged) {
        out.text = read_string_at(response, 0);
        out.has_text = true;
    }

    const auto* reason = base + 8 + optional_string_size;
    if (reason[1] != 0) {
        out.cancel_reason = reason[0];
    }
    return out;
}

PlayerView make_player_view(void* handle) {
    PlayerView view;
    if (handle == nullptr) {
        return view;
    }
    view.handle = handle;
    if (auto info = players_find_by_handle(handle); info.has_value()) {
        view.name = info->name;
        view.xuid = info->xuid;
        view.address = info->address;
        view.runtime_id = info->runtime_id;
    }
    if (view.runtime_id == 0) {
        view.runtime_id = live_runtime_id(handle);
    }
    return view;
}

PlayerView WrappedPacketHandler::player_for_network(const void* network_id) {
    return make_player_view(live_find_player_for_network(network_id));
}

void update_state(void* handle, const PlayerState& next) {
    if (handle == nullptr) {
        return;
    }
    std::lock_guard lock(state_mutex());
    player_states()[handle] = next;
}

bool state_for(void* handle, PlayerState& out) {
    if (handle == nullptr) {
        return false;
    }
    std::lock_guard lock(state_mutex());
    auto it = player_states().find(handle);
    if (it == player_states().end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool detail_for_action(int action, int packet_id, const int pos[3], int face, uint64_t runtime_id,
                       int& type, DetailEvent& detail, bool& cancellable) {
    cancellable = false;
    detail.packet_id = packet_id;
    detail.action = action;
    detail.x = pos[0];
    detail.y = pos[1];
    detail.z = pos[2];
    detail.face = face;
    detail.runtime_id = runtime_id;

    switch (action) {
    case ActionStartJump:
        type = YOYO_EVENT_PLAYER_JUMP;
        return true;
    case ActionStartSprinting:
        type = YOYO_EVENT_PLAYER_SPRINT;
        detail.value = 1;
        return true;
    case ActionStopSprinting:
        type = YOYO_EVENT_PLAYER_SPRINT;
        detail.value = 0;
        return true;
    case ActionStartSneaking:
        type = YOYO_EVENT_PLAYER_SNEAK;
        detail.value = 1;
        return true;
    case ActionStopSneaking:
        type = YOYO_EVENT_PLAYER_SNEAK;
        detail.value = 0;
        return true;
    case ActionStartDestroyBlock:
        type = YOYO_EVENT_BLOCK_START_DESTROY;
        cancellable = true;
        return true;
    case ActionStopDestroyBlock:
    case ActionCreativeDestroyBlock:
        type = YOYO_EVENT_BLOCK_DESTROY;
        cancellable = true;
        return true;
    case ActionInteractWithBlock:
        type = YOYO_EVENT_BLOCK_INTERACT;
        cancellable = true;
        return true;
    case ActionStartUsingItem:
        type = YOYO_EVENT_ITEM_USE;
        cancellable = true;
        return true;
    case ActionStartItemUseOn:
        type = YOYO_EVENT_ITEM_USE_ON;
        cancellable = true;
        return true;
    case ActionDropItem:
        type = YOYO_EVENT_ITEM_DROP;
        cancellable = true;
        return true;
    case ActionStartSleeping:
        type = YOYO_EVENT_BED_ENTER;
        cancellable = true;
        return true;
    case ActionStopSleeping:
        type = YOYO_EVENT_BED_LEAVE;
        cancellable = true;
        return true;
    case ActionRespawn:
        type = YOYO_EVENT_PLAYER_RESPAWN;
        cancellable = true;
        return true;
    case ActionHandledTeleport:
        type = YOYO_EVENT_PLAYER_TELEPORT;
        return true;
    case 31: /* MissedSwing */
        type = YOYO_EVENT_PLAYER_INTERACT;
        return true;
    default:
        return false;
    }
}

void handle_player_action(void* packet, PlayerView& player, bool& cancelled) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    int pos[3] = {0, 0, 0};
    read_block_pos(payload, 0, pos[0], pos[1], pos[2]);
    if (read_value<int32_t>(payload, 28) == 31) {
        LiveVec3 current;
        if (live_get_position(player.handle, current)) {
            pos[0] = static_cast<int>(current.x);
            pos[1] = static_cast<int>(current.y);
            pos[2] = static_cast<int>(current.z);
        }
    }
    const int face = read_value<int32_t>(payload, 24);
    const int action = read_value<int32_t>(payload, 28);
    const uint64_t runtime_id = read_value<uint64_t>(payload, 32);

    PlayerState state;
    const bool known = state_for(player.handle, state);
    const bool was_sneaking = known && state.sneaking;
    const bool was_sprinting = known && state.sprinting;
    const bool was_jumping = known && state.jumping;
    if (known) {
        if (action == ActionStartSneaking) state.sneaking = true;
        if (action == ActionStopSneaking) state.sneaking = false;
        if (action == ActionStartSprinting) state.sprinting = true;
        if (action == ActionStopSprinting) state.sprinting = false;
        if (action == ActionStartFlying) state.flying = true;
        if (action == ActionStopFlying) state.flying = false;
        if (action == ActionStartJump) state.jumping = true;
        update_state(player.handle, state);
    }

    DetailEvent raw;
    raw.packet_id = kPacketPlayerAction;
    raw.action = action;
    raw.x = pos[0];
    raw.y = pos[1];
    raw.z = pos[2];
    raw.face = face;
    raw.runtime_id = runtime_id;
    if (emit_detail(YOYO_EVENT_PLAYER_ACTION, player, raw)) {
        cancelled = true;
        return;
    }

    if ((action == ActionStartSneaking && was_sneaking) ||
        (action == ActionStopSneaking && !was_sneaking) ||
        (action == ActionStartSprinting && was_sprinting) ||
        (action == ActionStopSprinting && !was_sprinting) ||
        (action == ActionStartJump && was_jumping)) {
        return;
    }

    int type = 0;
    DetailEvent detail;
    bool cancellable = false;
    if (!detail_for_action(action, kPacketPlayerAction, pos, face, runtime_id, type, detail, cancellable)) {
        return;
    }
    if (emit_detail(type, player, detail) && cancellable) {
        cancelled = true;
    }
}

void handle_auth_input(void* packet, PlayerView& player) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    const auto* bits = payload + 88;
    uint64_t low = 0;
    uint16_t high = 0;
    std::memcpy(&low, bits, sizeof(low));
    std::memcpy(&high, bits + 8, sizeof(high));
    const auto test = [&](int bit) -> bool {
        if (bit < 64) {
            return (low & (uint64_t{1} << bit)) != 0;
        }
        return (high & (uint16_t{1} << (bit - 64))) != 0;
    };

    PlayerState state;
    if (!state_for(player.handle, state)) {
        state.initialized = true;
    }
    const bool sneaking = test(8);
    const bool sprinting = test(20);
    const bool flying = test(42);
    const bool jumping = test(31) || test(6);

    if (state.initialized) {
        if (sneaking != state.sneaking) {
            DetailEvent detail;
            detail.packet_id = kPacketPlayerAuthInput;
            detail.action = sneaking ? ActionStartSneaking : ActionStopSneaking;
            detail.value = sneaking ? 1 : 0;
            detail.x = static_cast<int>(read_value<float>(payload, 8));
            detail.y = static_cast<int>(read_value<float>(payload, 12));
            detail.z = static_cast<int>(read_value<float>(payload, 16));
            emit_detail(YOYO_EVENT_PLAYER_SNEAK, player, detail);
        }
        if (sprinting != state.sprinting) {
            DetailEvent detail;
            detail.packet_id = kPacketPlayerAuthInput;
            detail.action = sprinting ? ActionStartSprinting : ActionStopSprinting;
            detail.value = sprinting ? 1 : 0;
            emit_detail(YOYO_EVENT_PLAYER_SPRINT, player, detail);
        }
        if (jumping && !state.jumping) {
            DetailEvent detail;
            detail.packet_id = kPacketPlayerAuthInput;
            detail.action = ActionStartJump;
            detail.x = static_cast<int>(read_value<float>(payload, 8));
            detail.y = static_cast<int>(read_value<float>(payload, 12));
            detail.z = static_cast<int>(read_value<float>(payload, 16));
            emit_detail(YOYO_EVENT_PLAYER_JUMP, player, detail);
        }
    }
    state.initialized = true;
    state.sneaking = sneaking;
    state.sprinting = sprinting;
    state.flying = flying;
    state.jumping = jumping;
    update_state(player.handle, state);
}

void handle_player_equipment(void* packet, PlayerView& player) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    /*
     * The tail of MobEquipmentPacketPayload is `slot, selected_slot,
     * container_id`.  Descriptor sizes differ between ABI versions, so look
     * for the adjacent pair of small slot values instead of hard-coding an
     * offset that would silently read item data.
     */
    int slot = -1;
    int selected = -1;
    for (int offset = 72; offset + 8 <= 120; offset += 4) {
        const int a = read_value<int32_t>(payload, static_cast<size_t>(offset));
        const int b = read_value<int32_t>(payload, static_cast<size_t>(offset) + 4);
        if (a >= 0 && a <= 8 && b >= 0 && b <= 8) {
            slot = a;
            selected = b;
            break;
        }
    }
    PlayerState state;
    if (!state_for(player.handle, state)) {
        state.initialized = true;
    }
    state.held_slot = selected;
    update_state(player.handle, state);
    DetailEvent detail;
    detail.packet_id = kPacketPlayerEquipment;
    detail.slot = selected;
    detail.value = slot;
    emit_detail(YOYO_EVENT_PLAYER_ITEM_HELD, player, detail);
}

void handle_form_response(void* packet, PlayerView& player, bool& cancelled) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    const int32_t form_id = read_value<int32_t>(payload, 0);
    const FormResponse response = read_form_response(payload);
    DetailEvent detail;
    detail.packet_id = kPacketModalFormResponse;
    detail.value = form_id;
    detail.text = response.text;
    if (emit_detail(YOYO_EVENT_FORM_RESPONSE, player, detail)) {
        cancelled = true;
    }
    if (player.handle != nullptr && form_id > 0) {
        js::dispatch_form_response(static_cast<uint32_t>(form_id), player, response.text,
                                   response.cancel_reason);
    }
}

void handle_command(void* packet, PlayerView& player, bool& cancelled) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    std::string command = read_string_at(payload, 0);

    /*
     * Intercept before BDS parses it.  A plugin command is fully handled here
     * and the packet is dropped, so the vanilla command dispatcher never sees
     * an unknown command.  Unregistered commands fall through untouched.
     */
    if (player.handle != nullptr && !command.empty() && js::has_player_command(command)) {
        std::string output;
        if (js::run_player_command(player, command, output)) {
            if (!output.empty()) {
                live_send_text(player.handle, output, LiveTextType::Raw);
            }
            cancelled = true;
        }
        return;
    }

    DetailEvent detail;
    detail.packet_id = kPacketCommandRequest;
    detail.text = command;
    if (emit_detail(YOYO_EVENT_PLAYER_COMMAND, player, detail)) {
        cancelled = true;
    }
}

void handle_emote(void* packet, PlayerView& player) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    DetailEvent detail;
    detail.packet_id = kPacketEmote;
    detail.text = read_string_at(payload, 8);
    detail.text2 = read_string_at(payload, 48);
    detail.value = read_value<uint8_t>(payload, 112);
    emit_detail(YOYO_EVENT_PLAYER_EMOTE, player, detail);
}

void handle_skin(void* packet, PlayerView& player) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    DetailEvent detail;
    detail.packet_id = kPacketPlayerSkin;
    detail.text = uuid_hex(payload);
    emit_detail(YOYO_EVENT_PLAYER_SKIN, player, detail);
}

void handle_respawn(void* packet, PlayerView& player) {
    PlayerState state;
    if (state_for(player.handle, state)) {
        state.dead = false;
        state.health = -1;
        update_state(player.handle, state);
    }
    DetailEvent detail;
    detail.packet_id = kPacketRespawn;
    (void)packet;
    emit_detail(YOYO_EVENT_PLAYER_RESPAWN, player, detail);
}

bool WrappedPacketHandler::dispatch_packet(int packet_id, void* packet, PlayerView& player) {
    if (std::getenv("YOYO_DEBUG_CMD") != nullptr) {
        log_write(LogLevel::Info, "dispatch_packet: id=%d player=%p", packet_id, player.handle);
    }
    if (packet == nullptr) {
        return false;
    }

    DetailEvent generic;
    generic.packet_id = packet_id;
    if (emit_detail(YOYO_EVENT_PACKET_RECEIVE, player, generic)) {
        return true;
    }

    bool cancelled = false;
    switch (packet_id) {
    case kPacketPlayerAction:
        if (player.handle != nullptr) {
            handle_player_action(packet, player, cancelled);
        }
        break;
    case kPacketPlayerAuthInput:
        if (player.handle != nullptr) {
            handle_auth_input(packet, player);
        }
        break;
    case kPacketPlayerEquipment:
        if (player.handle != nullptr) {
            handle_player_equipment(packet, player);
        }
        break;
    case kPacketModalFormResponse:
        if (player.handle != nullptr) {
            handle_form_response(packet, player, cancelled);
        }
        break;
    case kPacketCommandRequest:
        if (player.handle != nullptr) {
            handle_command(packet, player, cancelled);
        }
        break;
    case kPacketEmote:
        if (player.handle != nullptr) {
            handle_emote(packet, player);
        }
        break;
    case kPacketPlayerSkin:
        if (player.handle != nullptr) {
            handle_skin(packet, player);
        }
        break;
    case kPacketRespawn:
        if (player.handle != nullptr) {
            handle_respawn(packet, player);
        }
        break;
    case kPacketInteract:
    case kPacketContainerOpen:
    case kPacketContainerClose:
    default:
        break;
    }
    return cancelled;
}

void remember_packet_vtable(void* packet, int packet_id) {
    if (packet == nullptr) {
        return;
    }
    void* vtable = *static_cast<void**>(packet);
    if (vtable == nullptr) {
        return;
    }
    std::lock_guard lock(vtable_mutex());
    packet_vtables()[vtable] = packet_id;
}

int packet_id_for(void* packet) {
    if (packet == nullptr) {
        return -1;
    }
    void* vtable = *static_cast<void**>(packet);
    std::lock_guard lock(vtable_mutex());
    auto it = packet_vtables().find(vtable);
    return it == packet_vtables().end() ? -1 : it->second;
}

bool is_bulk_packet(int packet_id) {
    switch (packet_id) {
    case 39:  /* SetActorData */
    case 58:  /* FullChunkData */
    case 121: /* NetworkChunkPublisherUpdate */
    case 122: /* BiomeDefinitionList */
    case 123: /* LevelSoundEvent */
    case 124: /* LevelEventGeneric */
    case 130: /* OnScreenTextureAnimation */
    case 172: /* UpdateSubChunkBlocks */
    case 174: /* SubChunkPacket */
    case 175: /* SubChunkRequestPacket */
        return true;
    default:
        return false;
    }
}

void handle_outgoing_health(PlayerView& player, void* packet) {
    const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
    const int health = read_value<int32_t>(payload, 0);
    PlayerState state;
    if (!state_for(player.handle, state)) {
        state.initialized = true;
    }
    const bool changed = state.health != health;
    const bool was_dead = state.dead;
    state.health = health;
    state.dead = health <= 0;
    update_state(player.handle, state);
    if (changed) {
        DetailEvent detail;
        detail.packet_id = kPacketSetHealth;
        detail.value = health;
        emit_detail(YOYO_EVENT_PLAYER_HEALTH, player, detail);
    }
    if (health <= 0 && !was_dead) {
        DetailEvent detail;
        detail.packet_id = kPacketSetHealth;
        emit_detail(YOYO_EVENT_PLAYER_DIE, player, detail);
    }
}

void send_packet_detour(void* player_handle, void* packet) {
    const int packet_id = packet_id_for(packet);
    if (std::getenv("YOYO_DEBUG_SEND") != nullptr) {
        log_write(LogLevel::Info, "send hook: player=%p packet=%p id=%d", player_handle, packet, packet_id);
    }
    bool cancelled = false;
    if (player_handle != nullptr && packet_id >= 0) {
        PlayerView player = make_player_view(player_handle);
        if (packet_id == kPacketSetTime) {
            const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
            const int ticks = read_value<int32_t>(payload, 0);
            std::lock_guard lock(world_mutex());
            world_state().time = ticks;
            world_state().time_valid = true;
        } else if (packet_id == kPacketLevelEvent) {
            /* LevelEventPacket: int16 event id then floats; 3001/3002 rain,
             * 3003 thunder. */
            const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
            const int16_t event_id = read_value<int16_t>(payload, 0);
            std::lock_guard lock(world_mutex());
            if (event_id == 3001) {
                world_state().weather = 1;
                world_state().weather_valid = true;
            } else if (event_id == 3002) {
                world_state().weather = 0;
                world_state().weather_valid = true;
            } else if (event_id == 3003) {
                world_state().weather = 2;
                world_state().weather_valid = true;
            } else if (event_id == 1003) {
                world_state().weather = world_state().weather == 1 ? 1 : 0;
                world_state().weather_valid = true;
            }
        } else if (packet_id == kPacketUpdateBlock) {
            /*
             * UpdateBlockPacket payload:
             *   +0  BlockPos position (int32 x, y, z)
             *   +12 uint32 block runtime id
             *   +16 uint32 flags
             *   +20 uint32 layer
             */
            const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
            BlockEntry entry;
            entry.x = read_value<int32_t>(payload, 0);
            entry.y = read_value<int32_t>(payload, 4);
            entry.z = read_value<int32_t>(payload, 8);
            entry.runtime_id = read_value<uint32_t>(payload, 12);
            entry.flags = read_value<int32_t>(payload, 16);
            if (entry.y >= -512 && entry.y <= 512 && std::abs(entry.x) < 30000000 &&
                std::abs(entry.z) < 30000000) {
                std::lock_guard lock(world_mutex());
                auto& blocks = world_state().blocks;
                if (blocks.size() > 65536) {
                    blocks.clear();
                }
                const uint64_t key = (static_cast<uint64_t>(entry.x & 0x3FFFFFF) << 38) |
                                     (static_cast<uint64_t>(entry.z & 0x3FFFFFF) << 12) |
                                     static_cast<uint64_t>(entry.y + 512);
                blocks[key] = entry;
            }
        } else if (packet_id == kPacketAvailableCommands) {
            /*
             * The client's command list comes from this packet. Keep the
             * payload so plugin commands can be merged into it later.
             */
            log_write(LogLevel::Info, "observed AvailableCommandsPacket (id 76)");
            client_commands_capture(packet);
        } else if (packet_id == kPacketSetHealth) {
            handle_outgoing_health(player, packet);
        } else if (packet_id == kPacketRespawn) {
            PlayerState state;
            if (state_for(player_handle, state)) {
                state.dead = false;
                state.health = -1;
                update_state(player_handle, state);
            }
        } else if (packet_id == kPacketDeathInfo) {
            PlayerState state;
            if (!state_for(player_handle, state) || !state.dead) {
                state.dead = true;
                update_state(player_handle, state);
                DetailEvent detail;
                detail.packet_id = kPacketDeathInfo;
                emit_detail(YOYO_EVENT_PLAYER_DIE, player, detail);
            }
        }
        if (!is_bulk_packet(packet_id) &&
            (event_has_listener(YOYO_EVENT_PACKET_SEND) || js::has_listener("onPacketSend"))) {
            DetailEvent detail;
            detail.packet_id = packet_id;
            cancelled = emit_detail(YOYO_EVENT_PACKET_SEND, player, detail);
        }
    }
    if (!cancelled && g_send_packet_original != nullptr) {
        g_send_packet_original(player_handle, packet);
    }
}

void wrap_packet(void* packet, int packet_id) {
    if (packet == nullptr || packet_id < 0 || packet_id >= static_cast<int>(std::size(g_wrappers))) {
        return;
    }
    remember_packet_vtable(packet, packet_id);
    std::lock_guard lock(wrap_mutex());
    auto** handler_slot = reinterpret_cast<void**>(static_cast<unsigned char*>(packet) + kPacketHandlerOffset);
    void* handler = *handler_slot;
    if (handler == nullptr) {
        return;
    }
    auto& wrapper = g_wrappers[packet_id];
    if (handler == &wrapper) {
        return;
    }
    wrapper.packet_id = packet_id;
    wrapper.original = reinterpret_cast<const PacketDispatcher*>(handler);
    *handler_slot = &wrapper;
    if (std::getenv("YOYO_DEBUG_WRAP") != nullptr) {
        log_write(LogLevel::Info, "wrap_packet: id=%d handler=%p", packet_id, handler);
    }
}

#ifdef _WIN32
std::shared_ptr<void> create_packet_detour(int packet_id) {
    auto packet = g_create_packet_original != nullptr ? g_create_packet_original(packet_id) : std::shared_ptr<void>{};
    wrap_packet(packet.get(), packet_id);
    return packet;
}
#else
void create_packet_detour(void* output, int packet_id) {
    if (g_create_packet_original == nullptr) {
        return;
    }
    g_create_packet_original(output, packet_id);
    wrap_packet(*reinterpret_cast<void**>(output), packet_id);
}
#endif


} // namespace


/*
 * CommandRequestPacket layout (verified against the 1.26.x protocol schema):
 *
 *   std::string command            (varint length + bytes)
 *   CommandOrigin origin:
 *       std::string type           ("player")
 *       mce::UUID uuid             (16 bytes)
 *       std::string request_id
 *       int64 player_entity_id
 *   bool internal
 *   std::string version            ("1")
 *
 * The command string is a native std::string, but the origin strings are
 * *wire* strings (varint-prefixed), which is why the earlier attempt crashed:
 * it wrote native strings there and the engine read garbage.
 */

/* Write a native std::string into a freshly created packet field. */
bool write_owned_string_at(void* address, std::string_view value) {
    if (address == nullptr) {
        return false;
    }
    const NativeAbi abi = native_abi();
    auto* base = static_cast<unsigned char*>(address);
    std::memset(base, 0, native_string_size(abi));
    if (value.empty()) {
        if (abi == NativeAbi::Msvc) {
            const size_t capacity = 15;
            std::memcpy(base + 24, &capacity, sizeof(capacity));
        }
        return true;
    }
    char* data = static_cast<char*>(std::malloc(value.size() + 1));
    if (data == nullptr) {
        return false;
    }
    std::memcpy(data, value.data(), value.size());
    data[value.size()] = 0;
    if (abi == NativeAbi::Msvc) {
        if (value.size() <= 15) {
            std::memcpy(base, data, value.size() + 1);
            std::free(data);
            const size_t capacity = 15;
            std::memcpy(base + 24, &capacity, sizeof(capacity));
        } else {
            std::memcpy(base, &data, sizeof(data));
            const size_t capacity = value.size();
            std::memcpy(base + 24, &capacity, sizeof(capacity));
        }
        size_t size = value.size();
        std::memcpy(base + 16, &size, sizeof(size));
    } else {
        if (value.size() <= 22) {
            base[0] = static_cast<unsigned char>(value.size() << 1);
            std::memcpy(base + 1, data, value.size() + 1);
            std::free(data);
        } else {
            const uint64_t capacity_field = (static_cast<uint64_t>(value.size()) << 1) | 1u;
            std::memcpy(base, &capacity_field, sizeof(capacity_field));
            size_t size = value.size();
            std::memcpy(base + 8, &size, sizeof(size));
            std::memcpy(base + 16, &data, sizeof(data));
        }
    }
    return true;
}

/*
 * Execute a command server-side.
 *
 * Rather than call an internal command entry point (whose signature changes
 * every build), this builds a CommandRequestPacket with the same in-memory
 * layout the server's own packet reader produces and hands it to the packet
 * dispatcher it already uses for client packets.  The layout is:
 *
 *   std::string  command
 *   struct origin { std::string type; uint8 uuid[16]; std::string request_id;
 *                   int64 player_entity_id; }
 *   bool   internal
 *   std::string version
 *
 * Every string is a native std::string; only serialisation uses the wire
 * format.  Getting this wrong is what crashed the earlier attempt, so each
 * field is written through the native-string helper and bounds are checked.
 */
bool packet_run_command(void* player, std::string_view command) {
    if (player == nullptr || command.empty()) {
        return false;
    }
    if (g_create_packet_original == nullptr) {
        log_write(LogLevel::Warn, "runcmd unavailable: packet factory hook is not installed");
        return false;
    }
    const PacketDispatcher* dispatcher = g_wrappers[kPacketCommandRequest].original;
    if (dispatcher == nullptr) {
        log_write(LogLevel::Warn, "runcmd unavailable: CommandRequest dispatcher was not captured");
        return false;
    }
    std::vector<uint8_t> network_id;
    uint8_t sub_client_id = 0;
    if (!live_get_network_id(player, network_id, sub_client_id)) {
        log_write(LogLevel::Warn, "runcmd unavailable: no NetworkIdentifier for this player");
        return false;
    }

#ifdef _WIN32
    std::shared_ptr<void> packet = g_create_packet_original(kPacketCommandRequest);
    if (!packet) {
        return false;
    }
    void* raw = packet.get();
#else
    alignas(16) unsigned char storage[16] = {};
    g_create_packet_original(storage, kPacketCommandRequest);
    void* raw = *reinterpret_cast<void**>(storage);
    if (raw == nullptr) {
        return false;
    }
#endif
    const size_t stride = native_string_size(native_abi());
    auto* payload = static_cast<unsigned char*>(raw) + kPacketBaseSize;

    /* command: the server-side parser expects the leading slash stripped,
     * but it accepts it too; normalise to always have one. */
    std::string normalized{command};
    if (!normalized.empty() && normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    if (!write_owned_string_at(payload, normalized)) {
        return false;
    }
    unsigned char* cursor = payload + stride;
    /* origin.type = "player" */
    if (!write_owned_string_at(cursor, "player")) {
        return false;
    }
    cursor += stride;
    /* origin.uuid: all zero is accepted for a server-side request. */
    std::memset(cursor, 0, 16);
    cursor += 16;
    /* origin.request_id */
    if (!write_owned_string_at(cursor, "")) {
        return false;
    }
    cursor += stride;
    /*
     * CommandOrigin is a plain struct: the int64 must be 8-byte aligned, so
     * insert padding when the string object size leaves the cursor misaligned.
     */
    while ((reinterpret_cast<uintptr_t>(cursor) & 7u) != 0) {
        *cursor++ = 0;
    }
    /*
     * origin.player_entity_id must be the *actor unique id* of this player's
     * entity.  BDS dereferences this to build the command origin; leaving it 0
     * makes the lookup fail and the server crashes right after the command
     * runs.  This was the crash.
     */
    /*
     * live_unique_id() reads an EnTT component whose hash is not stable across
     * BDS builds; on 1.26.52.3 it returns a garbage value that makes the
     * command origin invalid (and crashes the server afterwards).  The runtime
     * id is the identifier the packet layer itself uses, so use that.
     */
    const int64_t entity_id = static_cast<int64_t>(live_runtime_id(player));
    std::memcpy(cursor, &entity_id, sizeof(entity_id));
    cursor += sizeof(entity_id);
    /* internal flag */
    *cursor++ = 0;
    /* version string: the server rejects an empty version. */
    if (!write_owned_string_at(cursor, "1")) {
        return false;
    }

    PlayerView view = make_player_view(player);
    if (WrappedPacketHandler::dispatch_packet(kPacketCommandRequest, raw, view)) {
        return true;
    }
    /*
     * The dispatcher calls back into NetEventCallback; passing nullptr crashes
     * it.  ServerNetworkHandler inherits NetEventCallback and is the object the
     * server itself passes, so reuse the handler captured at login.
     */
    void* callback = live_server_handler();
    if (callback == nullptr) {
        log_write(LogLevel::Warn, "runcmd: NetEventCallback is not available yet");
        return false;
    }
#ifdef _WIN32
    /*
     * The dispatcher takes the shared_ptr by reference and may move a copy into
     * its own bookkeeping.  Hand it a separate copy so our local reset below
     * cannot drop the last reference while the server still uses it.
     */
    log_write(LogLevel::Info, "runcmd: entity_id=%lld", static_cast<long long>(entity_id));
    std::shared_ptr<void> dispatched = packet;
    dispatcher->handle(network_id.data(), callback, &dispatched);
    log_write(LogLevel::Info, "runcmd: dispatcher returned");
#else
    dispatcher->handle(network_id.data(), callback, &raw);
#endif
    return true;
}

bool install_packet_hooks() {
    if (g_packet_hook_installed) {
        return true;
    }
    const auto& target = state().symbols.create_packet;
    const auto* live = state().layout;
    if (live == nullptr || !target.valid() || live->create_packet_hook_length < 12) {
        log_write(LogLevel::Warn, "packet factory hook is not described for this build");
        return false;
    }
    void* address = reinterpret_cast<void*>(target.address);
    bool ok = false;
#ifdef _WIN32
    ok = install_inline_hook_masked(g_create_packet_hook, address, target.patch_bytes, target.patch_mask,
                                    live->create_packet_hook_length,
                                    reinterpret_cast<void*>(&create_packet_detour));
#else
    void* jcc_target = nullptr;
    if (live->create_packet_jcc_offset != 0) {
        jcc_target = static_cast<unsigned char*>(address) + live->create_packet_jcc_target_offset;
    }
    ok = install_inline_hook_short(g_create_packet_hook, address, target.patch_bytes,
                                   live->create_packet_hook_length,
                                   reinterpret_cast<void*>(&create_packet_detour),
                                   live->create_packet_jcc_offset,
                                   live->create_packet_jcc_offset != 0 ? 2 : 0,
                                   jcc_target);
#endif
    if (!ok) {
        log_write(LogLevel::Error, "failed to install MinecraftPackets::createPacket hook at rva=0x%llx",
                  static_cast<unsigned long long>(target.address - state().module.base));
        return false;
    }
    g_create_packet_original = reinterpret_cast<CreatePacketFn>(g_create_packet_hook.trampoline);
    g_packet_hook_installed = true;
    log_write(LogLevel::Info, "packet factory hook installed rva=0x%llx length=%u",
              static_cast<unsigned long long>(target.address - state().module.base),
              live->create_packet_hook_length);
    return true;
}

void* packet_send_hook_address() {
    return reinterpret_cast<void*>(&send_packet_detour);
}

void* packet_send_original_address() {
    return reinterpret_cast<void*>(g_send_packet_original);
}

bool install_packet_send_hook(void* player) {
    if (g_send_packet_hook_installed) {
        return true;
    }
    if (player == nullptr) {
        return false;
    }
    const auto* version = state().layout;
    if (version == nullptr || version->send_network_packet_slot == 0) {
        return false;
    }
    auto** vtable = *reinterpret_cast<void***>(player);
    if (vtable == nullptr) {
        return false;
    }
    const int slot = static_cast<int>(version->send_network_packet_slot / sizeof(void*));
    const char* error = nullptr;
    if (!install_vtable_hook(player, slot, reinterpret_cast<void*>(&send_packet_detour),
                             reinterpret_cast<void**>(&g_send_packet_original), nullptr, 0, &error)) {
        log_write(LogLevel::Warn, "sendNetworkPacket hook failed: %s", error ? error : "unknown error");
        return false;
    }
    g_send_packet_hook_installed = true;
    log_write(LogLevel::Info, "sendNetworkPacket vtable hook installed slot=%d", slot);
    return true;
}

bool packet_player_is_sneaking(void* player) {
    PlayerState state;
    return state_for(player, state) && state.sneaking;
}

bool packet_player_is_sprinting(void* player) {
    PlayerState state;
    return state_for(player, state) && state.sprinting;
}

bool packet_player_is_flying(void* player) {
    PlayerState state;
    return state_for(player, state) && state.flying;
}

int packet_player_held_slot(void* player) {
    PlayerState state;
    return state_for(player, state) ? state.held_slot : -1;
}

int packet_player_health(void* player) {
    PlayerState state;
    return state_for(player, state) ? state.health : -1;
}

void packet_forget_player(void* player) {
    std::lock_guard lock(state_mutex());
    player_states().erase(player);
}

bool packet_block_at(int x, int y, int z, uint32_t& runtime_id, int& flags) {
    const uint64_t key = (static_cast<uint64_t>(x & 0x3FFFFFF) << 38) |
                         (static_cast<uint64_t>(z & 0x3FFFFFF) << 12) |
                         static_cast<uint64_t>(y + 512);
    std::lock_guard lock(world_mutex());
    const auto& blocks = world_state().blocks;
    const auto it = blocks.find(key);
    if (it == blocks.end()) {
        return false;
    }
    runtime_id = it->second.runtime_id;
    flags = it->second.flags;
    return true;
}

void packet_observe_outgoing(void* player, void* packet) {
    const int packet_id = packet_id_for(packet);
    if (player == nullptr || packet_id < 0) {
        return;
    }
    PlayerView view = make_player_view(player);
    if (packet_id == kPacketSetTime) {
        const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
        const int ticks = read_value<int32_t>(payload, 0);
        std::lock_guard lock(world_mutex());
        world_state().time = ticks;
        world_state().time_valid = true;
    } else if (packet_id == kPacketLevelEvent) {
        const auto* payload = static_cast<const unsigned char*>(packet) + kPacketBaseSize;
        const int16_t event_id = read_value<int16_t>(payload, 0);
        std::lock_guard lock(world_mutex());
        if (event_id == 3001) { world_state().weather = 1; world_state().weather_valid = true; }
        else if (event_id == 3002) { world_state().weather = 0; world_state().weather_valid = true; }
        else if (event_id == 3003) { world_state().weather = 2; world_state().weather_valid = true; }
    }
    (void)view;
}

int yoyo_debug_packet_id(void* packet) {
    return packet_id_for(packet);
}

void packet_remember_vtable(void* packet, int packet_id) {
    remember_packet_vtable(packet, packet_id);
}

int packet_world_time() {
    std::lock_guard lock(world_mutex());
    return world_state().time_valid ? world_state().time : -1;
}

int packet_world_weather() {
    std::lock_guard lock(world_mutex());
    return world_state().weather_valid ? world_state().weather : -1;
}

void packet_clear() {
    std::lock_guard lock(state_mutex());
    player_states().clear();
}

} // namespace yoyo
