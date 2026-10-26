#include "version.h"

#include <cstring>

namespace yoyo {
namespace {

/*
 * Offsets are RVAs (module base + RVA) for the exact 1.26.52.3 binaries.
 * Each prologue is checked byte-for-byte before patching; an unknown build
 * or a changed prologue means no hook, never a guess.
 *
 * The two packet handlers are called inline by the server (the compiler
 * devirtualized the dispatch), so we hook the functions themselves rather
 * than their vtable slots.
 */

const VersionCapabilities kFullCapabilities = {
    true,  /* hook_pre_join */
    true,  /* hook_leave */
    true,  /* hook_join */
    true,  /* hook_chat */
    true,  /* hook_level_tick */
    true,  /* hook_packet_factory */
    true,  /* hook_packet_send */
    true,  /* player_position */
    true,  /* player_rotation */
    true,  /* player_identity */
    true,  /* player_game_mode */
    true,  /* player_teleport */
    true,  /* player_set_game_mode */
    true,  /* player_health */
    true,  /* packet_text_fields */
    true,  /* form_response_fields */
    true,  /* player_action_fields */
    true,  /* player_auth_input_fields */
    nullptr,
};

const VersionProfile kLinux = {
    "1.26.52.3",
    "c0ff36e2eacfc191f01e189237df71f139b5d04a",
    "linux",
    /* try_to_load_player */
    {0x892CD70,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x58},
     14},
    /* disconnect_client */
    {0x8904FE0,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0xF8, 0x01, 0x00,
      0x00},
     17},
    /* set_local_player_initialized */
    {0x8911320,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x58, 0x03, 0x00,
      0x00, 0x48, 0x89, 0xFB, 0x0F, 0xB6, 0x52, 0x10},
     24},
    /* text_packet: ends after "cmp byte [rdx+0x80], 1", before the rel8 jump */
    {0x8905AD0,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0xD8, 0x05, 0x00,
      0x00, 0x49, 0x89, 0xFD, 0x80, 0xBA, 0x80, 0x00, 0x00, 0x00, 0x01},
     27},
    /* live */
    {
        {0x89FB260,
         {0x53, 0x48, 0x89, 0xFB, 0x81, 0xFE, 0x61, 0x01, 0x00, 0x00, 0x77, 0x12, 0x89, 0xF0, 0x48, 0x8D},
         16},
        12, /* shortest safe prologue: through mov eax, esi */
        10, /* short jbe at +10 */
        30, /* its destination: createPacket + 30 */
        {0xC897700,
         {0x55, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x58, 0x49, 0x89, 0xFE, 0x0F, 0xB6, 0x05,
          0xB4, 0x4B, 0xD5, 0x02},
         20},
        16, /* RIP-relative displacement */
        7,  /* movzbl rip+disp32, eax */
        {0x89143F0,
         {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x98, 0x03, 0x00,
          0x00, 0x89, 0xD5, 0x48, 0x89, 0xF3},
         22},
        0x728, /* Player::sendNetworkPacket */
        0xB0,  /* Actor::teleportTo */
        0x698, /* Player::setPlayerGameType */
        0x18,  /* Dimension::getDimensionId */
        0x208, /* Actor::built_in_components_.state_vector_component */
        0x218, /* Actor::built_in_components_.actor_rotation_component */
        0x1D0, /* Actor::level_ */
        0x1C0, /* Actor::dimension_ (WeakRef::ptr) */
        0x251, /* Actor::removed_ */
        0x38,  /* EntityRegistry::pools begin */
        0x40,  /* EntityRegistry::pools end */
        0x50,  /* EntityRegistry::pools entries */
        88,    /* TextPacketPayload::body */
        56,    /* std::variant index inside body */
        0xDCB, /* ServerPlayer::local_player_initialized_ */
    },
    kFullCapabilities,
};

const VersionProfile kWindows = {
    "1.26.52.3",
    "d9f9837c2108f3824c4c44205044422e1",
    "windows",
    /* try_to_load_player */
    {0x9A0690,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0xC8,
      0x00, 0x00, 0x00},
     19},
    /* disconnect_client */
    {0x972F40,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x28,
      0x02, 0x00, 0x00},
     19},
    /* set_local_player_initialized: vtable slot 90; sets player+0xdb7 = 1 */
    {0x980970,
     {0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x28, 0x01, 0x00, 0x00, 0x0F, 0x29, 0xBC, 0x24,
      0x10, 0x01, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0x00, 0x01, 0x00, 0x00},
     28},
    /* text_packet: vtable slot 242; parses TextPacket and calls the chat helper */
    {0x973B30,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x18,
      0x05, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00},
     27},
    /* live */
    {
        {0x93A490,
         {0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0x81, 0xFA, 0x61, 0x01, 0x00, 0x00, 0x77, 0x12},
         16},
        14, /* through cmp edx, 0x161; the jbe starts after the patch */
        0,
        0,
        {0x7CBFD0,
         {0x55, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80,
          0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x30, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0xCF},
         30},
        0,
        0,
        {0x983800,
         {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x18,
          0x02, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00},
         27},
        0x720, /* Player::sendNetworkPacket */
        0xA8,  /* Actor::teleportTo */
        0x690, /* Player::setPlayerGameType */
        0x10,  /* Dimension::getDimensionId */
        0x218, /* Actor::built_in_components_.state_vector_component */
        0x228, /* Actor::built_in_components_.actor_rotation_component */
        0x1D8, /* Actor::level_ */
        0x1C8, /* Actor::dimension_ (WeakRef::ptr) */
        0x251, /* Actor::removed_ (same as Linux for this build) */
        0x48,  /* EntityRegistry::pools begin */
        0x50,  /* EntityRegistry::pools end */
        0x68,  /* EntityRegistry::pools entries */
        112,   /* TextPacketPayload::body */
        72,    /* std::variant index inside body */
        0xDB7, /* ServerPlayer::local_player_initialized_ */
    },
    kFullCapabilities,
};


/*
 * 1.26.70.21 (preview).
 *
 * Only targets that were actually located and verified are recorded here.
 * Everything else stays zero, which makes the resolver fall through to
 * structural probes and then to "unavailable"; the capability matrix below
 * mirrors that, so no code path tries to use an offset that was never checked
 * on this build.
 *
 * Verified on this build:
 *   TextPacket handler           0x1270450  (semantic probe, see signatures.cpp)
 *   SetLocalPlayerAsInitialized  0x127e270  (this-0x10 + initialized flag write)
 *   _onPlayerLeft                0x1282390  (unique assertion string xref)
 *
 * Not yet located: tryToLoadPlayer, disconnectClientWithMessage, Level::tick,
 * MinecraftPackets::createPacket.  These are resolved by probe/signature at
 * runtime when possible; otherwise the matching feature is disabled.
 */
const VersionCapabilities kPreviewCapabilities = {
    false, /* hook_pre_join: target not yet located on this build */
    true,  /* hook_leave */
    true,  /* hook_join */
    true,  /* hook_chat */
    false, /* hook_level_tick: target not yet located */
    false, /* hook_packet_factory: target not yet located */
    false, /* hook_packet_send: depends on the packet factory */
    false, /* player_position: Actor layout unverified for this build */
    false, /* player_rotation */
    false, /* player_identity */
    false, /* player_game_mode */
    false, /* player_teleport */
    false, /* player_set_game_mode */
    false, /* player_health: needs the outgoing packet hook */
    false, /* packet_text_fields: TextPacket payload offsets unverified */
    false, /* form_response_fields */
    false, /* player_action_fields */
    false, /* player_auth_input_fields */
    "1.26.70.21 preview: join/leave/chat hook targets located; object layout and "
    "packet payload offsets are not verified on this build yet",
};

const VersionProfile kWindows1270 = {
    "1.26.70.21",
    "fd266068f00d540b4c4c44205044422e1",
    "windows",
    /* try_to_load_player: unresolved */
    {0, {}, 0},
    /* disconnect_client: unresolved */
    {0, {}, 0},
    /* set_local_player_initialized */
    {0x127e270,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x98,
      0x03, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00},
     27},
    /* text_packet */
    {0x1270450,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x28,
      0x05, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00},
     27},
    /* live: layout inherited from 1.26.52.3, gated off by capabilities */
    {
        {0, {}, 0},
        14,
        0,
        0,
        {0, {}, 0},
        0,
        0,
        {0x1282390,
         {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x28,
          0x04, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00},
         27},
        0x720,
        0xA8,
        0x690,
        0x10,
        0x218,
        0x228,
        0x1D8,
        0x1C8,
        0x251,
        0x48,
        0x50,
        0x68,
        112,
        72,
        0xDB7,
    },
    kPreviewCapabilities,
};


/*
 * 1.26.70.21 (preview), Linux.
 *
 * Both targets below were recovered on the real binary by signature scan (the
 * log showed "recovered by signature"), so they are recorded as exact RVAs
 * with the prologue bytes observed at those addresses.  Everything else stays
 * unresolved; the capability matrix keeps the dependent features off.
 *
 * Verified by actually running the server in a container with the runtime
 * preloaded: join and createPacket hooks installed.
 */
const VersionCapabilities kLinuxPreviewCapabilities = {
    false, /* hook_pre_join */
    false, /* hook_leave */
    true,  /* hook_join */
    false, /* hook_chat */
    false, /* hook_level_tick */
    true,  /* hook_packet_factory */
    false, /* hook_packet_send */
    false, /* player_position */
    false, /* player_rotation */
    false, /* player_identity */
    false, /* player_game_mode */
    false, /* player_teleport */
    false, /* player_set_game_mode */
    false, /* player_health */
    false, /* packet_text_fields */
    false, /* form_response_fields */
    false, /* player_action_fields */
    false, /* player_auth_input_fields */
    "1.26.70.21 preview (Linux): join and packet-factory targets located by signature; "
    "chat/leave/tick and object layout are not verified on this build yet",
};

const VersionProfile kLinux1270 = {
    "1.26.70.21",
    "519f9a2c3cdc31c85ef87dd2f128f79136f854d4",
    "linux",
    /* try_to_load_player: unresolved */
    {0, {}, 0},
    /* disconnect_client: unresolved */
    {0, {}, 0},
    /* set_local_player_initialized */
    {0x9199160,
     {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x58, 0x03, 0x00,
      0x00, 0x48, 0x89, 0xFB, 0x0F, 0xB6, 0x52, 0x10},
     24},
    /* text_packet: unresolved */
    {0, {}, 0},
    /* live */
    {
        {0x9162360,
         {0x53, 0x48, 0x89, 0xFB, 0x81, 0xFE, 0x6C, 0x01, 0x00, 0x00, 0x77, 0x12, 0x89, 0xF0, 0x48, 0x8D},
         16},
        12,
        10,
        30,
        {0, {}, 0},
        16,
        7,
        {0, {}, 0},
        0x728,
        0xB0,
        0x698,
        0x18,
        0x208,
        0x218,
        0x1D0,
        0x1C0,
        0x251,
        0x38,
        0x40,
        0x50,
        88,
        56,
        0xDCB,
    },
    kLinuxPreviewCapabilities,
};

} // namespace

const VersionProfile* find_profile(const char* build_id) {
    if (build_id == nullptr) {
        return nullptr;
    }
    if (std::strcmp(build_id, kLinux.build_id) == 0) {
        return &kLinux;
    }
    if (std::strcmp(build_id, kWindows.build_id) == 0) {
        return &kWindows;
    }
    if (std::strcmp(build_id, kWindows1270.build_id) == 0) {
        return &kWindows1270;
    }
    if (std::strcmp(build_id, kLinux1270.build_id) == 0) {
        return &kLinux1270;
    }
    return nullptr;
}

const VersionProfile* all_profiles(size_t& count) {
    static const VersionProfile* kProfiles[] = {&kWindows, &kLinux, &kWindows1270, &kLinux1270};
    count = sizeof(kProfiles) / sizeof(kProfiles[0]);
    return *kProfiles;
}

VersionCapabilities conservative_capabilities() {
    VersionCapabilities caps;
    caps.note =
        "unrecognised build: hook targets are recovered by signature, and every offset-dependent "
        "feature stays disabled until the runtime layout check passes";
    return caps;
}

/*
 * The two known builds use different ABIs (libc++ vs MSVC), so the fallback
 * must match the platform, not the version.  Object offsets are driven by the
 * compiler layout, which is why a new build on the same platform normally
 * keeps working with the previous layout.
 */
const LiveProfile* fallback_layout() {
#ifdef _WIN32
    return &kWindows.live;
#else
    return &kLinux.live;
#endif
}

} // namespace yoyo
