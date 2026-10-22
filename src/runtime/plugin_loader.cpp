#include "plugin_loader.h"

#include "events.h"
#include "js/js_engine.h"
#include "live.h"
#include "log.h"
#include "packet_events.h"
#include "players.h"
#include "runtime.h"

#include <yoyo/plugin.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace yoyo {
namespace {

using EntryFn = const yoyo_api*(YOYO_CALL*)(const yoyo_api*);

const char* api_get_server_version() {
    return state().module.build_id;
}

uintptr_t api_get_module_base() {
    return state().module.base;
}

void api_log(int level, const char* message) {
    LogLevel mapped = LogLevel::Info;
    if (level == 1) {
        mapped = LogLevel::Warn;
    } else if (level >= 2) {
        mapped = LogLevel::Error;
    }
    log_write(mapped, "[plugin] %s", message ? message : "");
}

int api_register_event(int type, yoyo_event_callback callback, void* user) {
    return register_event(type, callback, user) ? 0 : 1;
}

int api_unregister_event(int type, yoyo_event_callback callback, void* user) {
    return unregister_event(type, callback, user) ? 0 : 1;
}

int api_player_send_message(void* player, const char* message) {
    return live_send_text(player, message ? message : "", LiveTextType::Raw) ? 0 : 1;
}

int api_server_broadcast(const char* message) {
    int sent = 0;
    for (const auto& player : players_online()) {
        if (player.handle != nullptr &&
            live_send_text(player.handle, message ? message : "", LiveTextType::Raw)) {
            ++sent;
        }
    }
    return sent > 0 ? 0 : 1;
}

int api_player_kick(void* player, const char* reason) {
    return live_kick(player, reason ? reason : "Kicked") ? 0 : 1;
}

int api_player_get_position(void* player, double* x, double* y, double* z) {
    LiveVec3 position;
    if (!live_get_position(player, position)) {
        return 1;
    }
    if (x != nullptr) *x = position.x;
    if (y != nullptr) *y = position.y;
    if (z != nullptr) *z = position.z;
    return 0;
}

int api_player_teleport(void* player, double x, double y, double z, int dimension) {
    return live_teleport(player, LiveVec3{x, y, z}, dimension) ? 0 : 1;
}

int api_player_get_dimension(void* player) {
    return live_get_dimension(player);
}

int api_player_send_title(void* player, const char* title, const char* subtitle, int fade_in, int stay,
                          int fade_out) {
    return live_send_title(player, title ? title : "", subtitle ? subtitle : "", fade_in, stay, fade_out) ? 0 : 1;
}

int api_player_send_actionbar(void* player, const char* text) {
    return live_send_actionbar(player, text ? text : "") ? 0 : 1;
}

int api_player_send_form(void* player, const char* json) {
    return live_send_form(player, json ? json : "{}") ? 0 : 1;
}

int api_player_get_game_mode(void* player) {
    return live_get_game_mode(player);
}

int api_player_set_game_mode(void* player, int mode) {
    return live_set_game_mode(player, mode) ? 0 : 1;
}

int api_player_get_health(void* player) {
    return packet_player_health(player);
}

int api_player_is_sneaking(void* player) {
    return packet_player_is_sneaking(player) ? 1 : 0;
}

int api_player_is_sprinting(void* player) {
    return packet_player_is_sprinting(player) ? 1 : 0;
}

int api_player_is_flying(void* player) {
    return packet_player_is_flying(player) ? 1 : 0;
}

int api_player_get_held_slot(void* player) {
    return packet_player_held_slot(player);
}

int api_player_send_tip(void* player, const char* message) {
    return live_send_text(player, message ? message : "", LiveTextType::Tip) ? 0 : 1;
}

int api_player_send_popup(void* player, const char* message) {
    return live_send_text(player, message ? message : "", LiveTextType::Popup) ? 0 : 1;
}

int api_player_send_system_message(void* player, const char* message) {
    return live_send_text(player, message ? message : "", LiveTextType::SystemMessage) ? 0 : 1;
}

int api_player_send_toast(void* player, const char* title, const char* content) {
    return live_send_toast(player, title ? title : "", content ? content : "") ? 0 : 1;
}

int api_player_reset_title(void* player) {
    return live_reset_title(player) ? 0 : 1;
}

int api_player_transfer(void* player, const char* host, int port) {
    return live_transfer(player, host ? host : "127.0.0.1", port) ? 0 : 1;
}

const yoyo_api kApi = {
    YOYO_PLUGIN_ABI_VERSION,
    &api_get_server_version,
    &api_get_module_base,
    &api_log,
    &api_register_event,
    &api_unregister_event,
    &api_player_send_message,
    &api_server_broadcast,
    &api_player_kick,
    &api_player_get_position,
    &api_player_teleport,
    &api_player_get_dimension,
    &api_player_send_title,
    &api_player_send_actionbar,
    &api_player_send_form,
    &api_player_get_game_mode,
    &api_player_set_game_mode,
    &api_player_get_health,
    &api_player_is_sneaking,
    &api_player_is_sprinting,
    &api_player_is_flying,
    &api_player_get_held_slot,
    &api_player_send_tip,
    &api_player_send_popup,
    &api_player_send_system_message,
    &api_player_send_toast,
    &api_player_reset_title,
    &api_player_transfer,
};

std::string to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
    return result;
#else
    return path.string();
#endif
}

void load_one(const std::filesystem::path& path) {
    const std::string display = to_utf8(path);
#ifdef _WIN32
    HMODULE module = LoadLibraryW(path.wstring().c_str());
    if (module == nullptr) {
        log_write(LogLevel::Error, "LoadLibrary failed for %s (error %lu)", display.c_str(), GetLastError());
        return;
    }
    auto entry = reinterpret_cast<EntryFn>(GetProcAddress(module, "yoyo_plugin_entry"));
#else
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        log_write(LogLevel::Error, "dlopen failed for %s: %s", display.c_str(), dlerror());
        return;
    }
    auto entry = reinterpret_cast<EntryFn>(dlsym(module, "yoyo_plugin_entry"));
#endif
    if (entry == nullptr) {
        log_write(LogLevel::Error, "plugin %s has no yoyo_plugin_entry export", display.c_str());
        return;
    }

    const yoyo_api* result = entry(&kApi);
    if (result == nullptr) {
        log_write(LogLevel::Error, "plugin %s refused to load", display.c_str());
        return;
    }
    log_write(LogLevel::Info, "plugin loaded: %s", display.c_str());
}

#ifdef _WIN32
DWORD WINAPI loader_thread(LPVOID) {
#else
void loader_thread() {
#endif
    log_write(LogLevel::Info, "plugin loader started");
    std::error_code ec;
    auto directory = std::filesystem::current_path(ec) / "plugins";
    if (ec || !std::filesystem::is_directory(directory, ec)) {
        log_write(LogLevel::Info, "no plugins directory at %s", to_utf8(directory).c_str());
#ifdef _WIN32
        return 0;
#else
        return;
#endif
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
#ifdef _WIN32
        if (extension == ".dll")
#else
        if (extension == ".so")
#endif
        {
            std::string stem = entry.path().stem().string();
            if (stem.find("yoyo_runtime") != std::string::npos || stem.find("yoyo_loader") != std::string::npos) {
                continue;
            }
            candidates.push_back(entry.path());
        }
    }

    for (const auto& path : candidates) {
        load_one(path);
    }
    log_write(LogLevel::Info, "plugin loader finished, %zu candidate(s)", candidates.size());
    if (!js::start()) {
        log_write(LogLevel::Error, "JavaScript engine failed to start");
    }
    emit_server_ready(state().module.build_id, state().module.base);
#ifdef _WIN32
    return 0;
#endif
}

} // namespace

void start_plugin_loader() {
#ifdef _WIN32
    HANDLE thread = CreateThread(nullptr, 0, loader_thread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
#else
    std::thread(loader_thread).detach();
#endif
}

} // namespace yoyo
