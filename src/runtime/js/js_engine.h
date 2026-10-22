#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yoyo {
struct PlayerView;
struct LiveVec3;
struct DetailEvent;
}

namespace yoyo::js {

bool start();
void stop();
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

} // namespace yoyo::js
