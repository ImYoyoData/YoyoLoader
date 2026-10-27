#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yoyo {

/*
 * Dynamic content module.
 *
 * This is the one place that talks to the official Script API bridge.  It is
 * deliberately separate from the native core (components/inventory/live): the
 * core reads and writes BDS objects through reverse-engineered layouts, while
 * everything here goes through the game's own scripting surface.
 *
 * Scope, as agreed:
 *   materials (block types/textures) - BEHAVIOUR PACK ONLY (see below)
 *   items                           - BEHAVIOUR PACK ONLY for new types;
 *                                     existing items can be given/spawned here
 *   mobs                            - spawning is supported here; new entity
 *                                     types need a behaviour pack
 *   recipes                         - BEHAVIOUR PACK ONLY
 *
 * The distinction matters: the Script API can manipulate instances of content
 * that already exists (spawn this mob, give this item, set this block), but it
 * cannot register a new item id, entity type, block type or recipe at runtime.
 * Those are loaded from behaviour packs before the world starts.  This module
 * therefore reports `false` with a precise reason for the unsupported cases
 * instead of silently doing nothing.
 *
 * Availability is probed at startup from the bridge status file, so a server
 * without the deployed pack degrades to "unavailable" rather than failing a
 * plugin call at an awkward moment.
 */

enum class DynamicContentStatus {
    Unknown,     /* not probed yet */
    Available,   /* bridge is deployed and reported ready */
    Unavailable, /* no bridge: see reason() */
};

struct DynamicContentResult {
    bool ok = false;
    /* Set when ok is false: why, in a form a plugin can show to its user. */
    std::string reason;
};

/* Probe the bridge.  Safe to call repeatedly; only the first call does work. */
DynamicContentStatus dynamic_content_probe();

/* Human-readable reason the module is unavailable, empty when available. */
std::string dynamic_content_reason();

/*
 * Spawn an entity (mob) at a position.  `type` is a namespaced id such as
 * "minecraft:zombie" or a custom one provided by a behaviour pack.
 */
DynamicContentResult dynamic_spawn_mob(const std::string& dimension, double x, double y, double z,
                                      const std::string& type);

/*
 * Drop an item stack into the world as an item entity.
 */
DynamicContentResult dynamic_spawn_item(const std::string& dimension, double x, double y, double z,
                                       const std::string& type, int count);

/*
 * Give an item to a player by name.  Returns false with a reason when the
 * player is offline or the item type is unknown to the server.
 */
DynamicContentResult dynamic_give_item(const std::string& player, const std::string& type, int count);

/*
 * Replace a block at a position.  `type` must be an existing block id.
 */
DynamicContentResult dynamic_set_block(const std::string& dimension, int x, int y, int z,
                                      const std::string& type);

/*
 * Run a command through the server's own dispatcher.
 *
 * This is the supported way to reach anything the Script API does not model
 * directly, including some recipe and material operations that the vanilla
 * commands expose.
 */
DynamicContentResult dynamic_run_command(const std::string& command);

/*
 * Capability report, for the startup log and for plugins that want to explain
 * the limitation to their users.  Each entry says whether the operation is
 * possible on this server and why.
 */
struct DynamicContentCapabilities {
    bool bridge_ready = false;
    bool can_spawn_mob = false;
    bool can_spawn_item = false;
    bool can_give_item = false;
    bool can_set_block = false;
    bool can_run_command = false;
    /* These need a behaviour pack; they are never true at runtime. */
    bool can_register_item_type = false;
    bool can_register_mob_type = false;
    bool can_register_material = false;
    bool can_register_recipe = false;
    std::string note;
};

DynamicContentCapabilities dynamic_content_capabilities();

/* Print the capability summary once, at startup. */
void dynamic_content_report();

} // namespace yoyo
