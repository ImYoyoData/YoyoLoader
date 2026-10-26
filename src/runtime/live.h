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

/* Player::sendForm(ModalFormRequestPacket).  `form_id`, when not null,
 * receives the id assigned to the packet so a response can be matched. */
bool live_send_form(void* player, std::string_view form_json, uint32_t* form_id = nullptr);

/*
 * Client-visible world effects.  These build the same packets BDS itself
 * sends; they change what clients see and are safe on every build, unlike
 * writing engine state whose layout we have not verified.
 */
bool live_spawn_particle(void* player, std::string_view name, const LiveVec3& position);
bool live_set_time(void* player, int ticks);
bool live_set_weather(void* player, int weather); /* 0 clear, 1 rain, 2 thunder */
/* UpdateBlockPacket: tell a client that a block changed. */
bool live_set_block(void* player, int x, int y, int z, uint32_t runtime_id, int flags);
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
/* Non-destructive lookup used by packet events. */
void* live_find_player_for_network(const void* network_id);
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

/*
 * Layout safety.
 *
 * Object offsets are compiler/ABI dependent, so a new build on the same
 * platform usually keeps them, but a build that reorders fields would make
 * every read and write silently wrong.  Once a real player exists, the bridge
 * validates the layout against that player: the component pointers must be
 * readable, positions must be finite, and the dimension vtable must resolve.
 *
 * Validation result is cached.  While a fallback layout is unvalidated, reads
 * are still attempted (they are bounds-checked) but writes are refused.
 */
enum class LayoutStatus {
    Unknown,  /* no player has been observed yet */
    Verified, /* offsets look correct for this build */
    Rejected, /* offsets are wrong; live object access is disabled */
};

void live_validate_layout(void* player);
LayoutStatus live_layout_status();
bool live_layout_allows_write();

/* Set when the running build has no exact version profile. */
void live_set_layout_fallback(bool fallback);

} // namespace yoyo
