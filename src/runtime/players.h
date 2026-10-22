#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yoyo {

struct PlayerInfo {
    std::string name;
    std::string xuid;
    std::string address;
    uint64_t runtime_id = 0;
    void* handle = nullptr;
    bool online = false;
};

void players_upsert(const PlayerInfo& info);
std::optional<PlayerInfo> players_find(const std::string& name_or_xuid);
std::vector<PlayerInfo> players_online();
std::optional<PlayerInfo> players_mark_joined(uint64_t runtime_id);
bool players_is_online(void* handle);
std::optional<PlayerInfo> players_find_by_handle(void* handle);
std::optional<PlayerInfo> players_mark_offline(void* handle);
void players_mark_all_offline();
void players_clear();

} // namespace yoyo
