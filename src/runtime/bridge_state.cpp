#include "bridge_state.h"

#include "log.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include "quickjs.h"

namespace yoyo {
namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

std::atomic<bool> g_running{false};
std::atomic<int64_t> g_last_push_ms{-1};
std::thread g_thread;
Socket g_listen = kInvalidSocket;

std::mutex& state_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, BridgePlayer>& players() {
    static std::unordered_map<std::string, BridgePlayer> map;
    return map;
}

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void close_socket(Socket socket) {
    if (socket == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

/*
 * Minimal JSON reader built on QuickJS, which is already linked in.
 *
 * Reusing the engine avoids a second JSON implementation and means malformed
 * input from the bridge is rejected by a real parser rather than by hand-rolled
 * string matching.
 */
struct JsonDocument {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JSValue root = JS_UNDEFINED;

    JsonDocument() = default;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    ~JsonDocument() {
        if (context != nullptr) {
            JS_FreeValue(context, root);
        }
        if (runtime != nullptr) {
            JS_FreeContext(context);
            JS_FreeRuntime(runtime);
        }
    }

    bool open(const std::string& text) {
        if (text.empty() || text.size() > 4 * 1024 * 1024) {
            return false;
        }
        runtime = JS_NewRuntime();
        if (runtime == nullptr) {
            return false;
        }
        JS_SetMemoryLimit(runtime, 16ull * 1024ull * 1024ull);
        context = JS_NewContext(runtime);
        if (context == nullptr) {
            return false;
        }
        root = JS_ParseJSON(context, text.data(), text.size(), "bridge");
        return !JS_IsException(root);
    }

    std::string string_at(JSValueConst object, const char* key) const {
        JSValue value = JS_GetPropertyStr(context, object, key);
        std::string out;
        if (JS_IsString(value)) {
            const char* text = JS_ToCString(context, value);
            if (text != nullptr) {
                out = text;
                JS_FreeCString(context, text);
            }
        }
        JS_FreeValue(context, value);
        return out;
    }

    double number_at(JSValueConst object, const char* key, bool& present) const {
        JSValue value = JS_GetPropertyStr(context, object, key);
        double out = 0.0;
        present = JS_IsNumber(value);
        if (present) {
            JS_ToFloat64(context, &out, value);
        }
        JS_FreeValue(context, value);
        return out;
    }
};

bool read_container(JSContext* ctx, JSValueConst parent, const char* key, BridgeContainer& out) {
    JSValue value = JS_GetPropertyStr(ctx, parent, key);
    if (!JS_IsObject(value)) {
        JS_FreeValue(ctx, value);
        return false;
    }
    JSValue size_value = JS_GetPropertyStr(ctx, value, "size");
    int32_t size = 0;
    JS_ToInt32(ctx, &size, size_value);
    JS_FreeValue(ctx, size_value);
    if (size <= 0 || size > 4096) {
        JS_FreeValue(ctx, value);
        return false;
    }
    JSValue slots = JS_GetPropertyStr(ctx, value, "slots");
    if (JS_IsArray(slots)) {
        JSValue length_value = JS_GetPropertyStr(ctx, slots, "length");
        int64_t length = 0;
        JS_ToInt64(ctx, &length, length_value);
        JS_FreeValue(ctx, length_value);
        if (length > 4096) {
            length = 4096;
        }
        for (int64_t i = 0; i < length; ++i) {
            JSValue entry = JS_GetPropertyUint32(ctx, slots, static_cast<uint32_t>(i));
            if (JS_IsObject(entry)) {
                BridgeItem item;
                JSValue slot_value = JS_GetPropertyStr(ctx, entry, "slot");
                int32_t slot = 0;
                JS_ToInt32(ctx, &slot, slot_value);
                JS_FreeValue(ctx, slot_value);
                JSValue amount_value = JS_GetPropertyStr(ctx, entry, "amount");
                int32_t amount = 0;
                JS_ToInt32(ctx, &amount, amount_value);
                JS_FreeValue(ctx, amount_value);
                JSValue type_value = JS_GetPropertyStr(ctx, entry, "typeId");
                const char* type_text = JS_ToCString(ctx, type_value);
                if (type_text != nullptr) {
                    item.type_id = type_text;
                    JS_FreeCString(ctx, type_text);
                }
                JS_FreeValue(ctx, type_value);
                item.slot = slot;
                item.amount = amount;
                if (slot >= 0 && amount > 0 && !item.type_id.empty()) {
                    out.slots.push_back(std::move(item));
                }
            }
            JS_FreeValue(ctx, entry);
        }
    }
    JS_FreeValue(ctx, slots);
    out.size = size;
    out.valid = true;
    JS_FreeValue(ctx, value);
    return true;
}

bool parse_state(const std::string& body) {
    JsonDocument document;
    if (!document.open(body)) {
        return false;
    }
    JSContext* ctx = document.context;
    JSValue players_value = JS_GetPropertyStr(ctx, document.root, "players");
    if (!JS_IsArray(players_value)) {
        JS_FreeValue(ctx, players_value);
        return false;
    }
    bool has_tick = false;
    const double tick = document.number_at(document.root, "tick", has_tick);
    JSValue length_value = JS_GetPropertyStr(ctx, players_value, "length");
    int64_t length = 0;
    JS_ToInt64(ctx, &length, length_value);
    JS_FreeValue(ctx, length_value);
    if (length < 0 || length > 256) {
        JS_FreeValue(ctx, players_value);
        return false;
    }

    std::lock_guard lock(state_mutex());
    auto& map = players();
    map.clear();
    for (int64_t i = 0; i < length; ++i) {
        JSValue entry = JS_GetPropertyUint32(ctx, players_value, static_cast<uint32_t>(i));
        if (JS_IsObject(entry)) {
            BridgePlayer player;
            player.name = document.string_at(entry, "name");
            player.id = document.string_at(entry, "id");
            player.game_mode = document.string_at(entry, "gameMode");
            player.dimension = document.string_at(entry, "dimension");
            bool has_location = false;
            JSValue location = JS_GetPropertyStr(ctx, entry, "location");
            if (JS_IsObject(location)) {
                bool has_x = false;
                bool has_y = false;
                bool has_z = false;
                player.x = document.number_at(location, "x", has_x);
                player.y = document.number_at(location, "y", has_y);
                player.z = document.number_at(location, "z", has_z);
                player.has_position = has_x && has_y && has_z;
            }
            JS_FreeValue(ctx, location);
            (void)has_location;
            bool has_health = false;
            player.health = document.number_at(entry, "health", has_health);
            read_container(ctx, entry, "inventory", player.inventory);
            read_container(ctx, entry, "enderChest", player.ender_chest);
            read_container(ctx, entry, "equipment", player.equipment);
            player.tick = has_tick ? static_cast<int64_t>(tick) : 0;
            if (!player.name.empty()) {
                map[player.name] = std::move(player);
            }
        }
        JS_FreeValue(ctx, entry);
    }
    JS_FreeValue(ctx, players_value);
    return true;
}

/* Read one HTTP request, answer 200, and return the body. */
bool serve_once(Socket client) {
    std::string request;
    char buffer[4096];
    size_t content_length = 0;
    size_t header_end = std::string::npos;

    while (request.size() < 128 * 1024) {
#ifdef _WIN32
        const int received = recv(client, buffer, sizeof(buffer), 0);
#else
        const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) {
            break;
        }
        request.append(buffer, static_cast<size_t>(received));
        if (header_end == std::string::npos) {
            header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                const std::string headers = request.substr(0, header_end);
                const auto key = headers.find("Content-Length:");
                if (key != std::string::npos) {
                    content_length = static_cast<size_t>(std::strtoul(headers.c_str() + key + 15, nullptr, 10));
                }
            }
        }
        if (header_end != std::string::npos && request.size() >= header_end + 4 + content_length) {
            break;
        }
    }

    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
#ifdef _WIN32
    send(client, response, static_cast<int>(std::strlen(response)), 0);
#else
    send(client, response, std::strlen(response), 0);
#endif

    if (header_end == std::string::npos) {
        return false;
    }
    return parse_state(request.substr(header_end + 4));
}

void listen_loop(int port) {
    Socket listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kInvalidSocket) {
        log_write(LogLevel::Warn, "bridge state: socket creation failed");
        return;
    }
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        log_write(LogLevel::Warn, "bridge state: cannot bind 127.0.0.1:%d", port);
        close_socket(listener);
        return;
    }
    if (listen(listener, 8) != 0) {
        log_write(LogLevel::Warn, "bridge state: listen failed");
        close_socket(listener);
        return;
    }
    g_listen = listener;
    log_write(LogLevel::Info, "bridge state: listening on 127.0.0.1:%d", port);

    while (g_running.load()) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peer_size = sizeof(peer);
#else
        socklen_t peer_size = sizeof(peer);
#endif
        Socket client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (client == kInvalidSocket) {
            if (!g_running.load()) {
                break;
            }
            continue;
        }
        if (serve_once(client)) {
            g_last_push_ms.store(now_ms());
        }
        close_socket(client);
    }
    close_socket(listener);
    g_listen = kInvalidSocket;
}

} // namespace

std::optional<BridgeItem> BridgeContainer::at(int32_t slot) const {
    for (const auto& item : slots) {
        if (item.slot == slot) {
            return item;
        }
    }
    return std::nullopt;
}

bool bridge_state_start(int port) {
    if (g_running.exchange(true)) {
        return true;
    }
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        log_write(LogLevel::Warn, "bridge state: WSAStartup failed");
        g_running.store(false);
        return false;
    }
#endif
    g_thread = std::thread(listen_loop, port);
    return true;
}

void bridge_state_stop() {
    if (!g_running.exchange(false)) {
        return;
    }
    /* Closing the listening socket unblocks accept(). */
    close_socket(g_listen);
    if (g_thread.joinable()) {
        g_thread.join();
    }
}

std::optional<BridgePlayer> bridge_state_player(const std::string& name) {
    std::lock_guard lock(state_mutex());
    auto it = players().find(name);
    if (it == players().end()) {
        return std::nullopt;
    }
    return it->second;
}

bool bridge_state_available() {
    return g_running.load();
}

int64_t bridge_state_age_ms() {
    const int64_t last = g_last_push_ms.load();
    if (last < 0) {
        return -1;
    }
    return now_ms() - last;
}

} // namespace yoyo
