#include "js_engine.h"

#include "js_internal.h"
#include "../capabilities.h"
#include "../events.h"
#include "../live.h"
#include "../log.h"
#include "../native_string.h"
#include "../packet_events.h"
#include "../players.h"
#include "../runtime.h"
#include "../worlds.h"

#include <cstring>
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
std::recursive_mutex g_mutex;
std::unordered_map<std::string, std::vector<JSValue>> g_listeners;

std::string to_std_string(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string result = text ? text : "";
    if (text) {
        JS_FreeCString(ctx, text);
    }
    return result;
}

void* player_handle(JSContext* ctx, JSValueConst object) {
    JSValue value = JS_GetPropertyStr(ctx, object, "__handle");
    int64_t handle = 0;
    if (JS_ToInt64(ctx, &handle, value) < 0) {
        handle = 0;
    }
    JS_FreeValue(ctx, value);
    return reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
}

JSValue throw_no_live_player(JSContext* ctx) {
    return JS_ThrowInternalError(ctx, "this Player is not backed by a live ServerPlayer");
}


JSValue throw_capability(JSContext* ctx, const char* feature) {
    return JS_ThrowInternalError(ctx,
                                 "%s is not verified for this BDS build; see the startup capability report",
                                 feature);
}

bool get_number_property(JSContext* ctx, JSValueConst object, const char* name, double& out) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    double number = 0.0;
    const int result = JS_ToFloat64(ctx, &number, value);
    JS_FreeValue(ctx, value);
    if (result < 0) {
        return false;
    }
    out = number;
    return true;
}

std::string normalize_event(std::string name) {
    for (char& c : name) {
        if (c == '-' || c == ' ') c = '_';
    }
    if (name == "onPreJoin" || name == "pre_join") return "onPreJoin";
    if (name == "onJoin" || name == "join") return "onJoin";
    if (name == "onChat" || name == "chat") return "onChat";
    if (name == "onLeft" || name == "left") return "onLeft";
    if (name == "onServerReady" || name == "server_ready") return "onServerReady";
    if (name == "onTick" || name == "tick") return "onTick";
    if (name == "onPlayerMove" || name == "player_move") return "onPlayerMove";
    if (name == "onPlayerChangeDimension" || name == "player_change_dimension" ||
        name == "player_dimension") {
        return "onPlayerChangeDimension";
    }
    if (name == "onJump" || name == "jump" || name == "player_jump") return "onPlayerJump";
    if (name == "onSneak" || name == "sneak" || name == "player_sneak") return "onSneak";
    if (name == "onChangeSprinting" || name == "sprint" || name == "player_sprint") {
        return "onChangeSprinting";
    }
    if (name == "onPlayerAction" || name == "player_action") return "onPlayerAction";
    if (name == "onStartDestroyBlock" || name == "start_destroy_block") return "onStartDestroyBlock";
    if (name == "onDestroyBlock" || name == "destroy_block") return "onDestroyBlock";
    if (name == "onBlockInteracted" || name == "block_interacted") return "onBlockInteracted";
    if (name == "onUseItem" || name == "use_item") return "onUseItem";
    if (name == "onUseItemOn" || name == "use_item_on") return "onUseItemOn";
    if (name == "onDropItem" || name == "drop_item" || name == "onPlayerDropItem") return "onDropItem";
    if (name == "onBedEnter" || name == "bed_enter") return "onBedEnter";
    if (name == "onBedLeave" || name == "bed_leave") return "onBedLeave";
    if (name == "onPlayerCmd" || name == "onCommand" || name == "player_command") return "onPlayerCmd";
    if (name == "onFormResponse" || name == "form_response") return "onFormResponse";
    if (name == "onPlayerSkinChange" || name == "onSkinChanged" || name == "skin_change") {
        return "onPlayerSkinChange";
    }
    if (name == "onPlayerEmote" || name == "onEmote" || name == "emote") return "onPlayerEmote";
    if (name == "onPlayerRespawn" || name == "onRespawn" || name == "player_respawn") {
        return "onPlayerRespawn";
    }
    if (name == "onPlayerTeleport" || name == "onTeleport" || name == "player_teleport") {
        return "onPlayerTeleport";
    }
    if (name == "onPlayerGameModeChange" || name == "onGameModeChange" || name == "game_mode_change") {
        return "onPlayerGameModeChange";
    }
    if (name == "onPlayerItemHeld" || name == "onItemHeld" || name == "item_held") {
        return "onPlayerItemHeld";
    }
    if (name == "onPacketReceive" || name == "onPacket" || name == "packet_receive") {
        return "onPacketReceive";
    }
    if (name == "onPlayerInteract" || name == "onPlayerInteractEntity" || name == "player_interact") {
        return "onPlayerInteract";
    }
    if (name == "onPlayerAttackEntity" || name == "onAttackEntity" || name == "attack_entity") {
        return "onPlayerAttackEntity";
    }
    if (name == "onPlayerHealthChange" || name == "onHealthChange" || name == "health_change") {
        return "onPlayerHealthChange";
    }
    if (name == "onPlayerDie" || name == "onDie" || name == "player_die") return "onPlayerDie";
    if (name == "onPacketSend" || name == "packet_send") return "onPacketSend";
    return name;
}

bool implemented_event(const std::string& name) {
    return name == "onPreJoin" || name == "onJoin" || name == "onChat" || name == "onLeft" ||
           name == "onServerReady" || name == "onTick" || name == "onPlayerMove" ||
           name == "onPlayerChangeDimension" || name == "onPlayerJump" || name == "onSneak" ||
           name == "onChangeSprinting" || name == "onPlayerAction" ||
           name == "onStartDestroyBlock" || name == "onDestroyBlock" ||
           name == "onBlockInteracted" || name == "onUseItem" || name == "onUseItemOn" ||
           name == "onDropItem" || name == "onBedEnter" || name == "onBedLeave" ||
           name == "onPlayerCmd" || name == "onFormResponse" || name == "onPlayerSkinChange" ||
           name == "onPlayerEmote" || name == "onPlayerRespawn" || name == "onPlayerTeleport" ||
           name == "onPlayerGameModeChange" || name == "onPlayerItemHeld" ||
           name == "onPacketReceive" || name == "onPlayerInteract" ||
           name == "onPlayerHealthChange" || name == "onPlayerDie" || name == "onPacketSend";
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

JSValue player_send_message(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "sendMessage(message) requires an argument");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    std::string message = to_std_string(ctx, argv[0]);
    return JS_NewBool(ctx, live_send_text(handle, message, LiveTextType::Raw));
}

JSValue player_send_title_impl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int alias) {
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    std::string title = argc > 0 ? to_std_string(ctx, argv[0]) : "";
    std::string subtitle = argc > 1 ? to_std_string(ctx, argv[1]) : "";
    int fade_in = 10;
    int stay = 70;
    int fade_out = 20;
    if (argc > 2) JS_ToInt32(ctx, &fade_in, argv[2]);
    if (argc > 3) JS_ToInt32(ctx, &stay, argv[3]);
    if (argc > 4) JS_ToInt32(ctx, &fade_out, argv[4]);
    if (alias == 0) return JS_NewBool(ctx, live_send_title(handle, title, subtitle, fade_in, stay, fade_out));
    if (alias == 1) return JS_NewBool(ctx, live_send_actionbar(handle, title));
    return JS_NewBool(ctx, live_reset_title(handle));
}

JSValue player_send_title(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return player_send_title_impl(ctx, this_val, argc, argv, 0);
}
JSValue player_send_actionbar(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return player_send_title_impl(ctx, this_val, argc, argv, 1);
}
JSValue player_reset_title(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return player_send_title_impl(ctx, this_val, 0, nullptr, 2);
}

JSValue player_send_tip(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "sendTip(message) requires an argument");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewBool(ctx, live_send_text(handle, to_std_string(ctx, argv[0]), LiveTextType::Tip));
}

JSValue player_send_popup(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "sendPopup(message) requires an argument");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewBool(ctx, live_send_text(handle, to_std_string(ctx, argv[0]), LiveTextType::Popup));
}

JSValue player_send_system(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "sendSystemMessage(message) requires an argument");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewBool(ctx, live_send_text(handle, to_std_string(ctx, argv[0]), LiveTextType::SystemMessage));
}

JSValue player_send_toast(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "sendToast(title, content) requires two arguments");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    std::string title = to_std_string(ctx, argv[0]);
    std::string content = argc > 1 ? to_std_string(ctx, argv[1]) : "";
    return JS_NewBool(ctx, live_send_toast(handle, title, content));
}

JSValue player_send_modal_form(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 4) {
        return JS_ThrowTypeError(ctx, "sendModalForm(title, content, confirm, cancel[, callback]) requires 4 arguments");
    }
    JSValue schema = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, schema, "type", JS_NewString(ctx, "modal"));
    JS_SetPropertyStr(ctx, schema, "title", JS_DupValue(ctx, argv[0]));
    JS_SetPropertyStr(ctx, schema, "content", JS_DupValue(ctx, argv[1]));
    JS_SetPropertyStr(ctx, schema, "button1", JS_DupValue(ctx, argv[2]));
    JS_SetPropertyStr(ctx, schema, "button2", JS_DupValue(ctx, argv[3]));
    JSValue json = JS_JSONStringify(ctx, schema, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, schema);
    if (JS_IsException(json)) {
        log_exception(ctx);
        return JS_EXCEPTION;
    }
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) {
        JS_FreeValue(ctx, json);
        return throw_no_live_player(ctx);
    }
    std::string text = to_std_string(ctx, json);
    JS_FreeValue(ctx, json);

    uint32_t form_id = 0;
    const bool sent = live_send_form(handle, text, &form_id);
    if (sent && argc > 4 && JS_IsFunction(ctx, argv[4])) {
        remember_form_callback(form_id, argv[4]);
    }
    return JS_NewBool(ctx, sent);
}

JSValue make_location(JSContext* ctx, const LiveVec3& position, int dimension, bool include_rotation, float pitch,
                      float yaw) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, position.x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, position.y));
    JS_SetPropertyStr(ctx, object, "z", JS_NewFloat64(ctx, position.z));
    JS_SetPropertyStr(ctx, object, "dimension", JS_NewInt32(ctx, dimension));
    JS_SetPropertyStr(ctx, object, "dimensionName", JS_NewString(ctx, live_dimension_name(dimension)));
    if (include_rotation) {
        JS_SetPropertyStr(ctx, object, "pitch", JS_NewFloat64(ctx, pitch));
        JS_SetPropertyStr(ctx, object, "yaw", JS_NewFloat64(ctx, yaw));
    }
    return object;
}

JSValue player_get_position(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_position)) return throw_capability(ctx, "getPosition");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    LiveVec3 position;
    if (!live_get_position(handle, position)) return JS_NULL;
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, position.x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, position.y));
    JS_SetPropertyStr(ctx, object, "z", JS_NewFloat64(ctx, position.z));
    return object;
}

JSValue player_get_location(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_position)) return throw_capability(ctx, "getLocation");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    LiveVec3 position;
    if (!live_get_feet_position(handle, position)) return JS_NULL;
    int dimension = live_get_dimension(handle);
    float pitch = 0.0f;
    float yaw = 0.0f;
    live_get_rotation(handle, pitch, yaw);
    return make_location(ctx, position, dimension, true, pitch, yaw);
}

JSValue player_get_rotation(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_rotation)) return throw_capability(ctx, "getRotation");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    float pitch = 0.0f;
    float yaw = 0.0f;
    if (!live_get_rotation(handle, pitch, yaw)) return JS_NULL;
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "pitch", JS_NewFloat64(ctx, pitch));
    JS_SetPropertyStr(ctx, object, "yaw", JS_NewFloat64(ctx, yaw));
    return object;
}

JSValue player_get_dimension(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_position)) return throw_capability(ctx, "getDimension");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewInt32(ctx, live_get_dimension(handle));
}

JSValue player_get_dimension_name(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_position)) return throw_capability(ctx, "getDimensionName");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewString(ctx, live_dimension_name(live_get_dimension(handle)));
}

JSValue player_get_runtime_id(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "runtimeId");
}

JSValue player_get_game_mode(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_game_mode)) return throw_capability(ctx, "getGameMode");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewInt32(ctx, live_get_game_mode(handle));
}

JSValue player_get_game_mode_name(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewString(ctx, live_game_mode_name(live_get_game_mode(handle)));
}

JSValue player_set_game_mode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setGameMode(mode) requires an argument");
    if (!capability_enabled(&VersionCapabilities::player_set_game_mode)) return throw_capability(ctx, "setGameMode");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    int32_t mode = 0;
    if (JS_IsString(argv[0])) {
        const std::string name = to_std_string(ctx, argv[0]);
        if (name == "survival") mode = 0;
        else if (name == "creative") mode = 1;
        else if (name == "adventure") mode = 2;
        else if (name == "spectator") mode = 6;
        else return JS_ThrowRangeError(ctx, "unknown game mode '%s'", name.c_str());
    } else if (JS_ToInt32(ctx, &mode, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, live_set_game_mode(handle, mode));
}

JSValue player_get_unique_id(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    if (!capability_enabled(&VersionCapabilities::player_identity)) return throw_capability(ctx, "getUniqueId");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewInt64(ctx, static_cast<int64_t>(live_unique_id(handle)));
}

JSValue player_is_sneaking(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    void* handle = player_handle(ctx, this_val);
    return JS_NewBool(ctx, handle != nullptr && packet_player_is_sneaking(handle));
}

JSValue player_is_sprinting(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    void* handle = player_handle(ctx, this_val);
    return JS_NewBool(ctx, handle != nullptr && packet_player_is_sprinting(handle));
}

JSValue player_is_flying(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    void* handle = player_handle(ctx, this_val);
    return JS_NewBool(ctx, handle != nullptr && packet_player_is_flying(handle));
}

JSValue player_get_held_slot(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    const int slot = packet_player_held_slot(handle);
    if (slot < 0) return JS_NULL;
    return JS_NewInt32(ctx, slot);
}

JSValue player_get_health(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    const int health = packet_player_health(handle);
    if (health < 0) return JS_NULL;
    return JS_NewInt32(ctx, health);
}

JSValue player_teleport(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!capability_enabled(&VersionCapabilities::player_teleport)) return throw_capability(ctx, "teleport");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    if (argc < 1) return JS_ThrowTypeError(ctx, "teleport(x, y, z[, dimension]) requires coordinates");

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int dimension = live_get_dimension(handle);
    if (JS_IsObject(argv[0])) {
        if (!get_number_property(ctx, argv[0], "x", x) || !get_number_property(ctx, argv[0], "y", y) ||
            !get_number_property(ctx, argv[0], "z", z)) {
            return JS_ThrowTypeError(ctx, "teleport(location) requires x, y and z");
        }
        int32_t value = 0;
        JSValue dim = JS_GetPropertyStr(ctx, argv[0], "dimension");
        if (!JS_IsUndefined(dim) && JS_ToInt32(ctx, &value, dim) == 0) {
            dimension = value;
        }
        JS_FreeValue(ctx, dim);
    } else {
        if (argc < 3) return JS_ThrowTypeError(ctx, "teleport(x, y, z[, dimension]) requires three coordinates");
        JS_ToFloat64(ctx, &x, argv[0]);
        JS_ToFloat64(ctx, &y, argv[1]);
        JS_ToFloat64(ctx, &z, argv[2]);
        if (argc > 3) {
            int32_t value = 0;
            if (JS_ToInt32(ctx, &value, argv[3]) == 0) dimension = value;
        }
    }
    LiveVec3 position{x, y, z};
    return JS_NewBool(ctx, live_teleport(handle, position, dimension));
}

JSValue player_transfer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "transfer(host, port) requires two arguments");
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    std::string host = to_std_string(ctx, argv[0]);
    int32_t port = 0;
    if (JS_ToInt32(ctx, &port, argv[1]) < 0) return JS_EXCEPTION;
    return JS_NewBool(ctx, live_transfer(handle, host, port));
}

JSValue player_kick(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    void* handle = player_handle(ctx, this_val);
    if (handle == nullptr) return throw_no_live_player(ctx);
    std::string reason = argc > 0 ? to_std_string(ctx, argv[0]) : "Kicked";
    return JS_NewBool(ctx, live_kick(handle, reason));
}

JSValue mc_get_online_players(JSContext* ctx, JSValueConst, int, JSValueConst*);
JSValue mc_get_online_count(JSContext* ctx, JSValueConst, int, JSValueConst*);

JSValue mc_broadcast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "broadcast(message) requires an argument");
    std::string message = to_std_string(ctx, argv[0]);
    int sent = 0;
    for (const auto& info : players_online()) {
        if (info.handle != nullptr && live_send_text(info.handle, message, LiveTextType::Raw)) {
            ++sent;
        }
    }
    return JS_NewInt32(ctx, sent);
}

JSValue mc_worlds_list(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue array = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const auto& world : worlds::list()) {
        JSValue object = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, world.name.c_str()));
        JS_SetPropertyStr(ctx, object, "port", JS_NewInt32(ctx, world.port));
        JS_SetPropertyStr(ctx, object, "level", JS_NewString(ctx, world.level.c_str()));
        JS_SetPropertyStr(ctx, object, "loaded", JS_NewBool(ctx, world.loaded));
        JS_SetPropertyStr(ctx, object, "current", JS_NewBool(ctx, world.current));
        JS_SetPropertyUint32(ctx, array, index++, object);
    }
    return array;
}

JSValue mc_worlds_load(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "worlds.load(name) requires an argument");
    return JS_NewBool(ctx, worlds::load(to_std_string(ctx, argv[0])));
}

JSValue mc_worlds_unload(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "worlds.unload(name) requires an argument");
    return JS_NewBool(ctx, worlds::unload(to_std_string(ctx, argv[0])));
}

JSValue mc_worlds_switch(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "worlds.switch(player, name) requires a player and a world");
    }
    void* handle = player_handle(ctx, argv[0]);
    if (handle == nullptr) return throw_no_live_player(ctx);
    return JS_NewBool(ctx, worlds::switch_player(handle, to_std_string(ctx, argv[1])));
}

JSValue mc_worlds_is_op(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_FALSE;
    JSValue value = JS_GetPropertyStr(ctx, argv[0], "xuid");
    std::string xuid = to_std_string(ctx, value);
    JS_FreeValue(ctx, value);
    return JS_NewBool(ctx, worlds::is_op(xuid));
}

JSValue mc_worlds_is_controller(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, worlds::is_controller());
}

JSValue mc_worlds_current(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewString(ctx, worlds::current_world().c_str());
}

JSValue mc_get_level(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, "Bedrock"));
    JS_SetPropertyStr(ctx, object, "getPlayers", JS_NewCFunction(ctx, mc_get_online_players, "getPlayers", 0));
    JS_SetPropertyStr(ctx, object, "getOnlinePlayers",
                      JS_NewCFunction(ctx, mc_get_online_players, "getOnlinePlayers", 0));
    JS_SetPropertyStr(ctx, object, "getOnlinePlayerCount",
                      JS_NewCFunction(ctx, mc_get_online_count, "getOnlinePlayerCount", 0));
    return object;
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

JSValue mc_get_dimension_id(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    const std::string name = to_std_string(ctx, argv[0]);
    if (name == "overworld" || name == "minecraft:overworld" || name == "0") return JS_NewInt32(ctx, 0);
    if (name == "nether" || name == "minecraft:nether" || name == "1") return JS_NewInt32(ctx, 1);
    if (name == "the_end" || name == "theend" || name == "minecraft:the_end" || name == "2") {
        return JS_NewInt32(ctx, 2);
    }
    return JS_NULL;
}

JSValue mc_get_dimension_name(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    int32_t dimension = 0;
    if (JS_ToInt32(ctx, &dimension, argv[0]) < 0) return JS_EXCEPTION;
    if (dimension < 0 || dimension > 2) return JS_NULL;
    return JS_NewString(ctx, live_dimension_name(dimension));
}

JSValue mc_get_bds_version(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewString(ctx, "1.26.52.3");
}

JSValue mc_get_online_player_num(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewInt32(ctx, static_cast<int32_t>(players_online().size()));
}

JSValue mc_unsupported(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return unsupported(ctx, "this mc function is not implemented for the current BDS build");
}

JSValue mc_simulate_chat(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "mc.__simulateChat(name, message) requires two arguments");
    }
    PlayerView player;
    player.name = to_std_string(ctx, argv[0]);
    std::string message = to_std_string(ctx, argv[1]);
    bool cancelled = false;
    emit_chat(player, message, cancelled);
    if (cancelled) return JS_NULL;
    return JS_NewStringLen(ctx, message.data(), message.size());
}

JSValue mc_rewrite_native(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "mc.__rewriteNative(message) requires an argument");
    std::string value = to_std_string(ctx, argv[0]);

    bool short_ok = false;
    bool long_ok = false;
    std::string short_result;
    std::string long_result;

    if (native_abi() == NativeAbi::Msvc) {
        unsigned char short_buffer[32] = {};
        std::memcpy(short_buffer, "base", 4);
        size_t size = 4;
        size_t capacity = 15;
        std::memcpy(short_buffer + 16, &size, 8);
        std::memcpy(short_buffer + 24, &capacity, 8);
        short_ok = write_native_string(short_buffer, value, NativeAbi::Msvc);
        std::memcpy(&size, short_buffer + 16, 8);
        short_result.assign(reinterpret_cast<char*>(short_buffer), size);

        std::vector<char> storage(512, '\0');
        std::memcpy(storage.data(), "base", 4);
        unsigned char long_buffer[32] = {};
        char* pointer = storage.data();
        std::memcpy(long_buffer, &pointer, 8);
        size = 4;
        capacity = 256;
        std::memcpy(long_buffer + 16, &size, 8);
        std::memcpy(long_buffer + 24, &capacity, 8);
        long_ok = write_native_string(long_buffer, value, NativeAbi::Msvc);
        std::memcpy(&size, long_buffer + 16, 8);
        long_result.assign(storage.data(), size);
    } else {
        unsigned char short_buffer[24] = {};
        short_buffer[0] = static_cast<unsigned char>(4 << 1);
        std::memcpy(short_buffer + 1, "base", 4);
        short_ok = write_native_string(short_buffer, value, NativeAbi::Libcxx);
        size_t size = short_buffer[0] >> 1;
        short_result.assign(reinterpret_cast<char*>(short_buffer + 1), size);

        std::vector<char> storage(512, '\0');
        std::memcpy(storage.data(), "base", 4);
        unsigned char long_buffer[24] = {};
        uint64_t capacity_field = 259; /* odd: long flag; max = 257 */
        size_t long_size = 4;
        char* pointer = storage.data();
        std::memcpy(long_buffer, &capacity_field, 8);
        std::memcpy(long_buffer + 8, &long_size, 8);
        std::memcpy(long_buffer + 16, &pointer, 8);
        long_ok = write_native_string(long_buffer, value, NativeAbi::Libcxx);
        std::memcpy(&long_size, long_buffer + 8, 8);
        long_result.assign(storage.data(), long_size);
    }

    std::string result = "short=" + std::string(short_ok ? "ok" : "fail") + "(" + short_result +
                         ") long=" + std::string(long_ok ? "ok" : "fail") + "(" + long_result + ")";
    return JS_NewStringLen(ctx, result.data(), result.size());
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
    JS_SetPropertyStr(ctx, mc, "getBDSVersion",
                      JS_NewCFunction(ctx, mc_get_bds_version, "getBDSVersion", 0));
    JS_SetPropertyStr(ctx, mc, "getModuleBase",
                      JS_NewCFunction(ctx, mc_get_module_base, "getModuleBase", 0));
    JS_SetPropertyStr(ctx, mc, "getDimensionId",
                      JS_NewCFunction(ctx, mc_get_dimension_id, "getDimensionId", 1));
    JS_SetPropertyStr(ctx, mc, "getDimensionName",
                      JS_NewCFunction(ctx, mc_get_dimension_name, "getDimensionName", 1));
    JS_SetPropertyStr(ctx, mc, "getOnlinePlayerNum",
                      JS_NewCFunction(ctx, mc_get_online_player_num, "getOnlinePlayerNum", 1));
    JS_SetPropertyStr(ctx, mc, "broadcast", JS_NewCFunction(ctx, mc_broadcast, "broadcast", 1));
    JS_SetPropertyStr(ctx, mc, "getLevel", JS_NewCFunction(ctx, mc_get_level, "getLevel", 0));

    JSValue worlds = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, worlds, "list", JS_NewCFunction(ctx, mc_worlds_list, "list", 0));
    JS_SetPropertyStr(ctx, worlds, "load", JS_NewCFunction(ctx, mc_worlds_load, "load", 1));
    JS_SetPropertyStr(ctx, worlds, "unload", JS_NewCFunction(ctx, mc_worlds_unload, "unload", 1));
    JS_SetPropertyStr(ctx, worlds, "switch", JS_NewCFunction(ctx, mc_worlds_switch, "switch", 2));
    JS_SetPropertyStr(ctx, worlds, "isOp", JS_NewCFunction(ctx, mc_worlds_is_op, "isOp", 1));
    JS_SetPropertyStr(ctx, worlds, "isController",
                      JS_NewCFunction(ctx, mc_worlds_is_controller, "isController", 0));
    JS_SetPropertyStr(ctx, worlds, "current", JS_NewCFunction(ctx, mc_worlds_current, "current", 0));
    JS_SetPropertyStr(ctx, mc, "worlds", worlds);
    JS_SetPropertyStr(ctx, mc, "runcmd", JS_NewCFunction(ctx, mc_unsupported, "runcmd", 1));
    JS_SetPropertyStr(ctx, mc, "runcmdEx", JS_NewCFunction(ctx, mc_unsupported, "runcmdEx", 1));
    JS_SetPropertyStr(ctx, mc, "getBlock", JS_NewCFunction(ctx, mc_unsupported, "getBlock", 1));
    JS_SetPropertyStr(ctx, mc, "getContainer", JS_NewCFunction(ctx, mc_unsupported, "getContainer", 1));
    JS_SetPropertyStr(ctx, mc, "getEntity", JS_NewCFunction(ctx, mc_unsupported, "getEntity", 1));
    JS_SetPropertyStr(ctx, mc, "__simulateChat",
                      JS_NewCFunction(ctx, mc_simulate_chat, "__simulateChat", 2));
    JS_SetPropertyStr(ctx, mc, "__rewriteNative",
                      JS_NewCFunction(ctx, mc_rewrite_native, "__rewriteNative", 1));
    JS_SetPropertyStr(ctx, mc, "version", JS_NewString(ctx, "0.2.0"));

    JSValue server = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, server, "broadcast", JS_NewCFunction(ctx, mc_broadcast, "broadcast", 1));
    JS_SetPropertyStr(ctx, server, "getOnlinePlayers",
                      JS_NewCFunction(ctx, mc_get_online_players, "getOnlinePlayers", 0));
    JS_SetPropertyStr(ctx, server, "onlinePlayers",
                      JS_NewCFunction(ctx, mc_get_online_players, "onlinePlayers", 0));
    JS_SetPropertyStr(ctx, server, "getOnlinePlayerCount",
                      JS_NewCFunction(ctx, mc_get_online_count, "getOnlinePlayerCount", 0));
    JS_SetPropertyStr(ctx, server, "getPlayer", JS_NewCFunction(ctx, mc_get_player, "getPlayer", 1));
    JS_SetPropertyStr(ctx, server, "getServerVersion",
                      JS_NewCFunction(ctx, mc_get_server_version, "getServerVersion", 0));

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "mc", mc);
    JS_SetPropertyStr(ctx, global, "server", server);
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
    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (JS_IsString(stack)) {
        const char* trace = JS_ToCString(ctx, stack);
        if (trace != nullptr) {
            log_write(LogLevel::Error, "js stack: %s", trace);
            JS_FreeCString(ctx, trace);
        }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
}

JSValue unsupported(JSContext* ctx, const char* message) {
    return JS_ThrowInternalError(ctx, "%s", message);
}

JSValue player_send_form(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "sendForm(form[, callback]) requires a form object");
    }
    JSValue function = JS_GetPropertyStr(ctx, argv[0], "sendTo");
    if (!JS_IsFunction(ctx, function)) {
        JS_FreeValue(ctx, function);
        return JS_ThrowTypeError(ctx, "form.sendTo(player) is not available");
    }
    JSValue args[2] = {JS_DupValue(ctx, this_val), JS_UNDEFINED};
    int arg_count = 1;
    if (argc > 1 && JS_IsFunction(ctx, argv[1])) {
        args[1] = JS_DupValue(ctx, argv[1]);
        arg_count = 2;
    }
    JSValue result = JS_Call(ctx, function, argv[0], arg_count, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, function);
    if (JS_IsException(result)) {
        log_exception(ctx);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, result);
    return JS_TRUE;
}

JSValue make_player(JSContext* ctx, const PlayerView& player) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, player.name.c_str()));
    JS_SetPropertyStr(ctx, object, "xuid", JS_NewString(ctx, player.xuid.c_str()));
    JS_SetPropertyStr(ctx, object, "address", JS_NewString(ctx, player.address.c_str()));
    JS_SetPropertyStr(ctx, object, "runtimeId", JS_NewInt64(ctx, static_cast<int64_t>(player.runtime_id)));
    JS_SetPropertyStr(ctx, object, "isOnline", JS_NewBool(ctx, player.handle != nullptr));
    JS_SetPropertyStr(ctx, object, "__handle", JS_NewInt64(ctx, static_cast<int64_t>(
                                                  reinterpret_cast<uintptr_t>(player.handle))));

    JS_SetPropertyStr(ctx, object, "sendMessage", JS_NewCFunction(ctx, player_send_message, "sendMessage", 1));
    JS_SetPropertyStr(ctx, object, "send_message", JS_NewCFunction(ctx, player_send_message, "send_message", 1));
    JS_SetPropertyStr(ctx, object, "sendTitle", JS_NewCFunction(ctx, player_send_title, "sendTitle", 5));
    JS_SetPropertyStr(ctx, object, "send_title", JS_NewCFunction(ctx, player_send_title, "send_title", 5));
    JS_SetPropertyStr(ctx, object, "sendTip", JS_NewCFunction(ctx, player_send_tip, "sendTip", 1));
    JS_SetPropertyStr(ctx, object, "send_tip", JS_NewCFunction(ctx, player_send_tip, "send_tip", 1));
    JS_SetPropertyStr(ctx, object, "sendPopup", JS_NewCFunction(ctx, player_send_popup, "sendPopup", 1));
    JS_SetPropertyStr(ctx, object, "send_popup", JS_NewCFunction(ctx, player_send_popup, "send_popup", 1));
    JS_SetPropertyStr(ctx, object, "sendSystemMessage",
                      JS_NewCFunction(ctx, player_send_system, "sendSystemMessage", 1));
    JS_SetPropertyStr(ctx, object, "sendToast", JS_NewCFunction(ctx, player_send_toast, "sendToast", 2));
    JS_SetPropertyStr(ctx, object, "send_toast", JS_NewCFunction(ctx, player_send_toast, "send_toast", 2));
    JS_SetPropertyStr(ctx, object, "sendActionBar",
                      JS_NewCFunction(ctx, player_send_actionbar, "sendActionBar", 1));
    JS_SetPropertyStr(ctx, object, "send_actionbar",
                      JS_NewCFunction(ctx, player_send_actionbar, "send_actionbar", 1));
    JS_SetPropertyStr(ctx, object, "resetTitle", JS_NewCFunction(ctx, player_reset_title, "resetTitle", 0));
    JS_SetPropertyStr(ctx, object, "reset_title", JS_NewCFunction(ctx, player_reset_title, "reset_title", 0));
    JS_SetPropertyStr(ctx, object, "sendForm", JS_NewCFunction(ctx, player_send_form, "sendForm", 2));
    JS_SetPropertyStr(ctx, object, "send_form", JS_NewCFunction(ctx, player_send_form, "send_form", 2));
    JS_SetPropertyStr(ctx, object, "sendModalForm",
                      JS_NewCFunction(ctx, player_send_modal_form, "sendModalForm", 5));
    JS_SetPropertyStr(ctx, object, "kick", JS_NewCFunction(ctx, player_kick, "kick", 1));
    JS_SetPropertyStr(ctx, object, "transfer", JS_NewCFunction(ctx, player_transfer, "transfer", 2));
    JS_SetPropertyStr(ctx, object, "getPosition", JS_NewCFunction(ctx, player_get_position, "getPosition", 0));
    JS_SetPropertyStr(ctx, object, "get_position", JS_NewCFunction(ctx, player_get_position, "get_position", 0));
    JS_SetPropertyStr(ctx, object, "getLocation", JS_NewCFunction(ctx, player_get_location, "getLocation", 0));
    JS_SetPropertyStr(ctx, object, "get_location", JS_NewCFunction(ctx, player_get_location, "get_location", 0));
    JS_SetPropertyStr(ctx, object, "getRotation", JS_NewCFunction(ctx, player_get_rotation, "getRotation", 0));
    JS_SetPropertyStr(ctx, object, "getDimension", JS_NewCFunction(ctx, player_get_dimension, "getDimension", 0));
    JS_SetPropertyStr(ctx, object, "get_dimension",
                      JS_NewCFunction(ctx, player_get_dimension, "get_dimension", 0));
    JS_SetPropertyStr(ctx, object, "getDimensionName",
                      JS_NewCFunction(ctx, player_get_dimension_name, "getDimensionName", 0));
    JS_SetPropertyStr(ctx, object, "getRuntimeId",
                      JS_NewCFunction(ctx, player_get_runtime_id, "getRuntimeId", 0));
    JS_SetPropertyStr(ctx, object, "getUniqueId",
                      JS_NewCFunction(ctx, player_get_unique_id, "getUniqueId", 0));
    JS_SetPropertyStr(ctx, object, "getGameMode",
                      JS_NewCFunction(ctx, player_get_game_mode, "getGameMode", 0));
    JS_SetPropertyStr(ctx, object, "get_game_mode",
                      JS_NewCFunction(ctx, player_get_game_mode, "get_game_mode", 0));
    JS_SetPropertyStr(ctx, object, "getGameModeName",
                      JS_NewCFunction(ctx, player_get_game_mode_name, "getGameModeName", 0));
    JS_SetPropertyStr(ctx, object, "setGameMode",
                      JS_NewCFunction(ctx, player_set_game_mode, "setGameMode", 1));
    JS_SetPropertyStr(ctx, object, "set_game_mode",
                      JS_NewCFunction(ctx, player_set_game_mode, "set_game_mode", 1));
    JS_SetPropertyStr(ctx, object, "teleport", JS_NewCFunction(ctx, player_teleport, "teleport", 4));
    JS_SetPropertyStr(ctx, object, "isSneaking", JS_NewCFunction(ctx, player_is_sneaking, "isSneaking", 0));
    JS_SetPropertyStr(ctx, object, "is_sneaking", JS_NewCFunction(ctx, player_is_sneaking, "is_sneaking", 0));
    JS_SetPropertyStr(ctx, object, "isSprinting", JS_NewCFunction(ctx, player_is_sprinting, "isSprinting", 0));
    JS_SetPropertyStr(ctx, object, "is_sprinting", JS_NewCFunction(ctx, player_is_sprinting, "is_sprinting", 0));
    JS_SetPropertyStr(ctx, object, "isFlying", JS_NewCFunction(ctx, player_is_flying, "isFlying", 0));
    JS_SetPropertyStr(ctx, object, "is_flying", JS_NewCFunction(ctx, player_is_flying, "is_flying", 0));
    JS_SetPropertyStr(ctx, object, "getHeldItemSlot",
                      JS_NewCFunction(ctx, player_get_held_slot, "getHeldItemSlot", 0));
    JS_SetPropertyStr(ctx, object, "get_held_item_slot",
                      JS_NewCFunction(ctx, player_get_held_slot, "get_held_item_slot", 0));
    JS_SetPropertyStr(ctx, object, "getHealth", JS_NewCFunction(ctx, player_get_health, "getHealth", 0));
    JS_SetPropertyStr(ctx, object, "get_health", JS_NewCFunction(ctx, player_get_health, "get_health", 0));
    JS_SetPropertyStr(ctx, object, "getNbt", JS_NewCFunction(ctx, player_unsupported, "getNbt", 0));
    JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, player_to_string, "toString", 0));
    return object;
}

bool has_listener(std::string_view event) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return false;
    const std::string name = normalize_event(std::string(event));
    auto it = g_listeners.find(name);
    return it != g_listeners.end() && !it->second.empty();
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

    log_write(LogLevel::Info, "JS registering mc");
    register_mc(g_context);
    log_write(LogLevel::Info, "JS registering NBT");
    register_nbt(g_context);
    log_write(LogLevel::Info, "JS registering forms");
    register_forms(g_context);
    log_write(LogLevel::Info, "JS registering timers and helpers");
    register_timers(g_context);
    register_script_helpers(g_context);
    register_commands(g_context);
    log_write(LogLevel::Info, "JS loading plugins");
    load_js_plugins(g_context);
    log_write(LogLevel::Info, "QuickJS ready");
    return true;
}

void stop() {
    std::lock_guard lock(g_mutex);
    if (g_context != nullptr) {
        clear_timers();
        clear_form_callbacks();
        clear_commands();
        close_files();
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
    JS_UpdateStackTop(g_runtime);
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
    JS_UpdateStackTop(g_runtime);
    JSValue js_player = make_player(g_context, player);
    JSValue args[1] = {js_player};
    call_listeners_locked("onJoin", 1, args);
    JS_FreeValue(g_context, js_player);
}

void dispatch_chat(const PlayerView& player, std::string& message, bool& cancelled) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JS_UpdateStackTop(g_runtime);
    JSValue js_player = make_player(g_context, player);
    JSValue js_message = JS_NewStringLen(g_context, message.data(), message.size());
    JSValue event = JS_NewObject(g_context);
    JS_SetPropertyStr(g_context, event, "cancelled", JS_FALSE);
    JS_SetPropertyStr(g_context, event, "message",
                      JS_NewStringLen(g_context, message.data(), message.size()));
    JSValue args[3] = {js_player, js_message, event};
    bool result = call_listeners_locked("onChat", 3, args);
    JSValue flag = JS_GetPropertyStr(g_context, event, "cancelled");
    if (JS_ToBool(g_context, flag) == 1) result = true;
    JS_FreeValue(g_context, flag);
    JSValue updated = JS_GetPropertyStr(g_context, event, "message");
    if (JS_IsString(updated)) {
        message = to_std_string(g_context, updated);
    }
    JS_FreeValue(g_context, updated);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, js_message);
    JS_FreeValue(g_context, event);
    if (result) cancelled = true;
}

void dispatch_left(const PlayerView& player, std::string_view reason) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JS_UpdateStackTop(g_runtime);
    JSValue js_player = make_player(g_context, player);
    JSValue js_reason = JS_NewStringLen(g_context, reason.data(), reason.size());
    JSValue args[2] = {js_player, js_reason};
    call_listeners_locked("onLeft", 2, args);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, js_reason);
}

void dispatch_server_ready(std::string_view version, uintptr_t module_base) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JS_UpdateStackTop(g_runtime);
    JSValue event = JS_NewObject(g_context);
    JS_SetPropertyStr(g_context, event, "serverVersion",
                      JS_NewStringLen(g_context, version.data(), version.size()));
    JS_SetPropertyStr(g_context, event, "moduleBase",
                      JS_NewInt64(g_context, static_cast<int64_t>(module_base)));
    JSValue args[1] = {event};
    call_listeners_locked("onServerReady", 1, args);
    JS_FreeValue(g_context, event);
}

void dispatch_tick(uint64_t tick) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    JS_UpdateStackTop(g_runtime);
    auto it = g_listeners.find("onTick");
    if (it != g_listeners.end() && !it->second.empty()) {
        JSValue event = JS_NewObject(g_context);
        JS_SetPropertyStr(g_context, event, "tick", JS_NewInt64(g_context, static_cast<int64_t>(tick)));
        JSValue args[1] = {event};
        call_listeners_locked("onTick", 1, args);
        JS_FreeValue(g_context, event);
    }
    /* Timers are drained after onTick on the game thread, so timer callbacks
     * may send packets or touch world state. */
    run_timers();
}

void dispatch_player_move(const PlayerView& player, const LiveVec3& from, const LiveVec3& to,
                          int from_dimension, int to_dimension) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    auto it = g_listeners.find("onPlayerMove");
    if (it == g_listeners.end() || it->second.empty()) return;
    JS_UpdateStackTop(g_runtime);
    JSValue js_player = make_player(g_context, player);
    JSValue js_from = make_location(g_context, from, from_dimension, false, 0.0f, 0.0f);
    JSValue js_to = make_location(g_context, to, to_dimension, false, 0.0f, 0.0f);
    JSValue args[3] = {js_player, js_from, js_to};
    call_listeners_locked("onPlayerMove", 3, args);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, js_from);
    JS_FreeValue(g_context, js_to);
}

void dispatch_player_dimension_change(const PlayerView& player, int from_dimension, int to_dimension) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    auto it = g_listeners.find("onPlayerChangeDimension");
    if (it == g_listeners.end() || it->second.empty()) return;
    JS_UpdateStackTop(g_runtime);
    JSValue js_player = make_player(g_context, player);
    JSValue args[3] = {js_player, JS_NewInt32(g_context, from_dimension),
                       JS_NewInt32(g_context, to_dimension)};
    call_listeners_locked("onPlayerChangeDimension", 3, args);
    JS_FreeValue(g_context, args[0]);
    JS_FreeValue(g_context, args[1]);
    JS_FreeValue(g_context, args[2]);
}

namespace {

const char* detail_event_name(int type) {
    switch (type) {
    case YOYO_EVENT_PLAYER_JUMP: return "onPlayerJump";
    case YOYO_EVENT_PLAYER_SNEAK: return "onSneak";
    case YOYO_EVENT_PLAYER_SPRINT: return "onChangeSprinting";
    case YOYO_EVENT_PLAYER_ACTION: return "onPlayerAction";
    case YOYO_EVENT_BLOCK_START_DESTROY: return "onStartDestroyBlock";
    case YOYO_EVENT_BLOCK_DESTROY: return "onDestroyBlock";
    case YOYO_EVENT_BLOCK_INTERACT: return "onBlockInteracted";
    case YOYO_EVENT_ITEM_USE: return "onUseItem";
    case YOYO_EVENT_ITEM_USE_ON: return "onUseItemOn";
    case YOYO_EVENT_ITEM_DROP: return "onDropItem";
    case YOYO_EVENT_BED_ENTER: return "onBedEnter";
    case YOYO_EVENT_BED_LEAVE: return "onBedLeave";
    case YOYO_EVENT_PLAYER_COMMAND: return "onPlayerCmd";
    case YOYO_EVENT_FORM_RESPONSE: return "onFormResponse";
    case YOYO_EVENT_PLAYER_SKIN: return "onPlayerSkinChange";
    case YOYO_EVENT_PLAYER_EMOTE: return "onPlayerEmote";
    case YOYO_EVENT_PLAYER_RESPAWN: return "onPlayerRespawn";
    case YOYO_EVENT_PLAYER_TELEPORT: return "onPlayerTeleport";
    case YOYO_EVENT_PLAYER_GAME_MODE: return "onPlayerGameModeChange";
    case YOYO_EVENT_PLAYER_ITEM_HELD: return "onPlayerItemHeld";
    case YOYO_EVENT_PACKET_RECEIVE: return "onPacketReceive";
    case YOYO_EVENT_PLAYER_INTERACT: return "onPlayerInteract";
    case YOYO_EVENT_PLAYER_ATTACK: return "onPlayerAttackEntity";
    case YOYO_EVENT_PLAYER_HEALTH: return "onPlayerHealthChange";
    case YOYO_EVENT_PLAYER_DIE: return "onPlayerDie";
    case YOYO_EVENT_PACKET_SEND: return "onPacketSend";
    default: return nullptr;
    }
}

JSValue make_position(JSContext* ctx, int x, int y, int z, int dimension) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewInt32(ctx, y));
    JS_SetPropertyStr(ctx, object, "z", JS_NewInt32(ctx, z));
    JS_SetPropertyStr(ctx, object, "dimension", JS_NewInt32(ctx, dimension));
    JS_SetPropertyStr(ctx, object, "dimensionName", JS_NewString(ctx, live_dimension_name(dimension)));
    return object;
}

} // namespace

void dispatch_detail(int type, const PlayerView& player, const DetailEvent& detail, bool& cancelled) {
    std::lock_guard lock(g_mutex);
    if (g_context == nullptr) return;
    const char* name = detail_event_name(type);
    if (name == nullptr) return;
    auto it = g_listeners.find(name);
    if (it == g_listeners.end() || it->second.empty()) return;
    JS_UpdateStackTop(g_runtime);

    const int dimension = player.handle != nullptr ? live_get_dimension(player.handle) : -9999;
    JSValue js_player = make_player(g_context, player);
    JSValue event = JS_NewObject(g_context);
    JS_SetPropertyStr(g_context, event, "cancelled", JS_FALSE);
    JS_SetPropertyStr(g_context, event, "packetId", JS_NewInt32(g_context, detail.packet_id));
    JS_SetPropertyStr(g_context, event, "action", JS_NewInt32(g_context, detail.action));

    JSValue args[6] = {};
    int argc = 0;
    args[argc++] = js_player;
    switch (type) {
    case YOYO_EVENT_PLAYER_JUMP:
    case YOYO_EVENT_ITEM_USE:
    case YOYO_EVENT_ITEM_DROP:
    case YOYO_EVENT_PLAYER_RESPAWN:
    case YOYO_EVENT_PLAYER_DIE:
        break;
    case YOYO_EVENT_PLAYER_SNEAK:
    case YOYO_EVENT_PLAYER_SPRINT:
        args[argc++] = JS_NewBool(g_context, detail.value != 0);
        break;
    case YOYO_EVENT_PLAYER_ACTION:
        args[argc++] = JS_NewInt32(g_context, detail.action);
        args[argc++] = make_position(g_context, detail.x, detail.y, detail.z, dimension);
        break;
    case YOYO_EVENT_BLOCK_START_DESTROY:
    case YOYO_EVENT_BLOCK_DESTROY:
    case YOYO_EVENT_BLOCK_INTERACT:
    case YOYO_EVENT_ITEM_USE_ON:
    case YOYO_EVENT_BED_ENTER:
    case YOYO_EVENT_BED_LEAVE:
    case YOYO_EVENT_PLAYER_INTERACT:
    case YOYO_EVENT_PLAYER_ATTACK:
        args[argc++] = make_position(g_context, detail.x, detail.y, detail.z, dimension);
        break;
    case YOYO_EVENT_PLAYER_COMMAND:
        args[argc++] = JS_NewStringLen(g_context, detail.text.data(), detail.text.size());
        break;
    case YOYO_EVENT_FORM_RESPONSE:
        args[argc++] = JS_NewInt32(g_context, detail.value);
        args[argc++] = JS_NewStringLen(g_context, detail.text.data(), detail.text.size());
        break;
    case YOYO_EVENT_PLAYER_SKIN:
        args[argc++] = JS_NewStringLen(g_context, detail.text.data(), detail.text.size());
        break;
    case YOYO_EVENT_PLAYER_EMOTE:
        args[argc++] = JS_NewStringLen(g_context, detail.text.data(), detail.text.size());
        args[argc++] = JS_NewStringLen(g_context, detail.text2.data(), detail.text2.size());
        break;
    case YOYO_EVENT_PLAYER_TELEPORT: {
        LiveVec3 position;
        if (player.handle == nullptr || !live_get_position(player.handle, position)) {
            position = {static_cast<double>(detail.x), static_cast<double>(detail.y),
                        static_cast<double>(detail.z)};
        }
        args[argc++] = make_location(g_context, position, dimension, false, 0.0f, 0.0f);
        break;
    }
    case YOYO_EVENT_PLAYER_GAME_MODE:
    case YOYO_EVENT_PLAYER_HEALTH:
        args[argc++] = JS_NewInt32(g_context, detail.value);
        break;
    case YOYO_EVENT_PLAYER_ITEM_HELD:
        args[argc++] = JS_NewInt32(g_context, detail.slot);
        args[argc++] = JS_NewInt32(g_context, detail.value);
        break;
    case YOYO_EVENT_PACKET_RECEIVE:
    case YOYO_EVENT_PACKET_SEND:
        args[argc++] = JS_NewInt32(g_context, detail.packet_id);
        break;
    default:
        break;
    }
    args[argc++] = event;

    bool result = call_listeners_locked(name, argc, args);
    JSValue flag = JS_GetPropertyStr(g_context, event, "cancelled");
    if (JS_ToBool(g_context, flag) == 1) result = true;
    JS_FreeValue(g_context, flag);
    for (int i = 0; i < argc; ++i) {
        JS_FreeValue(g_context, args[i]);
    }
    if (result) cancelled = true;
}

} // namespace yoyo::js
