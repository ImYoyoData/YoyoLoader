#include "js_internal.h"

#include "../live.h"
#include "../log.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

namespace yoyo::js {
namespace {

std::string text_of(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string out = text ? text : "";
    if (text) {
        JS_FreeCString(ctx, text);
    }
    return out;
}

int32_t int_of(JSContext* ctx, JSValueConst value, int32_t fallback = 0) {
    int32_t out = fallback;
    if (JS_ToInt32(ctx, &out, value) < 0) {
        return fallback;
    }
    return out;
}

double number_of(JSContext* ctx, JSValueConst value, double fallback = 0.0) {
    double out = fallback;
    if (JS_ToFloat64(ctx, &out, value) < 0) {
        return fallback;
    }
    return out;
}

} // namespace

/*
 * LegacyScriptEngine names dimensions with display strings, while the numeric
 * id is what all APIs take.  Both are exposed so plugin code written for
 * either convention works.
 */
const char* llse_dimension_name(int dimension) {
    switch (dimension) {
    case 0: return "主世界";
    case 1: return "下界";
    case 2: return "末地";
    default: return "未知";
    }
}

std::string llse_dimension_name_utf8(int dimension) {
    switch (dimension) {
    case 0: return "\xE4\xB8\xBB\xE4\xB8\x96\xE7\x95\x8C"; /* 主世界 */
    case 1: return "\xE4\xB8\x8B\xE7\x95\x8C";             /* 下界 */
    case 2: return "\xE6\x9C\xAB\xE5\x9C\xB0";             /* 末地 */
    default: return "\xE6\x9C\xAA\xE7\x9F\xA5";            /* 未知 */
    }
}

bool parse_llse_dimension(JSContext* ctx, JSValueConst value, int& out) {
    if (JS_IsNumber(value)) {
        out = int_of(ctx, value, 0);
        return true;
    }
    if (!JS_IsString(value)) {
        return false;
    }
    const std::string name = text_of(ctx, value);
    if (name == "overworld" || name == "minecraft:overworld" || name == "主世界") {
        out = 0;
        return true;
    }
    if (name == "nether" || name == "minecraft:nether" || name == "下界") {
        out = 1;
        return true;
    }
    if (name == "the_end" || name == "theend" || name == "minecraft:the_end" || name == "末地") {
        out = 2;
        return true;
    }
    return false;
}

/*
 * IntPos / FloatPos: x, y, z, dim (display string), dimid (integer).
 * `dimension` and `dimensionName` are kept as aliases for existing YoyoLoader
 * plugins; new code should use dimid / dim.
 */
JSValue make_int_pos(JSContext* ctx, int x, int y, int z, int dimid) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewInt32(ctx, y));
    JS_SetPropertyStr(ctx, object, "z", JS_NewInt32(ctx, z));
    JS_SetPropertyStr(ctx, object, "dimid", JS_NewInt32(ctx, dimid));
    JS_SetPropertyStr(ctx, object, "dim", JS_NewString(ctx, llse_dimension_name_utf8(dimid).c_str()));
    JS_SetPropertyStr(ctx, object, "dimension", JS_NewInt32(ctx, dimid));
    JS_SetPropertyStr(ctx, object, "dimensionName", JS_NewString(ctx, live_dimension_name(dimid)));
    JS_SetPropertyStr(ctx, object, "toString",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst this_val, int, JSValueConst*) {
                                          JSValue x = JS_GetPropertyStr(c, this_val, "x");
                                          JSValue y = JS_GetPropertyStr(c, this_val, "y");
                                          JSValue z = JS_GetPropertyStr(c, this_val, "z");
                                          JSValue d = JS_GetPropertyStr(c, this_val, "dimid");
                                          int32_t xi = 0, yi = 0, zi = 0, di = 0;
                                          JS_ToInt32(c, &xi, x);
                                          JS_ToInt32(c, &yi, y);
                                          JS_ToInt32(c, &zi, z);
                                          JS_ToInt32(c, &di, d);
                                          JS_FreeValue(c, x);
                                          JS_FreeValue(c, y);
                                          JS_FreeValue(c, z);
                                          JS_FreeValue(c, d);
                                          char buffer[96];
                                          std::snprintf(buffer, sizeof(buffer), "(%d, %d, %d, dimid %d)", xi, yi,
                                                        zi, di);
                                          return JS_NewString(c, buffer);
                                      },
                                      "toString", 0));
    return object;
}

JSValue make_float_pos(JSContext* ctx, double x, double y, double z, int dimid) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, object, "z", JS_NewFloat64(ctx, z));
    JS_SetPropertyStr(ctx, object, "dimid", JS_NewInt32(ctx, dimid));
    JS_SetPropertyStr(ctx, object, "dim", JS_NewString(ctx, llse_dimension_name_utf8(dimid).c_str()));
    JS_SetPropertyStr(ctx, object, "dimension", JS_NewInt32(ctx, dimid));
    JS_SetPropertyStr(ctx, object, "dimensionName", JS_NewString(ctx, live_dimension_name(dimid)));
    JS_SetPropertyStr(ctx, object, "toString",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst this_val, int, JSValueConst*) {
                                          JSValue x = JS_GetPropertyStr(c, this_val, "x");
                                          JSValue y = JS_GetPropertyStr(c, this_val, "y");
                                          JSValue z = JS_GetPropertyStr(c, this_val, "z");
                                          JSValue d = JS_GetPropertyStr(c, this_val, "dimid");
                                          double xd = 0, yd = 0, zd = 0;
                                          int32_t di = 0;
                                          JS_ToFloat64(c, &xd, x);
                                          JS_ToFloat64(c, &yd, y);
                                          JS_ToFloat64(c, &zd, z);
                                          JS_ToInt32(c, &di, d);
                                          JS_FreeValue(c, x);
                                          JS_FreeValue(c, y);
                                          JS_FreeValue(c, z);
                                          JS_FreeValue(c, d);
                                          char buffer[128];
                                          std::snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f, dimid %d)", xd,
                                                        yd, zd, di);
                                          return JS_NewString(c, buffer);
                                      },
                                      "toString", 0));
    return object;
}

JSValue make_direction(JSContext* ctx, double pitch, double yaw) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "pitch", JS_NewFloat64(ctx, pitch));
    JS_SetPropertyStr(ctx, object, "yaw", JS_NewFloat64(ctx, yaw));
    /* toFacing: 0 = north, 1 = east, 2 = south, 3 = west. */
    JS_SetPropertyStr(ctx, object, "toFacing",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst this_val, int, JSValueConst*) -> JSValue {
                                          JSValue yaw_value = JS_GetPropertyStr(c, this_val, "yaw");
                                          double yaw_degrees = 0.0;
                                          JS_ToFloat64(c, &yaw_degrees, yaw_value);
                                          JS_FreeValue(c, yaw_value);
                                          const double normalized =
                                              std::fmod(std::fmod(yaw_degrees, 360.0) + 360.0, 360.0);
                                          int facing = 0;
                                          if (normalized >= 45.0 && normalized < 135.0) {
                                              facing = 1;
                                          } else if (normalized >= 135.0 && normalized < 225.0) {
                                              facing = 2;
                                          } else if (normalized >= 225.0 && normalized < 315.0) {
                                              facing = 3;
                                          }
                                          return JS_NewInt32(c, facing);
                                      },
                                      "toFacing", 0));
    return object;
}

/*
 * Accept either (x, y, z[, dimid]) or a position object.  Returns false when
 * the arguments do not describe a position, so callers can raise a TypeError
 * with their own message.
 */
bool read_position_args(JSContext* ctx, int argc, JSValueConst* argv, double& x, double& y, double& z,
                        int& dimid, int dimid_default) {
    dimid = dimid_default;
    if (argc >= 3 && JS_IsNumber(argv[0]) && JS_IsNumber(argv[1]) && JS_IsNumber(argv[2])) {
        x = number_of(ctx, argv[0]);
        y = number_of(ctx, argv[1]);
        z = number_of(ctx, argv[2]);
        if (argc >= 4) {
            if (!parse_llse_dimension(ctx, argv[3], dimid)) {
                return false;
            }
        }
        return true;
    }
    if (argc >= 1 && JS_IsObject(argv[0])) {
        JSValue vx = JS_GetPropertyStr(ctx, argv[0], "x");
        JSValue vy = JS_GetPropertyStr(ctx, argv[0], "y");
        JSValue vz = JS_GetPropertyStr(ctx, argv[0], "z");
        const bool numeric = JS_IsNumber(vx) && JS_IsNumber(vy) && JS_IsNumber(vz);
        if (numeric) {
            x = number_of(ctx, vx);
            y = number_of(ctx, vy);
            z = number_of(ctx, vz);
        }
        JS_FreeValue(ctx, vx);
        JS_FreeValue(ctx, vy);
        JS_FreeValue(ctx, vz);
        if (!numeric) {
            return false;
        }
        JSValue vdim = JS_GetPropertyStr(ctx, argv[0], "dimid");
        if (JS_IsNumber(vdim)) {
            parse_llse_dimension(ctx, vdim, dimid);
        } else {
            JS_FreeValue(ctx, vdim);
            vdim = JS_GetPropertyStr(ctx, argv[0], "dimension");
            if (JS_IsNumber(vdim)) {
                parse_llse_dimension(ctx, vdim, dimid);
            }
        }
        JS_FreeValue(ctx, vdim);
        return true;
    }
    return false;
}

JSValue make_block_object(JSContext* ctx, int x, int y, int z, int dimid) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, "未知方块"));
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, "minecraft:unknown"));
    JS_SetPropertyStr(ctx, object, "id", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, object, "pos", make_int_pos(ctx, x, y, z, dimid));
    JS_SetPropertyStr(ctx, object, "tileData", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, object, "isAir", JS_FALSE);
    JS_SetPropertyStr(ctx, object, "getType",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst this_val, int, JSValueConst*) {
                                          return JS_GetPropertyStr(c, this_val, "type");
                                      },
                                      "getType", 0));
    JS_SetPropertyStr(ctx, object, "getPos",
                      JS_NewCFunction(ctx,
                                      [](JSContext* c, JSValueConst this_val, int, JSValueConst*) {
                                          return JS_GetPropertyStr(c, this_val, "pos");
                                      },
                                      "getPos", 0));
    return object;
}

namespace {

JSValue position_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv,
                             bool floating) {
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "%s(x, y, z[, dimid]) requires three coordinates",
                                 floating ? "FloatPos" : "IntPos");
    }
    int dimid = 0;
    if (argc >= 4 && !parse_llse_dimension(ctx, argv[3], dimid)) {
        return JS_ThrowTypeError(ctx, "dimid must be a number or a dimension name");
    }
    JSValue object = floating
                         ? make_float_pos(ctx, number_of(ctx, argv[0]), number_of(ctx, argv[1]),
                                          number_of(ctx, argv[2]), dimid)
                         : make_int_pos(ctx, int_of(ctx, argv[0]), int_of(ctx, argv[1]),
                                        int_of(ctx, argv[2]), dimid);
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsObject(proto)) {
        JS_SetPrototype(ctx, object, proto);
        JS_FreeValue(ctx, proto);
    }
    return object;
}

JSValue int_pos_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    return position_constructor(ctx, new_target, argc, argv, false);
}

JSValue float_pos_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    return position_constructor(ctx, new_target, argc, argv, true);
}

JSValue direction_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "DirectionAngle(pitch, yaw) requires two values");
    }
    JSValue object = make_direction(ctx, number_of(ctx, argv[0]), number_of(ctx, argv[1]));
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsObject(proto)) {
        JS_SetPrototype(ctx, object, proto);
        JS_FreeValue(ctx, proto);
    }
    return object;
}

} // namespace

void register_llse_objects(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "IntPos",
                      JS_NewCFunction2(ctx, int_pos_constructor, "IntPos", 4, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "FloatPos",
                      JS_NewCFunction2(ctx, float_pos_constructor, "FloatPos", 4, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "DirectionAngle",
                      JS_NewCFunction2(ctx, direction_constructor, "DirectionAngle", 2, JS_CFUNC_constructor, 0));
    JS_FreeValue(ctx, global);
}

} // namespace yoyo::js
