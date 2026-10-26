#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yoyo {

/* Installs the packet-factory hook that feeds the ABI 3 detail events. */
bool install_packet_hooks();

/* Installs the per-player outgoing-packet hook on first join. */
bool install_packet_send_hook(void* player);

/* Used by live.cpp after the shared sendNetworkPacket vtable slot is patched:
 * calls from the live bridge must bypass the observer and reach BDS directly. */
void* packet_send_hook_address();
void* packet_send_original_address();

/* Last state observed in PlayerAuthInput / PlayerAction / SetHealth packets. */
bool packet_player_is_sneaking(void* player);
bool packet_player_is_sprinting(void* player);
bool packet_player_is_flying(void* player);
int packet_player_held_slot(void* player);
int packet_player_health(void* player);
void packet_forget_player(void* player);
void packet_clear();

/* World time / weather observed from outgoing packets.  Returns -1 when the
 * value has never been seen on this connection. */
int packet_world_time();
int packet_world_weather();

/*
 * Register a packet's vtable so outgoing packets built by the live bridge can
 * be identified by id.  The live bridge creates packets through the same
 * factory, but its calls go to the original function directly, so the id must
 * be recorded here.
 */
void packet_remember_vtable(void* packet, int packet_id);

/* Feed an outgoing packet to the observer without re-entering the hook. */
void packet_observe_outgoing(void* player, void* packet);

/* A block the server has reported to this client.  Returns false when the
 * position has not been observed yet. */
bool packet_block_at(int x, int y, int z, uint32_t& runtime_id, int& flags);

} // namespace yoyo
