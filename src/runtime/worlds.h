#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yoyo::worlds {

struct WorldInfo {
    std::string name;
    int port = 0;
    std::string level;
    bool loaded = false;
    bool current = false;
    bool controller = false;
};

/* Reads yoyo_worlds.ini / yoyo_ops.txt from the server directory. */
void initialize();
std::vector<WorldInfo> list();
std::string current_world();
bool is_controller();
bool load(const std::string& name);
bool unload(const std::string& name);
bool switch_player(void* player, const std::string& name);
bool is_op(const std::string& xuid);

} // namespace yoyo::worlds
