#include "plugin_manifest.h"

#include "log.h"

extern "C" {
#include "quickjs.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace yoyo::plugins {
namespace {

namespace fs = std::filesystem;

std::string read_text(const fs::path& path, bool& ok) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ok = true;
    return buffer.str();
}

bool write_text(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return file.good();
}

/*
 * Minimal JSON access through QuickJS: the runtime already links the engine,
 * so manifests are parsed with the same parser the plugin scripts use rather
 * than a second hand-written one.
 */
class JsonValue {
public:
    JsonValue() = default;
    explicit JsonValue(JSContext* ctx, JSValue value) : ctx_(ctx), value_(value) {}
    JsonValue(const JsonValue&) = delete;
    JsonValue& operator=(const JsonValue&) = delete;
    JsonValue(JsonValue&& other) noexcept : ctx_(other.ctx_), value_(other.value_) {
        other.ctx_ = nullptr;
        other.value_ = JS_UNDEFINED;
    }
    JsonValue& operator=(JsonValue&& other) noexcept {
        if (this != &other) {
            reset();
            ctx_ = other.ctx_;
            value_ = other.value_;
            other.ctx_ = nullptr;
            other.value_ = JS_UNDEFINED;
        }
        return *this;
    }
    ~JsonValue() { reset(); }

    void reset() {
        if (ctx_ != nullptr && !JS_IsUndefined(value_)) {
            JS_FreeValue(ctx_, value_);
        }
        ctx_ = nullptr;
        value_ = JS_UNDEFINED;
    }

    [[nodiscard]] bool is_object() const { return ctx_ != nullptr && JS_IsObject(value_); }
    [[nodiscard]] bool is_string() const { return ctx_ != nullptr && JS_IsString(value_); }
    [[nodiscard]] bool is_array() const { return ctx_ != nullptr && JS_IsArray(value_); }
    [[nodiscard]] bool is_undefined() const { return ctx_ == nullptr || JS_IsUndefined(value_); }

    [[nodiscard]] std::string as_string() const {
        if (!is_string()) {
            return {};
        }
        const char* text = JS_ToCString(ctx_, value_);
        std::string out = text ? text : "";
        if (text) {
            JS_FreeCString(ctx_, text);
        }
        return out;
    }

    [[nodiscard]] JsonValue property(const char* name) const {
        if (!is_object()) {
            return {};
        }
        return JsonValue(ctx_, JS_GetPropertyStr(ctx_, value_, name));
    }

    [[nodiscard]] std::vector<std::string> as_string_array() const {
        std::vector<std::string> out;
        if (!is_array()) {
            return out;
        }
        JSValue length_value = JS_GetPropertyStr(ctx_, value_, "length");
        int32_t length = 0;
        JS_ToInt32(ctx_, &length, length_value);
        JS_FreeValue(ctx_, length_value);
        for (int32_t i = 0; i < length; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx_, value_, static_cast<uint32_t>(i));
            if (JS_IsString(item)) {
                const char* text = JS_ToCString(ctx_, item);
                if (text != nullptr) {
                    out.emplace_back(text);
                    JS_FreeCString(ctx_, text);
                }
            }
            JS_FreeValue(ctx_, item);
        }
        return out;
    }

    [[nodiscard]] JSContext* context() const { return ctx_; }
    [[nodiscard]] JSValue raw() const { return value_; }

private:
    JSContext* ctx_ = nullptr;
    JSValue value_ = JS_UNDEFINED;
};

struct JsonDocument {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JsonValue root;

    JsonDocument() = default;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    ~JsonDocument() { close(); }

    bool open(const std::string& text, const std::string& filename) {
        runtime = JS_NewRuntime();
        if (runtime == nullptr) {
            return false;
        }
        JS_SetMemoryLimit(runtime, 8ull * 1024ull * 1024ull);
        context = JS_NewContext(runtime);
        if (context == nullptr) {
            return false;
        }
        JSValue value = JS_ParseJSON(context, text.data(), text.size(), filename.c_str());
        if (JS_IsException(value)) {
            JS_FreeValue(context, value);
            return false;
        }
        root = JsonValue(context, value);
        return true;
    }

    void close() {
        root.reset();
        if (context != nullptr) {
            JS_FreeContext(context);
            context = nullptr;
        }
        if (runtime != nullptr) {
            JS_FreeRuntime(runtime);
            runtime = nullptr;
        }
    }
};

Engine engine_from_text(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text == "javascript" || text == "js" || text == "quickjs") {
        return Engine::Javascript;
    }
    return Engine::Native;
}

/* Escape a string for the manifest we generate ourselves. */
std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(ch));
                out += buffer;
            } else {
                out += ch;
            }
            break;
        }
    }
    return out;
}

/*
 * Loose .js file -> plugins/<stem>/<stem>.js + manifest.json.
 * Runs once; afterwards the directory is treated like any other plugin.
 */
bool adopt_loose_script(const fs::path& file, const std::string& plugins_directory, Manifest& out) {
    const std::string stem = file.stem().string();
    const fs::path target_dir = fs::path(plugins_directory) / stem;
    std::error_code error;
    fs::create_directories(target_dir, error);
    if (error) {
        log_write(LogLevel::Error, "cannot create plugin directory for %s: %s", stem.c_str(),
                  error.message().c_str());
        return false;
    }
    const fs::path target_file = target_dir / (stem + ".js");
    fs::copy_file(file, target_file, fs::copy_options::overwrite_existing, error);
    if (error) {
        log_write(LogLevel::Error, "cannot move %s into %s: %s", file.string().c_str(),
                  target_dir.string().c_str(), error.message().c_str());
        return false;
    }

    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"name\": \"" << json_escape(stem) << "\",\n"
             << "  \"version\": \"0.0.0\",\n"
             << "  \"engine\": \"javascript\",\n"
             << "  \"entry\": \"" << json_escape(stem) << ".js\",\n"
             << "  \"description\": \"auto-generated from " << json_escape(stem) << ".js\",\n"
             << "  \"dependencies\": [],\n"
             << "  \"optionalDependencies\": []\n"
             << "}\n";
    const fs::path manifest_path = target_dir / "manifest.json";
    if (!write_text(manifest_path, manifest.str())) {
        log_write(LogLevel::Error, "cannot write %s", manifest_path.string().c_str());
        return false;
    }
    fs::remove(file, error);

    out.name = stem;
    out.version = "0.0.0";
    out.engine = Engine::Javascript;
    out.entry = stem + ".js";
    out.directory = target_dir.string();
    out.generated = true;
    log_write(LogLevel::Info, "loose script %s adopted as plugins/%s/ with a generated manifest",
              file.filename().string().c_str(), stem.c_str());
    return true;
}

bool parse_manifest(const fs::path& directory, Manifest& out, std::string& error) {
    const fs::path manifest_path = directory / "manifest.json";
    bool ok = false;
    const std::string text = read_text(manifest_path, ok);
    if (!ok) {
        error = "missing manifest.json";
        return false;
    }
    JsonDocument document;
    if (!document.open(text, manifest_path.string())) {
        error = "manifest.json is not valid JSON";
        return false;
    }
    if (!document.root.is_object()) {
        error = "manifest.json must be an object";
        return false;
    }

    out.directory = directory.string();
    out.name = document.root.property("name").as_string();
    if (out.name.empty()) {
        out.name = directory.filename().string();
    }
    out.version = document.root.property("version").as_string();
    if (out.version.empty()) {
        out.version = "0.0.0";
    }
    out.description = document.root.property("description").as_string();
    out.engine = engine_from_text(document.root.property("engine").as_string());
    out.entry = document.root.property("entry").as_string();
    out.dependencies = document.root.property("dependencies").as_string_array();
    out.optional_dependencies = document.root.property("optionalDependencies").as_string_array();

    if (out.entry.empty()) {
        error = "manifest.json has no \"entry\"";
        return false;
    }
    const fs::path entry_path = directory / out.entry;
    std::error_code ec;
    if (!fs::exists(entry_path, ec)) {
        error = "entry file not found: " + out.entry;
        return false;
    }
    return true;
}

/*
 * Depth-first topological sort with cycle detection.  Plugins in a cycle, or
 * depending on a missing/disabled plugin, are dropped and reported.
 */
class Resolver {
public:
    Resolver(std::map<std::string, Manifest>& plugins, std::vector<std::string>& skipped)
        : plugins_(plugins), skipped_(skipped) {}

    std::vector<Manifest> run() {
        for (auto& [name, manifest] : plugins_) {
            visit(name, {});
        }
        /* Any plugin still unvisited is part of a cycle (or depended on one). */
        for (auto& [name, manifest] : plugins_) {
            if (state_[name] != State::Done) {
                skipped_.push_back(name + ": dependency cycle or unresolved dependency");
                log_write(LogLevel::Error, "plugin '%s' skipped: dependency cycle or unresolved dependency",
                          name.c_str());
            }
        }
        return order_;
    }

private:
    enum class State { Unvisited, Visiting, Done, Failed };

    bool visit(const std::string& name, std::vector<std::string> stack) {
        auto plugin_it = plugins_.find(name);
        if (plugin_it == plugins_.end()) {
            return false;
        }
        auto& state = state_[name];
        if (state == State::Done) {
            return true;
        }
        if (state == State::Visiting) {
            /* Cycle: report the path so the author can see what loops. */
            std::string path;
            for (const auto& item : stack) {
                path += item + " -> ";
            }
            path += name;
            skipped_.push_back(name + ": dependency cycle (" + path + ")");
            log_write(LogLevel::Error, "plugin '%s' has a dependency cycle: %s", name.c_str(), path.c_str());
            state = State::Failed;
            return false;
        }
        if (state == State::Failed) {
            return false;
        }
        state = State::Visiting;
        stack.push_back(name);

        for (const auto& dependency : plugin_it->second.dependencies) {
            if (plugins_.find(dependency) == plugins_.end()) {
                skipped_.push_back(name + ": missing required dependency '" + dependency + "'");
                log_write(LogLevel::Error, "plugin '%s' skipped: missing required dependency '%s'",
                          name.c_str(), dependency.c_str());
                state = State::Failed;
                return false;
            }
            if (!visit(dependency, stack)) {
                skipped_.push_back(name + ": dependency '" + dependency + "' failed to load");
                state = State::Failed;
                return false;
            }
        }
        /* Optional dependencies only order things; never required. */
        for (const auto& dependency : plugin_it->second.optional_dependencies) {
            if (plugins_.find(dependency) != plugins_.end()) {
                visit(dependency, stack);
            }
        }

        state = State::Done;
        order_.push_back(plugin_it->second);
        return true;
    }

    std::map<std::string, Manifest>& plugins_;
    std::vector<std::string>& skipped_;
    std::map<std::string, State> state_;
    std::vector<Manifest> order_;
};

} // namespace

const char* engine_name(Engine engine) {
    return engine == Engine::Javascript ? "javascript" : "native";
}

std::vector<Manifest> discover(const std::string& plugins_directory, std::vector<std::string>& skipped) {
    namespace fs = std::filesystem;
    std::map<std::string, Manifest> plugins;
    std::error_code error;

    if (!fs::is_directory(plugins_directory, error)) {
        return {};
    }

    /* Pass 1: loose scripts become directories, so pass 2 sees one shape. */
    std::vector<fs::path> loose;
    for (const auto& entry : fs::directory_iterator(plugins_directory, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".js") {
            loose.push_back(entry.path());
        }
    }
    std::set<std::string> adopted;
    for (const auto& file : loose) {
        Manifest manifest;
        if (adopt_loose_script(file, plugins_directory, manifest)) {
            /* Pass 2 will read the manifest we just wrote; remember the name so
             * it is not reported as a duplicate. */
            adopted.insert(manifest.name);
        }
    }

    /* Pass 2: read every manifest.json (including the ones just generated). */
    for (const auto& entry : fs::directory_iterator(plugins_directory, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory()) {
            continue;
        }
        /* `data` is reserved for plugin storage. */
        if (entry.path().filename() == "data") {
            continue;
        }
        Manifest manifest;
        std::string reason;
        if (!parse_manifest(entry.path(), manifest, reason)) {
            skipped.push_back(entry.path().filename().string() + ": " + reason);
            log_write(LogLevel::Error, "plugin directory %s skipped: %s",
                      entry.path().filename().string().c_str(), reason.c_str());
            continue;
        }
        if (plugins.find(manifest.name) != plugins.end()) {
            skipped.push_back(manifest.name + ": duplicate plugin name");
            log_write(LogLevel::Error, "duplicate plugin name '%s' in %s", manifest.name.c_str(),
                      entry.path().string().c_str());
            continue;
        }
        plugins[manifest.name] = manifest;
    }

    Resolver resolver(plugins, skipped);
    return resolver.run();
}

} // namespace yoyo::plugins
