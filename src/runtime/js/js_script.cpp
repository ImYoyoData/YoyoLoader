#include "js_internal.h"

#include "../log.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace yoyo::js {
namespace {

std::string value_to_text(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    std::string result = text ? text : "";
    if (text) {
        JS_FreeCString(ctx, text);
    }
    return result;
}

JSValue console_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) line += ' ';
        line += value_to_text(ctx, argv[i]);
    }
    log_write(LogLevel::Info, "[script] %s", line.c_str());
    return JS_UNDEFINED;
}

JSValue console_color_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "colorLog(color, ...) requires a color");
    }
    std::string line;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) line += ' ';
        line += value_to_text(ctx, argv[i]);
    }
    log_write(LogLevel::Info, "[script] %s%s", value_to_text(ctx, argv[0]).c_str(), line.c_str());
    return JS_UNDEFINED;
}

/*
 * Paths are resolved inside the server directory.  Absolute paths and any
 * component that escapes the root are rejected so a JS plugin cannot read or
 * overwrite arbitrary files on the host.
 */
bool resolve_relative(const std::string& input, std::filesystem::path& out) {
    if (input.empty()) {
        return false;
    }
    const std::filesystem::path path(input);
    if (path.is_absolute() || path.has_root_name()) {
        return false;
    }
    std::error_code error;
    const auto root = std::filesystem::current_path(error);
    if (error) {
        return false;
    }
    const auto combined = (root / path).lexically_normal();
    auto root_text = root.lexically_normal().string();
    auto combined_text = combined.string();
    if (combined_text.size() < root_text.size()) {
        return false;
    }
    if (combined_text.compare(0, root_text.size(), root_text) != 0) {
        return false;
    }
    out = combined;
    return true;
}

bool read_whole_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

bool write_whole_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return file.good();
}

JSValue json_read_file(JSContext* ctx, const std::filesystem::path& path, bool& ok) {
    std::string text;
    if (!read_whole_file(path, text) || text.empty()) {
        ok = true;
        return JS_UNDEFINED;
    }
    JSValue value = JS_ParseJSON(ctx, text.data(), text.size(), path.string().c_str());
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        ok = false;
        return JS_UNDEFINED;
    }
    ok = true;
    return value;
}

bool json_write_file(JSContext* ctx, const std::filesystem::path& path, JSValueConst value) {
    JSValue text = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(text)) {
        JS_FreeValue(ctx, text);
        return false;
    }
    const std::string output = value_to_text(ctx, text);
    JS_FreeValue(ctx, text);
    return write_whole_file(path, output);
}

bool config_path_of(JSContext* ctx, JSValueConst this_val, std::filesystem::path& path) {
    JSValue path_value = JS_GetPropertyStr(ctx, this_val, "__path");
    const bool resolved = resolve_relative(value_to_text(ctx, path_value), path);
    JS_FreeValue(ctx, path_value);
    return resolved;
}

/* ---------------- JSON config ---------------- */

JSValue config_init(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "init(name[, default]) requires a name");
    const std::string name = value_to_text(ctx, argv[0]);
    std::filesystem::path path;
    if (!config_path_of(ctx, this_val, path)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    bool ok = false;
    JSValue existing = json_read_file(ctx, path, ok);
    if (ok && JS_IsObject(existing)) {
        JSValue stored = JS_GetPropertyStr(ctx, existing, name.c_str());
        if (!JS_IsUndefined(stored)) {
            JS_FreeValue(ctx, existing);
            return stored;
        }
        JS_FreeValue(ctx, stored);
        if (argc > 1) {
            JS_SetPropertyStr(ctx, existing, name.c_str(), JS_DupValue(ctx, argv[1]));
            json_write_file(ctx, path, existing);
        }
        JS_FreeValue(ctx, existing);
        return argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
    }
    JS_FreeValue(ctx, existing);

    JSValue object = JS_NewObject(ctx);
    if (argc > 1) {
        JS_SetPropertyStr(ctx, object, name.c_str(), JS_DupValue(ctx, argv[1]));
        json_write_file(ctx, path, object);
    }
    JS_FreeValue(ctx, object);
    return argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
}

JSValue config_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "set(name, value) requires two arguments");
    std::filesystem::path path;
    if (!config_path_of(ctx, this_val, path)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    bool ok = false;
    JSValue object = json_read_file(ctx, path, ok);
    if (!ok || !JS_IsObject(object)) {
        JS_FreeValue(ctx, object);
        object = JS_NewObject(ctx);
    }
    JS_SetPropertyStr(ctx, object, value_to_text(ctx, argv[0]).c_str(), JS_DupValue(ctx, argv[1]));
    const bool written = json_write_file(ctx, path, object);
    JS_FreeValue(ctx, object);
    return JS_NewBool(ctx, written);
}

JSValue config_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "get(name[, default]) requires a name");
    std::filesystem::path path;
    if (!config_path_of(ctx, this_val, path)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    bool ok = false;
    JSValue object = json_read_file(ctx, path, ok);
    if (!ok || !JS_IsObject(object)) {
        JS_FreeValue(ctx, object);
        return argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
    }
    JSValue value = JS_GetPropertyStr(ctx, object, value_to_text(ctx, argv[0]).c_str());
    JS_FreeValue(ctx, object);
    if (JS_IsUndefined(value) && argc > 1) {
        JS_FreeValue(ctx, value);
        return JS_DupValue(ctx, argv[1]);
    }
    return value;
}

JSValue config_delete(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "delete(name) requires a name");
    std::filesystem::path path;
    if (!config_path_of(ctx, this_val, path)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    bool ok = false;
    JSValue object = json_read_file(ctx, path, ok);
    if (!ok || !JS_IsObject(object)) {
        JS_FreeValue(ctx, object);
        return JS_FALSE;
    }
    const JSAtom atom = JS_NewAtom(ctx, value_to_text(ctx, argv[0]).c_str());
    const bool removed = JS_DeleteProperty(ctx, object, atom, 0) >= 0;
    JS_FreeAtom(ctx, atom);
    const bool written = json_write_file(ctx, path, object);
    JS_FreeValue(ctx, object);
    return JS_NewBool(ctx, removed && written);
}

JSValue config_get_path(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    return JS_GetPropertyStr(ctx, this_val, "__path");
}

JSValue config_read(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    std::filesystem::path path;
    if (!config_path_of(ctx, this_val, path)) return JS_NULL;
    std::string text;
    if (!read_whole_file(path, text)) return JS_NULL;
    return JS_NewStringLen(ctx, text.data(), text.size());
}

JSValue config_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "write(content) requires content");
    std::filesystem::path path;
    if (!config_path_of(ctx, this_val, path)) return JS_FALSE;
    return JS_NewBool(ctx, write_whole_file(path, value_to_text(ctx, argv[0])));
}

JSValue config_ok(JSContext*, JSValueConst, int, JSValueConst*) {
    return JS_TRUE;
}

JSValue new_json_config(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "new JsonConfigFile(path[, default]) requires a path");
    const std::string requested = value_to_text(ctx, argv[0]);
    std::filesystem::path path;
    if (!resolve_relative(requested, path)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    if (!std::filesystem::exists(path)) {
        write_whole_file(path, argc > 1 ? value_to_text(ctx, argv[1]) : "{}\n");
    }

    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "__path", JS_NewString(ctx, requested.c_str()));
    JS_SetPropertyStr(ctx, object, "path", JS_NewString(ctx, requested.c_str()));
    JS_SetPropertyStr(ctx, object, "init", JS_NewCFunction(ctx, config_init, "init", 2));
    JS_SetPropertyStr(ctx, object, "set", JS_NewCFunction(ctx, config_set, "set", 2));
    JS_SetPropertyStr(ctx, object, "get", JS_NewCFunction(ctx, config_get, "get", 2));
    JS_SetPropertyStr(ctx, object, "delete", JS_NewCFunction(ctx, config_delete, "delete", 1));
    JS_SetPropertyStr(ctx, object, "getPath", JS_NewCFunction(ctx, config_get_path, "getPath", 0));
    JS_SetPropertyStr(ctx, object, "read", JS_NewCFunction(ctx, config_read, "read", 0));
    JS_SetPropertyStr(ctx, object, "write", JS_NewCFunction(ctx, config_write, "write", 1));
    JS_SetPropertyStr(ctx, object, "reload", JS_NewCFunction(ctx, config_ok, "reload", 0));
    JS_SetPropertyStr(ctx, object, "close", JS_NewCFunction(ctx, config_ok, "close", 0));
    return object;
}

/* ---------------- INI config ---------------- */

using IniSections = std::map<std::string, std::map<std::string, std::string>>;

IniSections parse_ini(const std::string& text) {
    IniSections sections;
    std::string section;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') {
            continue;
        }
        if (line[first] == '[') {
            const auto close = line.find(']', first);
            if (close != std::string::npos) {
                section = line.substr(first + 1, close - first - 1);
            }
            continue;
        }
        const auto equals = line.find('=', first);
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = line.substr(first, equals - first);
        std::string value = line.substr(equals + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        const auto value_start = value.find_first_not_of(" \t");
        value = value_start == std::string::npos ? "" : value.substr(value_start);
        sections[section][key] = value;
    }
    return sections;
}

std::string serialize_ini(const IniSections& sections) {
    std::string text;
    for (const auto& [section, values] : sections) {
        if (!section.empty()) {
            text += '[' + section + "]\n";
        }
        for (const auto& [key, value] : values) {
            text += key + '=' + value + '\n';
        }
        text += '\n';
    }
    return text;
}

bool ini_load(JSContext* ctx, JSValueConst this_val, std::filesystem::path& path, IniSections& sections) {
    if (!config_path_of(ctx, this_val, path)) {
        return false;
    }
    std::string text;
    read_whole_file(path, text);
    sections = parse_ini(text);
    return true;
}

JSValue ini_get_value(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int kind) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "get(section, name[, default]) requires two arguments");
    std::filesystem::path path;
    IniSections sections;
    if (!ini_load(ctx, this_val, path, sections)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    const std::string section = value_to_text(ctx, argv[0]);
    const std::string name = value_to_text(ctx, argv[1]);
    auto section_it = sections.find(section);
    std::string raw;
    bool found = false;
    if (section_it != sections.end()) {
        auto value_it = section_it->second.find(name);
        if (value_it != section_it->second.end()) {
            raw = value_it->second;
            found = true;
        }
    }
    if (!found) {
        if (argc > 2) return JS_DupValue(ctx, argv[2]);
        if (kind == 0) return JS_NewString(ctx, "");
        if (kind == 1) return JS_NewInt32(ctx, 0);
        if (kind == 2) return JS_NewFloat64(ctx, 0.0);
        return JS_FALSE;
    }
    if (kind == 0) return JS_NewString(ctx, raw.c_str());
    if (kind == 1) return JS_NewInt32(ctx, std::atoi(raw.c_str()));
    if (kind == 2) return JS_NewFloat64(ctx, std::atof(raw.c_str()));
    const bool truthy = raw == "true" || raw == "1" || raw == "yes" || raw == "on";
    return JS_NewBool(ctx, truthy);
}

JSValue ini_get_str(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return ini_get_value(ctx, this_val, argc, argv, 0);
}
JSValue ini_get_int(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return ini_get_value(ctx, this_val, argc, argv, 1);
}
JSValue ini_get_float(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return ini_get_value(ctx, this_val, argc, argv, 2);
}
JSValue ini_get_bool(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return ini_get_value(ctx, this_val, argc, argv, 3);
}

JSValue ini_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "set(section, name, value) requires three arguments");
    std::filesystem::path path;
    IniSections sections;
    if (!ini_load(ctx, this_val, path, sections)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    sections[value_to_text(ctx, argv[0])][value_to_text(ctx, argv[1])] = value_to_text(ctx, argv[2]);
    return JS_NewBool(ctx, write_whole_file(path, serialize_ini(sections)));
}

JSValue ini_init(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "init(section, name, default) requires three arguments");
    std::filesystem::path path;
    IniSections sections;
    if (!ini_load(ctx, this_val, path, sections)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    const std::string section = value_to_text(ctx, argv[0]);
    const std::string name = value_to_text(ctx, argv[1]);
    auto section_it = sections.find(section);
    if (section_it != sections.end()) {
        auto value_it = section_it->second.find(name);
        if (value_it != section_it->second.end()) {
            return JS_NewString(ctx, value_it->second.c_str());
        }
    }
    sections[section][name] = value_to_text(ctx, argv[2]);
    write_whole_file(path, serialize_ini(sections));
    return JS_DupValue(ctx, argv[2]);
}

JSValue ini_delete(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "delete(section, name) requires two arguments");
    std::filesystem::path path;
    IniSections sections;
    if (!ini_load(ctx, this_val, path, sections)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    const std::string section = value_to_text(ctx, argv[0]);
    const std::string name = value_to_text(ctx, argv[1]);
    auto section_it = sections.find(section);
    if (section_it == sections.end() || section_it->second.erase(name) == 0) {
        return JS_FALSE;
    }
    return JS_NewBool(ctx, write_whole_file(path, serialize_ini(sections)));
}

JSValue new_ini_config(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "new IniConfigFile(path[, default]) requires a path");
    const std::string requested = value_to_text(ctx, argv[0]);
    std::filesystem::path path;
    if (!resolve_relative(requested, path)) {
        return JS_ThrowInternalError(ctx, "config path is outside the server directory");
    }
    if (!std::filesystem::exists(path)) {
        write_whole_file(path, argc > 1 ? value_to_text(ctx, argv[1]) : "");
    }

    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "__path", JS_NewString(ctx, requested.c_str()));
    JS_SetPropertyStr(ctx, object, "path", JS_NewString(ctx, requested.c_str()));
    JS_SetPropertyStr(ctx, object, "init", JS_NewCFunction(ctx, ini_init, "init", 3));
    JS_SetPropertyStr(ctx, object, "set", JS_NewCFunction(ctx, ini_set, "set", 3));
    JS_SetPropertyStr(ctx, object, "getStr", JS_NewCFunction(ctx, ini_get_str, "getStr", 3));
    JS_SetPropertyStr(ctx, object, "getInt", JS_NewCFunction(ctx, ini_get_int, "getInt", 3));
    JS_SetPropertyStr(ctx, object, "getFloat", JS_NewCFunction(ctx, ini_get_float, "getFloat", 3));
    JS_SetPropertyStr(ctx, object, "getBool", JS_NewCFunction(ctx, ini_get_bool, "getBool", 3));
    JS_SetPropertyStr(ctx, object, "delete", JS_NewCFunction(ctx, ini_delete, "delete", 2));
    JS_SetPropertyStr(ctx, object, "getPath", JS_NewCFunction(ctx, config_get_path, "getPath", 0));
    JS_SetPropertyStr(ctx, object, "read", JS_NewCFunction(ctx, config_read, "read", 0));
    JS_SetPropertyStr(ctx, object, "write", JS_NewCFunction(ctx, config_write, "write", 1));
    JS_SetPropertyStr(ctx, object, "reload", JS_NewCFunction(ctx, config_ok, "reload", 0));
    JS_SetPropertyStr(ctx, object, "close", JS_NewCFunction(ctx, config_ok, "close", 0));
    return object;
}

/* ---------------- File object ---------------- */

/*
 * File(path, mode[, isBinary]) keeps a stdio handle in a process-wide table.
 * Handles are addressed by an `__id` property instead of an opaque pointer so
 * the object needs no registered QuickJS class.  The JS engine serializes all
 * calls, and the table mutex still guards creation/closure.
 */
struct FileState {
    FILE* handle = nullptr;
};

std::mutex& file_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint64_t, FileState>& file_table() {
    static std::unordered_map<uint64_t, FileState> table;
    return table;
}

uint64_t& next_file_id() {
    static uint64_t id = 1;
    return id;
}

uint64_t file_id_of(JSContext* ctx, JSValueConst value) {
    JSValue id_value = JS_GetPropertyStr(ctx, value, "__id");
    int64_t id = 0;
    JS_ToInt64(ctx, &id, id_value);
    JS_FreeValue(ctx, id_value);
    return static_cast<uint64_t>(id);
}

FILE* file_handle(JSContext* ctx, JSValueConst this_val) {
    const uint64_t id = file_id_of(ctx, this_val);
    std::lock_guard lock(file_mutex());
    auto it = file_table().find(id);
    if (it == file_table().end() || it->second.handle == nullptr) {
        JS_ThrowTypeError(ctx, "invalid or closed File object");
        return nullptr;
    }
    return it->second.handle;
}

JSValue file_read_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_read_line_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_write_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_write_line_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_seek(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_is_eof(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_flush(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_size(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_async_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue file_async_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

JSValue file_construct(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "new File(path, mode[, isBinary]) requires path and mode");
    std::filesystem::path path;
    const std::string requested = value_to_text(ctx, argv[0]);
    if (!resolve_relative(requested, path)) {
        return JS_ThrowInternalError(ctx, "file path is outside the server directory");
    }
    const std::string mode_text = value_to_text(ctx, argv[1]);
    const bool binary = argc > 2 && JS_ToBool(ctx, argv[2]) == 1;
    std::string mode = binary ? "rb" : "r";
    if (mode_text == "w" || mode_text == "WriteMode") {
        mode = binary ? "wb" : "w";
    } else if (mode_text == "a" || mode_text == "AppendMode") {
        mode = binary ? "ab" : "a";
    }

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    FILE* handle = std::fopen(path.string().c_str(), mode.c_str());
    if (handle == nullptr) {
        return JS_ThrowInternalError(ctx, "cannot open %s", requested.c_str());
    }

    uint64_t id = 0;
    {
        std::lock_guard lock(file_mutex());
        id = next_file_id()++;
        file_table()[id] = FileState{handle};
    }

    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "__id", JS_NewInt64(ctx, static_cast<int64_t>(id)));
    JS_SetPropertyStr(ctx, object, "path", JS_NewString(ctx, requested.c_str()));
    JS_SetPropertyStr(ctx, object, "absolutePath", JS_NewString(ctx, path.string().c_str()));
    JS_SetPropertyStr(ctx, object, "readSync", JS_NewCFunction(ctx, file_read_sync, "readSync", 1));
    JS_SetPropertyStr(ctx, object, "readLineSync",
                      JS_NewCFunction(ctx, file_read_line_sync, "readLineSync", 0));
    JS_SetPropertyStr(ctx, object, "readAllSync", JS_NewCFunction(ctx, file_read_sync, "readAllSync", 0));
    JS_SetPropertyStr(ctx, object, "writeSync", JS_NewCFunction(ctx, file_write_sync, "writeSync", 1));
    JS_SetPropertyStr(ctx, object, "writeLineSync",
                      JS_NewCFunction(ctx, file_write_line_sync, "writeLineSync", 1));
    JS_SetPropertyStr(ctx, object, "read", JS_NewCFunction(ctx, file_async_read, "read", 2));
    JS_SetPropertyStr(ctx, object, "readLine", JS_NewCFunction(ctx, file_async_read, "readLine", 1));
    JS_SetPropertyStr(ctx, object, "readAll", JS_NewCFunction(ctx, file_async_read, "readAll", 1));
    JS_SetPropertyStr(ctx, object, "write", JS_NewCFunction(ctx, file_async_write, "write", 2));
    JS_SetPropertyStr(ctx, object, "writeLine", JS_NewCFunction(ctx, file_async_write, "writeLine", 2));
    JS_SetPropertyStr(ctx, object, "seekTo", JS_NewCFunction(ctx, file_seek, "seekTo", 2));
    JS_SetPropertyStr(ctx, object, "close", JS_NewCFunction(ctx, file_close, "close", 0));
    JS_SetPropertyStr(ctx, object, "isEOF", JS_NewCFunction(ctx, file_is_eof, "isEOF", 0));
    JS_SetPropertyStr(ctx, object, "flush", JS_NewCFunction(ctx, file_flush, "flush", 0));
    JS_SetPropertyStr(ctx, object, "getSize", JS_NewCFunction(ctx, file_size, "getSize", 0));
    JS_SetPropertyStr(ctx, object, "size", JS_NewInt64(ctx, 0));

    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsObject(proto)) {
        JS_SetPrototype(ctx, object, proto);
        JS_FreeValue(ctx, proto);
    }
    return object;
}

JSValue file_read_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    int64_t count = -1;
    if (argc > 0) {
        JS_ToInt64(ctx, &count, argv[0]);
    }
    if (count < 0) {
        std::string data;
        char buffer[4096];
        size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), handle)) > 0) {
            data.append(buffer, read);
        }
        return JS_NewStringLen(ctx, data.data(), data.size());
    }
    std::vector<char> buffer(static_cast<size_t>(count));
    const size_t read = std::fread(buffer.data(), 1, buffer.size(), handle);
    return JS_NewStringLen(ctx, buffer.data(), read);
}

JSValue file_read_line_sync(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    std::string line;
    int ch = 0;
    while ((ch = std::fgetc(handle)) != EOF) {
        if (ch == '\n') break;
        line.push_back(static_cast<char>(ch));
    }
    if (ch == EOF && line.empty()) return JS_NULL;
    return JS_NewStringLen(ctx, line.data(), line.size());
}

JSValue file_size(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    const long current = std::ftell(handle);
    std::fseek(handle, 0, SEEK_END);
    const long size = std::ftell(handle);
    std::fseek(handle, current, SEEK_SET);
    return JS_NewInt64(ctx, size < 0 ? 0 : size);
}

JSValue file_write_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "writeSync(text) requires text");
    const std::string text = value_to_text(ctx, argv[0]);
    const size_t written = std::fwrite(text.data(), 1, text.size(), handle);
    return JS_NewBool(ctx, written == text.size());
}

JSValue file_write_line_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = file_write_sync(ctx, this_val, argc, argv);
    if (JS_IsException(result)) return result;
    FILE* handle = file_handle(ctx, this_val);
    if (handle != nullptr) {
        std::fputc('\n', handle);
    }
    return result;
}

JSValue file_seek(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    int64_t offset = 0;
    if (argc > 0 && JS_ToInt64(ctx, &offset, argv[0]) < 0) return JS_EXCEPTION;
    const bool relative = argc > 1 && JS_ToBool(ctx, argv[1]) == 1;
    return JS_NewBool(ctx, std::fseek(handle, static_cast<long>(offset),
                                      relative ? SEEK_CUR : SEEK_SET) == 0);
}

JSValue file_close(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    const uint64_t id = file_id_of(ctx, this_val);
    std::lock_guard lock(file_mutex());
    auto it = file_table().find(id);
    if (it == file_table().end()) {
        return JS_FALSE;
    }
    if (it->second.handle != nullptr) {
        std::fclose(it->second.handle);
        it->second.handle = nullptr;
    }
    file_table().erase(it);
    return JS_TRUE;
}

JSValue file_is_eof(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    return JS_NewBool(ctx, std::feof(handle) != 0);
}

JSValue file_flush(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    FILE* handle = file_handle(ctx, this_val);
    if (handle == nullptr) return JS_EXCEPTION;
    return JS_NewBool(ctx, std::fflush(handle) == 0);
}

JSValue file_async_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue result = file_read_sync(ctx, this_val, 0, nullptr);
    if (JS_IsException(result)) return result;
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue call_result = JS_Call(ctx, argv[0], JS_UNDEFINED, 1, &result);
        if (JS_IsException(call_result)) {
            log_exception(ctx);
        }
        JS_FreeValue(ctx, call_result);
    }
    JS_FreeValue(ctx, result);
    return JS_TRUE;
}

JSValue file_async_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_FALSE;
    JSValue result = file_write_sync(ctx, this_val, 1, argv);
    if (JS_IsException(result)) return result;
    if (argc > 1 && JS_IsFunction(ctx, argv[1])) {
        JSValue call_result = JS_Call(ctx, argv[1], JS_UNDEFINED, 1, &result);
        if (JS_IsException(call_result)) {
            log_exception(ctx);
        }
        JS_FreeValue(ctx, call_result);
    }
    JS_FreeValue(ctx, result);
    return JS_TRUE;
}

JSValue global_mc(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mc = JS_GetPropertyStr(ctx, global, "mc");
    JS_FreeValue(ctx, global);
    return mc;
}

} // namespace

void register_script_helpers(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "log", JS_NewCFunction(ctx, console_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "colorLog", JS_NewCFunction(ctx, console_color_log, "colorLog", 2));
    JS_SetPropertyStr(ctx, global, "fastLog", JS_NewCFunction(ctx, console_log, "fastLog", 1));
    JS_SetPropertyStr(ctx, global, "JsonConfigFile",
                      JS_NewCFunction2(ctx, new_json_config, "JsonConfigFile", 2, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "IniConfigFile",
                      JS_NewCFunction2(ctx, new_ini_config, "IniConfigFile", 2, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "File",
                      JS_NewCFunction2(ctx, file_construct, "File", 3, JS_CFUNC_constructor, 0));
    JS_FreeValue(ctx, global);

    JSValue mc = global_mc(ctx);
    JS_SetPropertyStr(ctx, mc, "newConfig",
                      JS_NewCFunction2(ctx, new_json_config, "newConfig", 2, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, mc, "newJsonConfig",
                      JS_NewCFunction2(ctx, new_json_config, "newJsonConfig", 2, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, mc, "newIniConfig",
                      JS_NewCFunction2(ctx, new_ini_config, "newIniConfig", 2, JS_CFUNC_constructor, 0));
    JS_FreeValue(ctx, mc);
}

void close_files() {
    std::lock_guard lock(file_mutex());
    for (auto& [id, state] : file_table()) {
        if (state.handle != nullptr) {
            std::fclose(state.handle);
            state.handle = nullptr;
        }
    }
    file_table().clear();
}

} // namespace yoyo::js
