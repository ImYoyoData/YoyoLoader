#pragma once

#include <cstdint>

namespace yoyo {

struct Prologue {
    uintptr_t rva = 0;
    uint8_t bytes[32] = {};
    uint8_t length = 0;
};

/*
 * Layout and entry points used by the live API bridge.  Slot values are
 * byte offsets into the vtable; every offset is verified against the exact
 * BDS build and its prologue before use.
 */
struct LiveProfile {
    Prologue create_packet;
    Prologue level_tick;
    Prologue player_left;
    uint32_t send_network_packet_slot = 0;
    uint32_t teleport_slot = 0;
    uint32_t set_game_mode_slot = 0;
    uint32_t dimension_id_slot = 0;
    uint32_t state_vector_offset = 0;
    uint32_t rotation_offset = 0;
    uint32_t level_offset = 0;
    uint32_t dimension_ref_offset = 0;
    uint32_t removed_offset = 0;
    uint32_t entt_pools_begin = 0;
    uint32_t entt_pools_end = 0;
    uint32_t entt_entries = 0;
    uint32_t text_body_offset = 0;
    uint32_t text_variant_index_offset = 0;
    uint32_t join_flag_offset = 0;
};

struct VersionProfile {
    const char* build_id = nullptr;
    Prologue try_to_load_player;
    Prologue disconnect_client;
    Prologue set_local_player_initialized;
    Prologue text_packet;
    LiveProfile live;
};

const VersionProfile* find_profile(const char* build_id);

} // namespace yoyo
