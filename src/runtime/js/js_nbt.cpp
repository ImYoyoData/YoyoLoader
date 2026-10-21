#include "js_internal.h"
#include "nbt/nbt.h"

#include <cmath>
#include <string>

namespace yoyo::js {
namespace {

JSClassID g_nbt_class = 0;

using nbt::Tag;
using nbt::TagPtr;
using nbt::Type;

std::string text_of(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string out = text ? text : "";
    if (text) JS_FreeCString(ctx, text);
    return out;
}

Tag* tag_of(JSContext* ctx, JSValueConst value) {
    return static_cast<Tag*>(JS_GetOpaque2(ctx, value, g_nbt_class));
}

void nbt_finalizer(JSRuntime*, JSValue value) {
    delete static_cast<Tag*>(JS_GetOpaque(value, g_nbt_class));
}

JSValue wrap(JSContext* ctx, const TagPtr& tag) {
    JSValue object = JS_NewObjectClass(ctx, g_nbt_class);
    if (JS_IsException(object)) return object;
    JS_SetOpaque(object, new Tag(*tag));
    return object;
}

JSValue tag_to_js(JSContext* ctx, const Tag& tag);
TagPtr js_to_tag(JSContext* ctx, JSValueConst value, int depth);

JSValue tag_to_js(JSContext* ctx, const Tag& tag) {
    switch (tag.type) {
    case Type::Byte:
    case Type::Short:
    case Type::Int:
        return JS_NewInt32(ctx, static_cast<int32_t>(tag.integer));
    case Type::Long:
        return JS_NewInt64(ctx, tag.integer);
    case Type::Float:
    case Type::Double:
        return JS_NewFloat64(ctx, tag.floating);
    case Type::String:
        return JS_NewStringLen(ctx, tag.text.data(), tag.text.size());
    case Type::ByteArray: {
        JSValue array = JS_NewArray(ctx);
        uint32_t index = 0;
        for (uint8_t value : tag.byte_array) {
            JS_SetPropertyUint32(ctx, array, index++, JS_NewInt32(ctx, value));
        }
        return array;
    }
    case Type::IntArray: {
        JSValue array = JS_NewArray(ctx);
        uint32_t index = 0;
        for (int32_t value : tag.int_array) {
            JS_SetPropertyUint32(ctx, array, index++, JS_NewInt32(ctx, value));
        }
        return array;
    }
    case Type::LongArray: {
        JSValue array = JS_NewArray(ctx);
        uint32_t index = 0;
        for (int64_t value : tag.long_array) {
            JS_SetPropertyUint32(ctx, array, index++, JS_NewInt64(ctx, value));
        }
        return array;
    }
    case Type::List: {
        JSValue array = JS_NewArray(ctx);
        uint32_t index = 0;
        for (const auto& element : tag.list) {
            if (element) {
                JS_SetPropertyUint32(ctx, array, index++, tag_to_js(ctx, *element));
            }
        }
        return array;
    }
    case Type::Compound: {
        JSValue object = JS_NewObject(ctx);
        for (const auto& [name, child] : tag.compound) {
            if (!child) continue;
            JS_SetPropertyStr(ctx, object, name.c_str(), tag_to_js(ctx, *child));
        }
        return object;
    }
    default:
        return JS_NULL;
    }
}

TagPtr js_to_tag(JSContext* ctx, JSValueConst value, int depth) {
    if (depth > 64) {
        JS_ThrowRangeError(ctx, "NBT nesting is too deep");
        return nullptr;
    }
    if (JS_IsBool(value)) {
        return nbt::make_byte(JS_ToBool(ctx, value) ? 1 : 0);
    }
    if (JS_IsNumber(value)) {
        double number = 0;
        if (JS_ToFloat64(ctx, &number, value) < 0) return nullptr;
        if (std::isfinite(number) && std::floor(number) == number) {
            if (number >= INT32_MIN && number <= INT32_MAX) {
                return nbt::make_int(static_cast<int32_t>(number));
            }
            if (number >= static_cast<double>(INT64_MIN) && number <= static_cast<double>(INT64_MAX)) {
                return nbt::make_long(static_cast<int64_t>(number));
            }
        }
        return nbt::make_double(number);
    }
    if (JS_IsString(value)) {
        return nbt::make_string(text_of(ctx, value));
    }
    if (JS_IsArray(value)) {
        JSValue length_value = JS_GetPropertyStr(ctx, value, "length");
        int32_t length = 0;
        JS_ToInt32(ctx, &length, length_value);
        JS_FreeValue(ctx, length_value);
        if (length < 0 || length > 4 * 1024 * 1024) {
            JS_ThrowRangeError(ctx, "NBT array is too large");
            return nullptr;
        }
        auto list = nbt::make_list();
        Type element_type = Type::End;
        for (int32_t i = 0; i < length; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, value, static_cast<uint32_t>(i));
            auto child = js_to_tag(ctx, item, depth + 1);
            JS_FreeValue(ctx, item);
            if (!child) return nullptr;
            if (element_type == Type::End) element_type = child->type;
            list->list.push_back(std::move(child));
        }
        list->integer = static_cast<int64_t>(element_type);
        return list;
    }
    if (JS_IsObject(value)) {
        JSPropertyEnum* properties = nullptr;
        uint32_t count = 0;
        if (JS_GetOwnPropertyNames(ctx, &properties, &count, value,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            return nullptr;
        }
        auto compound = nbt::make_compound();
        for (uint32_t i = 0; i < count; ++i) {
            const char* key = JS_AtomToCString(ctx, properties[i].atom);
            if (key == nullptr) continue;
            JSValue item = JS_GetProperty(ctx, value, properties[i].atom);
            auto child = js_to_tag(ctx, item, depth + 1);
            JS_FreeValue(ctx, item);
            if (child) {
                compound->compound.emplace_back(key, std::move(child));
            }
            JS_FreeCString(ctx, key);
        }
        JS_FreePropertyEnum(ctx, properties, count);
        return compound;
    }
    if (JS_IsNull(value) || JS_IsUndefined(value)) {
        return nullptr;
    }
    JS_ThrowTypeError(ctx, "value cannot be converted to NBT");
    return nullptr;
}

JSValue nbt_ctor(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return wrap(ctx, nbt::make_compound());
}

JSValue nbt_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 1) return JS_NULL;
    auto child = tag->find(text_of(ctx, argv[0]));
    return child ? tag_to_js(ctx, *child) : JS_NULL;
}

JSValue nbt_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "nbt.set(key, value) requires two arguments");
    auto child = js_to_tag(ctx, argv[1], 0);
    if (!child) {
        if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
            tag->erase(text_of(ctx, argv[0]));
            return JS_DupValue(ctx, this_val);
        }
        return JS_EXCEPTION;
    }
    tag->set(text_of(ctx, argv[0]), std::move(child));
    return JS_DupValue(ctx, this_val);
}

JSValue nbt_get_tag(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 1) return JS_NULL;
    auto child = tag->find(text_of(ctx, argv[0]));
    return child ? wrap(ctx, child) : JS_NULL;
}

JSValue nbt_set_tag(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "nbt.setTag(key, nbt) requires two arguments");
    Tag* child = tag_of(ctx, argv[1]);
    if (!child) return JS_ThrowTypeError(ctx, "second argument must be an NBT object");
    tag->set(text_of(ctx, argv[0]), std::make_shared<Tag>(*child));
    return JS_DupValue(ctx, this_val);
}

JSValue nbt_remove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 1) return JS_FALSE;
    return JS_NewBool(ctx, tag->erase(text_of(ctx, argv[0])));
}

JSValue nbt_keys(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    JSValue array = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const auto& [name, _] : tag->compound) {
        JS_SetPropertyUint32(ctx, array, index++, JS_NewString(ctx, name.c_str()));
    }
    return array;
}

JSValue nbt_get_type(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 1) return JS_NULL;
    auto child = tag->find(text_of(ctx, argv[0]));
    return child ? JS_NewString(ctx, nbt::type_name(child->type)) : JS_NULL;
}

JSValue nbt_to_json(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    Tag* tag = tag_of(ctx, this_val);
    return tag ? tag_to_js(ctx, *tag) : JS_EXCEPTION;
}

JSValue nbt_to_string(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    JSValue object = tag_to_js(ctx, *tag);
    JSValue text = JS_JSONStringify(ctx, object, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, object);
    return text;
}

JSValue nbt_to_hex(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    auto bytes = nbt::serialize(*tag);
    std::string hex = nbt::to_hex(bytes);
    return JS_NewStringLen(ctx, hex.data(), hex.size());
}

JSValue nbt_clone(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    Tag* tag = tag_of(ctx, this_val);
    return tag ? wrap(ctx, std::make_shared<Tag>(*tag)) : JS_EXCEPTION;
}

JSValue nbt_typed(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, Type type) {
    Tag* tag = tag_of(ctx, this_val);
    if (!tag) return JS_EXCEPTION;
    if (argc < 2 || !JS_IsArray(argv[1])) {
        return JS_ThrowTypeError(ctx, "expected (key, array)");
    }
    JSValue length_value = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    if (length < 0 || length > 4 * 1024 * 1024) {
        return JS_ThrowRangeError(ctx, "array is too large");
    }
    if (type == Type::ByteArray) {
        std::vector<uint8_t> values;
        values.reserve(static_cast<size_t>(length));
        for (int32_t i = 0; i < length; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[1], static_cast<uint32_t>(i));
            int32_t value = 0;
            JS_ToInt32(ctx, &value, item);
            JS_FreeValue(ctx, item);
            values.push_back(static_cast<uint8_t>(value));
        }
        tag->set(text_of(ctx, argv[0]), nbt::make_byte_array(std::move(values)));
    } else if (type == Type::IntArray) {
        std::vector<int32_t> values;
        values.reserve(static_cast<size_t>(length));
        for (int32_t i = 0; i < length; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[1], static_cast<uint32_t>(i));
            int32_t value = 0;
            JS_ToInt32(ctx, &value, item);
            JS_FreeValue(ctx, item);
            values.push_back(value);
        }
        tag->set(text_of(ctx, argv[0]), nbt::make_int_array(std::move(values)));
    } else {
        std::vector<int64_t> values;
        values.reserve(static_cast<size_t>(length));
        for (int32_t i = 0; i < length; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[1], static_cast<uint32_t>(i));
            int64_t value = 0;
            JS_ToInt64(ctx, &value, item);
            JS_FreeValue(ctx, item);
            values.push_back(value);
        }
        tag->set(text_of(ctx, argv[0]), nbt::make_long_array(std::move(values)));
    }
    return JS_DupValue(ctx, this_val);
}

JSValue nbt_byte_array(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return nbt_typed(ctx, this_val, argc, argv, Type::ByteArray);
}
JSValue nbt_int_array(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return nbt_typed(ctx, this_val, argc, argv, Type::IntArray);
}
JSValue nbt_long_array(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return nbt_typed(ctx, this_val, argc, argv, Type::LongArray);
}

JSValue nbt_from_json(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return wrap(ctx, nbt::make_compound());
    JSValue value = JS_DupValue(ctx, argv[0]);
    if (JS_IsString(value)) {
        std::string text = text_of(ctx, value);
        JS_FreeValue(ctx, value);
        value = JS_ParseJSON(ctx, text.data(), text.size(), "<nbt>");
        if (JS_IsException(value)) return value;
    }
    auto tag = js_to_tag(ctx, value, 0);
    JS_FreeValue(ctx, value);
    if (!tag) return JS_EXCEPTION;
    return wrap(ctx, tag);
}

JSValue nbt_from_hex(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "NBT.parseBinary(hex) requires data");
    auto bytes = nbt::from_hex(text_of(ctx, argv[0]));
    auto tag = nbt::parse(bytes.data(), bytes.size());
    if (!tag) return JS_ThrowSyntaxError(ctx, "invalid NBT binary");
    return wrap(ctx, tag);
}

} // namespace

void register_nbt(JSContext* ctx) {
    JS_NewClassID(JS_GetRuntime(ctx), &g_nbt_class);
    JSClassDef definition = {};
    definition.class_name = "NBT";
    definition.finalizer = nbt_finalizer;
    JS_NewClass(JS_GetRuntime(ctx), g_nbt_class, &definition);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "get", JS_NewCFunction(ctx, nbt_get, "get", 1));
    JS_SetPropertyStr(ctx, proto, "set", JS_NewCFunction(ctx, nbt_set, "set", 2));
    JS_SetPropertyStr(ctx, proto, "getTag", JS_NewCFunction(ctx, nbt_get_tag, "getTag", 1));
    JS_SetPropertyStr(ctx, proto, "setTag", JS_NewCFunction(ctx, nbt_set_tag, "setTag", 2));
    JS_SetPropertyStr(ctx, proto, "remove", JS_NewCFunction(ctx, nbt_remove, "remove", 1));
    JS_SetPropertyStr(ctx, proto, "keys", JS_NewCFunction(ctx, nbt_keys, "keys", 0));
    JS_SetPropertyStr(ctx, proto, "getType", JS_NewCFunction(ctx, nbt_get_type, "getType", 1));
    JS_SetPropertyStr(ctx, proto, "toJSON", JS_NewCFunction(ctx, nbt_to_json, "toJSON", 0));
    JS_SetPropertyStr(ctx, proto, "toString", JS_NewCFunction(ctx, nbt_to_string, "toString", 0));
    JS_SetPropertyStr(ctx, proto, "toBinaryHex", JS_NewCFunction(ctx, nbt_to_hex, "toBinaryHex", 0));
    JS_SetPropertyStr(ctx, proto, "clone", JS_NewCFunction(ctx, nbt_clone, "clone", 0));
    JS_SetPropertyStr(ctx, proto, "setByteArray", JS_NewCFunction(ctx, nbt_byte_array, "setByteArray", 2));
    JS_SetPropertyStr(ctx, proto, "setIntArray", JS_NewCFunction(ctx, nbt_int_array, "setIntArray", 2));
    JS_SetPropertyStr(ctx, proto, "setLongArray", JS_NewCFunction(ctx, nbt_long_array, "setLongArray", 2));
    JS_SetClassProto(ctx, g_nbt_class, proto);

    JSValue ctor = JS_NewCFunction2(ctx, nbt_ctor, "NBT", 0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, ctor, "fromJSON", JS_NewCFunction(ctx, nbt_from_json, "fromJSON", 1));
    JS_SetPropertyStr(ctx, ctor, "parse", JS_NewCFunction(ctx, nbt_from_json, "parse", 1));
    JS_SetPropertyStr(ctx, ctor, "parseBinary", JS_NewCFunction(ctx, nbt_from_hex, "parseBinary", 1));
    JS_SetPropertyStr(ctx, ctor, "fromHex", JS_NewCFunction(ctx, nbt_from_hex, "fromHex", 1));

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "NBT", ctor);
    JS_FreeValue(ctx, global);
}

} // namespace yoyo::js
