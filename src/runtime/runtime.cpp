#include "runtime.h"

#include "hooks.h"
#include "log.h"
#include "plugin_loader.h"
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
    if (g_state.profile == nullptr) {
        log_write(LogLevel::Error, "unsupported bedrock_server build; no hooks installed");
        return;
    }

    if (!install_player_hooks()) {
        log_write(LogLevel::Error, "player hooks could not be installed");
        return;
    }

    worlds::initialize();
    start_plugin_loader();
}

} // namespace yoyo
