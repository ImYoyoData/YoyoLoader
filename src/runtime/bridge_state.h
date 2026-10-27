#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yoyo {

/*
 * State pushed by the official Script API bridge.
 *
 * The bridge runs inside BDS's own JavaScript engine; YoyoLoader plugins run in
 * QuickJS.  The two cannot share objects, and the server-side inventory is not
 * reachable through any verified native layout, so the bridge posts a snapshot
 * over HTTP and the injector caches it here.  Everything is optional: when the
 * bridge is unavailable the cache stays empty and callers fall back.
 */
struct BridgeItem {
    int32_t slot = 0;
    std::string type_id;
    int32_t amount = 0;
};

struct BridgeContainer {
    int32_t size = 0;
    std::vector<BridgeItem> slots; /* sparse: only occupied slots are listed */
    bool valid = false;

    [[nodiscard]] std::optional<BridgeItem> at(int32_t slot) const;
};

struct BridgePlayer {
    std::string name;
    std::string id;
    std::string game_mode;
    std::string dimension;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double health = 0.0;
    bool has_position = false;
    BridgeContainer inventory;
    BridgeContainer ender_chest;
    BridgeContainer equipment;
    int64_t tick = 0;
};

/* Start listening on 127.0.0.1:<port>.  Safe to call once at startup. */
bool bridge_state_start(int port);

void bridge_state_stop();

/* Latest snapshot for a player, matched by name. */
std::optional<BridgePlayer> bridge_state_player(const std::string& name);

bool bridge_state_available();

/* Milliseconds since the last successful push, or -1 when none arrived. */
int64_t bridge_state_age_ms();

} // namespace yoyo
