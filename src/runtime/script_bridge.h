#pragma once

#include <string>
#include <vector>

namespace yoyo {

/*
 * Script bridge.
 *
 * BDS ships the official Script API (@minecraft/server) and loads it from a
 * behaviour pack with a `script` module.  That API already exposes the objects
 * an injector cannot safely synthesise: containers, item stacks, health, blocks,
 * entities, scoreboard and runCommand.
 *
 * Rather than reverse engineering those C++ types, YoyoLoader deploys a small
 * behaviour pack at startup.  The pack's entry script imports @minecraft/server
 * and re-exports a bridge object; YoyoLoader plugins then use the official
 * objects through it.
 *
 * The pack is generated on first run and registered in the world's
 * world_behavior_packs.json, so no manual setup is needed.
 */
struct ScriptBridgeStatus {
    bool available = false;
    std::string pack_directory;
    std::string reason;
};

/*
 * Deploy the bridge behaviour pack and register it for the active world.
 * Safe to call more than once; it is idempotent.
 */
ScriptBridgeStatus script_bridge_install(const std::string& server_root);

/* True when the bridge pack is present and registered. */
bool script_bridge_available();

} // namespace yoyo
