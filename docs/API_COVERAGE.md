# YoyoLoader API 覆盖审计

日期：2026-10-22  
目标 BDS：1.26.52.3（Windows / Linux）

图例：

- ✅ 已实现并实测
- 🟡 数据模型可用，但未连接 BDS 实时世界
- ❌ 未实现；调用会抛出带说明的错误，不会返回伪造数据

> **Live API 已通过 Endstone 后端补齐。** 需要真实 Player/Block/Item/Container/
> Entity/Forms/Scoreboard 时，使用 `endstone_plugins/` 和
> [docs/ENDSTONE_BACKEND.md](ENDSTONE_BACKEND.md)。下表的 ✅/🟡/❌ 描述的是
> YoyoLoader **原生 JS API** 的边界。

## 事件

| LLSE 事件 | 状态 | 说明 |
|---|---|---|
| `onPreJoin` | ✅ | `tryToLoadPlayer` inline hook |
| `onJoin` | ✅ | `SetLocalPlayerAsInitialized` inline hook；player_id 已实测 |
| `onChat` | ✅ | `TextPacket` handler inline hook；支持 `event.message` 原位重写；测试 bot 的聊天包被 BDS 丢弃，真实客户端待确认 |
| `onLeft` | 🟡 | `disconnectClientWithMessage` 路径（踢人/超时）可触发；客户端正常退出未覆盖 |
| 其他 LLSE 事件 | ❌ | `mc.listen` 接受注册但会警告，不会触发 |

## 玩家

| API | 状态 |
|---|---|
| `player.name` / `xuid` / `address` / `runtimeId` / `isOnline` | ✅（pre_join / chat 事件填充） |
| `player.toString()` | ✅ |
| `mc.getPlayer(nameOrXuid)` | ✅（事件注册表） |
| `mc.getOnlinePlayers()` / `mc.getPlayers()` | ✅（事件注册表） |
| `mc.getOnlinePlayerCount()` | ✅ |
| `player.sendMessage` | ❌ 需要 `ServerPlayer::sendNetworkPacket` / `NetworkSystem::send` 与 TextPacket 构造 |
| `player.kick` | ❌ 需要稳定的 `NetworkIdentifier` 副本与 `disconnectClientWithMessage` 调用约定 |
| `player.getPosition` / `getDimension` | ❌ 需要 Actor/ServerPlayer 的位置与维度布局 |
| `player.teleport` | ❌ 需要 `Player::teleportTo` 调用与 Vec3/维度参数 |
| `player.getNbt` | ❌ 需要 ServerPlayer/EntityContext NBT 访问 |

## 方块

| API | 状态 |
|---|---|
| `mc.newBlock(pos, type, nbt)` | 🟡 本地数据模型 |
| `block.getType/getPos/getNBT/setNBT` | 🟡 本地数据模型 |
| `mc.getBlock(pos)` | ❌ 需要 Level/BlockSource/BlockPos 布局与 `getBlock` 调用 |

## 物品

| API | 状态 |
|---|---|
| `mc.newItem(type, count, nbt)` | 🟡 本地数据模型 |
| `item.getType/getCount/setCount/getNBT/setNBT/isNull/getMaxStackSize` | 🟡 本地数据模型 |
| 实时背包物品、`ItemStack` 读写 | ❌ 需要 Inventory/ItemStack 布局与函数 |

## 容器

| API | 状态 |
|---|---|
| `mc.newContainer(size)` | 🟡 本地数据模型 |
| `container.getItem/setItem/addItem/removeItem/getAllItems/isEmpty/clear/getSize` | 🟡 本地数据模型 |
| `mc.getContainer(pos)` / 实时容器 | ❌ 需要 Container/BlockActor 布局与 `Level::getContainer` |

## 实体

| API | 状态 |
|---|---|
| `mc.newEntity(name, pos, health)` | 🟡 本地数据模型 |
| `entity.getName/getHealth/setHealth/getPos/getNBT/setNBT` | 🟡 本地数据模型 |
| `mc.getEntity(id)` / 实时 Actor 操作 | ❌ 需要 Actor/EntityContext/ActorRuntimeID 布局 |

## NBT

✅ 已完整实现：

- `new NBT()`
- `get` / `set` / `getTag` / `setTag` / `remove` / `keys` / `getType`
- `toJSON` / `toString`
- `toBinaryHex` / `NBT.parseBinary` / `NBT.fromHex`
- `NBT.parse` / `NBT.fromJSON`
- `setByteArray` / `setIntArray` / `setLongArray`
- Bedrock little-endian NBT 解析/序列化（含 network varint 选项）

## 表单

| API | 状态 |
|---|---|
| `mc.newSimpleForm` / `CustomForm` / `ModalForm` | 🟡 数据模型 |
| 按钮、输入、开关、下拉、滑条、步进滑条 | 🟡 |
| `setTitle` / `setContent` / `setConfirm` / `setCancel` | 🟡 |
| `toJSON` / `toString` | 🟡 |
| `form.sendTo(player)` | ❌ 需要 ModalFormRequestPacket 构造与发送路径 |

## 基础功能

| API | 状态 |
|---|---|
| `mc.log` | ✅ |
| `mc.getServerVersion` | ✅ |
| `mc.getModuleBase` | ✅ |
| `mc.broadcast` | ❌ 需要 TextPacket 构造与 NetworkSystem/ServerPlayer 发送 |
| `mc.runcmd` / `mc.runcmdEx` | ❌ 需要 CommandRegistry/MinecraftCommands 与 CommandOrigin 构造 |

## 插件

| 类型 | 状态 |
|---|---|
| C++ 插件事件 ABI | ✅（sample_cpp 编译/加载） |
| Go 插件事件 ABI | ✅ 接口已提供；cgo 构建需 MinGW-w64 |
| JS 插件 | ✅ QuickJS-ng 0.17.0，`plugins/*.js`，事件 + NBT + 表单/数据模型 |

## 继续实现实时 API 所需的关键逆向目标

1. `Actor` / `ServerPlayer` 的位置、维度、生命值、NBT 布局与 getter。
2. `NetworkSystem::send` + `MinecraftPackets::createPacket` + TextPacket/ModalFormRequestPacket 布局。
3. `ServerPlayer::sendNetworkPacket` 或等价发送函数。
4. `Level::getBlock` / `BlockSource::getBlock` / `setBlock`。
5. `Inventory` / `Container` / `ItemStack` 布局与读写函数。
6. `EntityContext` / `ActorRuntimeID` 到 Actor 的映射。
7. 正常断线的 `_onPlayerLeft` / `ServerPlayer::disconnect` 路径。

这些都需要继续用 REA + Ghidra 做定点反编译和版本签名，不能靠猜偏移实现。

## 自带测试插件

| 插件 | 实现方式 | 测试状态 |
|---|---|---|
| 称号插件 `plugins/title.js` | YoyoLoader JS + `onChat` / `event.message` 原位重写 | JS 监听与 MSVC/libc++ 原生字符串写入已自测；真实聊天包被测试 bot 丢弃，需真实客户端 |
| 侧边栏 `plugins/sidebar_pack` | BDS 原生行为包 + `@minecraft/server` + `/scoreboard` | Windows/Linux 实测，TPS/时间/玩家/实体/掉落物每秒刷新；安装脚本 `scripts/install_sidebar.py` |
| Endstone 称号 `endstone_plugins/yoyo_title` | Endstone `PlayerChatEvent.message` | 插件加载/启用通过；聊天包被测试 bot 丢弃，需真实客户端 |
| Endstone 侧边栏 `endstone_plugins/yoyo_sidebar` | Endstone Scoreboard + Scheduler + live entities | Windows/Linux 实测，`SIDE_BAR` 已显示，`players=1 entities=3` |
