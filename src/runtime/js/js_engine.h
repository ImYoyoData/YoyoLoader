#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../plugin_manifest.h"

namespace yoyo {
struct PlayerView;
struct LiveVec3;
struct DetailEvent;
}

namespace yoyo::js {

bool start();
void stop();

/*
 * Load already-ordered JS plugins, each in its own QuickJS context.
 * Returns false when any plugin threw at load time (the rest still load).
 */
bool load_plugins(const std::vector<plugins::Manifest>& manifests);
bool has_listener(std::string_view event);

void dispatch_pre_join(const PlayerView& player, bool& cancelled);
void dispatch_join(const PlayerView& player, uint64_t player_id, bool& cancelled);
void dispatch_chat(const PlayerView& player, std::string& message, bool& cancelled);
void dispatch_left(const PlayerView& player, std::string_view reason);
void dispatch_server_ready(std::string_view version, uintptr_t module_base);
void dispatch_tick(uint64_t tick);
void dispatch_player_move(const PlayerView& player, const LiveVec3& from, const LiveVec3& to,
                          int from_dimension, int to_dimension);
void dispatch_player_dimension_change(const PlayerView& player, int from_dimension, int to_dimension);
void dispatch_detail(int type, const PlayerView& player, const DetailEvent& detail, bool& cancelled);

/* Pending form callbacks, keyed by the id sent in ModalFormRequest. */
void dispatch_form_response(uint32_t form_id, const PlayerView& player, const std::string& response,
                            int cancel_reason);

/* Chat commands registered from JS (mc.regPlayerCmd). */
bool has_player_command(const std::string& message);
bool run_player_command(const PlayerView& player, const std::string& message, std::string& output);

/* Snapshot of the registered plugin commands, for the client command table. */
struct RegisteredCommand {
    std::string name;
    std::string description;
    int permission = 0;
};
std::vector<RegisteredCommand> js_registered_commands();

} // namespace yoyo::js
