#include "plugin_loader.h"

#include "events.h"
#include "js/js_engine.h"
#include "log.h"
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

const yoyo_api kApi = {
    YOYO_PLUGIN_ABI_VERSION,
    &api_get_server_version,
    &api_get_module_base,
    &api_log,
    &api_register_event,
    &api_unregister_event,
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
