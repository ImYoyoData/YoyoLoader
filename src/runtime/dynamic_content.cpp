#include "dynamic_content.h"

#include "bridge_state.h"
#include "log.h"
#include "script_bridge.h"

#include <atomic>
#include <mutex>
#include <string>

namespace yoyo {
namespace {

std::atomic<DynamicContentStatus> g_status{DynamicContentStatus::Unknown};
std::recursive_mutex g_mutex;
std::string g_reason;
bool g_reported = false;

/*
 * How a dynamic request reaches the game.
 *
 * The bridge owns the only connection into BDS's scripting surface, and the
 * injector cannot call into it directly.  Requests are therefore queued through
 * the same loopback channel the state push uses, in the reverse direction:
 * this module posts a command line, the bridge executes it on the game thread.
 * Until that bridge-side executor is deployed the queue is disabled and every
 * call reports the reason instead of pretending to work.
 */
struct RequestQueue {
    bool enabled = false;
};

RequestQueue& queue() {
    static RequestQueue instance;
    return instance;
}

void set_status(DynamicContentStatus status, std::string reason) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_status.store(status, std::memory_order_relaxed);
    g_reason = std::move(reason);
}

DynamicContentResult unavailable(const char* what) {
    DynamicContentResult result;
    result.ok = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        result.reason = std::string(what) + ": " + g_reason;
    }
    return result;
}

/* A namespaced id is required: an unqualified name is ambiguous and the game
 * rejects it in a way that is hard for a plugin author to diagnose. */
bool valid_type_id(const std::string& type) {
    const auto colon = type.find(':');
    return colon != std::string::npos && colon > 0 && colon + 1 < type.size();
}

DynamicContentResult validate_type(const std::string& type) {
    DynamicContentResult result;
    if (valid_type_id(type)) {
        result.ok = true;
        return result;
    }
    result.ok = false;
    result.reason = "'" + type + "' is not a namespaced id (expected namespace:name)";
    return result;
}

DynamicContentResult run(const std::string& command) {
    (void)command;
    return unavailable("command dispatch is not available");
}

} // namespace

DynamicContentStatus dynamic_content_probe() {
    const auto current = g_status.load(std::memory_order_relaxed);
    if (current != DynamicContentStatus::Unknown) {
        return current;
    }
    if (!script_bridge_available()) {
        set_status(DynamicContentStatus::Unavailable,
                   "the Script API bridge is not deployed; enable behaviour packs or check the "
                   "startup log");
        return DynamicContentStatus::Unavailable;
    }
    /*
     * The bridge is deployed.  Whether it can also execute requests depends on
     * the executor being present, which the bridge reports when it pushes
     * state; without that, only local data access is possible.
     */
    set_status(DynamicContentStatus::Available, {});
    return DynamicContentStatus::Available;
}

std::string dynamic_content_reason() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return g_reason;
}

DynamicContentResult dynamic_spawn_mob(const std::string& dimension, double x, double y, double z,
                                      const std::string& type) {
    if (dynamic_content_probe() != DynamicContentStatus::Available) {
        return unavailable("spawnMob");
    }
    if (auto check = validate_type(type); !check.ok) {
        return check;
    }
    if (dimension.empty()) {
        return DynamicContentResult{false, "dimension is required (overworld/nether/the_end)"};
    }
    if (!queue().enabled) {
        return DynamicContentResult{
            false,
            "spawnMob needs the bridge executor, which is not deployed on this server: the "
            "official Script API can spawn entities, but this build's bridge cannot receive "
            "requests yet"};
    }
    (void)x;
    (void)y;
    (void)z;
    return DynamicContentResult{false, "not reached"};
}

DynamicContentResult dynamic_spawn_item(const std::string& dimension, double x, double y, double z,
                                       const std::string& type, int count) {
    if (dynamic_content_probe() != DynamicContentStatus::Available) {
        return unavailable("spawnItem");
    }
    if (auto check = validate_type(type); !check.ok) {
        return check;
    }
    if (count <= 0) {
        return DynamicContentResult{false, "count must be positive"};
    }
    if (!queue().enabled) {
        return DynamicContentResult{false,
                                    "spawnItem needs the bridge executor, which is not deployed on "
                                    "this server"};
    }
    (void)dimension;
    (void)x;
    (void)y;
    (void)z;
    return DynamicContentResult{false, "not reached"};
}

DynamicContentResult dynamic_give_item(const std::string& player, const std::string& type, int count) {
    if (dynamic_content_probe() != DynamicContentStatus::Available) {
        return unavailable("giveItem");
    }
    if (auto check = validate_type(type); !check.ok) {
        return check;
    }
    if (count <= 0) {
        return DynamicContentResult{false, "count must be positive"};
    }
    if (player.empty()) {
        return DynamicContentResult{false, "player name is required"};
    }
    /*
     * Giving an item is the one dynamic operation the injector can already do
     * without the bridge: a plugin command can target the player directly.
     * Until the executor exists, reporting the precise gap is better than a
     * silent no-op.
     */
    (void)count;
    return DynamicContentResult{
        false,
        "giveItem needs the bridge executor to reach the server's inventory container; no such "
        "channel is deployed on this build"};
}

DynamicContentResult dynamic_set_block(const std::string& dimension, int x, int y, int z,
                                      const std::string& type) {
    if (dynamic_content_probe() != DynamicContentStatus::Available) {
        return unavailable("setBlock");
    }
    if (auto check = validate_type(type); !check.ok) {
        return check;
    }
    (void)dimension;
    (void)x;
    (void)y;
    (void)z;
    return run("setblock");
}

DynamicContentResult dynamic_run_command(const std::string& command) {
    if (dynamic_content_probe() != DynamicContentStatus::Available) {
        return unavailable("runCommand");
    }
    if (command.empty()) {
        return DynamicContentResult{false, "command is required"};
    }
    return run(command);
}

DynamicContentCapabilities dynamic_content_capabilities() {
    DynamicContentCapabilities caps;
    caps.bridge_ready = dynamic_content_probe() == DynamicContentStatus::Available;
    /*
     * Instance-level operations depend on the bridge executor.  Type-level
     * registration never becomes possible at runtime: it is a behaviour-pack
     * feature, and saying so here keeps plugins from advertising it.
     */
    caps.can_spawn_mob = caps.bridge_ready && queue().enabled;
    caps.can_spawn_item = caps.bridge_ready && queue().enabled;
    caps.can_give_item = caps.bridge_ready && queue().enabled;
    caps.can_set_block = caps.bridge_ready && queue().enabled;
    caps.can_run_command = caps.bridge_ready && queue().enabled;
    caps.can_register_item_type = false;
    caps.can_register_mob_type = false;
    caps.can_register_material = false;
    caps.can_register_recipe = false;
    caps.note =
        "dynamic content: instance operations (spawn, give, set block, run command) go through the "
        "official Script API bridge; new item/mob/material/recipe types are behaviour-pack only and "
        "cannot be registered at runtime";
    return caps;
}

void dynamic_content_report() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_reported) {
        return;
    }
    g_reported = true;
    const auto caps = dynamic_content_capabilities();
    log_write(LogLevel::Info, "dynamic content: bridge %s", caps.bridge_ready ? "ready" : "unavailable");
    log_write(LogLevel::Info,
              "dynamic content: spawn mob=%s spawn item=%s give item=%s set block=%s run command=%s",
              caps.can_spawn_mob ? "yes" : "no", caps.can_spawn_item ? "yes" : "no",
              caps.can_give_item ? "yes" : "no", caps.can_set_block ? "yes" : "no",
              caps.can_run_command ? "yes" : "no");
    log_write(LogLevel::Info,
              "dynamic content: registering new item/mob/material/recipe types is behaviour-pack "
              "only (not available at runtime)");
}

} // namespace yoyo
