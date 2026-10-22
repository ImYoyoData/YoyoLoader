#include <yoyo/plugin.h>

#include <cstdio>
#include <cstring>

namespace {

const yoyo_api* g_api = nullptr;

const char* player_name(const yoyo_player_view& player) {
    return (player.name != nullptr && player.name[0] != '\0') ? player.name : "<unknown>";
}

void on_event(void*, int type, const void* event) {
    if (g_api == nullptr || event == nullptr) {
        return;
    }
    char line[512];
    switch (type) {
    case YOYO_EVENT_PRE_JOIN: {
        const auto* e = static_cast<const yoyo_pre_join_event*>(event);
        std::snprintf(line, sizeof(line), "pre_join name=%s xuid=%s", player_name(e->player),
                      e->player.xuid ? e->player.xuid : "");
        break;
    }
    case YOYO_EVENT_JOIN: {
        const auto* e = static_cast<const yoyo_join_event*>(event);
        std::snprintf(line, sizeof(line), "join name=%s player_id=%llu", player_name(e->player),
                      static_cast<unsigned long long>(e->player_id));
        if (e->player.handle != nullptr && g_api->player_send_message != nullptr) {
            g_api->player_send_message(e->player.handle, "§a[YoyoLoader] hello from the C++ sample");
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            const int dimension = g_api->player_get_dimension(e->player.handle);
            if (g_api->player_get_position(e->player.handle, &x, &y, &z) == 0) {
                char location[160];
                std::snprintf(location, sizeof(location), "position %.1f %.1f %.1f dimension %d", x, y, z,
                              dimension);
                g_api->log(0, location);
            }
        }
        break;
    }
    case YOYO_EVENT_CHAT: {
        const auto* e = static_cast<const yoyo_chat_event*>(event);
        std::snprintf(line, sizeof(line), "chat name=%s message=%s", player_name(e->player),
                      e->message ? e->message : "");
        break;
    }
    case YOYO_EVENT_LEFT: {
        const auto* e = static_cast<const yoyo_left_event*>(event);
        std::snprintf(line, sizeof(line), "left name=%s reason=%s", player_name(e->player),
                      e->reason ? e->reason : "");
        break;
    }
    case YOYO_EVENT_PLAYER_HEALTH: {
        const auto* e = static_cast<const yoyo_detail_event*>(event);
        std::snprintf(line, sizeof(line), "health name=%s value=%d", player_name(e->player), e->value);
        break;
    }
    case YOYO_EVENT_PLAYER_JUMP: {
        const auto* e = static_cast<const yoyo_detail_event*>(event);
        std::snprintf(line, sizeof(line), "jump name=%s", player_name(e->player));
        break;
    }
    default: return;
    }
    g_api->log(0, line);
}

} // namespace

extern "C" {
YOYO_EXPORT const yoyo_api* YOYO_CALL yoyo_plugin_entry(const yoyo_api* api) {
    if (api == nullptr || api->abi_version != YOYO_PLUGIN_ABI_VERSION) {
        return nullptr;
    }
    g_api = api;
    api->register_event(YOYO_EVENT_PRE_JOIN, &on_event, nullptr);
    api->register_event(YOYO_EVENT_JOIN, &on_event, nullptr);
    api->register_event(YOYO_EVENT_CHAT, &on_event, nullptr);
    api->register_event(YOYO_EVENT_LEFT, &on_event, nullptr);
    api->register_event(YOYO_EVENT_PLAYER_HEALTH, &on_event, nullptr);
    api->register_event(YOYO_EVENT_PLAYER_JUMP, &on_event, nullptr);
    api->log(0, "sample_cpp plugin loaded");
    return api;
}
}
