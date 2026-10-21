#include "js_internal.h"

#include <string>

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

JSValue form_send_to(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return unsupported(ctx, "form sending is not implemented: the BDS form packet bridge is not available yet");
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
    JS_SetPropertyStr(ctx, object, "toJSON", JS_NewCFunction(ctx, form_to_json, "toJSON", 0));
    JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, form_to_string, "toString", 0));
    JS_SetPropertyStr(ctx, object, "sendTo", JS_NewCFunction(ctx, form_send_to, "sendTo", 2));
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
    JS_SetPropertyStr(ctx, object, "toJSON", JS_NewCFunction(ctx, form_to_json, "toJSON", 0));
    JS_SetPropertyStr(ctx, object, "toString", JS_NewCFunction(ctx, form_to_string, "toString", 0));
    JS_SetPropertyStr(ctx, object, "sendTo", JS_NewCFunction(ctx, form_send_to, "sendTo", 2));
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
    return object;
}

JSValue new_block(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "pos", argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_NULL);
    JS_SetPropertyStr(ctx, object, "type", JS_NewString(ctx, argc > 1 ? text_of(ctx, argv[1]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "nbt", argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, object, "getType", JS_NewCFunction(ctx, get_type, "getType", 0));
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
    return object;
}

JSValue new_entity(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, argc > 0 ? text_of(ctx, argv[0]).c_str() : ""));
    JS_SetPropertyStr(ctx, object, "pos", argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL);
    JS_SetPropertyStr(ctx, object, "health", argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, object, "getNBT", JS_NewCFunction(ctx, get_nbt, "getNBT", 0));
    JS_SetPropertyStr(ctx, object, "setNBT", JS_NewCFunction(ctx, set_nbt, "setNBT", 1));
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
