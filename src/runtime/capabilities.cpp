#include "capabilities.h"

#include "log.h"
#include "runtime.h"

#include <cstdio>
#include <string>

namespace yoyo {
namespace {

struct CapabilityEntry {
    const char* name;
    bool VersionCapabilities::*flag;
};

/*
 * Order matters: this is the order shown in the startup report, grouped by
 * what a plugin author would look for.
 */
const CapabilityEntry kEntries[] = {
    {"pre_join event", &VersionCapabilities::hook_pre_join},
    {"join event", &VersionCapabilities::hook_join},
    {"chat event", &VersionCapabilities::hook_chat},
    {"leave event", &VersionCapabilities::hook_leave},
    {"tick event", &VersionCapabilities::hook_level_tick},
    {"packet receive events", &VersionCapabilities::hook_packet_factory},
    {"packet send events", &VersionCapabilities::hook_packet_send},
    {"player position/dimension", &VersionCapabilities::player_position},
    {"player rotation", &VersionCapabilities::player_rotation},
    {"player id (runtime/unique)", &VersionCapabilities::player_identity},
    {"player game mode read", &VersionCapabilities::player_game_mode},
    {"player teleport", &VersionCapabilities::player_teleport},
    {"player game mode write", &VersionCapabilities::player_set_game_mode},
    {"player health/death", &VersionCapabilities::player_health},
    {"chat packet text fields", &VersionCapabilities::packet_text_fields},
    {"form response fields", &VersionCapabilities::form_response_fields},
    {"player action fields", &VersionCapabilities::player_action_fields},
    {"player input fields", &VersionCapabilities::player_auth_input_fields},
};

std::string disabled_list(const VersionCapabilities& caps) {
    std::string text;
    for (const auto& entry : kEntries) {
        if (caps.*(entry.flag)) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        text += entry.name;
    }
    return text;
}

} // namespace

const VersionCapabilities& capabilities() {
    static const VersionCapabilities fallback = conservative_capabilities();
    const auto& state = yoyo::state();
    if (state.profile != nullptr) {
        return state.profile->capabilities;
    }
    return fallback;
}

bool capability_enabled(bool VersionCapabilities::*flag) {
    return capabilities().*(flag);
}

void log_capability_report() {
    const auto& runtime = state();
    const VersionCapabilities& caps = capabilities();

    const char* version = runtime.profile != nullptr ? runtime.profile->version : "unknown";
    const char* platform = runtime.profile != nullptr ? runtime.profile->platform : "unknown";

    log_write(LogLevel::Info,
              "version support: %s (%s) build=%s%s", version, platform, runtime.module.build_id,
              runtime.profile != nullptr ? "" : " [NO EXACT PROFILE]");

    const std::string disabled = disabled_list(caps);
    if (disabled.empty()) {
        log_write(LogLevel::Info, "all features enabled for this build");
    } else {
        log_write(LogLevel::Warn, "limited features on this build: %s", disabled.c_str());
        if (caps.note != nullptr) {
            log_write(LogLevel::Warn, "reason: %s", caps.note);
        }
    }

    /* Also print a short line to stdout so the console shows it even when the
     * log file is the only thing being tailed. */
    std::printf("[yoyo] BDS %s (%s) build %s\n", version, platform, runtime.module.build_id);
    if (!disabled.empty()) {
        std::printf("[yoyo] not available on this build: %s\n", disabled.c_str());
        if (caps.note != nullptr) {
            std::printf("[yoyo] note: %s\n", caps.note);
        }
    }
    std::fflush(stdout);
}

std::string capability_summary() {
    const VersionCapabilities& caps = capabilities();
    const std::string disabled = disabled_list(caps);
    return disabled.empty() ? std::string("full") : disabled;
}

} // namespace yoyo
