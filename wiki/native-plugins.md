# C++ / Go 插件

原生插件导出 C ABI 入口：

```cpp
extern "C" YOYO_EXPORT
const yoyo_api* YOYO_CALL yoyo_plugin_entry(const yoyo_api* api);
```

返回同一个 `api` 指针表示加载成功，返回 `NULL` 表示拒绝。

## 目录形式（必需）

原生插件**必须是目录**，因为共享库本身无法携带 manifest：

```text
plugins/myplugin/
  manifest.json
  myplugin.dll       (Windows)
  myplugin.so        (Linux)
```

```json
{
  "name": "myplugin",
  "version": "1.0.0",
  "engine": "native",
  "entry": "myplugin.dll"
}
```

## 头文件

```cpp
#include <yoyo/plugin.h>
```

## ABI v3 接口

```c
/* 信息 */
const char* (*get_server_version)(void);
uintptr_t   (*get_module_base)(void);
void        (*log)(int level, const char* message);

/* 事件 */
int (*register_event)(int type, yoyo_event_callback callback, void* user);
int (*unregister_event)(int type, yoyo_event_callback callback, void* user);

/* 玩家 */
int (*player_send_message)(void* player, const char* message);
int (*player_send_title)(void* player, const char* title, const char* subtitle,
                         int fade_in, int stay, int fade_out);
int (*player_send_actionbar)(void* player, const char* text);
int (*player_send_tip)(void* player, const char* message);
int (*player_send_popup)(void* player, const char* message);
int (*player_send_system_message)(void* player, const char* message);
int (*player_send_toast)(void* player, const char* title, const char* content);
int (*player_reset_title)(void* player);
int (*player_send_form)(void* player, const char* json);
int (*player_kick)(void* player, const char* reason);
int (*player_transfer)(void* player, const char* host, int port);

int (*player_get_position)(void* player, double* x, double* y, double* z);
int (*player_teleport)(void* player, double x, double y, double z, int dimension);
int (*player_get_dimension)(void* player);
int (*player_get_game_mode)(void* player);
int (*player_set_game_mode)(void* player, int mode);
int (*player_get_health)(void* player);
int (*player_is_sneaking)(void* player);
int (*player_is_sprinting)(void* player);
int (*player_is_flying)(void* player);
int (*player_get_held_slot)(void* player);

int (*server_broadcast)(const char* message);
```

## 事件类型

```c
YOYO_EVENT_PRE_JOIN, YOYO_EVENT_JOIN, YOYO_EVENT_CHAT, YOYO_EVENT_LEFT,
YOYO_EVENT_SERVER_READY, YOYO_EVENT_TICK, YOYO_EVENT_PLAYER_MOVE,
YOYO_EVENT_PLAYER_CHANGE_DIMENSION, YOYO_EVENT_PLAYER_JUMP,
YOYO_EVENT_PLAYER_SNEAK, YOYO_EVENT_PLAYER_SPRINT, YOYO_EVENT_PLAYER_ACTION,
YOYO_EVENT_BLOCK_START_DESTROY, YOYO_EVENT_BLOCK_DESTROY, YOYO_EVENT_BLOCK_INTERACT,
YOYO_EVENT_ITEM_USE, YOYO_EVENT_ITEM_USE_ON, YOYO_EVENT_ITEM_DROP,
YOYO_EVENT_BED_ENTER, YOYO_EVENT_BED_LEAVE, YOYO_EVENT_PLAYER_COMMAND,
YOYO_EVENT_FORM_RESPONSE, YOYO_EVENT_PLAYER_SKIN, YOYO_EVENT_PLAYER_EMOTE,
YOYO_EVENT_PLAYER_RESPAWN, YOYO_EVENT_PLAYER_TELEPORT,
YOYO_EVENT_PLAYER_GAME_MODE, YOYO_EVENT_PLAYER_ITEM_HELD,
YOYO_EVENT_PACKET_RECEIVE, YOYO_EVENT_PLAYER_INTERACT, YOYO_EVENT_PLAYER_ATTACK,
YOYO_EVENT_PLAYER_HEALTH, YOYO_EVENT_PLAYER_DIE, YOYO_EVENT_PACKET_SEND
```

详细事件结构的 `yoyo_detail_event` 用于 ABI 3 新增事件。

## 隔离

原生插件运行在服务器进程内，**无法像 JS 插件那样做上下文隔离**。
插件崩溃会带走服务器。因此：

- 插件内所有对游戏对象的调用前先检查返回值
- 不要长期持有 `player` 指针（玩家退出后失效）
- 必须把异常/崩溃风险限制在自己的回调里

## 构建

C++（Windows）：

```bat
cl /LD /std:c++20 /Iinclude myplugin.cpp /Fe:plugins/myplugin/myplugin.dll
```

C++（Linux）：

```bash
g++ -std=c++20 -fPIC -shared -Iinclude myplugin.cpp -o plugins/myplugin/myplugin.so
```

Go（需要 cgo）：

```bash
CGO_ENABLED=1 go build -buildmode=c-shared -o plugins/myplugin/myplugin.so .
```
