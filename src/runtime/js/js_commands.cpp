#include "js_internal.h"

#include "../client_commands.h"
#include "../js/js_engine.h"

#include "../events.h"
#include "../log.h"
#include "../players.h"
#include "../worlds.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yoyo::js {
namespace {

struct CommandEntry {
    std::string description;
    JSValue callback = JS_UNDEFINED;
    JSContext* ctx = nullptr; /* owning plugin context */
    int32_t permission = 0;   /* 0 = any player, >0 = OP only */
};

std::mutex& command_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, CommandEntry>& player_commands() {
    static std::unordered_map<std::string, CommandEntry> commands;
    return commands;
}

std::unordered_map<std::string, CommandEntry>& console_commands() {
    static std::unordered_map<std::string, CommandEntry> commands;
    return commands;
}

std::string value_to_text(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string result = text ? text : "";
    if (text) {
        JS_FreeCString(ctx, text);
    }
    return result;
}

std::string lower(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

std::string command_name(const std::string& message) {
    std::string trimmed = message;
    while (!trimmed.empty() && (trimmed.front() == '/' || trimmed.front() == '!')) {
        trimmed.erase(trimmed.begin());
    }
    const auto space = trimmed.find(' ');
    return lower(trimmed.substr(0, space));
}

std::string command_args(const std::string& message) {
    std::string trimmed = message;
    while (!trimmed.empty() && (trimmed.front() == '/' || trimmed.front() == '!')) {
        trimmed.erase(trimmed.begin());
    }
    const auto space = trimmed.find(' ');
    return space == std::string::npos ? std::string() : trimmed.substr(space + 1);
}

/* A minimal output object; LLSE exposes success()/error()/addMessage(). */
JSValue make_output(JSContext* ctx) {
    JSValue output = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, output, "success",
                      JS_NewCFunction(ctx,
                                      [](JSContext*, JSValueConst, int, JSValueConst*) {
                                          return JS_TRUE;
                                      },
                                      "success", 0));
    JS_SetPropertyStr(ctx, output, "error",
                      JS_NewCFunction(ctx,
                                      [](JSContext*, JSValueConst, int, JSValueConst*) {
                                          return JS_FALSE;
                                      },
                                      "error", 0));
    JS_SetPropertyStr(ctx, output, "addMessage",
                      JS_NewCFunction(ctx,
                                      [](JSContext*, JSValueConst, int, JSValueConst*) {
                                          return JS_TRUE;
                                      },
                                      "addMessage", 0));
    return output;
}

bool player_has_permission(const PlayerView& player, int32_t permission) {
    if (permission <= 0) {
        return true;
    }
    auto info = players_find_by_handle(player.handle);
    return info.has_value() && worlds::is_op(info->xuid);
}

} // namespace

void register_commands(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mc = JS_GetPropertyStr(ctx, global, "mc");
    JS_SetPropertyStr(ctx, mc, "regPlayerCmd",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst, int argc, JSValueConst* argv) -> JSValue {
                                          if (argc < 3 || !JS_IsFunction(c, argv[2])) {
                                              return JS_ThrowTypeError(
                                                  c, "regPlayerCmd(name, description, callback[, level])");
                                          }
                                          const std::string name = lower(value_to_text(c, argv[0]));
                                          if (name.empty()) {
                                              return JS_FALSE;
                                          }
                                          int32_t permission = 0;
                                          if (argc > 3) {
                                              JS_ToInt32(c, &permission, argv[3]);
                                          }
                                          JSValue callback = JS_DupValue(c, argv[2]);
                                          const std::string description = value_to_text(c, argv[1]);
                                          {
                                              /* Register first, then release the lock:
                                               * client_commands_refresh() reads the registry
                                               * and would deadlock on this same mutex. */
                                              std::lock_guard lock(command_mutex());
                                              auto& commands = player_commands();
                                              auto it = commands.find(name);
                                              if (it != commands.end()) {
                                                  JS_FreeValue(c, it->second.callback);
                                                  it->second.callback = callback;
                                                  it->second.ctx = c;
                                                  it->second.description = description;
                                                  it->second.permission = permission;
                                              } else {
                                                  commands[name] = CommandEntry{description, callback, c, permission};
                                              }
                                          }
                                          /* Mirror the registration in the client
                                           * command table so it shows up in
                                           * tab-completion and /help. */
                                          client_commands_refresh();
                                          return JS_TRUE;
                                      },
                                      "regPlayerCmd", 4));
    JS_SetPropertyStr(ctx, mc, "regConsoleCmd",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst, int argc, JSValueConst* argv) -> JSValue {
                                          if (argc < 3 || !JS_IsFunction(c, argv[2])) {
                                              return JS_ThrowTypeError(
                                                  c, "regConsoleCmd(name, description, callback)");
                                          }
                                          const std::string name = lower(value_to_text(c, argv[0]));
                                          if (name.empty()) {
                                              return JS_FALSE;
                                          }
                                          JSValue callback = JS_DupValue(c, argv[2]);
                                          std::lock_guard lock(command_mutex());
                                          auto& commands = console_commands();
                                          auto it = commands.find(name);
                                          if (it != commands.end()) {
                                              JS_FreeValue(c, it->second.callback);
                                              it->second.callback = callback;
                                              it->second.ctx = c;
                                              it->second.description = value_to_text(c, argv[1]);
                                          } else {
                                              commands[name] = CommandEntry{value_to_text(c, argv[1]), callback, c, 0};
                                          }
                                          return JS_TRUE;
                                      },
                                      "regConsoleCmd", 3));
    JS_FreeValue(ctx, mc);
    JS_FreeValue(ctx, global);
}

std::vector<RegisteredCommand> js_registered_commands() {
    std::vector<RegisteredCommand> out;
    std::lock_guard lock(command_mutex());
    out.reserve(player_commands().size());
    for (const auto& [name, entry] : player_commands()) {
        out.push_back({name, entry.description, entry.permission});
    }
    return out;
}

void clear_commands() {
    std::lock_guard lock(command_mutex());
    if (g_context != nullptr) {
        for (auto& [name, entry] : player_commands()) {
            JS_FreeValue(g_context, entry.callback);
        }
        for (auto& [name, entry] : console_commands()) {
            JS_FreeValue(g_context, entry.callback);
        }
    }
    player_commands().clear();
    console_commands().clear();
}

bool has_player_command(const std::string& message) {
    const std::string name = command_name(message);
    if (name.empty()) {
        return false;
    }
    std::lock_guard lock(command_mutex());
    return player_commands().find(name) != player_commands().end();
}

bool run_player_command(const PlayerView& player, const std::string& message, std::string& output) {
    if (g_context == nullptr) {
        return false;
    }
    const std::string name = command_name(message);
    if (name.empty()) {
        return false;
    }
    JSValue callback = JS_UNDEFINED;
    JSContext* owner = nullptr;
    int32_t permission = 0;
    {
        std::lock_guard lock(command_mutex());
        auto& commands = player_commands();
        auto it = commands.find(name);
        if (it == commands.end() || it->second.ctx == nullptr) {
            return false;
        }
        owner = it->second.ctx;
        callback = JS_DupValue(owner, it->second.callback);
        permission = it->second.permission;
    }
    if (owner == nullptr) {
        return false;
    }
    if (!player_has_permission(player, permission)) {
        JS_FreeValue(owner, callback);
        output = "§cYou do not have permission to run this command";
        return true;
    }

    const std::string args = command_args(message);
    JSValue js_player = make_player(owner, player);
    JSValue js_args = JS_NewStringLen(owner, args.data(), args.size());
    JSValue js_output = make_output(owner);
    JSValue call_args[3] = {js_player, js_args, js_output};
    JSValue result = JS_Call(owner, callback, JS_UNDEFINED, 3, call_args);
    if (JS_IsException(result)) {
        log_exception(owner);
    } else if (JS_IsString(result)) {
        output = value_to_text(owner, result);
    }
    JS_FreeValue(owner, result);
    JS_FreeValue(owner, js_player);
    JS_FreeValue(owner, js_args);
    JS_FreeValue(owner, js_output);
    JS_FreeValue(owner, callback);
    return true;
}

} // namespace yoyo::js
