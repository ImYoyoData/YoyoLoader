#include "live.h"

#include "capabilities.h"
#include "hooks.h"
#include "log.h"
#include "native_string.h"
#include "packet_events.h"
#include "players.h"
#include "runtime.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace yoyo {
namespace {

constexpr int kPacketText = 9;
constexpr int kPacketSetTitle = 88;
constexpr int kPacketShowModalForm = 100;
constexpr int kPacketToastRequest = 186;
constexpr int kPacketTransfer = 85;
constexpr int kPacketLevelEvent = 25;
constexpr int kPacketSetTime = 10;
constexpr int kPacketSpawnParticleEffect = 118;
constexpr int kPacketUpdateBlock = 21;

constexpr uint32_t kRuntimeIdHash = 0xFC0DBBB5u;
constexpr uint32_t kUniqueIdHash = 0x18F957AFu;

constexpr uint32_t kPacketBaseSize = 48;
constexpr float kPlayerEyeHeight = 1.62f;

std::atomic<uint32_t> g_form_id{0};
void* g_server_handler = nullptr;

std::atomic<LayoutStatus> g_layout_status{LayoutStatus::Unknown};
std::atomic<bool> g_layout_fallback{false};

bool plausible_pointer(const void* pointer) {
    const auto value = reinterpret_cast<uintptr_t>(pointer);
    return value > 0x10000 && value < 0x00007FFFFFFFFFFFull;
}

bool plausible_position(const LiveVec3& position) {
    constexpr double kLimit = 30000000.0;
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::abs(position.x) <= kLimit && std::abs(position.z) <= kLimit &&
           std::abs(position.y) <= 4096.0;
}

struct NetworkIdEntry {
    std::vector<uint8_t> bytes;
    uint8_t sub_client_id = 0;
};

std::mutex& network_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<void*, NetworkIdEntry>& network_ids() {
    static std::unordered_map<void*, NetworkIdEntry> ids;
    return ids;
}

std::unordered_map<void*, int>& game_mode_cache() {
    static std::unordered_map<void*, int> modes;
    return modes;
}

const LiveProfile* profile() {
    return state().layout;
}

bool write_owned_string(void* address, std::string_view value) {
    if (address == nullptr || value.size() > 0x100000) {
        return false;
    }
    const NativeAbi abi = native_abi();
    const size_t object_size = native_string_size(abi);
    auto* base = static_cast<unsigned char*>(address);

    /* The packet factory always gives us default-constructed strings, but
     * freeing a long value first makes this helper safe for reused fields. */
    if (abi == NativeAbi::Msvc) {
        size_t capacity = 0;
        std::memcpy(&capacity, base + 24, sizeof(capacity));
        if (capacity >= 16) {
            char* old = nullptr;
            std::memcpy(&old, base, sizeof(old));
            std::free(old);
        }
    } else if ((base[0] & 1u) != 0) {
        char* old = nullptr;
        std::memcpy(&old, base + 16, sizeof(old));
        std::free(old);
    }
    std::memset(base, 0, object_size);

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
    data[value.size()] = '\0';

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

void free_owned_string(void* address) {
    if (address == nullptr) {
        return;
    }
    auto* base = static_cast<unsigned char*>(address);
    if (native_abi() == NativeAbi::Msvc) {
        size_t capacity = 0;
        std::memcpy(&capacity, base + 24, sizeof(capacity));
        if (capacity >= 16) {
            char* old = nullptr;
            std::memcpy(&old, base, sizeof(old));
            std::free(old);
        }
    } else if ((base[0] & 1u) != 0) {
        char* old = nullptr;
        std::memcpy(&old, base + 16, sizeof(old));
        std::free(old);
    }
    std::memset(base, 0, native_string_size(native_abi()));
}

#ifdef __linux__
void release_shared_control_block(void* control) {
    if (control == nullptr) {
        return;
    }
    auto* bytes = static_cast<unsigned char*>(control);
    auto** vtable = *reinterpret_cast<void***>(bytes);
    if (vtable == nullptr) {
        return;
    }
    /* libc++ 20 control block: vtable, shared owners (+8), weak owners (+16).
     * The shared count starts at zero, so the old value zero means the new
     * value is the expired sentinel (-1). */
    const long old_shared = __atomic_fetch_add(reinterpret_cast<long*>(bytes + 8), -1, __ATOMIC_ACQ_REL);
    if (old_shared != 0) {
        return;
    }
    auto on_zero_shared = reinterpret_cast<void (*)(void*)>(vtable[2]);
    if (on_zero_shared != nullptr) {
        on_zero_shared(control);
    }
    const long old_weak = __atomic_fetch_add(reinterpret_cast<long*>(bytes + 16), -1, __ATOMIC_ACQ_REL);
    if (old_weak == 0) {
        auto on_zero_shared_weak = reinterpret_cast<void (*)(void*)>(vtable[4]);
        if (on_zero_shared_weak != nullptr) {
            on_zero_shared_weak(control);
        }
    }
}
#endif

class NativePacket {
public:
    NativePacket() = default;
    explicit NativePacket(int id) {
        const auto* live = profile();
        if (live == nullptr || live->create_packet.rva == 0) {
            return;
        }
        auto* target = reinterpret_cast<void*>(state().module.base + live->create_packet.rva);
#ifdef _WIN32
        using CreatePacketFn = std::shared_ptr<void> (*)(int);
        auto create = reinterpret_cast<CreatePacketFn>(target);
        owner_ = create(id);
        packet_remember_vtable(owner_.get(), id);
#else
        using CreatePacketFn = void* (*)(void*, int);
        alignas(16) unsigned char storage[16] = {};
        auto create = reinterpret_cast<CreatePacketFn>(target);
        create(storage, id);
        ptr_ = *reinterpret_cast<void**>(storage);
        control_ = *reinterpret_cast<void**>(storage + 8);
        packet_remember_vtable(ptr_, id);
#endif
    }

    ~NativePacket() {
#ifdef _WIN32
        owner_.reset();
#else
        release_shared_control_block(control_);
#endif
    }

    NativePacket(const NativePacket&) = delete;
    NativePacket& operator=(const NativePacket&) = delete;

    [[nodiscard]] void* get() const {
#ifdef _WIN32
        return owner_.get();
#else
        return ptr_;
#endif
    }

    [[nodiscard]] explicit operator bool() const { return get() != nullptr; }

private:
#ifdef _WIN32
    std::shared_ptr<void> owner_;
#else
    void* ptr_ = nullptr;
    void* control_ = nullptr;
#endif
};

bool send_network_packet(void* player, void* packet) {
    const auto* live = profile();
    if (live == nullptr || player == nullptr || packet == nullptr || live->send_network_packet_slot == 0) {
        return false;
    }
    auto** vtable = *reinterpret_cast<void***>(player);
    if (vtable == nullptr) {
        return false;
    }
    const size_t slot = live->send_network_packet_slot / sizeof(void*);
    void* target = vtable[slot];
    /*
     * When the observer hook is installed, calling through the vtable would
     * re-enter our own detour.  Call the original directly, but first let the
     * observer see the packet so world state (time, weather, blocks) stays
     * accurate for packets we generate ourselves.
     */
    if (target == packet_send_hook_address()) {
        packet_observe_outgoing(player, packet);
        target = packet_send_original_address();
    }
    const uintptr_t base = state().module.base;
    auto address = reinterpret_cast<uintptr_t>(target);
    if (target == nullptr || address < base || address >= base + 0x10000000) {
        log_write(LogLevel::Warn, "sendNetworkPacket slot %zu is not a game function (0x%llx)", slot,
                  static_cast<unsigned long long>(address));
        return false;
    }
    using SendNetworkPacketFn = void (*)(void*, void*);
    reinterpret_cast<SendNetworkPacketFn>(target)(player, packet);
    return true;
}

bool send_set_title(void* player, int title_type, std::string_view text, int fade_in, int stay, int fade_out) {
    NativePacket packet(kPacketSetTitle);
    if (!packet) {
        log_write(LogLevel::Warn, "SetTitlePacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    std::memcpy(payload, &title_type, sizeof(title_type));
    const size_t stride = native_string_size(native_abi());
    if (!text.empty() && !write_owned_string(payload + 8, text)) {
        return false;
    }
    auto* times = payload + 8 + 2 * stride;
    std::memcpy(times + 0, &fade_in, sizeof(fade_in));
    std::memcpy(times + 4, &stay, sizeof(stay));
    std::memcpy(times + 8, &fade_out, sizeof(fade_out));
    return send_network_packet(player, packet.get());
}

/*
 * EnTT component lookup.  BDS stores components inline in dense pages, so
 * the returned pointer is page + index * component_size (not a pointer
 * loaded from the page).
 */
void* entt_try_get(void* actor, uint32_t type_hash, size_t component_size) {
    const auto* live = profile();
    if (live == nullptr || actor == nullptr || live->entt_pools_begin == 0) {
        return nullptr;
    }
    auto* registry = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(actor) + 0x10);
    if (registry == nullptr) {
        return nullptr;
    }
    const uint32_t entity = *reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(actor) + 0x18);
    auto* begin = *reinterpret_cast<unsigned char**>(registry + live->entt_pools_begin);
    auto* end = *reinterpret_cast<unsigned char**>(registry + live->entt_pools_end);
    auto* entries = *reinterpret_cast<unsigned char**>(registry + live->entt_entries);
    if (begin == nullptr || end == nullptr || entries == nullptr || end <= begin) {
        return nullptr;
    }
    const size_t count = static_cast<size_t>(end - begin) / sizeof(void*);
    if (count == 0) {
        return nullptr;
    }
    size_t probe = (count - 1) & type_hash;
    for (size_t attempt = 0; attempt < count; ++attempt) {
        auto* bucket = reinterpret_cast<uint64_t*>(begin + probe * sizeof(void*));
        const uint64_t value = *bucket;
        if (value == UINT64_MAX) {
            return nullptr;
        }
        auto* entry = entries + (value << 5);
        if (*reinterpret_cast<uint32_t*>(entry + 8) == type_hash) {
            auto* pool = *reinterpret_cast<unsigned char**>(entry + 0x10);
            if (pool == nullptr) {
                return nullptr;
            }
            const uint32_t sparse_page_index = (entity & 0x3FFFFu) >> 11;
            auto** sparse_pages = *reinterpret_cast<void***>(pool + 8);
            auto* sparse_end = *reinterpret_cast<unsigned char**>(pool + 0x10);
            if (sparse_pages == nullptr) {
                return nullptr;
            }
            const size_t sparse_count =
                sparse_end == nullptr ? 0 : (sparse_end - reinterpret_cast<unsigned char*>(sparse_pages)) / 8;
            if (sparse_page_index >= sparse_count) {
                return nullptr;
            }
            auto* sparse_page = static_cast<unsigned char*>(sparse_pages[sparse_page_index]);
            if (sparse_page == nullptr) {
                return nullptr;
            }
            const uint32_t item = *reinterpret_cast<uint32_t*>(sparse_page + (entity & 0x7FFu) * sizeof(uint32_t));
            if ((item ^ (entity & 0xFFFC0000u)) > 0x3FFFEu) {
                return nullptr;
            }
            /* BDS uses the masked value as a byte offset into the dense
             * page-pointer table. */
            const uint32_t dense_byte_offset = (item & 0x3FF80u) >> 4;
            auto* dense_pages = *reinterpret_cast<unsigned char**>(pool + 0x50);
            if (dense_pages == nullptr) {
                return nullptr;
            }
            auto* dense_page = *reinterpret_cast<unsigned char**>(dense_pages + dense_byte_offset);
            if (dense_page == nullptr) {
                return nullptr;
            }
            return dense_page + (item & 0x7Fu) * component_size;
        }
        probe = (probe + 1) % count;
    }
    return nullptr;
}

} // namespace

void live_set_server_handler(void* handler) {
    g_server_handler = handler;
}

void* live_server_handler() {
    return g_server_handler;
}

bool live_send_text(void* player, std::string_view message, LiveTextType type) {
    NativePacket packet(kPacketText);
    if (!packet) {
        log_write(LogLevel::Warn, "TextPacket allocation failed");
        return false;
    }
    const auto* live = profile();
    if (live == nullptr) {
        return false;
    }
    auto* body = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize + live->text_body_offset;
    const int type_value = static_cast<int>(type);
    body[0] = static_cast<unsigned char>(type_value);
    if (!write_owned_string(body + 8, message)) {
        log_write(LogLevel::Warn, "TextPacket message could not be copied");
        return false;
    }
    /* Popup is serialized by BDS as MessageAndParams, not MessageOnly.
     * Keep the variant's index in sync and leave an empty parameter vector. */
    if (type == LiveTextType::Popup) {
        std::memset(body + 8 + native_string_size(native_abi()), 0, 3 * sizeof(void*));
        body[live->text_variant_index_offset] = 2;
    } else {
        body[live->text_variant_index_offset] = 0;
    }
    return send_network_packet(player, packet.get());
}

bool live_send_title(void* player, std::string_view title, std::string_view subtitle, int fade_in, int stay,
                     int fade_out) {
    bool ok = false;
    if (!title.empty()) {
        ok = send_set_title(player, 2, title, fade_in, stay, fade_out);
    }
    if (!subtitle.empty()) {
        ok = send_set_title(player, 3, subtitle, fade_in, stay, fade_out) || ok;
    }
    return ok;
}

bool live_send_actionbar(void* player, std::string_view text) {
    return send_set_title(player, 4, text, 0, 20, 0);
}

bool live_reset_title(void* player) {
    return send_set_title(player, 1, {}, 0, 0, 0);
}

bool live_send_toast(void* player, std::string_view title, std::string_view content) {
    NativePacket packet(kPacketToastRequest);
    if (!packet) {
        log_write(LogLevel::Warn, "ToastRequestPacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    const size_t stride = native_string_size(native_abi());
    if (!write_owned_string(payload, title) || !write_owned_string(payload + stride, content)) {
        return false;
    }
    return send_network_packet(player, packet.get());
}

bool live_send_form(void* player, std::string_view form_json, uint32_t* form_id) {
    NativePacket packet(kPacketShowModalForm);
    if (!packet) {
        log_write(LogLevel::Warn, "ModalFormRequestPacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    const uint32_t assigned = ++g_form_id;
    std::memcpy(payload, &assigned, sizeof(assigned));
    if (!write_owned_string(payload + 8, form_json)) {
        return false;
    }
    if (form_id != nullptr) {
        *form_id = assigned;
    }
    return send_network_packet(player, packet.get());
}

/*
 * LevelEventPacket with the "particle" event id.  The payload is
 * (int16 event_id, float x, float y, float z, int32 data); the particle name
 * travels in the trailing string.  If a build changes the layout the packet
 * still serialises, the client just ignores it, so this is safe to attempt.
 */
bool live_spawn_particle(void* player, std::string_view name, const LiveVec3& position) {
    NativePacket packet(kPacketSpawnParticleEffect);
    if (!packet) {
        log_write(LogLevel::Warn, "SpawnParticleEffectPacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    const int32_t dimension = 0;
    std::memcpy(payload, &dimension, sizeof(dimension));
    payload[4] = 0xFF; /* actor id: none */
    const float xyz[3] = {static_cast<float>(position.x), static_cast<float>(position.y),
                          static_cast<float>(position.z)};
    std::memcpy(payload + 8, xyz, sizeof(xyz));
    if (!write_owned_string(payload + 20, name)) {
        free_owned_string(payload + 20);
        return false;
    }
    const bool sent = send_network_packet(player, packet.get());
    free_owned_string(payload + 20);
    return sent;
}

/* SetTimePacket: int32 world time. */
bool live_set_time(void* player, int ticks) {
    NativePacket packet(kPacketSetTime);
    if (!packet) {
        log_write(LogLevel::Warn, "SetTimePacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    std::memcpy(payload, &ticks, sizeof(ticks));
    if (std::getenv("YOYO_DEBUG_SEND") != nullptr) {
        extern int yoyo_debug_packet_id(void*);
        log_write(LogLevel::Info, "live_set_time: built packet, resolved id=%d", yoyo_debug_packet_id(packet.get()));
    }
    return send_network_packet(player, packet.get());
}

/*
 * LevelEventPacket: 3001 = start rain, 3002 = stop rain, 3003 = start thunder,
 * 1003 = stop thunder.  `data` carries the rain level.
 */
bool live_set_weather(void* player, int weather) {
    const auto send_event = [&](int16_t event_id, int32_t data) {
        NativePacket packet(kPacketLevelEvent);
        if (!packet) {
            return false;
        }
        auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
        std::memcpy(payload, &event_id, sizeof(event_id));
        const float zeros[3] = {0.0f, 0.0f, 0.0f};
        std::memcpy(payload + 2, zeros, sizeof(zeros));
        std::memcpy(payload + 14, &data, sizeof(data));
        return send_network_packet(player, packet.get());
    };
    switch (weather) {
    case 0: /* clear */
        return send_event(3002, 0) && send_event(1003, 0);
    case 1: /* rain */
        return send_event(3001, 10000);
    case 2: /* thunder */
        return send_event(3001, 10000) && send_event(3003, 10000);
    default:
        return false;
    }
}

/*
 * UpdateBlockPacket payload: BlockPos, uint32 runtime id, uint32 flags,
 * uint32 layer.  This only tells one client that a block changed; it does not
 * write engine state, so it cannot corrupt the world.
 */
bool live_set_block(void* player, int x, int y, int z, uint32_t runtime_id, int flags) {
    NativePacket packet(kPacketUpdateBlock);
    if (!packet) {
        log_write(LogLevel::Warn, "UpdateBlockPacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    std::memcpy(payload, &x, sizeof(x));
    std::memcpy(payload + 4, &y, sizeof(y));
    std::memcpy(payload + 8, &z, sizeof(z));
    std::memcpy(payload + 12, &runtime_id, sizeof(runtime_id));
    const uint32_t block_flags = static_cast<uint32_t>(flags);
    std::memcpy(payload + 16, &block_flags, sizeof(block_flags));
    const uint32_t layer = 0;
    std::memcpy(payload + 20, &layer, sizeof(layer));
    return send_network_packet(player, packet.get());
}

bool live_transfer(void* player, std::string_view host, int port) {
    NativePacket packet(kPacketTransfer);
    if (!packet) {
        log_write(LogLevel::Warn, "TransferPacket allocation failed");
        return false;
    }
    auto* payload = static_cast<unsigned char*>(packet.get()) + kPacketBaseSize;
    if (!write_owned_string(payload, host)) {
        return false;
    }
    const int destination_port = port;
    std::memcpy(payload + native_string_size(native_abi()), &destination_port, sizeof(destination_port));
    return send_network_packet(player, packet.get());
}

bool live_get_position(void* actor, LiveVec3& out) {
    const auto* live = profile();
    if (live == nullptr || actor == nullptr) {
        return false;
    }
    auto* component = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(actor) + live->state_vector_offset);
    if (component == nullptr) {
        return false;
    }
    const auto* values = reinterpret_cast<const float*>(component);
    out.x = values[0];
    out.y = values[1];
    out.z = values[2];
    return true;
}

bool live_get_feet_position(void* actor, LiveVec3& out) {
    if (!live_get_position(actor, out)) {
        return false;
    }
    out.y -= kPlayerEyeHeight;
    return true;
}

bool live_get_rotation(void* actor, float& pitch, float& yaw) {
    const auto* live = profile();
    if (live == nullptr || actor == nullptr) {
        return false;
    }
    auto* component = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(actor) + live->rotation_offset);
    if (component == nullptr) {
        return false;
    }
    const auto* values = reinterpret_cast<const float*>(component);
    pitch = values[0];
    yaw = values[1];
    return true;
}

int live_get_dimension(void* actor) {
    const auto* live = profile();
    if (live == nullptr || actor == nullptr || live->dimension_ref_offset == 0) {
        return -9999;
    }
    auto* bytes = static_cast<unsigned char*>(actor);
    auto* dimension = *reinterpret_cast<void**>(bytes + live->dimension_ref_offset);
    if (dimension == nullptr) {
        return -9999;
    }
    auto** vtable = *reinterpret_cast<void***>(dimension);
    if (vtable == nullptr || live->dimension_id_slot == 0) {
        return -9999;
    }
    auto* target = vtable[live->dimension_id_slot / sizeof(void*)];
    const uintptr_t base = state().module.base;
    auto address = reinterpret_cast<uintptr_t>(target);
    if (target == nullptr || address < base || address >= base + 0x10000000) {
        return -9999;
    }
#ifdef _WIN32
    /* MSVC returns the small DimensionType object through a hidden pointer
     * in RDX (this is RCX), matching the game's getType disassembly. */
    int value = -9999;
    using DimensionIdFn = void (*)(void*, void*);
    reinterpret_cast<DimensionIdFn>(target)(dimension, &value);
    return value;
#else
    using DimensionIdFn = int (*)(void*);
    return reinterpret_cast<DimensionIdFn>(target)(dimension);
#endif
}

const char* live_dimension_name(int dimension) {
    switch (dimension) {
    case 0: return "overworld";
    case 1: return "nether";
    case 2: return "the_end";
    default: return "unknown";
    }
}

bool live_actor_valid(void* actor) {
    const auto* live = profile();
    if (live == nullptr || actor == nullptr) {
        return false;
    }
    auto** vtable = *reinterpret_cast<void***>(actor);
    const uintptr_t base = state().module.base;
    auto address = reinterpret_cast<uintptr_t>(vtable);
    if (vtable == nullptr || address < base || address >= base + 0x10000000) {
        return false;
    }
    if (live->removed_offset != 0) {
        if (*(static_cast<unsigned char*>(actor) + live->removed_offset) != 0) {
            return false;
        }
    }
    return true;
}

bool live_teleport(void* actor, const LiveVec3& position, int dimension) {
    if (!capability_enabled(&VersionCapabilities::player_teleport)) {
        log_write(LogLevel::Warn, "teleport is not verified for this build");
        return false;
    }
    const auto* live = profile();
    if (live == nullptr || !live_actor_valid(actor) || live->teleport_slot == 0) {
        return false;
    }
    if (!live_layout_allows_write()) {
        log_write(LogLevel::Warn, "teleport refused: object layout is not verified for this build");
        return false;
    }
    if (dimension != -9999 && dimension != live_get_dimension(actor)) {
        log_write(LogLevel::Warn, "cross-dimension teleport is not ported yet");
        return false;
    }
    auto** vtable = *reinterpret_cast<void***>(actor);
    auto* target = vtable[live->teleport_slot / sizeof(void*)];
    const uintptr_t base = state().module.base;
    auto address = reinterpret_cast<uintptr_t>(target);
    if (target == nullptr || address < base || address >= base + 0x10000000) {
        return false;
    }
    struct Vec3f {
        float x;
        float y;
        float z;
    } value{static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)};
    using TeleportFn = void (*)(void*, const void*, bool, int, int, bool);
    reinterpret_cast<TeleportFn>(target)(actor, &value, true, 3, 1, false);
    return true;
}

void live_set_player_network_id(void* player, const void* network_id, uint8_t sub_client_id) {
    if (player == nullptr || network_id == nullptr) {
        return;
    }
    NetworkIdEntry entry;
    const auto* bytes = static_cast<const unsigned char*>(network_id);
    entry.bytes.assign(bytes, bytes + 176);
    entry.sub_client_id = sub_client_id;
    std::lock_guard lock(network_mutex());
    network_ids()[player] = std::move(entry);
}

void live_forget_player(void* player) {
    std::lock_guard lock(network_mutex());
    network_ids().erase(player);
    game_mode_cache().erase(player);
}

void* live_take_player_for_network(const void* network_id) {
    if (network_id == nullptr) {
        return nullptr;
    }
    std::lock_guard lock(network_mutex());
    auto& ids = network_ids();
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (it->second.bytes.size() == 176 &&
            std::memcmp(it->second.bytes.data(), network_id, 176) == 0) {
            void* player = it->first;
            ids.erase(it);
            return player;
        }
    }
    return nullptr;
}

void* live_find_player_for_network(const void* network_id) {
    if (network_id == nullptr) {
        return nullptr;
    }
    std::lock_guard lock(network_mutex());
    for (const auto& [player, entry] : network_ids()) {
        if (entry.bytes.size() == 176 &&
            std::memcmp(entry.bytes.data(), network_id, 176) == 0) {
            return player;
        }
    }
    return nullptr;
}

bool live_kick(void* player, std::string_view reason) {
    if (player == nullptr || g_server_handler == nullptr) {
        return false;
    }
    std::vector<uint8_t> network_id;
    uint8_t sub_client_id = 0;
    {
        std::lock_guard lock(network_mutex());
        auto& ids = network_ids();
        auto it = ids.find(player);
        if (it != ids.end()) {
            network_id = it->second.bytes;
            sub_client_id = it->second.sub_client_id;
        }
    }
    if (network_id.empty()) {
        log_write(LogLevel::Warn, "kick failed: no NetworkIdentifier was captured for this player");
        return false;
    }
    void* original = disconnect_original_fn();
    if (original == nullptr) {
        log_write(LogLevel::Warn, "kick failed: disconnect hook is not installed");
        return false;
    }

    alignas(16) unsigned char message[64] = {};
    alignas(16) unsigned char filtered[64] = {};
    if (!write_owned_string(message, reason)) {
        return false;
    }
    using DisconnectFn = void (*)(void*, const void*, uint8_t, int, const void*, const void*);
    reinterpret_cast<DisconnectFn>(original)(g_server_handler, network_id.data(), sub_client_id, 55, message,
                                            filtered);
    free_owned_string(message);
    return true;
}

int live_get_game_mode(void* actor) {
    auto* component = entt_try_get(actor, 0x88D3EDDFu, sizeof(int));
    if (component != nullptr) {
        const int mode = *reinterpret_cast<const int*>(component);
        std::lock_guard lock(network_mutex());
        game_mode_cache()[actor] = mode;
        return mode;
    }
    std::lock_guard lock(network_mutex());
    auto it = game_mode_cache().find(actor);
    return it == game_mode_cache().end() ? 0 : it->second;
}

bool live_set_game_mode(void* actor, int mode) {
    if (!capability_enabled(&VersionCapabilities::player_set_game_mode)) {
        log_write(LogLevel::Warn, "setGameMode is not verified for this build");
        return false;
    }
    const auto* live = profile();
    if (live == nullptr || actor == nullptr || live->set_game_mode_slot == 0) {
        return false;
    }
    if (!live_layout_allows_write()) {
        log_write(LogLevel::Warn, "setGameMode refused: object layout is not verified for this build");
        return false;
    }
    auto** vtable = *reinterpret_cast<void***>(actor);
    if (vtable == nullptr) {
        return false;
    }
    auto* target = vtable[live->set_game_mode_slot / sizeof(void*)];
    const uintptr_t base = state().module.base;
    auto address = reinterpret_cast<uintptr_t>(target);
    if (target == nullptr || address < base || address >= base + 0x10000000) {
        return false;
    }
    using SetGameModeFn = void (*)(void*, int);
    reinterpret_cast<SetGameModeFn>(target)(actor, mode);
    {
        std::lock_guard lock(network_mutex());
        game_mode_cache()[actor] = mode;
    }
    return true;
}

const char* live_game_mode_name(int mode) {
    switch (mode) {
    case 0: return "survival";
    case 1: return "creative";
    case 2: return "adventure";
    case 5: return "default";
    case 6: return "spectator";
    default: return "unknown";
    }
}

uint64_t live_runtime_id(void* actor) {
    if (auto* component = entt_try_get(actor, kRuntimeIdHash, sizeof(uint64_t)); component != nullptr) {
        return *reinterpret_cast<const uint64_t*>(component);
    }
    if (auto info = players_find_by_handle(actor); info.has_value()) {
        return info->runtime_id;
    }
    return 0;
}

uint64_t live_unique_id(void* actor) {
    if (auto* component = entt_try_get(actor, kUniqueIdHash, sizeof(uint64_t)); component != nullptr) {
        return *reinterpret_cast<const uint64_t*>(component);
    }
    return live_runtime_id(actor);
}

bool live_get_network_id(void* player, std::vector<uint8_t>& network_id, uint8_t& sub_client_id) {
    std::lock_guard lock(network_mutex());
    auto& ids = network_ids();
    auto it = ids.find(player);
    if (it == ids.end() || it->second.bytes.size() != 176) {
        return false;
    }
    network_id = it->second.bytes;
    sub_client_id = it->second.sub_client_id;
    return true;
}

void live_set_layout_fallback(bool fallback) {
    g_layout_fallback.store(fallback, std::memory_order_relaxed);
}

LayoutStatus live_layout_status() {
    return g_layout_status.load(std::memory_order_relaxed);
}

bool live_layout_allows_write() {
    if (!g_layout_fallback.load(std::memory_order_relaxed)) {
        /* Exact profile: offsets are authoritative for this build. */
        return true;
    }
    return g_layout_status.load(std::memory_order_relaxed) == LayoutStatus::Verified;
}

/*
 * Validate the object layout against a real ServerPlayer.
 *
 * Every check is a sanity bound rather than an exact value, so a build that
 * only moved code around still passes while one that reordered Actor fields
 * fails.  A failure permanently disables layout-dependent writes; reads stay
 * available because they are bounds-checked at the call site.
 */
void live_validate_layout(void* player) {
    if (player == nullptr || g_layout_status.load(std::memory_order_relaxed) != LayoutStatus::Unknown) {
        return;
    }
    const auto* live = profile();
    if (live == nullptr) {
        g_layout_status.store(LayoutStatus::Rejected, std::memory_order_relaxed);
        return;
    }

    const auto* bytes = static_cast<unsigned char*>(player);
    const bool state_ok = plausible_pointer(*reinterpret_cast<void* const*>(bytes + live->state_vector_offset));
    const bool rotation_ok =
        plausible_pointer(*reinterpret_cast<void* const*>(bytes + live->rotation_offset));

    LiveVec3 position;
    const bool position_ok = live_get_position(player, position) && plausible_position(position);

    float pitch = 0.0f;
    float yaw = 0.0f;
    const bool rotation_values_ok = live_get_rotation(player, pitch, yaw) && std::isfinite(pitch) &&
                                    std::isfinite(yaw) && std::abs(pitch) <= 720.0f &&
                                    std::abs(yaw) <= 100000.0f;

    const int dimension = live_get_dimension(player);
    const bool dimension_ok = dimension >= 0 && dimension <= 2;

    if (state_ok && rotation_ok && position_ok && rotation_values_ok && dimension_ok) {
        g_layout_status.store(LayoutStatus::Verified, std::memory_order_relaxed);
        log_write(LogLevel::Info, "object layout verified against a live player");
        return;
    }

    g_layout_status.store(LayoutStatus::Rejected, std::memory_order_relaxed);
    log_write(LogLevel::Error,
              "object layout rejected (state=%d rotation=%d position=%d angles=%d dimension=%d); "
              "live object writes are disabled",
              state_ok ? 1 : 0, rotation_ok ? 1 : 0, position_ok ? 1 : 0, rotation_values_ok ? 1 : 0,
              dimension_ok ? 1 : 0);
}

} // namespace yoyo
