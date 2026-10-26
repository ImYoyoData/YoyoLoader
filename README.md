# YoyoLoader

YoyoLoader 是一个 C++20 注入式 mod loader，为 **Minecraft Bedrock Dedicated Server** 提供
接近 LegacyScriptEngine 的事件与实时 API。它不修改原版 `bedrock_server`，而是用一个名为
`yoyo` 的启动器把 `yoyo_runtime` 注入进程。

- 启动器：`yoyo.exe` / `yoyo`
- 运行时：`yoyo_runtime.dll` / `libyoyo_runtime.so`
- 命名空间：`yoyo`
- 插件：C++ / Go（C ABI）、JavaScript（内嵌 QuickJS-ng）

**与 LegacyScriptEngine 的关系。** YoyoLoader 的事件与脚本 API 有意向
[LegacyScriptEngine](https://docs.levimc.org/LegacyScriptEngine/zh/apis/) 对齐：`mc.listen`、
`player.sendMessage` / `sendTitle` / `sendForm`、`setTimeout` / `setInterval`、
`JsonConfigFile` / `IniConfigFile`、`File`、`mc.regPlayerCmd` 等都沿用相同命名与调用形式，
目的是让为 LLSE 写的插件尽量不用改写就能运行。这不是逐字段复制：内部实现完全独立，
只对齐插件作者看到的那一层。差异与未实现项见 [docs/API_COVERAGE.md](docs/API_COVERAGE.md)。

**定位说明。** YoyoLoader 不是官方 Script API 的实现，也不是 LLSE 的替代品。它走的是
外部注入 + 逆向定位的路线：能力边界比官方插件 API 大（可以接管 packet、做多进程世界、
发送真实客户端包），代价是版本绑定、需要真实客户端回归。当前已用特征签名把“版本一更新
就全部失效”降到最低，详见第 4 节。

## 1. 设计选择

不修改原版二进制：

- Windows：挂起方式创建 `bedrock_server.exe`，远程 `LoadLibraryW` 注入，再恢复主线程。
- Linux：设置 `LD_PRELOAD` 后 `execv` 原版 `bedrock_server`。
- 原版 `bedrock_server` 保持只读，下载、校验、回滚都简单。
- 所有 patch 都以字节特征为门槛；特征不唯一或与预期不符就拒绝 patch，绝不盲打。

启动器优先在当前目录查找 `bedrock_server(.exe)`，找不到再回退到自身所在目录。
runtime、`plugins/` 和服务器放在同一目录。需要 drop-in 名字时直接复制启动器即可：

```text
Windows:  copy yoyo.exe bedrock_server_mod.exe
Linux:    cp yoyo bedrock_server_mod
```

## 2. 已验证环境

### 2.1 版本支持表

`支持状态` 指这个版本上有哪些能力经过实际验证。未列出的能力在该版本上会返回
“当前版本不支持”的错误，而不是静默执行。启动时 `yoyo.log` 会打印同一份清单。

| BDS 版本 | 平台 | Build ID | 支持状态 | 受限功能 |
|---|---|---|---|---|
| 1.26.52.3 | Windows x64 | `d9f9837c2108f3824c4c44205044422e1` | ✅ 完整 | 无 |
| 1.26.52.3 | Linux x64 | `c0ff36e2eacfc191f01e189237df71f139b5d04a` | ✅ 完整 | 无 |
| 1.26.70.21 | Windows x64 | `fd266068f00d540b4c4c44205044422e1` | 🟡 部分 | 无 `onPreJoin`、`onTick`、packet 事件；无位置/朝向/维度/游戏模式/生命值/传送/表单字段（对象布局未校验） |
| 1.26.70.21 | Linux x64 | `519f9a2c3cdc31c85ef87dd2f128f79136f854d4` | 🟡 部分 | 仅 `onJoin` 与 packet 工厂 hook；无聊天/离开/tick、无对象布局相关能力 |
| 其他版本 | 任意 | 未知 | 🟡 自动探测 | 靠特征签名尝试定位 hook；对象相关的全部能力默认关闭，直到运行时布局校验通过 |

`✅ 完整` 表示上表所有能力都在该版本实测过。`🟡 部分` 表示只保证清单里列出的
那几项，其余一律禁用。未知版本永远从“全部关闭”开始，绝不会因为“看起来像”就
按旧布局读写内存。

### 2.2 测试环境

测试使用 NetherNet / RakNet 传输的离线 bot（Windows 与 Linux 均在 Docker 中）连接真实 BDS。
Linux runtime 在 Debian/glibc 2.36 与 `gcc:13` 容器中构建；1.26.70.21 的 Linux 运行验证
使用 `gcc:13` 容器 + `LD_PRELOAD` 注入。

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
| `onTick` | `Level::tick` 内联 hook | 否 | Windows / Linux 实测 |
| `onPlayerMove` | `Level::tick` 中轮询位置 | 否 | Windows / Linux 实测，阈值 0.01 格 |
| `onPlayerChangeDimension` | `Level::tick` 中比较维度 | 否 | 已实现，等待跨维度场景实测 |
| `onPlayerJump` / `onJump` | `PlayerAuthInputPacket` / `PlayerActionPacket` | 否 | 已接入，等待真实客户端验证 |
| `onSneak` / `onChangeSprinting` | `PlayerAuthInputPacket` / `PlayerActionPacket` | 否 | 状态变化去重 |
| `onPlayerAction` | `PlayerActionPacket` | 部分 | 原始 action；动作明细见下 |
| `onStartDestroyBlock` / `onDestroyBlock` | `PlayerActionPacket` | 是* | 取消会丢弃该 action，不保证客户端预测回滚 |
| `onBlockInteracted` / `onUseItem` / `onUseItemOn` / `onDropItem` | `PlayerActionPacket` | 是* | 同上 |
| `onBedEnter` / `onBedLeave` | `PlayerActionPacket` | 是* | 同上 |
| `onPlayerCmd` | `CommandRequestPacket` | 是 | `event.text` 为命令原文 |
| `onFormResponse` | `ModalFormResponsePacket` | 是 | `formId` + 响应文本 |
| `onPlayerSkinChange` / `onPlayerEmote` / `onPlayerItemHeld` | 对应 packet | 否 | 皮肤 UUID / emote piece / 槽位 |
| `onPlayerRespawn` | `RespawnPacket` / `PlayerActionPacket` | 是* | |
| `onPlayerTeleport` | tick 位置跳变 + `HandledTeleport` | 否 | 超过 16 格的跳变作为传送 |
| `onPlayerGameModeChange` | tick 比较游戏模式 | 否 | |
| `onPlayerHealthChange` / `onPlayerDie` | 出站 `SetHealth` / `DeathInfo` | 否 | 需要 `sendNetworkPacket` vtable hook |
| `onPacketReceive` / `onPacketSend` | `createPacket` handler 包装 / 出站 vtable | 是 | 高流量包会跳过 `onPacketSend` |

`*` 这些事件可以设置 `event.cancelled`，但方块/物品动作的取消只丢弃客户端 action，
不做完整的事务回滚；正式服使用前应在真实客户端验证。

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
| `Level::tick`（内联） | `0xC897700` | `0x7CBFD0` | 20 / 30 |
| `MinecraftPackets::createPacket` | `0x89FB260` | `0x93A490` | 12 / 14 |
| `Player::sendNetworkPacket` | vtable `0x728` | vtable `0x720` | vtable slot |

序言在 patch 前逐字节校验。Linux 的 `Level::tick` 序言含一条 RIP-relative 指令，
trampoline 会重定位它，并分配在 BDS 映射附近以保证 rel32 可达。Linux 的
`createPacket` 前 12 字节包含一条短条件跳转，短 hook 会把它改写成近跳转；
`Packet::handler_` 位于 Packet 基类 `+0x20`。

## 4.1 跨版本支持（特征签名）

上面这张 RVA 表只是“已知版本的高速路径”，不是唯一入口。`src/runtime/signatures.cpp`
里为每个 hook 目标保存了一条以函数入口为锚点的字节特征：

- 只固定 opcode 和寄存器操作数；栈帧大小、帧指针偏移、参数溢出偏移、相对位移全部通配。
- 解析顺序：先试 RVA 表，并用同一套掩码校验那里的字节；不匹配就改用特征扫描。
- 特征必须在可执行段内**唯一命中**，否则判定失败、不 patch。
- 命中后仍要用掩码再验一次入口字节，防止特征落在函数中间。

因此“版本更新 → 全部功能失效”变成了：

| 情况 | 结果 |
|---|---|
| build id 已知 | 走 RVA 表，行为与之前完全一致 |
| build id 未知，但特征仍唯一 | 全部 hook 自动定位，功能继续可用 |
| build id 未知，某特征不唯一 | 该 hook 跳过，其它功能不受影响，日志给出原因 |
| 特征命中但入口字节不符 | 拒绝该 hook |

对象偏移（`Actor` 组件、维度引用等）比函数入口更容易随字段重排而变，所以额外加了一层
运行时校验：首个真实玩家出现时，用它的组件指针、位置、朝向和维度做合理性检查。

- 校验通过：按正常流程读写。
- 校验失败：**永久禁用依赖布局的写操作**（传送、设置游戏模式），读取仍然可用。
- 完全未知的版本：`yoyo.log` 会明确写出 `object layout is a fallback`，不会假装成功。

自测（`_test/sig_selftest.cpp`）做了三件事：

1. 在真实 `bedrock_server.exe` 的 `.text` 里验证 7 条特征各自唯一，且落在记录的 RVA 上；
2. 以 `DONT_RESOLVE_DLL_REFERENCES` 映射镜像后传入空版本档案，要求 7 个目标全部靠特征解析成功；
3. 篡改被通配的栈帧立即数，要求特征仍然命中同一位置。

## 4.2 为对齐 LLSE 做的取舍

版本无关性有成本，所以下面这些 LLSE 能力**有意不实现**：

| 未实现 | 原因 |
|---|---|
| `mc.runcmd` / `mc.newCommand` | 依赖 `CommandOrigin` / `CommandRegistry` 的内部布局，每个版本都要重新逆向，收益低于维护成本 |
| 时间 / 天气 / MOTD / 最大玩家数 | 只能伪造客户端显示，无法真正改引擎状态；宁可留空也不假装可用 |
| `addEffect` / `removeEffect` | 同上：能发包让客户端显示，但服务端状态不变，容易误导 |
| 模拟玩家 `spawnSimulatedPlayer` | 需要构造完整 `ServerPlayer` 生命周期，极不稳定 |
| 深层方块事件（红石、活塞、流体、漏斗） | 每个都要独立 hook，且是版本最脆弱的部分 |
| Scoreboard / BossBar / Map | 需要重建协议对象与状态机，维护成本高 |
| 完整 PlayerNBT 读写 | 序列化结构随版本变化最大 |
| 跨维度传送 | 需要 `changeDimension` 全流程，风险高 |

相对的，下面这些**优先保证**，因为它们在多个版本间结构稳定、且是插件最常用的：

- 玩家生命周期事件、聊天、命令前缀
- 消息 / 标题 / ActionBar / Toast / 表单（含回调）
- 位置、朝向、维度、生命值、游戏模式、潜行/疾跑/飞行状态
- 定时器、配置、文件、NBT
- 多进程多世界

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
player.isSneaking();
player.isSprinting();
player.isFlying();
player.getHeldItemSlot();               // 最近一次 PlayerEquipment 包
player.getHealth();                     // 最近一次 SetHealth 包，未知时 null

// 管理
player.kick("reason");
mc.getOnlinePlayers();
mc.getOnlinePlayerCount();
mc.broadcast("hello everyone");

// 服务器 tick 与移动
mc.listen("onTick", (event) => {
    // event.tick 是服务器累计 tick
});
mc.listen("onPlayerMove", (player, from, to) => {
    // from/to: { x, y, z, dimension, dimensionName }
});
mc.listen("onPlayerChangeDimension", (player, fromDimension, toDimension) => {});
mc.listen("onPlayerJump", (player, event) => {});
mc.listen("onSneak", (player, sneaking, event) => {});
mc.listen("onChangeSprinting", (player, sprinting, event) => {});
mc.listen("onStartDestroyBlock", (player, pos, event) => {});
mc.listen("onDestroyBlock", (player, pos, event) => {});
mc.listen("onBlockInteracted", (player, pos, event) => {});
mc.listen("onUseItem", (player, event) => {});
mc.listen("onUseItemOn", (player, pos, event) => {});
mc.listen("onDropItem", (player, event) => {});
mc.listen("onPlayerCmd", (player, command, event) => {});
mc.listen("onFormResponse", (player, formId, response, event) => {});
mc.listen("onPlayerHealthChange", (player, health, event) => {});
mc.listen("onPlayerDie", (player, event) => {});
mc.listen("onPacketReceive", (player, packetId, event) => {});
mc.listen("onPacketSend", (player, packetId, event) => {});

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
- 表单回调到具体 Form 对象的 `onResponse`（现在有全局 `onFormResponse`）
- 玩家饥饿、经验、权限、皮肤对象、Ping、跨维度传送、`setHealth`
- 世界时间/天气、Level/Dimension 实时对象

Item/Block/Container/Entity 的 `mc.newItem` / `mc.newBlock` / `mc.newContainer` /
`mc.newEntity` 仍是本地数据模型，适合构造参数或测试；不能修改真实世界。

## 5.5 多世界与多核

### 单世界多核：不可安全实现

BDS 的 `Level::tick`、实体/方块更新、全局注册表和玩家状态共享同一套可变状态。
通过外部 hook 把同一世界的 tick 拆到多个线程，需要给引擎内部大量容器加锁并保证
顺序一致；在无法修改 BDS 源码的前提下，这会导致随机崩溃、状态撕裂和存档损坏。
因此 YoyoLoader **不会**伪造“单世界多线程”，也没有把 `Level::tick` 并行化。

### 多进程多世界：可以真正用多核

YoyoLoader 内置了多世界管理器。每个世界是一个独立的 `bedrock_server.exe`
进程，拥有自己的存档、端口和 tick 线程；操作系统可以把不同世界调度到不同核心。
主世界是 controller，子世界由主世界按需启动。

配置文件 `yoyo_worlds.ini`（服务器根目录）：

```text
# name|port|level
main|19132|world
world2|19142|world2
```

OP 列表 `yoyo_ops.txt`，一行一个 XUID：

```text
2535405997872704
```

游戏内聊天命令：

```text
!world list
!world current
!world load world2
!world unload world2
!world switch world2
```

- 所有世界命令都要求 XUID 出现在 `yoyo_ops.txt`。
- `load` / `unload` 只能在主世界（controller）执行。
- `switch` 会下发真实 `TransferPacket`，客户端重连到目标世界端口。
- 子世界目录在 `instances/<name>`，服务器文件用硬链接复用（跨卷时回退为复制），
  存档目录独立。
- Windows 已实测：`world2` 启动为第二个 BDS 进程（19142）、卸载后进程退出；
  bot 收到了目标地址/端口正确的 TransferPacket。
- Linux 使用 `fork` + `exec` 启动 `instances/<name>/yoyo`，通过管道写 `stop`，
  超时后 `SIGTERM`/`SIGKILL`；子进程设置 `PR_SET_PDEATHSIG`，主进程退出时
  子世界也会收到 `SIGTERM`。该路径已通过 Linux 编译/链接检查，尚未在本轮启动服务器。
- 真实客户端从 NetherNet 主世界重连到子世界端口的完整链路仍需要客户端实测；
  如果客户端不接受直接端口 Transfer，需要再补 NetherNet 发现/网络 ID。

`load` / `unload` 在等待子进程端口就绪或退出时会阻塞调用线程数秒，这是当前
原型为了简化同步的已知代价。

## 6. 插件 ABI

插件必须导出：

```cpp
extern "C" YOYO_EXPORT
const yoyo_api* YOYO_CALL yoyo_plugin_entry(const yoyo_api* api);
```

完整定义见 [include/yoyo/plugin.h](include/yoyo/plugin.h)。ABI 版本 3 在事件之外增加了：

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

/* ABI 3 */
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
```

事件 ABI 新增：

```c
YOYO_EVENT_TICK                    // const yoyo_tick_event*
YOYO_EVENT_PLAYER_MOVE             // const yoyo_player_move_event*
YOYO_EVENT_PLAYER_CHANGE_DIMENSION // const yoyo_player_dimension_event*
/* ABI 3 detail events */
YOYO_EVENT_PLAYER_JUMP .. YOYO_EVENT_PACKET_SEND
                                   // const yoyo_detail_event*
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
`player_send_message`。Go 侧检查 ABI 版本 3。

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
