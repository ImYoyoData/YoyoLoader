# 玩家对象

## 属性

| 属性 | 类型 | 说明 |
|---|---|---|
| `pl.name` | String | 玩家名 |
| `pl.xuid` | String | XUID |
| `pl.uuid` | String | UUID（同 xuid） |
| `pl.runtimeId` | Number | 运行时 ID |
| `pl.isOnline` | Boolean | 是否在线 |
| `pl.pos` | FloatPos | 眼睛位置，含 `dimid` / `dim` |
| `pl.feetPos` | FloatPos | 脚部位置 |
| `pl.blockPos` | IntPos | 所在方块坐标 |
| `pl.direction` | DirectionAngle | `pitch` / `yaw` / `toFacing()` |

### 位置对象

```js
const p = pl.pos;
p.x, p.y, p.z;   // Number
p.dimid;         // 0 主世界 / 1 下界 / 2 末地
p.dim;           // "主世界" / "下界" / "末地"
p.dimension;     // 同 dimid（YoyoLoader 旧别名）
p.dimensionName; // "overworld" / "nether" / "the_end"
```

构造：

```js
new IntPos(x, y, z, dimid)
new FloatPos(x, y, z, dimid)
new DirectionAngle(pitch, yaw)
```

## 消息

| 方法 | 说明 |
|---|---|
| `pl.tell(msg[, type])` | 发送消息；type: 0 聊天, 1 Tip, 4 系统, 5 Popup |
| `pl.sendText(msg[, type])` | 同 `tell` |
| `pl.sendMessage(msg)` | 直接聊天消息 |
| `pl.sendTip(msg)` | Tip |
| `pl.sendPopup(msg)` | Popup |
| `pl.sendSystemMessage(msg)` | 系统消息 |
| `pl.sendToast(title, content)` | Toast |
| `pl.sendTitle(title[, subtitle, fadeIn, stay, fadeOut])` | 标题 |
| `pl.setTitle(text[, type[, fadeIn, stay, fadeOut]])` | LLSE 风格；type 0 标题 / 1 动作栏 / 2 副标题 |
| `pl.sendActionBar(text)` | 动作栏 |
| `pl.resetTitle()` | 清除标题 |

## 位置与移动

| 方法 | 说明 |
|---|---|
| `pl.getPos()` | 脚部位置 FloatPos |
| `pl.getFeetPos()` | 同 getPos |
| `pl.getBlockPos()` | 方块坐标 IntPos |
| `pl.getPosition()` | 眼睛位置 |
| `pl.getRotation()` | `{ pitch, yaw }` |
| `pl.getDirection()` | DirectionAngle |
| `pl.getDimension()` | 数字维度 |
| `pl.getDimensionName()` | 维度名 |
| `pl.teleport(pos[, rot])` | 传送到位置对象 |
| `pl.teleport(x, y, z, dimid)` | 传送到坐标 |

`teleport` 要求目标维度与当前维度相同（跨维度未实现，返回 `false`）。

## 状态

| 方法 | 说明 |
|---|---|
| `pl.getGameMode()` | 0 生存 / 1 创造 / 2 冒险 / 6 旁观 |
| `pl.getGameModeName()` | 模式名 |
| `pl.setGameMode(mode)` | 设置模式，接受数字或名字 |
| `pl.isSneaking()` | 是否潜行 |
| `pl.isSprinting()` | 是否疾跑 |
| `pl.isFlying()` | 是否飞行 |
| `pl.getHealth()` | 生命值（未观测到返回 null） |
| `pl.getHeldItemSlot()` | 手持槽位 |
| `pl.isOP()` | 是否在 `yoyo_ops.txt` |
| `pl.getRuntimeId()` | 运行时 ID |
| `pl.getUniqueId()` | ActorUniqueID（可能为负） |

## 管理

| 方法 | 说明 |
|---|---|
| `pl.kick([reason])` | 踢出 |
| `pl.transfer(host, port)` | 传送到另一服务器（发 TransferPacket） |
| `pl.sendForm(form[, callback])` | 发送表单 |
| `pl.sendModalForm(title, content, ok, cancel[, cb])` | 确认框 |
| `pl.toString()` | 字符串表示 |

## 未实现

以下调用会抛出具名错误，而不是返回错误数据：

| API | 原因 |
|---|---|
| `pl.getInventory()` | 需要实时 `Container` 布局 |
| `pl.giveItem(item)` | 同上 |
| `pl.getArmor()` | 同上 |
| `pl.getEnderChest()` | 同上 |
| `pl.getNbt()` / `pl.setNbt(nbt)` | 完整玩家 NBT 序列化版本差异极大 |
| `pl.runcmd(cmd)` | 见 [命令](commands.md) |
