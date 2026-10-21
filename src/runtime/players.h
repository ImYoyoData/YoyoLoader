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
void players_clear();

} // namespace yoyo
