#include "hooks.h"

#include "events.h"
#include "hook_engine.h"
#include "log.h"
#include "native_string.h"
#include "players.h"
#include "runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yoyo {
namespace {

InlineHook g_try_hook;
InlineHook g_disconnect_hook;
InlineHook g_join_hook;
InlineHook g_chat_hook;

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
    PlayerView player;
    player.handle = server_player;
    player.name = read_string_at(auth_info, 9 * native_string_size(native_abi())); /* best_display_name */
    if (player.name.empty()) {
        player.name = read_string_at(auth_info, 6 * native_string_size(native_abi())); /* xbox_live_name */
    }
    player.xuid = read_string_at(auth_info, 0);
    player.address.clear();

    remember_player(player);
    players_upsert({player.name, player.xuid, player.address, player.runtime_id, player.handle, true});

    bool cancelled = false;
    emit_pre_join(player, cancelled);
    if (cancelled) {
        log_write(LogLevel::Warn, "pre_join cancelled for '%s'", player.name.c_str());
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

    char reason_text[64];
    std::snprintf(reason_text, sizeof(reason_text), "reason=%d", reason);
    auto text = read_native_string(message, native_abi());
    emit_left(player, text.empty() ? std::string_view(reason_text) : text);

    if (g_disconnect_original != nullptr) {
        g_disconnect_original(self, network_id, sub_client_id, reason, message, filtered_message);
    }
}

void set_local_player_initialized_detour(void* self, const void* network_id, const void* packet) {
    uint64_t player_id = 0;
    if (packet != nullptr) {
        std::memcpy(&player_id, static_cast<const unsigned char*>(packet) + 48, sizeof(player_id));
    }
    PlayerView player;
    player.runtime_id = player_id;
    bool cancelled = false;
    emit_join(player, player_id, cancelled);
    /* onJoin is informational in LLSE; never drop the original call. */
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
    bool recognised = false;

    switch (type) {
    case 1: /* Chat */
    case 7: /* Whisper */
    case 8: /* Announcement */
        author = read_string_at(body, 8);
        message = read_string_at(body, 8 + string_size);
        recognised = true;
        break;
    case 0: /* Raw */
    case 3: /* Popup */
    case 5: /* Tip */
    case 6: /* SystemMessage */
        message = read_string_at(body, 8);
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

    bool cancelled = false;
    emit_chat(player, message, cancelled);
    if (cancelled) {
        log_write(LogLevel::Info, "chat cancelled for '%s'", player.name.c_str());
        return;
    }
    if (g_text_packet_original != nullptr) {
        g_text_packet_original(self, network_id, packet);
    }
}

bool install_one(InlineHook& hook, const char* name, const Prologue& spec, void* detour, void** original) {
    if (spec.rva == 0 || spec.length == 0) {
        log_write(LogLevel::Warn, "%s hook is not available for this build", name);
        return false;
    }
    void* target = reinterpret_cast<void*>(state().module.base + spec.rva);
    if (!install_inline_hook(hook, target, spec.bytes, spec.length, detour)) {
        log_write(LogLevel::Error, "failed to install %s hook at rva=0x%llx", name,
                  static_cast<unsigned long long>(spec.rva));
        return false;
    }
    if (original != nullptr) {
        *original = hook.trampoline;
    }
    log_write(LogLevel::Info, "inline hook installed %s rva=0x%llx", name,
              static_cast<unsigned long long>(spec.rva));
    return true;
}

} // namespace

bool install_player_hooks() {
    auto& rt = state();
    if (rt.profile == nullptr) {
        log_write(LogLevel::Error, "no version profile; refusing to hook");
        return false;
    }

    bool ok = install_one(g_try_hook, "tryToLoadPlayer", rt.profile->try_to_load_player,
                          reinterpret_cast<void*>(&try_to_load_player_detour),
                          reinterpret_cast<void**>(&g_try_original));
    if (!ok) {
        return false;
    }

    install_one(g_disconnect_hook, "disconnectClientWithMessage", rt.profile->disconnect_client,
                reinterpret_cast<void*>(&disconnect_detour), reinterpret_cast<void**>(&g_disconnect_original));
    install_one(g_join_hook, "SetLocalPlayerAsInitialized", rt.profile->set_local_player_initialized,
                reinterpret_cast<void*>(&set_local_player_initialized_detour),
                reinterpret_cast<void**>(&g_set_local_init_original));
    install_one(g_chat_hook, "TextPacket", rt.profile->text_packet,
                reinterpret_cast<void*>(&text_packet_detour), reinterpret_cast<void**>(&g_text_packet_original));
    return true;
}

} // namespace yoyo
