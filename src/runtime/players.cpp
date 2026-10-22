#include "players.h"

#include <mutex>

namespace yoyo {
namespace {
std::mutex g_mutex;
std::vector<PlayerInfo> g_players;
}

void players_upsert(const PlayerInfo& info) {
    if (info.name.empty() && info.xuid.empty()) {
        return;
    }
    std::lock_guard lock(g_mutex);
    for (auto& existing : g_players) {
        bool same = (!info.xuid.empty() && existing.xuid == info.xuid) ||
                    (info.xuid.empty() && !info.name.empty() && existing.name == info.name);
        if (!same) {
            continue;
        }
        if (!info.name.empty()) existing.name = info.name;
        if (!info.xuid.empty()) existing.xuid = info.xuid;
        if (!info.address.empty()) existing.address = info.address;
        if (info.runtime_id != 0) existing.runtime_id = info.runtime_id;
        if (info.handle != nullptr) existing.handle = info.handle;
        existing.online = info.online;
        return;
    }
    g_players.push_back(info);
}

std::optional<PlayerInfo> players_find(const std::string& name_or_xuid) {
    std::lock_guard lock(g_mutex);
    for (const auto& player : g_players) {
        if (player.name == name_or_xuid || (!player.xuid.empty() && player.xuid == name_or_xuid)) {
            return player;
        }
    }
    return std::nullopt;
}

std::vector<PlayerInfo> players_online() {
    std::lock_guard lock(g_mutex);
    std::vector<PlayerInfo> result;
    for (const auto& player : g_players) {
        if (player.online) result.push_back(player);
    }
    return result;
}

std::optional<PlayerInfo> players_mark_joined(uint64_t runtime_id) {
    std::lock_guard lock(g_mutex);
    if (runtime_id != 0) {
        for (auto& player : g_players) {
            if (player.runtime_id == runtime_id) {
                player.online = true;
                return player;
            }
        }
    }
    /* The join packet in this BDS build only carries the runtime id.  When
     * the component lookup succeeded in pre_join the exact match above is
     * used; otherwise the newest pending player is the only safe guess. */
    for (auto it = g_players.rbegin(); it != g_players.rend(); ++it) {
        if (!it->online && it->handle != nullptr) {
            if (runtime_id != 0) it->runtime_id = runtime_id;
            it->online = true;
            return *it;
        }
    }
    return std::nullopt;
}

bool players_is_online(void* handle) {
    std::lock_guard lock(g_mutex);
    for (const auto& player : g_players) {
        if (player.handle == handle && player.online) {
            return true;
        }
    }
    return false;
}

std::optional<PlayerInfo> players_find_by_handle(void* handle) {
    std::lock_guard lock(g_mutex);
    for (const auto& player : g_players) {
        if (player.handle == handle) {
            return player;
        }
    }
    return std::nullopt;
}

std::optional<PlayerInfo> players_mark_offline(void* handle) {
    std::lock_guard lock(g_mutex);
    for (auto& player : g_players) {
        if (player.handle == handle) {
            player.online = false;
            return player;
        }
    }
    return std::nullopt;
}

void players_mark_all_offline() {
    std::lock_guard lock(g_mutex);
    for (auto& player : g_players) {
        player.online = false;
    }
}

void players_clear() {
    std::lock_guard lock(g_mutex);
    g_players.clear();
}

} // namespace yoyo
