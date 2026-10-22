#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <yoyo/plugin.h>

#include "live.h"

namespace yoyo {

struct PlayerView {
    void* handle = nullptr;
    uint64_t runtime_id = 0;
    std::string name;
    std::string xuid;
    std::string address;
};

bool register_event(int type, yoyo_event_callback callback, void* user);
bool unregister_event(int type, yoyo_event_callback callback, void* user);
bool event_has_listener(int type);

void emit_pre_join(PlayerView& player, bool& cancelled);
void emit_join(PlayerView& player, uint64_t player_id, bool& cancelled);
void emit_chat(PlayerView& player, std::string& message, bool& cancelled);
void emit_left(PlayerView& player, std::string_view reason);
void emit_server_ready(const char* server_version, uintptr_t module_base);
void emit_tick(uint64_t tick);
void emit_player_move(PlayerView& player, const LiveVec3& from, const LiveVec3& to, int from_dimension,
                      int to_dimension);
void emit_player_dimension_change(PlayerView& player, int from_dimension, int to_dimension);

/*
 * Detail event shared by the packet-driven events in ABI 3.  `text` and
 * `text2` are borrowed for the duration of the call.
 */
struct DetailEvent {
    int packet_id = 0;
    int action = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int face = -1;
    int slot = -1;
    uint64_t runtime_id = 0;
    int value = 0;
    std::string text;
    std::string text2;
};

bool emit_detail(int type, PlayerView& player, DetailEvent& detail);

} // namespace yoyo
