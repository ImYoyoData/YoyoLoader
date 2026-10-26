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
    uint32_t create_packet_hook_length = 14;
    uint32_t create_packet_jcc_offset = 0;
    uint32_t create_packet_jcc_target_offset = 0;
    Prologue level_tick;
    uint32_t level_tick_rip_offset = 0;
    uint32_t level_tick_rip_instruction_length = 0;
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

/*
 * Layout used when the running build has no exact profile.  Object and packet
 * offsets are compiler/ABI dependent rather than version dependent, so the
 * closest known layout usually still reads correctly; writes that would
 * corrupt state are disabled by the live bridge when the layout is a fallback.
 */
const LiveProfile* fallback_layout();

} // namespace yoyo
