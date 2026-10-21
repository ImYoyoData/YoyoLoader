#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <yoyo/plugin.h>

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

void emit_pre_join(PlayerView& player, bool& cancelled);
void emit_join(PlayerView& player, uint64_t player_id, bool& cancelled);
void emit_chat(PlayerView& player, std::string& message, bool& cancelled);
void emit_left(PlayerView& player, std::string_view reason);
void emit_server_ready(const char* server_version, uintptr_t module_base);

} // namespace yoyo
