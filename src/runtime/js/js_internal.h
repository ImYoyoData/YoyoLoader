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

/* LLSE-compatible value helpers (positions, dimensions). */
const char* llse_dimension_name(int dimension);
std::string llse_dimension_name_utf8(int dimension);
bool parse_llse_dimension(JSContext* ctx, JSValueConst value, int& out);
JSValue make_int_pos(JSContext* ctx, int x, int y, int z, int dimid);
JSValue make_float_pos(JSContext* ctx, double x, double y, double z, int dimid);
JSValue make_direction(JSContext* ctx, double pitch, double yaw);
bool read_position_args(JSContext* ctx, int argc, JSValueConst* argv, double& x, double& y, double& z,
                        int& dimid, int dimid_default);
void register_llse_objects(JSContext* ctx);

/*
 * A Block-shaped object for event callbacks: LLSE passes Block (with pos,
 * name, type, isAir) where YoyoLoader used to pass a bare position.
 */
JSValue make_block_object(JSContext* ctx, int x, int y, int z, int dimid);

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
