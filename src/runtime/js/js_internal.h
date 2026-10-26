#pragma once

extern "C" {
#include "quickjs.h"
}

#include <cstdint>
#include <string>

namespace yoyo {
struct PlayerView;
}

namespace yoyo::js {

extern JSContext* g_context;

void log_exception(JSContext* ctx);
JSValue unsupported(JSContext* ctx, const char* message);
JSValue make_player(JSContext* ctx, const PlayerView& player);

void register_nbt(JSContext* ctx);
void register_forms(JSContext* ctx);
void register_timers(JSContext* ctx);
void register_script_helpers(JSContext* ctx);
void register_commands(JSContext* ctx);
void clear_commands();
bool has_player_command(const std::string& message);
bool run_player_command(const PlayerView& player, const std::string& message, std::string& output);
void clear_timers();
void close_files();
void run_timers();

/* Pending form callbacks, keyed by the id sent in ModalFormRequest. */
void remember_form_callback(uint32_t form_id, JSValue callback);
void dispatch_form_response(uint32_t form_id, const PlayerView& player, const std::string& response,
                            int cancel_reason);
void clear_form_callbacks();

} // namespace yoyo::js
