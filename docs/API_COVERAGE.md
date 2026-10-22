# YoyoLoader API 覆盖审计

日期：2026-10-22  
目标：BDS 1.26.52.3，Windows x64 / Linux x64  
测试方式：NetherNet 离线 bot 连接真实服务器；Windows 与 Linux 各一轮。

图例：

- ✅ 已实现并实测
- 🟡 数据模型可用，但未连接实时世界
- ❌ 未实现；调用会抛出说明性错误

## 事件

| 事件 | 状态 | 说明 |
|---|---|---|
| `onPreJoin` | ✅ | `tryToLoadPlayer` inline hook；可取消 |
| `onJoin` | ✅ | `SetLocalPlayerAsInitialized` inline hook；两端 bot 实测 |
| `onChat` | ✅（功能）/ 🟡（真实客户端） | `TextPacket` hook；支持 `event.message` 原生重写；测试 bot 的聊天包被 BDS 丢弃 |
| `onLeft` | ✅ | `_onPlayerLeft` inline hook；正常退出、踢人、超时均实测；`disconnectClientWithMessage` 路径去重 |
| `onServerReady` | ✅ | 插件加载完成后触发 |
| `onTick` | ✅ | `Level::tick` 内联 hook，Windows/Linux 实测 |
| `onPlayerMove` | ✅ | tick 内轮询位置，阈值 0.01 格；传送/移动实测 |
| `onPlayerChangeDimension` | 🟡 | tick 内比较维度；逻辑已接入，等待跨维度场景实测 |
| `onPlayerJump` / `onSneak` / `onChangeSprinting` | 🟡 | `PlayerAuthInputPacket` / `PlayerActionPacket`；已实现，等待真实客户端验证 |
| `onPlayerAction` / 方块和物品 action 明细 | 🟡 | `PlayerActionPacket`；取消只丢 action，不做事务回滚 |
| `onPlayerCmd` | 🟡 | `CommandRequestPacket`；命令原文可取消 |
| `onFormResponse` | 🟡 | `ModalFormResponsePacket`；form id + 响应文本 |
| `onPlayerSkinChange` / `onPlayerEmote` / `onPlayerItemHeld` | 🟡 | 对应 packet；槽位解析为启发式 |
| `onPlayerRespawn` / `onPlayerTeleport` / `onPlayerGameModeChange` | 🟡 | respawn/传送/游戏模式 |
| `onPlayerHealthChange` / `onPlayerDie` | 🟡 | 出站 `SetHealth` / `DeathInfo` + `sendNetworkPacket` vtable hook |
| `onPacketReceive` / `onPacketSend` | 🟡 | 统一 packet 包装；高流量包跳过发送事件 |
| 其他 LLSE/Endstone 事件 | ❌ | 区块、红石、活塞、投射物、天气、计分板等仍需新引擎 hook |

## 玩家

| API | 状态 | 说明 |
|---|---|---|
| `name` / `xuid` / `address` / `runtimeId` / `isOnline` | ✅ | xuid/name 来自 `PlayerAuthenticationInfo`；address 暂空 |
| `toString()` | ✅ | |
| `mc.getPlayer` | ✅ | 按 name/xuid 查事件注册表 |
| `mc.getOnlinePlayers` / `getPlayers` / `getOnlinePlayerCount` | ✅ | 正常退出会移除 |
| `sendMessage` / `send_message` | ✅ | 真实 TextPacket |
| `sendTitle` / `send_title` | ✅ | 真实 SetTitle，title + subtitle |
| `sendTip` / `sendTip` | ✅ | TextPacket Tip |
| `sendPopup` | ✅ | TextPacket Popup（MessageAndParams） |
| `sendSystemMessage` | ✅ | TextPacket SystemMessage |
| `sendToast` | ✅ | ToastRequest |
| `sendActionBar` / `send_actionbar` | ✅ | SetTitle Actionbar |
| `resetTitle` / `reset_title` | ✅ | SetTitle Reset |
| `getPosition` / `get_position` | ✅ | StateVectorComponent，眼位 |
| `getLocation` / `get_location` | ✅ | 脚位 + dimension + rotation |
| `getRotation` | ✅ | pitch / yaw |
| `getDimension` / `get_dimension` | ✅ | 0/1/2，虚拟 `getDimensionId` |
| `getDimensionName` | ✅ | overworld / nether / the_end |
| `teleport` | ✅ | 同维度；跨维度返回 false |
| `kick` | ✅ | 保存 join 时的 NetworkIdentifier，调用 disconnect trampoline |
| `getGameMode` / `getGameModeName` | ✅（回退缓存） | 优先 GameType 组件；读不到时用 set 缓存/默认 0 |
| `setGameMode` | ✅ | `Player::setPlayerGameType` vtable |
| `getRuntimeId` | ✅ | RuntimeIDComponent |
| `getUniqueId` | ✅ | ActorUniqueID 原始值，可能为负 |
| `getHealth` | 🟡 | 出站 SetHealth 缓存；未收到包时返回 null |
| `isSneaking` / `isSprinting` / `isFlying` | 🟡 | PlayerAuthInput / PlayerAction 状态缓存 |
| `getHeldItemSlot` | 🟡 | PlayerEquipment 槽位启发式解析 |
| `getNbt` / 背包 / 权限 / 皮肤对象 / Ping / `setHealth` | ❌ | 尚未接入 |
| 跨维度传送 | ❌ | 需要 `Level::entityChangeDimension` / changeDimension request |

## NBT

✅ 完整实现并自测：

- `new NBT()`、`get`、`set`、`getTag`、`setTag`、`remove`、`keys`、`getType`
- `toJSON`、`toString`、`toBinaryHex`、`NBT.parse`、`NBT.fromJSON`、`NBT.parseBinary`、`NBT.fromHex`
- `setByteArray`、`setIntArray`、`setLongArray`
- Bedrock little-endian NBT 解析/序列化

## 表单

| API | 状态 |
|---|---|
| `mc.newSimpleForm` / `newCustomForm` / `newModalForm` 及按钮/元素构建 | ✅ |
| `setTitle` / `setContent` / `setConfirm` / `setCancel` | ✅ |
| `toJSON` / `toString` | ✅ |
| `form.sendTo(player)` / `player.sendForm` | ✅ 真实 ModalFormRequest 包下发 |
| 表单响应回调（客户端提交 → JS callback） | 🟡 全局 `onFormResponse`；Form 对象级回调未接入 |

## 物品 / 方块 / 容器 / 实体

| API | 状态 |
|---|---|
| `mc.newItem` / `newBlock` / `newContainer` / `newEntity` | 🟡 本地数据模型 |
| 上述对象的本地 get/set/NBT/数量/大小 | 🟡 本地数据模型 |
| `mc.getBlock` / `mc.getContainer` / `mc.getEntity` | ❌ |
| 实时 ItemStack / Inventory / Container / Block / Actor 读写 | ❌ |

## 其他基础功能

| API | 状态 |
|---|---|
| `mc.log` | ✅ |
| `mc.getServerVersion` | ✅（build id） |
| `mc.getModuleBase` | ✅ |
| `mc.broadcast` | ✅ 真实 TextPacket，发送给在线玩家 |
| `mc.getLevel` | 🟡 轻量对象，仅玩家列表/数量 |
| `mc.worlds.list/current/isController/load/unload/switch/isOp` | ✅ Windows / Linux 多进程世界；Linux 使用 fork+exec+yoyo |
| `player.transfer(host, port)` | ✅ 真实 TransferPacket |
| `mc.runcmd` / `mc.runcmdEx` | ❌ 需要 CommandOrigin / MinecraftCommands |
| Scoreboard / BossBar / Map / Boss Event | ❌ |
| 世界时间 / 天气 | ❌ |

## 插件类型

| 类型 | 状态 |
|---|---|
| C++ 插件（C ABI v3） | ✅ 事件 + live 玩家 API + 状态查询；`sample_cpp` 编译 |
| Go 插件（cgo c-shared） | ✅ 事件 + live 玩家 API 声明；Windows 构建需 MinGW-w64 |
| JavaScript 插件 | ✅ QuickJS-ng 0.17.0；事件、NBT、表单、live 玩家 API |

## 已实测清单（Windows + Linux）

- 服务器启动、注入、原有 inline hook + `_onPlayerLeft` 安装（本轮之前）
- pre_join / join / left 事件
- onTick / onPlayerMove（Windows/Linux 实测）
- 玩家位置、脚位、朝向、维度
- sendMessage、sendTitle、sendTip、sendPopup、sendToast、sendActionBar、resetTitle
- SimpleForm 下发（客户端收到 ModalFormRequest）
- 同维度 teleport（位置立即变化）
- kick（客户端收到 `kicked` + 指定消息）
- 正常退出后 online count 归零
- C++ 插件在 join 时发送消息并读取位置/维度
- 游戏模式设置与读取（缓存回退）
- 多世界（Windows）：`world2` 以独立 BDS 进程启动在 19142，卸载后退出
- TransferPacket 目标地址/端口正确（bot 收到）
- 真实客户端跨 NetherNet 端口重连尚未验证

## 尚未实测 / 需要真实客户端

- `onChat` 的完整链路：测试 bot 的 TextPacket 在 BDS 聊天处理前被丢弃；
  handler 与原生字符串重写在 `mc.__simulateChat` / `mc.__rewriteNative` 中已自测。
- 跨维度 teleport。
- 本批新增的 packet 事件（Jump/Sneak/Sprint、Block/Item action、Command、
  FormResponse、Skin/Emote/ItemHeld、Health/Die、PacketSend/Receive）。
  这些代码已通过 Windows 编译和 hook 分支自测，但按照本轮要求没有启动服务器，
  需要在真实客户端上做一轮回归。
- Linux 多世界新代码已写入，并在 `gcc:13` 容器中完成全量编译/链接检查；
  尚未在本轮启动服务器做运行验证。
- Windows 的 12 字节短 hook 和 Linux 的 12 字节短 hook 都做了独立分支跳转自测
  （`low=42 high=7`）。
