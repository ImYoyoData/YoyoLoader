#include "events.h"

#include "js/js_engine.h"
#include "log.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace yoyo {
namespace {

struct Registration {
    int type = 0;
    yoyo_event_callback callback = nullptr;
    void* user = nullptr;
};

std::mutex g_mutex;
std::vector<Registration> g_registrations;

std::vector<Registration> snapshot(int type) {
    std::lock_guard lock(g_mutex);
    std::vector<Registration> result;
    result.reserve(g_registrations.size());
    for (const auto& item : g_registrations) {
        if (item.type == type) {
            result.push_back(item);
        }
    }
    return result;
}

void fill_player(yoyo_player_view& out, const PlayerView& player) {
    out.handle = player.handle;
    out.runtime_id = player.runtime_id;
    out.name = player.name.empty() ? "" : player.name.c_str();
    out.xuid = player.xuid.empty() ? "" : player.xuid.c_str();
    out.address = player.address.empty() ? "" : player.address.c_str();
}

void call(const std::vector<Registration>& callbacks, int type, const void* event) {
    for (const auto& item : callbacks) {
        if (item.callback != nullptr) {
            item.callback(item.user, type, event);
        }
    }
}

} // namespace

bool register_event(int type, yoyo_event_callback callback, void* user) {
    if (callback == nullptr || type < YOYO_EVENT_PRE_JOIN || type > YOYO_EVENT_SERVER_READY) {
        return false;
    }
    std::lock_guard lock(g_mutex);
    g_registrations.push_back({type, callback, user});
    return true;
}

bool unregister_event(int type, yoyo_event_callback callback, void* user) {
    std::lock_guard lock(g_mutex);
    auto before = g_registrations.size();
    g_registrations.erase(
        std::remove_if(g_registrations.begin(), g_registrations.end(), [&](const Registration& item) {
            return item.type == type && item.callback == callback && item.user == user;
        }),
        g_registrations.end()
    );
    return g_registrations.size() != before;
}

void emit_pre_join(PlayerView& player, bool& cancelled) {
    yoyo_pre_join_event event{};
    fill_player(event.player, player);
    int cancel = 0;
    event.cancelled = &cancel;
    call(snapshot(YOYO_EVENT_PRE_JOIN), YOYO_EVENT_PRE_JOIN, &event);
    cancelled = cancel != 0;
    js::dispatch_pre_join(player, cancelled);
    log_write(LogLevel::Info, "event pre_join name='%s' xuid='%s' cancelled=%d", player.name.c_str(),
              player.xuid.c_str(), cancel);
}

void emit_join(PlayerView& player, uint64_t player_id, bool& cancelled) {
    yoyo_join_event event{};
    fill_player(event.player, player);
    event.player_id = player_id;
    int cancel = 0;
    event.cancelled = &cancel;
    call(snapshot(YOYO_EVENT_JOIN), YOYO_EVENT_JOIN, &event);
    cancelled = cancel != 0;
    js::dispatch_join(player, player_id, cancelled);
    log_write(LogLevel::Info, "event join name='%s' player_id=%llu cancelled=%d", player.name.c_str(),
              static_cast<unsigned long long>(player_id), cancel);
}

void emit_chat(PlayerView& player, std::string_view message, bool& cancelled) {
    std::string text(message);
    yoyo_chat_event event{};
    fill_player(event.player, player);
    event.message = text.c_str();
    int cancel = 0;
    event.cancelled = &cancel;
    call(snapshot(YOYO_EVENT_CHAT), YOYO_EVENT_CHAT, &event);
    cancelled = cancel != 0;
    js::dispatch_chat(player, message, cancelled);
    log_write(LogLevel::Info, "event chat name='%s' message='%.*s' cancelled=%d", player.name.c_str(),
              static_cast<int>(message.size()), message.data(), cancel);
}

void emit_left(PlayerView& player, std::string_view reason) {
    std::string text(reason);
    yoyo_left_event event{};
    fill_player(event.player, player);
    event.reason = text.c_str();
    call(snapshot(YOYO_EVENT_LEFT), YOYO_EVENT_LEFT, &event);
    js::dispatch_left(player, reason);
    log_write(LogLevel::Info, "event left name='%s' reason='%s'", player.name.c_str(), text.c_str());
}

void emit_server_ready(const char* server_version, uintptr_t module_base) {
    yoyo_server_ready_event event{};
    event.server_version = server_version;
    event.module_base = module_base;
    call(snapshot(YOYO_EVENT_SERVER_READY), YOYO_EVENT_SERVER_READY, &event);
}

} // namespace yoyo
