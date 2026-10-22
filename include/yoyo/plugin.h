/*
 * YoyoLoader plugin ABI.
 *
 * The ABI is deliberately plain C: it is the only boundary that C++,
 * Go (c-shared) and a future JS host can all agree on.  Nothing here
 * exposes STL types or Bedrock engine layouts.
 */
#ifndef YOYO_PLUGIN_H
#define YOYO_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YOYO_PLUGIN_ABI_VERSION 3u

#ifdef _WIN32
#  define YOYO_EXPORT __declspec(dllexport)
#  define YOYO_CALL __cdecl
#else
#  define YOYO_EXPORT __attribute__((visibility("default")))
#  define YOYO_CALL
#endif

typedef enum yoyo_event_type {
    YOYO_EVENT_PRE_JOIN = 1, /* player is starting to connect; cancellable */
    YOYO_EVENT_JOIN = 2,     /* player finished loading the world */
    YOYO_EVENT_CHAT = 3,     /* player chat message; cancellable */
    YOYO_EVENT_LEFT = 4,     /* player disconnected */
    YOYO_EVENT_SERVER_READY = 5,
    YOYO_EVENT_TICK = 6,     /* one server tick */
    YOYO_EVENT_PLAYER_MOVE = 7,
    YOYO_EVENT_PLAYER_CHANGE_DIMENSION = 8,
    YOYO_EVENT_PLAYER_JUMP = 9,
    YOYO_EVENT_PLAYER_SNEAK = 10,          /* value: 1 started, 0 stopped */
    YOYO_EVENT_PLAYER_SPRINT = 11,         /* value: 1 started, 0 stopped */
    YOYO_EVENT_PLAYER_ACTION = 12,         /* raw PlayerActionPacket */
    YOYO_EVENT_BLOCK_START_DESTROY = 13,
    YOYO_EVENT_BLOCK_DESTROY = 14,
    YOYO_EVENT_BLOCK_INTERACT = 15,
    YOYO_EVENT_ITEM_USE = 16,
    YOYO_EVENT_ITEM_USE_ON = 17,
    YOYO_EVENT_ITEM_DROP = 18,
    YOYO_EVENT_BED_ENTER = 19,
    YOYO_EVENT_BED_LEAVE = 20,
    YOYO_EVENT_PLAYER_COMMAND = 21,        /* text: command, cancellable */
    YOYO_EVENT_FORM_RESPONSE = 22,         /* value: form id, text: response, cancellable */
    YOYO_EVENT_PLAYER_SKIN = 23,           /* text: UUID */
    YOYO_EVENT_PLAYER_EMOTE = 24,          /* text: piece id, text2: xuid */
    YOYO_EVENT_PLAYER_RESPAWN = 25,
    YOYO_EVENT_PLAYER_TELEPORT = 26,
    YOYO_EVENT_PLAYER_GAME_MODE = 27,      /* value: new game mode */
    YOYO_EVENT_PLAYER_ITEM_HELD = 28,      /* slot: selected slot, value: previous slot */
    YOYO_EVENT_PACKET_RECEIVE = 29,        /* packet_id, cancellable */
    YOYO_EVENT_PLAYER_INTERACT = 30,
    YOYO_EVENT_PLAYER_ATTACK = 31,         /* runtime_id: target */
    YOYO_EVENT_PLAYER_HEALTH = 32,         /* value: current health */
    YOYO_EVENT_PLAYER_DIE = 33,
    YOYO_EVENT_PACKET_SEND = 34            /* packet_id, cancellable */
} yoyo_event_type;

/*
 * All pointers inside an event are borrowed and only valid for the duration
 * of the callback.  Copy anything you need to keep.
 *
 * `handle` is an opaque ServerPlayer* when the runtime could resolve one.
 * Compare it only for identity; never dereference it from a plugin.
 */
typedef struct yoyo_player_view {
    void *handle;
    uint64_t runtime_id;
    const char *name;
    const char *xuid;
    const char *address;
} yoyo_player_view;

typedef struct yoyo_pre_join_event {
    yoyo_player_view player;
    int *cancelled;
} yoyo_pre_join_event;

typedef struct yoyo_join_event {
    yoyo_player_view player;
    uint64_t player_id;
    int *cancelled;
} yoyo_join_event;

typedef struct yoyo_chat_event {
    yoyo_player_view player;
    const char *message;
    int *cancelled;
} yoyo_chat_event;

typedef struct yoyo_left_event {
    yoyo_player_view player;
    const char *reason;
} yoyo_left_event;

typedef struct yoyo_server_ready_event {
    const char *server_version;
    uintptr_t module_base;
} yoyo_server_ready_event;

typedef struct yoyo_tick_event {
    uint64_t tick;
} yoyo_tick_event;

typedef struct yoyo_player_move_event {
    yoyo_player_view player;
    double from_x;
    double from_y;
    double from_z;
    double to_x;
    double to_y;
    double to_z;
    int from_dimension;
    int to_dimension;
} yoyo_player_move_event;

typedef struct yoyo_player_dimension_event {
    yoyo_player_view player;
    int from_dimension;
    int to_dimension;
} yoyo_player_dimension_event;

/*
 * Shared payload for the detail events added in ABI 3.  Unused fields are
 * zero (or -1 for slot/face) and must be ignored for the event type.
 *
 *  - action:     PlayerActionType for action/block/item events
 *  - x/y/z:      block position, or target position for teleport
 *  - face:       block face
 *  - slot:       selected hotbar slot for PLAYER_ITEM_HELD
 *  - runtime_id: target actor runtime id (attack/interact) or packet actor id
 *  - value:      state boolean, game mode, form id, or previous hotbar slot
 *  - text:       command, form response, skin UUID, or emote piece id
 *  - text2:      emote xuid
 */
typedef struct yoyo_detail_event {
    yoyo_player_view player;
    int packet_id;
    int action;
    int x;
    int y;
    int z;
    int face;
    int slot;
    uint64_t runtime_id;
    int value;
    const char *text;
    const char *text2;
    int *cancelled;
} yoyo_detail_event;

typedef void(YOYO_CALL *yoyo_event_callback)(void *user, int type, const void *event);

typedef struct yoyo_api {
    uint32_t abi_version;

    /* Build ID of the Bedrock server binary, e.g. "c0ff36e2...". */
    const char *(*get_server_version)(void);

    /* Main module base of bedrock_server(.exe). */
    uintptr_t (*get_module_base)(void);

    /* level: 0 = info, 1 = warning, 2 = error. */
    void (*log)(int level, const char *message);

    /* Returns 0 on success. */
    int (*register_event)(int type, yoyo_event_callback callback, void *user);
    int (*unregister_event)(int type, yoyo_event_callback callback, void *user);

    /*
     * Live game bridge (ABI 2+).  `player` is the opaque ServerPlayer handle
     * delivered in yoyo_player_view.  All functions return 0 on success.
     */
    int (*player_send_message)(void *player, const char *message);
    int (*server_broadcast)(const char *message);
    int (*player_kick)(void *player, const char *reason);
    int (*player_get_position)(void *player, double *x, double *y, double *z);
    int (*player_teleport)(void *player, double x, double y, double z, int dimension);
    int (*player_get_dimension)(void *player);
    int (*player_send_title)(void *player, const char *title, const char *subtitle, int fade_in, int stay,
                             int fade_out);
    int (*player_send_actionbar)(void *player, const char *text);
    int (*player_send_form)(void *player, const char *json);
    int (*player_get_game_mode)(void *player);
    int (*player_set_game_mode)(void *player, int mode);

    /* ABI 3 additions.  State getters return -1 when the packet has not been
     * observed yet; booleans return 0/1. */
    int (*player_get_health)(void *player);
    int (*player_is_sneaking)(void *player);
    int (*player_is_sprinting)(void *player);
    int (*player_is_flying)(void *player);
    int (*player_get_held_slot)(void *player);
    int (*player_send_tip)(void *player, const char *message);
    int (*player_send_popup)(void *player, const char *message);
    int (*player_send_system_message)(void *player, const char *message);
    int (*player_send_toast)(void *player, const char *title, const char *content);
    int (*player_reset_title)(void *player);
    int (*player_transfer)(void *player, const char *host, int port);
} yoyo_api;

typedef const yoyo_api *(YOYO_CALL *yoyo_plugin_entry_fn)(const yoyo_api *api);

/*
 * Every plugin (DLL/SO) must export this symbol.  Return the same `api`
 * pointer on success, or NULL if the plugin refuses to load.
 * The definition should be written as:
 *     YOYO_EXPORT const yoyo_api* YOYO_CALL yoyo_plugin_entry(const yoyo_api* api)
 */
YOYO_EXPORT const yoyo_api *YOYO_CALL yoyo_plugin_entry(const yoyo_api *api);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YOYO_PLUGIN_H */
