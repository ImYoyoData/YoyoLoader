#pragma once

#include <cstdint>

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

} // namespace yoyo
