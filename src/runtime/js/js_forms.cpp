#include "js_internal.h"

#include "../events.h"
#include "../live.h"
#include "../log.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yoyo::js {
namespace {

std::string text_of(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string out = text ? text : "";
    if (text) JS_FreeCString(ctx, text);
    return out;
}

void push_value(JSContext* ctx, JSValueConst object, const char* property, JSValue value) {
    JSValue array = JS_GetPropertyStr(ctx, object, property);
    JSValue length_value = JS_GetPropertyStr(ctx, array, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(length), value);
    JS_FreeValue(ctx, array);
}

JSValue form_to_string(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_JSONStringify(ctx, this_val, JS_UNDEFINED, JS_UNDEFINED);
}

JSValue form_to_json(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_DupValue(ctx, this_val);
}

std::string property_string(JSContext* ctx, JSValueConst object, const char* name) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    std::string result = text_of(ctx, value);
    JS_FreeValue(ctx, value);
    return result;
}

JSValue copy_property(JSContext* ctx, JSValueConst source, const char* name) {
    return JS_GetPropertyStr(ctx, source, name);
}

JSValue schema_element(JSContext* ctx, JSValueConst element) {
    const std::string type = property_string(ctx, element, "type");
    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "text", JS_NewString(ctx, property_string(ctx, element, "title").c_str()));
    if (type == "label") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "label"));
    } else if (type == "input") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "input"));
        JS_SetPropertyStr(ctx, out, "placeholder",
                          JS_NewString(ctx, property_string(ctx, element, "placeholder").c_str()));
        JS_SetPropertyStr(ctx, out, "default", copy_property(ctx, element, "default"));
    } else if (type == "toggle") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "toggle"));
        JS_SetPropertyStr(ctx, out, "default", copy_property(ctx, element, "default"));
    } else if (type == "dropdown") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "dropdown"));
        JS_SetPropertyStr(ctx, out, "options", copy_property(ctx, element, "options"));
        JS_SetPropertyStr(ctx, out, "default", copy_property(ctx, element, "default"));
    } else if (type == "slider") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "slider"));
        JS_SetPropertyStr(ctx, out, "min", copy_property(ctx, element, "min"));
        JS_SetPropertyStr(ctx, out, "max", copy_property(ctx, element, "max"));
        JS_SetPropertyStr(ctx, out, "step", copy_property(ctx, element, "step"));
        JS_SetPropertyStr(ctx, out, "default", copy_property(ctx, element, "default"));
    } else if (type == "step_slider") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "step_slider"));
        JS_SetPropertyStr(ctx, out, "steps", copy_property(ctx, element, "options"));
        JS_SetPropertyStr(ctx, out, "default", copy_property(ctx, element, "default"));
    } else {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "label"));
    }
    return out;
}

JSValue schema_form(JSContext* ctx, JSValueConst form) {
    const std::string type = property_string(ctx, form, "type");
    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "title", JS_NewString(ctx, property_string(ctx, form, "title").c_str()));
    if (type == "simple") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "form"));
        JS_SetPropertyStr(ctx, out, "content", JS_NewString(ctx, property_string(ctx, form, "content").c_str()));
        JS_SetPropertyStr(ctx, out, "buttons", copy_property(ctx, form, "buttons"));
    } else if (type == "modal") {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "modal"));
        JS_SetPropertyStr(ctx, out, "content", JS_NewString(ctx, property_string(ctx, form, "content").c_str()));
        JS_SetPropertyStr(ctx, out, "button1", JS_NewString(ctx, property_string(ctx, form, "confirm").c_str()));
        JS_SetPropertyStr(ctx, out, "button2", JS_NewString(ctx, property_string(ctx, form, "cancel").c_str()));
    } else {
        JS_SetPropertyStr(ctx, out, "type", JS_NewString(ctx, "custom_form"));
        JSValue content = JS_NewArray(ctx);
        JSValue elements = JS_GetPropertyStr(ctx, form, "elements");
        JSValue length_value = JS_GetPropertyStr(ctx, elements, "length");
        int32_t length = 0;
        JS_ToInt32(ctx, &length, length_value);
        JS_FreeValue(ctx, length_value);
        for (int32_t i = 0; i < length; ++i) {
            JSValue element = JS_GetPropertyUint32(ctx, elements, static_cast<uint32_t>(i));
            JS_SetPropertyUint32(ctx, content, static_cast<uint32_t>(i), schema_element(ctx, element));
            JS_FreeValue(ctx, element);
        }
        JS_FreeValue(ctx, elements);
        JS_SetPropertyStr(ctx, out, "content", content);
    }
    return out;
}

JSValue form_send_to(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "sendTo(player) requires a player");
    }
    JSValue handle_value = JS_GetPropertyStr(ctx, argv[0], "__handle");
    int64_t handle_bits = 0;
    if (JS_ToInt64(ctx, &handle_bits, handle_value) < 0) {
        handle_bits = 0;
    }
    JS_FreeValue(ctx, handle_value);
    void* handle = reinterpret_cast<void*>(static_cast<uintptr_t>(handle_bits));
    if (handle == nullptr) {
        return JS_ThrowInternalError(ctx, "form.sendTo requires a live Player");
    }
    JSValue schema = schema_form(ctx, this_val);
    JSValue json = JS_JSONStringify(ctx, schema, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, schema);
    if (JS_IsException(json)) {
        log_exception(ctx);
        return JS_EXCEPTION;
    }
    std::string text = text_of(ctx, json);
    JS_FreeValue(ctx, json);

    uint32_t form_id = 0;
    const bool sent = live_send_form(handle, text, &form_id);
    if (sent && argc > 1 && JS_IsFunction(ctx, argv[1])) {
        remember_form_callback(form_id, argv[1]);
    }
    return JS_NewBool(ctx, sent);
}

JSValue form_set_title(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc > 0) JS_SetPropertyStr(ctx, this_val, "title", JS_NewString(ctx, text_of(ctx, argv[0]).c_str()));
    return JS_DupValue(ctx, this_val);
}

JSValue form_set_content(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc > 0) JS_SetPropertyStr(ctx, this_val, "content", JS_NewString(ctx, text_of(ctx, argv[0]).c_str()));
    return JS_DupValue(ctx, this_val);
}

JSValue form_add_button(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue button = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, button, "text", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        JS_SetPropertyStr(ctx, button, "image", JS_DupValue(ctx, argv[1]));
    }
    push_value(ctx, this_val, "buttons", button);
    return JS_DupValue(ctx, this_val);
}

JSValue add_element_start(JSContext* ctx, JSValueConst this_val, const char* kind, JSValueConst title) {
    JSValue element = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, element, "type", JS_NewString(ctx, kind));
    JS_SetPropertyStr(ctx, element, "title", JS_NewString(ctx, text_of(ctx, title).c_str()));
    push_value(ctx, this_val, "elements", element);
    return JS_TRUE;
}

JSValue form_add_label(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return add_element_start(ctx, this_val, "label", argc > 0 ? argv[0] : JS_UNDEFINED);
}
JSValue form_add_input(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    add_element_start(ctx, this_val, "input", argc > 0 ? argv[0] : JS_UNDEFINED);
    JSValue elements = JS_GetPropertyStr(ctx, this_val, "elements");
    JSValue length_value = JS_GetPropertyStr(ctx, elements, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JSValue element = JS_GetPropertyUint32(ctx, elements, static_cast<uint32_t>(length - 1));
    if (argc > 1) JS_SetPropertyStr(ctx, element, "placeholder", JS_NewString(ctx, text_of(ctx, argv[1]).c_str()));
    if (argc > 2) JS_SetPropertyStr(ctx, element, "default", JS_DupValue(ctx, argv[2]));
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, elements);
    return JS_DupValue(ctx, this_val);
}
JSValue form_add_toggle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    add_element_start(ctx, this_val, "toggle", argc > 0 ? argv[0] : JS_UNDEFINED);
    JSValue elements = JS_GetPropertyStr(ctx, this_val, "elements");
    JSValue length_value = JS_GetPropertyStr(ctx, elements, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JSValue element = JS_GetPropertyUint32(ctx, elements, static_cast<uint32_t>(length - 1));
    if (argc > 1) JS_SetPropertyStr(ctx, element, "default", JS_DupValue(ctx, argv[1]));
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, elements);
    return JS_DupValue(ctx, this_val);
}
JSValue form_add_dropdown(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    add_element_start(ctx, this_val, "dropdown", argc > 0 ? argv[0] : JS_UNDEFINED);
    JSValue elements = JS_GetPropertyStr(ctx, this_val, "elements");
    JSValue length_value = JS_GetPropertyStr(ctx, elements, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JSValue element = JS_GetPropertyUint32(ctx, elements, static_cast<uint32_t>(length - 1));
    if (argc > 1) JS_SetPropertyStr(ctx, element, "options", JS_DupValue(ctx, argv[1]));
    if (argc > 2) JS_SetPropertyStr(ctx, element, "default", JS_DupValue(ctx, argv[2]));
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, elements);
    return JS_DupValue(ctx, this_val);
}
JSValue form_add_slider(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    add_element_start(ctx, this_val, "slider", argc > 0 ? argv[0] : JS_UNDEFINED);
    JSValue elements = JS_GetPropertyStr(ctx, this_val, "elements");
    JSValue length_value = JS_GetPropertyStr(ctx, elements, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JSValue element = JS_GetPropertyUint32(ctx, elements, static_cast<uint32_t>(length - 1));
    if (argc > 1) JS_SetPropertyStr(ctx, element, "min", JS_DupValue(ctx, argv[1]));
    if (argc > 2) JS_SetPropertyStr(ctx, element, "max", JS_DupValue(ctx, argv[2]));
    if (argc > 3) JS_SetPropertyStr(ctx, element, "step", JS_DupValue(ctx, argv[3]));
    if (argc > 4) JS_SetPropertyStr(ctx, element, "default", JS_DupValue(ctx, argv[4]));
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, elements);
    return JS_DupValue(ctx, this_val);
}
JSValue form_add_step_slider(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    add_element_start(ctx, this_val, "step_slider", argc > 0 ? argv[0] : JS_UNDEFINED);
    JSValue elements = JS_GetPropertyStr(ctx, this_val, "elements");
    JSValue length_value = JS_GetPropertyStr(ctx, elements, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JSValue element = JS_GetPropertyUint32(ctx, elements, static_cast<uint32_t>(length - 1));
    if (argc > 1) JS_SetPropertyStr(ctx, element, "options", JS_DupValue(ctx, argv[1]));
    if (argc > 2) JS_SetPropertyStr(ctx, element, "default", JS_DupValue(ctx, argv[2]));
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, elements);
    return JS_DupValue(ctx, this_val);
}

JSValue modal_set_confirm(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc > 0) JS_SetPropertyStr(ctx, this_val, "confirm", JS_NewString(ctx, text_of(ctx, argv[0]).c_str()));
    return JS_DupValue(ctx, this_val);
}

JSValue modal_set_cancel(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc > 0) JS_SetPropertyStr(ctx, this_val, "cancel", JS_NewString(ctx, text_of(ctx, argv[0]).c_str()));
    return JS_DupValue(ctx, this_val);
}

void set_form_methods(JSContext* ctx, JSValue object, const char* array_property) {
    if (std::string(array_property) == "buttons") {
        JS_SetPropertyStr(ctx, object, "addButton", JS_NewCFunction(ctx, form_add_button, "addButton", 2));
    } else {
        JS_SetPropertyStr(ctx, object, "addLabel", JS_NewCFunction(ctx, form_add_label, "addLabel", 1));
        JS_SetPropertyStr(ctx, object, "addInput", JS_NewCFunction(ctx, form_add_input, "addInput", 3));
        JS_SetPropertyStr(ctx, object, "addToggle", JS_NewCFunction(ctx, form_add_toggle, "addToggle", 2));
        JS_SetPropertyStr(ctx, object, "addDropdown", JS_NewCFunction(ctx, form_add_dropdown, "addDropdown", 3));
        JS_SetPropertyStr(ctx, object, "addSlider", JS_NewCFunction(ctx, form_add_slider, "addSlider", 4));
        JS_SetPropertyStr(ctx, object, "addStepSlider",
                          JS_NewCFunction(ctx, form_add_step_slider, "addStepSlider", 4));
    }
    JS_SetPropertyStr(ctx, object, "setTitle", JS_NewCFunction(ctx, form_set_title, "setTitle", 1));
    JS_SetPropertyStr(ctx, object, "setContent", JS_NewCFunction(ctx, form_set_content, "setContent", 1));
    JS_SetPropertyStr(ctx, object, "toJSON", JS_NewCFunction(ctx, form_to_json, "toJSON", 0));
    JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, form_to_string, "toString", 0));
    JS_SetPropertyStr(ctx, object, "sendTo", JS_NewCFunction(ctx, form_send_to, "sendTo", 1));
    JS_SetPropertyStr(ctx, object, "send_to", JS_NewCFunction(ctx, form_send_to, "send_to", 1));
}

JSValue new_simple_form(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, "simple"));
    JS_SetPropertyStr(ctx, object, "title", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "content", JS_NewString(ctx, argc > 1 ? text_of(ctx, argv[1]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "buttons", JS_NewArray(ctx));
    set_form_methods(ctx, object, "buttons");
    return object;
}

JSValue new_custom_form(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, "custom"));
    JS_SetPropertyStr(ctx, object, "title", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "elements", JS_NewArray(ctx));
    set_form_methods(ctx, object, "elements");
    return object;
}

JSValue new_modal_form(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, "modal"));
    JS_SetPropertyStr(ctx, object, "title", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "content", JS_NewString(ctx, argc > 1 ? text_of(ctx, argv[1]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "confirm", JS_NewString(ctx, "Confirm"));
    JS_SetPropertyStr(ctx, object, "cancel", JS_NewString(ctx, "Cancel"));
    JS_SetPropertyStr(ctx, object, "setConfirm", JS_NewCFunction(ctx, modal_set_confirm, "setConfirm", 1));
    JS_SetPropertyStr(ctx, object, "setCancel", JS_NewCFunction(ctx, modal_set_cancel, "setCancel", 1));
    JS_SetPropertyStr(ctx, object, "setTitle", JS_NewCFunction(ctx, form_set_title, "setTitle", 1));
    JS_SetPropertyStr(ctx, object, "setContent", JS_NewCFunction(ctx, form_set_content, "setContent", 1));
    JS_SetPropertyStr(ctx, object, "toJSON", JS_NewCFunction(ctx, form_to_json, "toJSON", 0));
    JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, form_to_string, "toString", 0));
    JS_SetPropertyStr(ctx, object, "sendTo", JS_NewCFunction(ctx, form_send_to, "sendTo", 1));
    JS_SetPropertyStr(ctx, object, "send_to", JS_NewCFunction(ctx, form_send_to, "send_to", 1));
    return object;
}

JSValue get_nbt(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "nbt");
}

JSValue set_nbt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setNBT(nbt) requires an argument");
    JS_SetPropertyStr(ctx, this_val, "nbt", JS_DupValue(ctx, argv[0]));
    return JS_DupValue(ctx, this_val);
}

JSValue get_type(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "type");
}

JSValue get_count(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "count");
}

JSValue set_count(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setCount(value) requires an argument");
    JS_SetPropertyStr(ctx, this_val, "count", JS_DupValue(ctx, argv[0]));
    return JS_DupValue(ctx, this_val);
}

JSValue item_is_null(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_FALSE;
}

JSValue item_max_stack(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewInt32(ctx, 64);
}

JSValue block_get_pos(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "pos");
}

JSValue container_is_empty(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JSValue items = JS_GetPropertyStr(ctx, this_val, "items");
    JSValue length_value = JS_GetPropertyStr(ctx, items, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    JS_FreeValue(ctx, items);
    return JS_NewBool(ctx, length == 0);
}

JSValue container_clear(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JS_SetPropertyStr(ctx, this_val, "items", JS_NewArray(ctx));
    return JS_TRUE;
}

JSValue entity_get_name(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "name");
}

JSValue entity_get_health(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "health");
}

JSValue entity_set_health(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc > 0) JS_SetPropertyStr(ctx, this_val, "health", JS_DupValue(ctx, argv[0]));
    return JS_DupValue(ctx, this_val);
}

JSValue entity_get_pos(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "pos");
}

JSValue new_item(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "count",
                      argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, object, "nbt", argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, object, "getType", JS_NewCFunction(ctx, get_type, "getType", 0));
    JS_SetPropertyStr(ctx, object, "getCount", JS_NewCFunction(ctx, get_count, "getCount", 0));
    JS_SetPropertyStr(ctx, object, "setCount", JS_NewCFunction(ctx, set_count, "setCount", 1));
    JS_SetPropertyStr(ctx, object, "getNBT", JS_NewCFunction(ctx, get_nbt, "getNBT", 0));
    JS_SetPropertyStr(ctx, object, "setNBT", JS_NewCFunction(ctx, set_nbt, "setNBT", 1));
    JS_SetPropertyStr(ctx, object, "isNull", JS_NewCFunction(ctx, item_is_null, "isNull", 0));
    JS_SetPropertyStr(ctx, object, "getMaxStackSize",
                      JS_NewCFunction(ctx, item_max_stack, "getMaxStackSize", 0));
    return object;
}

JSValue new_block(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "pos", argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_NULL);
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, argc > 1 ? text_of(ctx, argv[1]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "nbt", argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, object, "getType", JS_NewCFunction(ctx, get_type, "getType", 0));
    JS_SetPropertyStr(ctx, object, "getPos", JS_NewCFunction(ctx, block_get_pos, "getPos", 0));
    JS_SetPropertyStr(ctx, object, "getNBT", JS_NewCFunction(ctx, get_nbt, "getNBT", 0));
    JS_SetPropertyStr(ctx, object, "setNBT", JS_NewCFunction(ctx, set_nbt, "setNBT", 1));
    return object;
}

JSValue container_get_size(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "size");
}

JSValue container_get_item(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    int32_t index = 0;
    JS_ToInt32(ctx, &index, argv[0]);
    JSValue items = JS_GetPropertyStr(ctx, this_val, "items");
    JSValue item = JS_GetPropertyUint32(ctx, items, static_cast<uint32_t>(index));
    JS_FreeValue(ctx, items);
    return item;
}

JSValue container_set_item(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "setItem(index, item) requires two arguments");
    int32_t index = 0;
    JS_ToInt32(ctx, &index, argv[0]);
    JSValue items = JS_GetPropertyStr(ctx, this_val, "items");
    JS_SetPropertyUint32(ctx, items, static_cast<uint32_t>(index), JS_DupValue(ctx, argv[1]));
    JS_FreeValue(ctx, items);
    return JS_TRUE;
}

JSValue container_add_item(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    JSValue size_value = JS_GetPropertyStr(ctx, this_val, "size");
    int32_t size = 0;
    JS_ToInt32(ctx, &size, size_value);
    JS_FreeValue(ctx, size_value);
    JSValue items = JS_GetPropertyStr(ctx, this_val, "items");
    JSValue length_value = JS_GetPropertyStr(ctx, items, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    if (length >= size) {
        JS_FreeValue(ctx, items);
        return JS_FALSE;
    }
    JS_SetPropertyUint32(ctx, items, static_cast<uint32_t>(length), JS_DupValue(ctx, argv[0]));
    JS_FreeValue(ctx, items);
    return JS_TRUE;
}

JSValue container_remove_item(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    int32_t index = 0;
    JS_ToInt32(ctx, &index, argv[0]);
    JSValue items = JS_GetPropertyStr(ctx, this_val, "items");
    JSValue length_value = JS_GetPropertyStr(ctx, items, "length");
    int32_t length = 0;
    JS_ToInt32(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    if (index < 0 || index >= length) {
        JS_FreeValue(ctx, items);
        return JS_NULL;
    }
    JSValue removed = JS_GetPropertyUint32(ctx, items, static_cast<uint32_t>(index));
    for (int32_t i = index; i + 1 < length; ++i) {
        JSValue next = JS_GetPropertyUint32(ctx, items, static_cast<uint32_t>(i + 1));
        JS_SetPropertyUint32(ctx, items, static_cast<uint32_t>(i), next);
    }
    JS_SetPropertyStr(ctx, items, "length", JS_NewInt32(ctx, length - 1));
    JS_FreeValue(ctx, items);
    return removed;
}

JSValue container_get_all(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "items");
}

JSValue new_container(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int32_t size = 0;
    if (argc > 0) JS_ToInt32(ctx, &size, argv[0]);
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "size", JS_NewInt32(ctx, size));
    JS_SetPropertyStr(ctx, object, "items", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, object, "getSize", JS_NewCFunction(ctx, container_get_size, "getSize", 0));
    JS_SetPropertyStr(ctx, object, "getItem", JS_NewCFunction(ctx, container_get_item, "getItem", 1));
    JS_SetPropertyStr(ctx, object, "setItem", JS_NewCFunction(ctx, container_set_item, "setItem", 2));
    JS_SetPropertyStr(ctx, object, "addItem", JS_NewCFunction(ctx, container_add_item, "addItem", 1));
    JS_SetPropertyStr(ctx, object, "removeItem", JS_NewCFunction(ctx, container_remove_item, "removeItem", 1));
    JS_SetPropertyStr(ctx, object, "getAllItems", JS_NewCFunction(ctx, container_get_all, "getAllItems", 0));
    JS_SetPropertyStr(ctx, object, "isEmpty", JS_NewCFunction(ctx, container_is_empty, "isEmpty", 0));
    JS_SetPropertyStr(ctx, object, "clear", JS_NewCFunction(ctx, container_clear, "clear", 0));
    return object;
}

JSValue new_entity(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "pos", argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL);
    JS_SetPropertyStr(ctx, object, "health", argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, object, "getNBT", JS_NewCFunction(ctx, get_nbt, "getNBT", 0));
    JS_SetPropertyStr(ctx, object, "setNBT", JS_NewCFunction(ctx, set_nbt, "setNBT", 1));
    JS_SetPropertyStr(ctx, object, "getName", JS_NewCFunction(ctx, entity_get_name, "getName", 0));
    JS_SetPropertyStr(ctx, object, "getHealth", JS_NewCFunction(ctx, entity_get_health, "getHealth", 0));
    JS_SetPropertyStr(ctx, object, "setHealth", JS_NewCFunction(ctx, entity_set_health, "setHealth", 1));
    JS_SetPropertyStr(ctx, object, "getPos", JS_NewCFunction(ctx, entity_get_pos, "getPos", 0));
    return object;
}

JSValue global_mc(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mc = JS_GetPropertyStr(ctx, global, "mc");
    JS_FreeValue(ctx, global);
    return mc;
}

void set_mc(JSContext* ctx, const char* name, JSValue function) {
    JSValue mc = global_mc(ctx);
    JS_SetPropertyStr(ctx, mc, name, function);
    JS_FreeValue(ctx, mc);
}

} // namespace

namespace {

std::mutex& form_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint32_t, JSValue>& form_callbacks() {
    static std::unordered_map<uint32_t, JSValue> callbacks;
    return callbacks;
}

/* ModalFormResponse carries either a JSON array/object (simple and custom
 * forms) or "true"/"false" for modal forms. */
JSValue decode_form_response(JSContext* ctx, const std::string& response) {
    if (response.empty()) {
        return JS_NULL;
    }
    if (response == "true") {
        return JS_TRUE;
    }
    if (response == "false") {
        return JS_FALSE;
    }
    JSValue value = JS_ParseJSON(ctx, response.data(), response.size(), "<form-response>");
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return JS_NewStringLen(ctx, response.data(), response.size());
    }
    return value;
}

} // namespace

void remember_form_callback(uint32_t form_id, JSValue callback) {
    if (g_context == nullptr) {
        return;
    }
    std::lock_guard lock(form_mutex());
    auto& callbacks = form_callbacks();
    auto it = callbacks.find(form_id);
    if (it != callbacks.end()) {
        JS_FreeValue(g_context, it->second);
        it->second = JS_DupValue(g_context, callback);
        return;
    }
    callbacks[form_id] = JS_DupValue(g_context, callback);
    /* Keep the table bounded if a client never answers a form. */
    if (callbacks.size() > 256) {
        callbacks.erase(callbacks.begin());
    }
}

void dispatch_form_response(uint32_t form_id, const PlayerView& player, const std::string& response,
                            int cancel_reason) {
    if (g_context == nullptr) {
        return;
    }
    JSValue callback = JS_UNDEFINED;
    {
        std::lock_guard lock(form_mutex());
        auto& callbacks = form_callbacks();
        auto it = callbacks.find(form_id);
        if (it == callbacks.end()) {
            return;
        }
        callback = it->second;
        callbacks.erase(it);
    }

    JSValue js_player = make_player(g_context, player);
    JSValue result;
    if (cancel_reason >= 0 && response.empty()) {
        result = JS_NULL;
    } else {
        result = decode_form_response(g_context, response);
    }
    JSValue reason = JS_NewInt32(g_context, cancel_reason);
    JSValue args[3] = {js_player, result, reason};
    JSValue call_result = JS_Call(g_context, callback, JS_UNDEFINED, 3, args);
    if (JS_IsException(call_result)) {
        log_exception(g_context);
    }
    JS_FreeValue(g_context, call_result);
    JS_FreeValue(g_context, js_player);
    JS_FreeValue(g_context, result);
    JS_FreeValue(g_context, reason);
    JS_FreeValue(g_context, callback);
}

void clear_form_callbacks() {
    std::lock_guard lock(form_mutex());
    if (g_context != nullptr) {
        for (auto& [id, callback] : form_callbacks()) {
            JS_FreeValue(g_context, callback);
        }
    }
    form_callbacks().clear();
}

void register_forms(JSContext* ctx) {
    set_mc(ctx, "newSimpleForm", JS_NewCFunction(ctx, new_simple_form, "newSimpleForm", 2));
    set_mc(ctx, "newCustomForm", JS_NewCFunction(ctx, new_custom_form, "newCustomForm", 1));
    set_mc(ctx, "newModalForm", JS_NewCFunction(ctx, new_modal_form, "newModalForm", 2));
    set_mc(ctx, "newItem", JS_NewCFunction(ctx, new_item, "newItem", 3));
    set_mc(ctx, "newBlock", JS_NewCFunction(ctx, new_block, "newBlock", 3));
    set_mc(ctx, "newContainer", JS_NewCFunction(ctx, new_container, "newContainer", 1));
    set_mc(ctx, "newEntity", JS_NewCFunction(ctx, new_entity, "newEntity", 3));

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "SimpleForm",
                      JS_NewCFunction2(ctx, new_simple_form, "SimpleForm", 2, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "CustomForm",
                      JS_NewCFunction2(ctx, new_custom_form, "CustomForm", 1, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "ModalForm",
                      JS_NewCFunction2(ctx, new_modal_form, "ModalForm", 2, JS_CFUNC_constructor, 0));
    JS_FreeValue(ctx, global);
}

} // namespace yoyo::js
