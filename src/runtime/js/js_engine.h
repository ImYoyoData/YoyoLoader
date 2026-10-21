#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yoyo {
struct PlayerView;
}

namespace yoyo::js {

bool start();
void stop();

void dispatch_pre_join(const PlayerView& player, bool& cancelled);
void dispatch_join(const PlayerView& player, uint64_t player_id, bool& cancelled);
void dispatch_chat(const PlayerView& player, std::string& message, bool& cancelled);
void dispatch_left(const PlayerView& player, std::string_view reason);

} // namespace yoyo::js
