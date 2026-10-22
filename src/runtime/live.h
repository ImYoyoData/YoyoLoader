#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yoyo {

/*
 * Live-object bridge.
 *
 * YoyoLoader calls BDS objects without linking against the game: packet
 * factories and virtual methods are entry points from the version profile,
 * while component access uses the EnTT registry layout that BDS exposes on
 * every Actor.
 */

struct LiveVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class LiveTextType : int {
    Raw = 0,
    Chat = 1,
    Translate = 2,
    Popup = 3,
    Tip = 5,
    SystemMessage = 6,
};

void live_set_server_handler(void* handler);
void* live_server_handler();

/* Player::sendNetworkPacket(TextPacket). */
bool live_send_text(void* player, std::string_view message, LiveTextType type);

/* Player::sendTitle / sendTip / sendPopup / sendToast equivalents. */
bool live_send_title(void* player, std::string_view title, std::string_view subtitle, int fade_in, int stay,
                     int fade_out);
bool live_send_actionbar(void* player, std::string_view text);
bool live_reset_title(void* player);
bool live_send_toast(void* player, std::string_view title, std::string_view content);

/* Player::sendForm(ModalFormRequestPacket). */
bool live_send_form(void* player, std::string_view form_json);
bool live_transfer(void* player, std::string_view host, int port);

/* Actor geometry.  getPosition returns the eye position BDS stores in
 * StateVectorComponent; getFeetPosition subtracts the player eye height. */
bool live_get_position(void* actor, LiveVec3& out);
bool live_get_feet_position(void* actor, LiveVec3& out);
bool live_get_rotation(void* actor, float& pitch, float& yaw);
int live_get_dimension(void* actor);
const char* live_dimension_name(int dimension);

/* Actor::teleportTo; cross-dimension requests are rejected for now. */
bool live_teleport(void* actor, const LiveVec3& position, int dimension);

/* Remember the NetworkIdentifier delivered by the join packet handler. */
void live_set_player_network_id(void* player, const void* network_id, uint8_t sub_client_id);
void* live_take_player_for_network(const void* network_id);
void live_forget_player(void* player);

/* Kick through ServerNetworkHandler::disconnectClientWithMessage. */
bool live_kick(void* player, std::string_view reason);

/* Identity helpers backed by EnTT components. */
int live_get_game_mode(void* actor);
bool live_set_game_mode(void* actor, int mode);
const char* live_game_mode_name(int mode);

uint64_t live_runtime_id(void* actor);
uint64_t live_unique_id(void* actor);
bool live_get_network_id(void* player, std::vector<uint8_t>& network_id, uint8_t& sub_client_id);

bool live_actor_valid(void* actor);

} // namespace yoyo
