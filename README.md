# YoyoLoader

YoyoLoader 是面向 **Minecraft Bedrock Dedicated Server 1.26.52.3** 的 C++20 注入式 mod loader。
它不修改原版 `bedrock_server`，而是用一个名为 `yoyo` 的启动器把 `yoyo_runtime` 注入进程，
再通过逆向得到的 RVA、vtable 槽位和对象布局提供事件与实时 API。

- 启动器：`yoyo.exe` / `yoyo`
- 运行时：`yoyo_runtime.dll` / `libyoyo_runtime.so`
- 命名空间：`yoyo`
- 插件：C++ / Go（C ABI）、JavaScript（内嵌 QuickJS-ng）

## 1. 设计选择

不修改原版二进制：

- Windows：挂起方式创建 `bedrock_server.exe`，远程 `LoadLibraryW` 注入，再恢复主线程。
- Linux：设置 `LD_PRELOAD` 后 `execv` 原版 `bedrock_server`。
- 原版 `bedrock_server` 保持只读，下载、校验、回滚都简单。
- 未知 build id 或函数序言不匹配时拒绝 patch，不会盲打。

启动器优先在当前目录查找 `bedrock_server(.exe)`，找不到再回退到自身所在目录。
runtime、`plugins/` 和服务器放在同一目录。需要 drop-in 名字时直接复制启动器即可：

```text
Windows:  copy yoyo.exe bedrock_server_mod.exe
Linux:    cp yoyo bedrock_server_mod
```

## 2. 已验证环境

| 平台 | BDS | Build ID | 实测内容 |
|---|---|---|---|
| Windows x64 | 1.26.52.3 | `d9f9837c2108f3824c4c44205044422e1` | 注入、事件、消息/标题/Toast/表单、位置/维度、传送、踢人、正常退出事件、C++/JS 插件 |
| Linux x64 | 1.26.52.3 | `c0ff36e2eacfc191f01e189237df71f139b5d04a` | 注入、事件、消息/标题/Toast/表单、位置/维度、传送、踢人、正常退出事件、JS 插件 |

测试使用 NetherNet 传输的离线 bot（Windows 与 Linux 均在 Docker 中）连接真实 BDS。
Linux runtime 在 Debian/glibc 2.36 + clang 20 下构建并加载；建议使用 LLD 链接。

> 仅支持 x86-64。Windows 需要 MSVC 2022；Linux 需要 clang++ 20 或更新版本。

## 3. 构建与运行

### Windows

```bat
scripts\build_windows.bat
```

部署到 BDS 目录：

```text
bedrock_server.exe
yoyo.exe
yoyo_runtime.dll
plugins\sample_cpp.dll
plugins\live_demo.js
```

运行 `yoyo.exe`。日志写入同目录 `yoyo.log`。

### Linux

```bash
bash scripts/build_linux.sh
```

部署：

```text
bedrock_server
yoyo
libyoyo_runtime.so
plugins/sample_cpp.so
plugins/live_demo.js
```

运行 `./yoyo`。脚本默认静态链接 libstdc++；如果目标机使用不同的 glibc，
可用 `YOYO_STATIC=0 bash scripts/build_linux.sh` 生成动态链接版本。

### CMake（可选）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 4. 事件

| 事件 | 触发点 | 可取消 | 状态 |
|---|---|---|---|
| `onPreJoin` | `ServerNetworkHandler::tryToLoadPlayer` | 是 | Windows / Linux 实测 |
| `onJoin` | `SetLocalPlayerAsInitialized` 处理函数 | 否 | Windows / Linux 实测 |
| `onChat` | `TextPacket` 处理函数 | 是 | 已安装并支持 `event.message`；测试 bot 的聊天包被 BDS 丢弃，需真实客户端确认 |
| `onLeft` | `ServerNetworkHandler::_onPlayerLeft` | 否 | Windows / Linux 实测，覆盖正常退出、踢人和超时 |
| `onLeft`（兼容路径） | `disconnectClientWithMessage` | 否 | 与上一个事件去重 |
| `onServerReady` | 插件加载完成后 | 否 | 两端实测 |

`onChat` 会原地修改原生 `TextPacket` 的字符串；如果新消息超过原字符串容量，
会保留原消息并写警告日志。不要依赖“取消后还能继续发包”的语义。

### 1.26.52.3 的 Hook RVA

| Hook | Linux RVA | Windows RVA | 序言长度 |
|---|---|---|---|
| `tryToLoadPlayer` | `0x892CD70` | `0x9A0690` | 14 / 19 |
| `disconnectClientWithMessage` | `0x8904FE0` | `0x972F40` | 17 / 19 |
| `SetLocalPlayerAsInitialized` | `0x8911320` | `0x980970` | 24 / 28 |
| `TextPacket` | `0x8905AD0` | `0x973B30` | 27 / 27 |
| `_onPlayerLeft` | `0x89143F0` | `0x983800` | 22 / 27 |

序言在 patch 前逐字节校验。`Level::tick` 的 vtable 钩子代码保留在 runtime 中，
但当前版本没有用它处理事件；所有事件都走上面的定点 hook。

## 5. 实时 API（YoyoLoader 原生）

实时桥接不依赖任何第三方符号文件、PDB 或 DWARF。使用的关键 BDS 入口：

| 功能 | Linux RVA | Windows RVA |
|---|---|---|
| `MinecraftPackets::createPacket` | `0x89FB260` | `0x93A490` |
| `Player::sendNetworkPacket` | vtable `0x728` | vtable `0x720` |
| `Actor::teleportTo` | vtable `0xB0` | vtable `0xA8` |
| `Player::setPlayerGameType` | vtable `0x698` | vtable `0x690` |
| `Dimension::getDimensionId` | vtable `0x18` | vtable `0x10` |

对象访问：

- 位置/朝向：`Actor` 的 `built_in_components_` 指针（Linux `+0x208`/`+0x218`，Windows `+0x218`/`+0x228`）。
- 维度：`Actor::dimension_`（Linux `+0x1C0`，Windows `+0x1C8`），虚拟 `getDimensionId`。
- 玩家标识：`RuntimeIDComponent` / `ActorUniqueIDComponent` 通过 EntityContext 的 EnTT 组件表读取。
- 踢人：从 join 处理函数保存 176 字节 `NetworkIdentifier`，调用 `disconnectClientWithMessage` 的 trampoline。
- 正常退出：`_onPlayerLeft` 直接提供 `Player*`，因此不需要再猜网络 ID。

### 已实现的 JS 实时 API

```js
// 消息
player.sendMessage("hello");
player.sendTitle("title", "subtitle", 10, 70, 20);
player.sendTip("tip");
player.sendPopup("popup");
player.sendSystemMessage("system");
player.sendToast("title", "content");
player.sendActionBar("actionbar");
player.resetTitle();

// 位置 / 维度 / 朝向
const pos = player.getPosition();       // { x, y, z }
const loc = player.getLocation();       // 脚部位置 + dimension + rotation
const rot = player.getRotation();       // { pitch, yaw }
player.teleport({ x, y, z, dimension: 0 });
player.getDimension();                  // 0 overworld, 1 nether, 2 the_end
player.getDimensionName();

// 身份 / 状态
player.getRuntimeId();
player.getUniqueId();                   // 原始 ActorUniqueID，可能为负值
player.getGameMode();                   // 0 survival, 1 creative, 2 adventure, 6 spectator
player.getGameModeName();
player.setGameMode("creative");         // 也接受数字

// 管理
player.kick("reason");
mc.getOnlinePlayers();
mc.getOnlinePlayerCount();
mc.broadcast("hello everyone");

// 表单（真实下发）
const form = mc.newSimpleForm("Title", "Content");
form.addButton("OK");
form.sendTo(player);
```

别名：`send_message`、`send_title`、`send_tip`、`send_popup`、`send_toast`、
`send_actionbar`、`reset_title`、`get_position`、`get_location`、`get_dimension`、
`get_game_mode`、`set_game_mode`、`send_form`。

实时行为：

- 消息/标题/Toast/表单使用 `MinecraftPackets::createPacket` 创建真实 BDS Packet，
  再通过 `Player::sendNetworkPacket` 发送；不是伪造的本地回调。
- 弹窗使用 `MessageAndParams` variant，客户端会收到 `parameters: []` 的 Popup。
- `teleport` 仅支持同维度；跨维度会返回 `false` 并记录警告。
- `getGameMode` 优先读 GameType 组件；读不到时使用 `setGameMode` 缓存/默认 0。
- 正常退出后玩家会从 `getOnlinePlayers()` 移除。

#### 尚未接入的实时 API

以下接口仍会抛出带说明的错误，或只返回本地数据模型：

- `mc.getBlock` / `mc.getContainer` / `mc.getEntity`
- 实时 Block/Item/Container/Entity/Inventory 读写
- Scoreboard、BossBar、Map、Boss Event、命令执行（`mc.runcmd` / `mc.runcmdEx`）
- 表单响应回调（客户端提交后回传 JS callback）
- 玩家生命值、饥饿、经验、飞行、权限、皮肤、Ping、跨维度传送
- 世界时间/天气、Level/Dimension 实时对象

Item/Block/Container/Entity 的 `mc.newItem` / `mc.newBlock` / `mc.newContainer` /
`mc.newEntity` 仍是本地数据模型，适合构造参数或测试；不能修改真实世界。

## 6. 插件 ABI

插件必须导出：

```cpp
extern "C" YOYO_EXPORT
const yoyo_api* YOYO_CALL yoyo_plugin_entry(const yoyo_api* api);
```

完整定义见 [include/yoyo/plugin.h](include/yoyo/plugin.h)。ABI 版本 2 在事件之外增加了：

```c
int (*player_send_message)(void *player, const char *message);
int (*server_broadcast)(const char *message);
int (*player_kick)(void *player, const char *reason);
int (*player_get_position)(void *player, double *x, double *y, double *z);
int (*player_teleport)(void *player, double x, double y, double z, int dimension);
int (*player_get_dimension)(void *player);
int (*player_send_title)(void *player, const char *title, const char *subtitle,
                         int fade_in, int stay, int fade_out);
int (*player_send_actionbar)(void *player, const char *text);
int (*player_send_form)(void *player, const char *json);
int (*player_get_game_mode)(void *player);
int (*player_set_game_mode)(void *player, int mode);
```

### C++

[plugins/sample_cpp/plugin.cpp](plugins/sample_cpp/plugin.cpp) 在 `onJoin` 中
发送消息并读取位置/维度。构建脚本会输出 `sample_cpp.dll` / `sample_cpp.so`。

### Go

[plugins/go_sample](plugins/go_sample) 使用 cgo `-buildmode=c-shared`：

```bash
# Linux（需要 gcc 与 Go）
cd plugins/go_sample
CGO_ENABLED=1 go build -buildmode=c-shared -o ../../build-linux/bin/plugins/sample_go.so .

# Windows（需要 MinGW-w64 的 gcc；MSVC 不能用于 cgo）
go build -buildmode=c-shared -o sample_go.dll .
```

`callback.c` 的 trampoline 把 C 回调转给 Go 的 `//export` 函数，并在 join 时调用
`player_send_message`。Go 侧检查 ABI 版本 2。

### JavaScript

运行时内嵌 **QuickJS-ng 0.17.0**（MIT，`third_party/quickjs`）。把 `.js` 放到
`<server>/plugins/` 即可。示例见 [plugins/live_demo.js](plugins/live_demo.js)
和 [plugins/title.js](plugins/title.js)，API 细节见 [plugins/js/README.md](plugins/js/README.md)。

```js
mc.listen("onJoin", (player) => {
    player.sendMessage("§a欢迎 " + player.name);
    player.sendTitle("§eYoyoLoader", "§bLive API", 10, 70, 20);
    const form = mc.newSimpleForm("测试", "选择一个操作");
    form.addButton("好的");
    form.sendTo(player);
});
```

## 7. 技术原理

1. **版本识别**：Windows 读 PE CodeView/RSDS；Linux 读 ELF `PT_NOTE` 的 GNU build-id。
   不使用 PDB/DWARF/符号文件。
2. **版本表**：`src/runtime/version.cpp` 保存 build id、RVA、函数序言和 vtable 槽位。
   未知 build 不安装 hook。
3. **inline hook**：复制完整指令边界的序言到 trampoline，写入 14 字节绝对跳转
   `FF 25 ... + abs64`。序言必须结束在指令边界，且不能包含 RIP-relative 指令。
4. **vtable hook**：对数据/虚函数路径使用只改槽位的 hook，避免复制序言；
   `Level::tick`、`sendNetworkPacket`、`teleportTo` 等不需要 inline trampoline。
5. **跨 STL 边界不传递 STL 对象**：runtime 自己编译，但读写 BDS 的 `std::string`
   时按 ABI 手工构造/解析：
   - MSVC：32 字节，size `+16`，capacity `+24`，短数据 `+0`，长指针 `+0`；
   - libc++ LLVM 20：24 字节，短字符串 `byte0 = size << 1`、数据 `+1`；长字符串
     size `+8`、指针 `+16`、capacity 字段 `+0`（低位为长标志）。
6. **Packet 布局**：`Packet` 基类 48 字节；`TextPacketPayload.body` 在 MSVC 下
   `+112`、libc++ 下 `+88`。Popup 使用 `std::variant` 的第三个 alternative
   （MessageAndParams），索引分别位于 body `+72` / `+56`。
7. **EnTT 组件**：EntityContext 的组件在 dense page 中按值存放，组件指针是
   `page + index * component_size`，不是从 page 里再读一个指针。

更细致的逆向记录见 [docs/RE_NOTES.md](docs/RE_NOTES.md)。

## 8. 测试与已知限制

- 测试使用 NetherNet + 离线 bot，Windows 和 Linux 都连接真实 BDS 1.26.52.3。
- Windows/Linux 的 join、消息、标题、Tip、Popup、Toast、Actionbar、表单、
  位置/维度、传送、踢人、正常退出均实测通过。
- `onChat` 的 bot 聊天包会在到达 handler 前被 BDS 丢弃；handler 和字符串重写
  已用 `mc.__simulateChat` / `mc.__rewriteNative` 自测，仍需真实客户端确认一次。
- 表单只支持下发；客户端提交的响应尚未回调到 JS。
- 跨维度传送、生命值/背包/权限/飞行、Scoreboard、命令执行、实时方块/容器/实体
  尚未接入。
- `getUniqueId` 返回 BDS 原始 `ActorUniqueID`，在当前版本可能表现为负数；
  需要稳定正数 ID 时请使用 `getRuntimeId`。
- `pre_join` 的 `address` 目前为空；`PlayerAuthenticationInfo` 不含客户端地址。
- JS 回调在触发事件的服务器/网络线程同步执行并持有引擎锁；不要 sleep、做长 I/O
  或长循环。QuickJS 内存上限 64 MB。
- 强制 trace / 反作弊软件可能阻止远程线程或 `LD_PRELOAD`；本项目不对抗这类检测。
- BDS 的 EULA/许可对逆向和再分发有限制。请只分发 YoyoLoader 自身，不要重新分发
  修改后的 `bedrock_server`。

## 9. 第三方

- [QuickJS-ng](https://github.com/quickjs-ng/quickjs) 0.17.0（MIT），源码在
  `third_party/quickjs`。
- 没有使用 LeviLamina 的 PreLoader/PeEditor，也没有使用任何第三方符号文件；
  hook 地址、序言和槽位来自本项目自己的版本表与二进制分析。
