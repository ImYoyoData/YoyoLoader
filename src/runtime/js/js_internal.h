#pragma once

extern "C" {
#include "quickjs.h"
}

#include <cstdint>

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

} // namespace yoyo::js
