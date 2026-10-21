#pragma once

#include <cstdint>

namespace yoyo {

struct Prologue {
    uintptr_t rva = 0;
    uint8_t bytes[32] = {};
    uint8_t length = 0;
};

struct VersionProfile {
    const char* build_id = nullptr;
    Prologue try_to_load_player;
    Prologue disconnect_client;
    Prologue set_local_player_initialized;
    Prologue text_packet;
};

const VersionProfile* find_profile(const char* build_id);

} // namespace yoyo
