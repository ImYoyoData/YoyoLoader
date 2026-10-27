#include "runtime.h"

#include "capabilities.h"
#include "hooks.h"
#include "live.h"
#include "log.h"
#include "packet_events.h"
#include "plugin_loader.h"
#include "script_bridge.h"
#include "worlds.h"

namespace yoyo {
namespace {
RuntimeState g_state;
}

RuntimeState& state() {
    return g_state;
}

void runtime_start() {
    log_open();
    log_write(LogLevel::Info, "YoyoLoader runtime starting");

    if (!init_module_info(g_state.module)) {
        log_write(LogLevel::Error, "failed to read the bedrock_server module information");
        return;
    }
    log_write(LogLevel::Info, "module: %s", g_state.module.path);
    log_write(LogLevel::Info, "module base: 0x%llx", static_cast<unsigned long long>(g_state.module.base));
    log_write(LogLevel::Info, "build id: %s", g_state.module.build_id);

    g_state.profile = find_profile(g_state.module.build_id);
    if (g_state.profile != nullptr) {
        g_state.layout = &g_state.profile->live;
        g_state.layout_fallback = false;
        log_write(LogLevel::Info, "build matched an exact version profile");
    } else {
        /*
         * Unknown build: the hook targets still come from signatures, and the
         * closest layout is used for object offsets.  Writes that depend on an
         * unverified layout are skipped by the live bridge, so the runtime
         * degrades to read-only observation instead of guessing.
         */
        g_state.layout = fallback_layout();
        g_state.layout_fallback = g_state.layout != nullptr;
        live_set_layout_fallback(true);
        log_write(LogLevel::Warn,
                  "unsupported build id; hooks will be found by signature and object layout is a fallback");
    }

    log_capability_report();

    g_state.symbols = resolve_symbols(g_state.module.base, g_state.profile);
    log_write(LogLevel::Info, "symbol resolution: %s", describe_resolution(g_state.symbols).c_str());
    if (!g_state.symbols.any()) {
        log_write(LogLevel::Error, "no hook targets could be resolved; refusing to patch");
        return;
    }

    if (!install_player_hooks()) {
        log_write(LogLevel::Error, "player hooks could not be installed");
        return;
    }
    if (!install_packet_hooks()) {
        log_write(LogLevel::Warn, "packet detail events are unavailable for this build");
    }

    /*
     * Deploy the official Script API bridge before the world loads: BDS reads
     * behaviour packs during startup, so the pack must exist by then.
     */
    const auto bridge = script_bridge_install(g_state.module.base != 0 ? "." : ".");
    if (!bridge.available) {
        log_write(LogLevel::Warn, "script bridge unavailable: %s", bridge.reason.c_str());
    }

    worlds::initialize();
    start_plugin_loader();
}

} // namespace yoyo
