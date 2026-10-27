#include "hooks.h"

#include "client_commands.h"
#include "events.h"
#include "hook_engine.h"
#include "live.h"
#include "log.h"
#include "native_string.h"
#include "packet_events.h"
#include "players.h"
#include "runtime.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace yoyo {
namespace {

InlineHook g_try_hook;
InlineHook g_disconnect_hook;
InlineHook g_join_hook;
InlineHook g_chat_hook;
InlineHook g_left_hook;
InlineHook g_level_tick_hook;

using PlayerLeftFn = void (*)(void* self, void* player, int sub_client_id);
PlayerLeftFn g_left_original = nullptr;

using TryToLoadFn = bool (*)(void* self, void* server_player, const void* connection_request, const void* auth_info);
using DisconnectFn = void (*)(void* self, const void* network_id, uint8_t sub_client_id, int reason,
                              const void* message, const void* filtered_message);
using PacketHandlerFn = void (*)(void* self, const void* network_id, const void* packet);

TryToLoadFn g_try_original = nullptr;
DisconnectFn g_disconnect_original = nullptr;
PacketHandlerFn g_set_local_init_original = nullptr;
PacketHandlerFn g_text_packet_original = nullptr;

std::mutex g_players_mutex;
std::unordered_map<std::string, PlayerView> g_players_by_xuid;

void set_local_player_initialized_detour(void* self, const void* network_id, const void* packet);
void text_packet_detour(void* self, const void* network_id, const void* packet);
void player_left_detour(void* self, void* player, int sub_client_id);

std::string read_string_at(const void* base, size_t offset) {
    if (base == nullptr) {
        return {};
    }
    auto* address = static_cast<const unsigned char*>(base) + offset;
    auto view = read_native_string(address, native_abi());
    return std::string(view);
}

void remember_player(const PlayerView& player) {
    if (player.xuid.empty()) {
        return;
    }
    std::lock_guard lock(g_players_mutex);
    g_players_by_xuid[player.xuid] = player;
}

PlayerView lookup_player(const std::string& xuid) {
    if (xuid.empty()) {
        return {};
    }
    std::lock_guard lock(g_players_mutex);
    auto it = g_players_by_xuid.find(xuid);
    if (it == g_players_by_xuid.end()) {
        return {};
    }
    return it->second;
}

struct PendingJoin {
    void* handle = nullptr;
    PlayerView player;
};

std::mutex& pending_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<PendingJoin>& pending_joins() {
    static std::vector<PendingJoin> joins;
    return joins;
}

void* g_level_tick_original = nullptr;
bool g_level_hook_attempted = false;
bool g_level_hook_installed = false;

void level_tick_detour(void* level);

void queue_join_watch(void* handle, const PlayerView& player) {
    if (handle == nullptr) {
        return;
    }
    std::lock_guard lock(pending_mutex());
    auto& joins = pending_joins();
    for (const auto& item : joins) {
        if (item.handle == handle) {
            return;
        }
    }
    joins.push_back({handle, player});
    log_write(LogLevel::Info, "join watch queued handle=%p", handle);
}

void install_level_tick_hook(void*) {
    if (g_level_hook_installed) {
        return;
    }
    const auto& target = state().symbols.level_tick;
    if (!target.valid() || target.patch_length < 14) {
        if (!g_level_hook_attempted) {
            g_level_hook_attempted = true;
            log_write(LogLevel::Warn, "Level::tick target was not resolved; tick events are unavailable");
        }
        return;
    }
    void* address = reinterpret_cast<void*>(target.address);
    const bool ok = install_inline_hook_masked(g_level_tick_hook, address, target.patch_bytes,
                                               target.patch_mask, target.patch_length,
                                               reinterpret_cast<void*>(&level_tick_detour),
                                               target.rip_offset, target.rip_instruction_length);
    if (!ok) {
        if (!g_level_hook_attempted) {
            g_level_hook_attempted = true;
            log_write(LogLevel::Warn, "Level::tick inline hook failed at rva=0x%llx",
                      static_cast<unsigned long long>(target.address - state().module.base));
        }
        return;
    }
    g_level_tick_original = g_level_tick_hook.trampoline;
    g_level_hook_installed = true;
    log_write(LogLevel::Info, "Level::tick inline hook installed rva=0x%llx length=%u",
              static_cast<unsigned long long>(target.address - state().module.base),
              static_cast<unsigned>(target.patch_length));
}

struct MoveState {
    LiveVec3 position;
    int dimension = -9999;
    int game_mode = -1;
};

std::atomic<uint64_t> g_tick_counter{0};

void level_tick_detour(void* level) {
    using TickFn = void (*)(void*);
    if (g_level_tick_original != nullptr) {
        reinterpret_cast<TickFn>(g_level_tick_original)(level);
    }

    emit_tick(++g_tick_counter);

    {
        std::vector<PendingJoin> pending;
        {
            std::lock_guard lock(pending_mutex());
            pending.swap(pending_joins());
        }
        if (!pending.empty()) {
            const auto& offset = state().layout->join_flag_offset;
            std::vector<PendingJoin> keep;
            for (auto& item : pending) {
                if (players_is_online(item.handle)) {
                    continue;
                }
                const auto flag = *(static_cast<volatile unsigned char*>(item.handle) + offset);
                if (flag == 0) {
                    keep.push_back(std::move(item));
                    continue;
                }
                if (item.player.runtime_id == 0) {
                    item.player.runtime_id = live_runtime_id(item.handle);
                }
                if (auto joined = players_mark_joined(item.player.runtime_id); joined.has_value()) {
                    install_packet_send_hook(joined->handle);
                    PlayerView view;
                    view.handle = joined->handle;
                    view.name = joined->name;
                    view.xuid = joined->xuid;
                    view.address = joined->address;
                    view.runtime_id = joined->runtime_id;
                    bool cancelled = false;
                    log_write(LogLevel::Info, "join watcher saw initialized handle=%p", item.handle);
                    emit_join(view, view.runtime_id, cancelled);
                    /*
                     * Re-send the command table once the player is fully in the
                     * world: that is when the vanilla table has been captured,
                     * so plugin commands can be merged into it.
                     */
                    client_commands_send(item.handle);
                }
            }
            if (!keep.empty()) {
                std::lock_guard lock(pending_mutex());
                auto& joins = pending_joins();
                for (auto& item : keep) {
                    joins.push_back(std::move(item));
                }
            }
        }
    }

    /* Poll movement once per tick.  This runs on the game thread, so packet
     * sends triggered by listeners are safe. */
    struct MoveEvent {
        PlayerView player;
        LiveVec3 from;
        LiveVec3 to;
        int from_dimension = -9999;
        int to_dimension = -9999;
    };
    static std::mutex state_mutex;
    static std::unordered_map<void*, MoveState> states;
    static std::vector<MoveEvent> moves;
    static std::vector<std::pair<PlayerView, std::pair<int, int>>> dimension_changes;
    static std::vector<std::pair<PlayerView, int>> game_mode_changes;
    static std::vector<std::pair<PlayerView, LiveVec3>> teleports;
    moves.clear();
    dimension_changes.clear();
    game_mode_changes.clear();
    teleports.clear();

    std::unordered_set<void*> online_handles;
    for (const auto& info : players_online()) {
        if (info.handle == nullptr) {
            continue;
        }
        online_handles.insert(info.handle);
        LiveVec3 position;
        if (!live_get_position(info.handle, position)) {
            continue;
        }
        const int dimension = live_get_dimension(info.handle);
        PlayerView view;
        view.handle = info.handle;
        view.name = info.name;
        view.xuid = info.xuid;
        view.address = info.address;
        view.runtime_id = info.runtime_id;

        const int game_mode = live_get_game_mode(info.handle);
        std::lock_guard lock(state_mutex);
        auto it = states.find(info.handle);
        if (it == states.end()) {
            states[info.handle] = {position, dimension, game_mode};
            continue;
        }
        const LiveVec3& old = it->second.position;
        const double dx = position.x - old.x;
        const double dy = position.y - old.y;
        const double dz = position.z - old.z;
        const double distance_sq = dx * dx + dy * dy + dz * dz;
        const bool moved = distance_sq > 1e-4;
        const bool changed_dimension = it->second.dimension != dimension;
        if (changed_dimension) {
            dimension_changes.push_back({view, {it->second.dimension, dimension}});
        }
        if (moved || changed_dimension) {
            moves.push_back({view, old, position, it->second.dimension, dimension});
        }
        if (!changed_dimension && distance_sq > 256.0) {
            teleports.push_back({view, position});
        }
        if (it->second.game_mode >= 0 && game_mode != it->second.game_mode) {
            game_mode_changes.push_back({view, game_mode});
        }
        it->second = {position, dimension, game_mode};
    }
    {
        std::lock_guard lock(state_mutex);
        for (auto it = states.begin(); it != states.end();) {
            if (online_handles.find(it->first) == online_handles.end()) {
                it = states.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& item : dimension_changes) {
        emit_player_dimension_change(item.first, item.second.first, item.second.second);
    }
    for (auto& item : moves) {
        emit_player_move(item.player, item.from, item.to, item.from_dimension, item.to_dimension);
    }
    for (auto& item : game_mode_changes) {
        DetailEvent detail;
        detail.value = item.second;
        emit_detail(YOYO_EVENT_PLAYER_GAME_MODE, item.first, detail);
    }
    for (auto& item : teleports) {
        DetailEvent detail;
        detail.x = static_cast<int>(item.second.x);
        detail.y = static_cast<int>(item.second.y);
        detail.z = static_cast<int>(item.second.z);
        emit_detail(YOYO_EVENT_PLAYER_TELEPORT, item.first, detail);
    }
}

void dump_vtable_if_requested(void* self) {
    if (std::getenv("YOYO_DUMP_VTABLE") == nullptr || self == nullptr) {
        return;
    }
    auto** vtable = *reinterpret_cast<void***>(self);
    if (vtable == nullptr) {
        log_write(LogLevel::Warn, "vtable dump: object has no vtable");
        return;
    }
    const uintptr_t base = state().module.base;
    log_write(LogLevel::Info, "vtable dump: object=%p vtable=%p", self, static_cast<void*>(vtable));
    for (int i = 0; i < 120; ++i) {
        auto address = reinterpret_cast<uintptr_t>(vtable[i]);
        if (address == 0) {
            log_write(LogLevel::Info, "vt[%d] = null", i);
            continue;
        }
        if (address < base || address > base + 0x10000000) {
            log_write(LogLevel::Info, "vt[%d] = 0x%llx (outside module)", i,
                      static_cast<unsigned long long>(address));
            continue;
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(address);
        log_write(LogLevel::Info,
                  "vt[%d] rva=0x%llx  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                  i, static_cast<unsigned long long>(address - base), bytes[0], bytes[1], bytes[2], bytes[3],
                  bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12],
                  bytes[13], bytes[14], bytes[15]);
    }
}

bool try_to_load_player_detour(void* self, void* server_player, const void* connection_request,
                               const void* auth_info) {
    dump_vtable_if_requested(self);
    live_set_server_handler(self);

    PlayerView player;
    player.handle = server_player;
    player.name = read_string_at(auth_info, 9 * native_string_size(native_abi())); /* best_display_name */
    if (player.name.empty()) {
        player.name = read_string_at(auth_info, 6 * native_string_size(native_abi())); /* xbox_live_name */
    }
    player.xuid = read_string_at(auth_info, 0);
    player.runtime_id = live_runtime_id(server_player);
    player.address.clear();

    remember_player(player);
    players_upsert({player.name, player.xuid, player.address, player.runtime_id, player.handle, false});
    install_packet_send_hook(player.handle);
    live_validate_layout(player.handle);

    bool cancelled = false;
    emit_pre_join(player, cancelled);
    if (cancelled) {
        log_write(LogLevel::Warn, "pre_join cancelled for '%s'", player.name.c_str());
        players_upsert({player.name, player.xuid, player.address, player.runtime_id, player.handle, false});
        return false;
    }
    if (g_try_original == nullptr) {
        return true;
    }
    return g_try_original(self, server_player, connection_request, auth_info);
}

void disconnect_detour(void* self, const void* network_id, uint8_t sub_client_id, int reason, const void* message,
                       const void* filtered_message) {
    PlayerView player;
    player.address.clear();
    bool known = false;
    if (void* handle = live_take_player_for_network(network_id); handle != nullptr) {
        known = players_is_online(handle);
        player.handle = handle;
        if (auto info = players_mark_offline(handle); info.has_value()) {
            player.name = info->name;
            player.xuid = info->xuid;
            player.address = info->address;
            player.runtime_id = info->runtime_id;
            known = true;
        }
        packet_forget_player(handle);
        live_forget_player(handle);
    }

    if (known) {
        char reason_text[64];
        std::snprintf(reason_text, sizeof(reason_text), "reason=%d", reason);
        auto text = read_native_string(message, native_abi());
        emit_left(player, text.empty() ? std::string_view(reason_text) : text);
    }

    if (g_disconnect_original != nullptr) {
        g_disconnect_original(self, network_id, sub_client_id, reason, message, filtered_message);
    }
}

void player_left_detour(void* self, void* player, int sub_client_id) {
    if (player != nullptr && players_is_online(player)) {
        PlayerView view;
        view.handle = player;
        if (auto info = players_mark_offline(player); info.has_value()) {
            view.name = info->name;
            view.xuid = info->xuid;
            view.address = info->address;
            view.runtime_id = info->runtime_id;
        }
        packet_forget_player(player);
        live_forget_player(player);
        emit_left(view, "left");
    }
    if (g_left_original != nullptr) {
        g_left_original(self, player, sub_client_id);
    }
}

void set_local_player_initialized_detour(void* self, const void* network_id, const void* packet) {
    uint64_t player_id = 0;
    if (packet != nullptr) {
        std::memcpy(&player_id, static_cast<const unsigned char*>(packet) + 48, sizeof(player_id));
    }
    PlayerView player;
    player.runtime_id = player_id;
    if (auto joined = players_mark_joined(player_id); joined.has_value()) {
        player.handle = joined->handle;
        player.name = joined->name;
        player.xuid = joined->xuid;
        player.address = joined->address;
        player.runtime_id = joined->runtime_id != 0 ? joined->runtime_id : player_id;
    }
    if (player.handle != nullptr) {
        if (network_id != nullptr) {
            live_set_player_network_id(player.handle, network_id, 0);
        }
        install_packet_send_hook(player.handle);
    }
    bool cancelled = false;
    emit_join(player, player_id, cancelled);

    /*
     * The client's command list comes from AvailableCommandsPacket, which the
     * server builds internally from CommandRegistry.  Writing our own packet
     * is not safe: the packet object owns STL members (vectors), so filling its
     * payload with our bytes corrupts those members and the server crashes.
     * Advertising plugin commands therefore requires registering them with
     * CommandRegistry itself, which is still being reverse engineered.
     */
    if (player.handle != nullptr && client_commands_ready()) {
        client_commands_send(player.handle);
    }

    /* onJoin is informational; never drop the original call. */
    if (g_set_local_init_original != nullptr) {
        g_set_local_init_original(self, network_id, packet);
    }
}

void text_packet_detour(void* self, const void* network_id, const void* packet) {
    if (packet == nullptr) {
        if (g_text_packet_original != nullptr) {
            g_text_packet_original(self, network_id, packet);
        }
        return;
    }

    const bool msvc = native_abi() == NativeAbi::Msvc;
    const size_t string_size = native_string_size(native_abi());
    const auto* payload = static_cast<const unsigned char*>(packet) + 48; /* Packet base size */
    const auto* body = payload + (msvc ? 112 : 88);
    const uint8_t type = *body;

    std::string author;
    std::string message;
    size_t message_offset = 0;
    bool recognised = false;

    switch (type) {
    case 1: /* Chat */
    case 7: /* Whisper */
    case 8: /* Announcement */
        author = read_string_at(body, 8);
        message_offset = 8 + string_size;
        message = read_string_at(body, message_offset);
        recognised = true;
        break;
    case 0: /* Raw */
    case 3: /* Popup */
    case 5: /* Tip */
    case 6: /* SystemMessage */
        message_offset = 8;
        message = read_string_at(body, message_offset);
        recognised = true;
        break;
    default:
        break;
    }

    if (!recognised || message.empty()) {
        log_write(LogLevel::Warn, "chat packet type=%u not recognised or empty", static_cast<unsigned>(type));
        if (g_text_packet_original != nullptr) {
            g_text_packet_original(self, network_id, packet);
        }
        return;
    }

    std::string xuid = read_string_at(payload, 8);
    PlayerView player = lookup_player(xuid);
    if (player.handle == nullptr) {
        player.handle = nullptr;
        player.name = author;
        player.xuid = xuid;
    } else if (!author.empty() && player.name.empty()) {
        player.name = author;
    }

    if (!player.name.empty() || !player.xuid.empty()) {
        players_upsert({player.name, player.xuid, player.address, player.runtime_id, player.handle, true});
    }

    const std::string original_message = message;
    bool cancelled = false;
    emit_chat(player, message, cancelled);
    if (!cancelled && message != original_message) {
        auto* target = const_cast<unsigned char*>(body) + message_offset;
        if (write_native_string(target, message, native_abi())) {
            log_write(LogLevel::Info, "chat message rewritten for '%s'", player.name.c_str());
        } else {
            log_write(LogLevel::Warn, "chat rewrite did not fit the native string; keeping original");
            message = original_message;
        }
    }
    if (cancelled) {
        log_write(LogLevel::Info, "chat cancelled for '%s'", player.name.c_str());
        return;
    }
    if (g_text_packet_original != nullptr) {
        g_text_packet_original(self, network_id, packet);
    }
}

bool install_one(InlineHook& hook, const char* name, const ResolvedTarget& spec, void* detour,
                 void** original) {
    if (!spec.valid() || spec.patch_length == 0) {
        log_write(LogLevel::Warn, "%s hook target is not available for this build", name);
        return false;
    }
    void* target = reinterpret_cast<void*>(spec.address);
    if (!install_inline_hook_masked(hook, target, spec.patch_bytes, spec.patch_mask, spec.patch_length,
                                    detour, spec.rip_offset, spec.rip_instruction_length)) {
        log_write(LogLevel::Error, "failed to install %s hook at rva=0x%llx", name,
                  static_cast<unsigned long long>(spec.address - state().module.base));
        return false;
    }
    if (original != nullptr) {
        *original = hook.trampoline;
    }
    log_write(LogLevel::Info, "inline hook installed %s rva=0x%llx%s", name,
              static_cast<unsigned long long>(spec.address - state().module.base),
              spec.recovered ? " (signature)" : "");
    return true;
}

} // namespace

void* disconnect_original_fn() {
    return reinterpret_cast<void*>(g_disconnect_original);
}

bool install_player_hooks() {
    auto& rt = state();
    if (!rt.symbols.any()) {
        log_write(LogLevel::Error, "no hook targets could be resolved; refusing to hook");
        return false;
    }
    if (!rt.symbols.complete()) {
        /*
         * Partial support is allowed and reported: each feature is gated on
         * its own capability, so an unresolved target only disables the
         * events that need it.  See PROJECT_RULES.md 1.3.
         */
        log_write(LogLevel::Warn, "partial hook coverage on this build; unresolved features stay disabled");
    }

    /* At least one relationship hook must work, otherwise the runtime is
     * useless and we prefer to leave the server untouched. */
    const bool core = install_one(g_try_hook, "tryToLoadPlayer", rt.symbols.try_to_load_player,
                                  reinterpret_cast<void*>(&try_to_load_player_detour),
                                  reinterpret_cast<void**>(&g_try_original)) |
                      install_one(g_join_hook, "SetLocalPlayerAsInitialized",
                                  rt.symbols.set_local_player_initialized,
                                  reinterpret_cast<void*>(&set_local_player_initialized_detour),
                                  reinterpret_cast<void**>(&g_set_local_init_original));
    if (!core) {
        log_write(LogLevel::Error, "neither join nor pre_join could be hooked; refusing to patch further");
        return false;
    }

    install_one(g_disconnect_hook, "disconnectClientWithMessage", rt.symbols.disconnect_client,
                reinterpret_cast<void*>(&disconnect_detour), reinterpret_cast<void**>(&g_disconnect_original));
    install_one(g_chat_hook, "TextPacket", rt.symbols.text_packet,
                reinterpret_cast<void*>(&text_packet_detour), reinterpret_cast<void**>(&g_text_packet_original));
    install_one(g_left_hook, "_onPlayerLeft", rt.symbols.player_left,
                reinterpret_cast<void*>(&player_left_detour), reinterpret_cast<void**>(&g_left_original));
    install_level_tick_hook(nullptr);
    return true;
}

} // namespace yoyo
