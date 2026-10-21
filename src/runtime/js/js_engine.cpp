#include "js_engine.h"

#include "js_internal.h"
#include "../events.h"
#include "../log.h"
#include "../players.h"
#include "../runtime.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace yoyo::js {

JSContext* g_context = nullptr;

namespace {

JSRuntime* g_runtime = nullptr;
std::mutex g_mutex;
std::unordered_map<std::string, std::vector<JSValue>> g_listeners;

std::string to_std_string(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string result = text ? text : "";
    if (text) {
        JS_FreeCString(ctx, text);
    }
    return result;
}

std::string normalize_event(std::string name) {
    for (char& c : name) {
        if (c == '-' || c == ' ') c = '_';
    }
    if (name == "onPreJoin" || name == "pre_join") return "onPreJoin";
    if (name == "onJoin" || name == "join") return "onJoin";
    if (name == "onChat" || name == "chat") return "onChat";
    if (name == "onLeft" || name == "left") return "onLeft";
    return name;
}

bool implemented_event(const std::string& name) {
    return name == "onPreJoin" || name == "onJoin" || name == "onChat" || name == "onLeft";
}

bool call_listeners_locked(const std::string& event, int argc, JSValueConst* argv) {
    auto it = g_listeners.find(event);
    if (it == g_listeners.end() || it->second.empty()) {
        return false;
    }
    std::vector<JSValue> callbacks;
    callbacks.reserve(it->second.size());
    for (JSValue fn : it->second) {
        callbacks.push_back(JS_DupValue(g_context, fn));
    }

    bool cancelled = false;
    for (JSValue fn : callbacks) {
        JSValue result = JS_Call(g_context, fn, JS_UNDEFINED, argc, argv);
        if (JS_IsException(result)) {
            log_exception(g_context);
        } else if (JS_IsBool(result) && JS_ToBool(g_context, result) == 0) {
            cancelled = true;
        }
        JS_FreeValue(g_context, result);
        JS_FreeValue(g_context, fn);
    }
    return cancelled;
}

JSValue player_unsupported(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return unsupported(ctx, "this Player method is not implemented for the current BDS build");
}

JSValue player_to_string(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JSValue name = JS_GetPropertyStr(ctx, this_val, "name");
    std::string text = "[Player " + to_std_string(ctx, name) + "]";
    JS_FreeValue(ctx, name);
    return JS_NewString(ctx, text.c_str());
}

JSValue mc_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) line += ' ';
        line += to_std_string(ctx, argv[i]);
    }
    log_write(LogLevel::Info, "[js] %s", line.c_str());
    return JS_UNDEFINED;
}

JSValue mc_listen(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "mc.listen(event, callback) requires two arguments");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "mc.listen callback must be a function");
    }
    std::string event = normalize_event(to_std_string(ctx, argv[0]));
    if (!implemented_event(event)) {
        log_write(LogLevel::Warn, "js event '%s' is not implemented yet; listener stored but it will not fire",
                  event.c_str());
    }
    g_listeners[event].push_back(JS_DupValue(ctx, argv[1]));
    return JS_TRUE;
}

PlayerView from_info(const PlayerInfo& info) {
    PlayerView view;
    view.handle = info.handle;
    view.runtime_id = info.runtime_id;
    view.name = info.name;
    view.xuid = info.xuid;
    view.address = info.address;
    return view;
}

JSValue mc_get_player(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    auto info = players_find(to_std_string(ctx, argv[0]));
    if (!info.has_value()) return JS_NULL;
    auto view = from_info(*info);
    return make_player(ctx, view);
}

JSValue mc_get_online_players(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto players = players_online();
    JSValue array = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const auto& info : players) {
        auto view = from_info(info);
        JS_SetPropertyUint32(ctx, array, index++, make_player(ctx, view));
    }
    return array;
}

JSValue mc_get_online_count(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewInt32(ctx, static_cast<int32_t>(players_online().size()));
}

JSValue mc_get_server_version(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewString(ctx, state().module.build_id);
}

JSValue mc_get_module_base(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewInt64(ctx, static_cast<int64_t>(state().module.base));
}

JSValue mc_unsupported(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return unsupported(ctx, "this mc function is not implemented for the current BDS build");
}

void register_mc(JSContext* ctx) {
    JSValue mc = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, mc, "log", JS_NewCFunction(ctx, mc_log, "log", 1));
    JS_SetPropertyStr(ctx, mc, "listen", JS_NewCFunction(ctx, mc_listen, "listen", 2));
    JS_SetPropertyStr(ctx, mc, "getPlayer", JS_NewCFunction(ctx, mc_get_player, "getPlayer", 1));
    JS_SetPropertyStr(ctx, mc, "getOnlinePlayers",
                      JS_NewCFunction(ctx, mc_get_online_players, "getOnlinePlayers", 0));
    JS_SetPropertyStr(ctx, mc, "getPlayers",
                      JS_NewCFunction(ctx, mc_get_online_players, "getPlayers", 0));
    JS_SetPropertyStr(ctx, mc, "getOnlinePlayerCount",
                      JS_NewCFunction(ctx, mc_get_online_count, "getOnlinePlayerCount", 0));
    JS_SetPropertyStr(ctx, mc, "getServerVersion",
                      JS_NewCFunction(ctx, mc_get_server_version, "getServerVersion", 0));
    JS_SetPropertyStr(ctx, mc, "getModuleBase",
                      JS_NewCFunction(ctx, mc_get_module_base, "getModuleBase", 0));
    JS_SetPropertyStr(ctx, mc, "broadcast", JS_NewCFunction(ctx, mc_unsupported, "broadcast", 1));
    JS_SetPropertyStr(ctx, mc, "runcmd", JS_NewCFunction(ctx, mc_unsupported, "runcmd", 1));
    JS_SetPropertyStr(ctx, mc, "runcmdEx", JS_NewCFunction(ctx, mc_unsupported, "runcmdEx", 1));
    JS_SetPropertyStr(ctx, mc, "getBlock", JS_NewCFunction(ctx, mc_unsupported, "getBlock", 1));
    JS_SetPropertyStr(ctx, mc, "getContainer", JS_NewCFunction(ctx, mc_unsupported, "getContainer", 1));
    JS_SetPropertyStr(ctx, mc, "getEntity", JS_NewCFunction(ctx, mc_unsupported, "getEntity", 1));
    JS_SetPropertyStr(ctx, mc, "version", JS_NewString(ctx, "0.2.0"));

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "mc", mc);
    JS_FreeValue(ctx, global);
}

void load_js_plugins(JSContext* ctx) {
    std::error_code error;
    auto directory = std::filesystem::current_path(error) / "plugins";
    if (error || !std::filesystem::is_directory(directory, error)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".js") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) {
            log_write(LogLevel::Error, "cannot open JS plugin %s", entry.path().string().c_str());
            continue;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        JSValue result = JS_Eval(ctx, source.data(), source.size(), entry.path().string().c_str(),
                                 JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            log_exception(ctx);
            log_write(LogLevel::Error, "JS plugin failed: %s", entry.path().string().c_str());
        } else {
            JS_FreeValue(ctx, result);
            log_write(LogLevel::Info, "JS plugin loaded: %s", entry.path().string().c_str());
        }
    }
}

} // namespace

void log_exception(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    const char* text = JS_ToCString(ctx, exception);
    log_write(LogLevel::Error, "js exception: %s", text ? text : "<unknown>");
    if (text) JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, exception);
}

JSValue unsupported(JSContext* ctx, const char* message) {
    return JS_ThrowInternalError(ctx, "%s", message);
}

JSValue make_player(JSContext* ctx, const PlayerView& player) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, player.name.c_str()));
    JS_SetPropertyStr(ctx, object, "xuid", JS_NewString(ctx, player.xuid.c_str()));
    JS_SetPropertyStr(ctx, object, "address", JS_NewString(ctx, player.address.c_str()));
    JS_SetPropertyStr(ctx, object, "runtimeId", JS_NewInt64(ctx, static_cast<int64_t>(player.runtime_id)));
    JS_SetPropertyStr(ctx, object, "isOnline", JS_TRUE);
    JS_SetPropertyStr(ctx, object, "sendMessage", JS_NewCFunction(ctx, player_unsupported, "sendMessage", 1));
    JS_SetPropertyStr(ctx, object, "kick", JS_NewCFunction(ctx, player_unsupported, "kick", 1));
    JS_SetPropertyStr(ctx, object, "getPosition", JS_NewCFunction(ctx, player_unsupported, "getPosition", 0));
    JS_SetPropertyStr(ctx, object, "getDimension", JS_NewCFunction(ctx, player_unsupported, "getDimension", 0));
    JS_SetPropertyStr(ctx, object, "teleport", JS_NewCFunction(ctx, player_unsupported, "teleport", 3));
    JS_SetPropertyStr(ctx, object, "getNbt", JS_NewCFunction(ctx, player_unsupported, "getNbt", 0));
    JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, player_to_string, "toString", 0));
    return object;
}

bool start() {
    std::lock_guard lock(g_mutex);
    if (g_context != nullptr) return true;

    g_runtime = JS_NewRuntime();
    if (g_runtime == nullptr) {
        log_write(LogLevel::Error, "JS_NewRuntime failed");
        return false;
    }
    JS_SetMemoryLimit(g_runtime, 64ull * 1024ull * 1024ull);
    JS_SetMaxStackSize(g_runtime, 1024ull * 1024ull);

    g_context = JS_NewContext(g_runtime);
    if (g_context == nullptr) {
        log_write(LogLevel::Error, "JS_NewContext failed");
        JS_FreeRuntime(g_runtime);
        g_runtime = nullptr;
        return false;
    }

    try {
        register_mc(g_context);
        register_nbt(g_context);
        register_forms(g_context);
        load_js_plugins(g_context);
    } catch (...) {
        log_write(LogLevel::Error, "JS initialization failed");
        JS_FreeContext(g_context);
        g_context = nullptr;
        JS_FreeRuntime(g_runtime);
        g_runtime = nullptr;
        return false;
    }
    log_write(LogLevel::Info, "QuickJS ready");
    return true;
}

void stop() {
    std::lock_guard lock(g_mutex);
    if (g_context != nullptr) {
        for (auto& [name, listeners] : g_listeners) {
            for (JSValue fn : listeners) JS_FreeValue(g_context, fn);
        }
        g_listeners.clear();
        JS_FreeContext(g_context);
        g_context = nullptr;
    }
    if (g_runtime != nullptr) {
        JS_FreeRuntime(g_runtime);
        g_runtime = nullptr;
    }
}

void dispatch_pre_join(const PlayerView& player, bool& cancelled) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JSValue js_player = make_player(g_context, player);
    JSValue event = JS_NewObject(g_context);
    JS_SetPropertyStr(g_context, event, "cancelled", JS_FALSE);
    JSValue args[2] = {js_player, event};
    bool result = call_listeners_locked("onPreJoin", 2, args);
    JSValue flag = JS_GetPropertyStr(g_context, event, "cancelled");
    if (JS_ToBool(g_context, flag) == 1) result = true;
    JS_FreeValue(g_context, flag);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, event);
    if (result) cancelled = true;
}

void dispatch_join(const PlayerView& player, uint64_t, bool&) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JSValue js_player = make_player(g_context, player);
    JSValue args[1] = {js_player};
    call_listeners_locked("onJoin", 1, args);
    JS_FreeValue(g_context, js_player);
}

void dispatch_chat(const PlayerView& player, std::string_view message, bool& cancelled) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JSValue js_player = make_player(g_context, player);
    JSValue js_message = JS_NewStringLen(g_context, message.data(), message.size());
    JSValue event = JS_NewObject(g_context);
    JS_SetPropertyStr(g_context, event, "cancelled", JS_FALSE);
    JSValue args[3] = {js_player, js_message, event};
    bool result = call_listeners_locked("onChat", 3, args);
    JSValue flag = JS_GetPropertyStr(g_context, event, "cancelled");
    if (JS_ToBool(g_context, flag) == 1) result = true;
    JS_FreeValue(g_context, flag);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, js_message);
    JS_FreeValue(g_context, event);
    if (result) cancelled = true;
}

void dispatch_left(const PlayerView& player, std::string_view reason) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JSValue js_player = make_player(g_context, player);
    JSValue js_reason = JS_NewStringLen(g_context, reason.data(), reason.size());
    JSValue args[2] = {js_player, js_reason};
    call_listeners_locked("onLeft", 2, args);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, js_reason);
}

} // namespace yoyo::js
