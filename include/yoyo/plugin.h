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

#define YOYO_PLUGIN_ABI_VERSION 1u

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
    YOYO_EVENT_SERVER_READY = 5
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
