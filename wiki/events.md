# 事件

## 监听

```js
mc.listen(event, callback)
```

返回 `Boolean`：是否监听成功。

### 取消规则

**拦截事件：从回调返回 `false`。**

```js
mc.listen("onChat", (pl, msg) => {
    if (msg === "!bad") return false;  // 拦截，不再广播
});
```

也兼容 `event.cancelled = true` 写法。

拦截只影响 BDS 侧处理，其他插件的监听器仍会收到事件。

## 玩家事件

| 事件 | 参数 | 可拦截 |
|---|---|---|
| `onPreJoin` | `(player)` | 是 |
| `onJoin` | `(player)` | 否 |
| `onLeft` | `(player, reason)` | 否 |
| `onChat` | `(player, message)` | 是 |
| `onPlayerCmd` | `(player, command)` | 是 |
| `onRespawn` | `(player)` | 是 |
| `onPlayerDie` | `(player)` | 否 |
| `onChangeDim` | `(player, newDimension)` | 否 |
| `onJump` | `(player)` | 否 |
| `onSneak` | `(player, isSneaking)` | 否 |
| `onChangeSprinting` | `(player, sprinting)` | 否 |
| `onStartDestroyBlock` | `(player, block)` | 是 |
| `onDestroyBlock` | `(player, block)` | 是 |
| `onBlockInteracted` | `(player, block)` | 是 |
| `onUseItem` | `(player)` | 是 |
| `onUseItemOn` | `(player, block)` | 是 |
| `onDropItem` | `(player)` | 是 |
| `onBedEnter` | `(player, block)` | 是 |
| `onBedLeave` | `(player, block)` | 是 |
| `onPlayerSkinChange` | `(player, uuid)` | 否 |
| `onPlayerEmote` | `(player, pieceId, xuid)` | 否 |
| `onPlayerTeleport` | `(player, location)` | 否 |
| `onPlayerGameModeChange` | `(player, mode)` | 否 |
| `onPlayerItemHeld` | `(player, slot, previousSlot)` | 否 |
| `onPlayerHealthChange` | `(player, health)` | 否 |
| `onPlayerInteract` | `(player, pos)` | 否 |
| `onPlayerMove` | `(player, from, to)` | 否 |
| `onFormResponse` | `(player, formId, response)` | 是 |

## 服务器事件

| 事件 | 参数 | 说明 |
|---|---|---|
| `onServerStarted` / `onServerReady` | `(event)` | `event.serverVersion` / `event.moduleBase` |
| `onTick` | `(event)` | `event.tick`，每秒 20 次 |
| `onPacketReceive` | `(player, packetId)` | 可拦截 |
| `onPacketSend` | `(player, packetId)` | 可拦截；高流量包跳过 |

## 事件别名

| 别名 | 目标 |
|---|---|
| `onServerStarted` | `onServerReady` |
| `onChangeDim` | `onPlayerChangeDimension` |
| `onexit` | `onLeft` |
| `onMobDie` | `onPlayerDie`（仅玩家死亡） |
| `onJump` / `jump` | `onPlayerJump` |
| `onSneak` / `sneak` | `onSneak` |
| `onChangeSprinting` / `sprint` | `onChangeSprinting` |
| `onCommand` | `onPlayerCmd` |
| `onHealthChange` | `onPlayerHealthChange` |
| `onDie` | `onPlayerDie` |
| `packet_receive` / `packet_send` | 对应事件 |

## 判定字段

回调里的 `event` 参数包含：

| 字段 | 类型 | 说明 |
|---|---|---|
| `cancelled` | Boolean | 设为 `true` 拦截 |
| `message` | String | `onChat` 可改写 |
| `packetId` | Number | packet 事件 |
| `action` | Number | 玩家动作 |
| `tick` | Number | `onTick` |

## 未实现的事件

以下 LLSE 事件需要额外的引擎 hook，尚未实现：

- `onBlockChanged` / `onBlockExplode` / `onBlockExploded`
- `onMobHurt` / `onMobSpawned` / `onMobTrySpawn`
- `onProjectileHitBlock` / `onProjectileHitEntity` / `onSpawnProjectile`
- `onRedStoneUpdate` / `onPistonPush` / `onLiquidFlow` / `onHopper*`
- `onContainerChange` / `onInventoryChange`
- `onScoreChanged`（缺 Scoreboard 对象）
- `onWeatherChange` / `onThunderChange`

调用 `mc.listen` 注册这些事件会记录警告，不会静默失败。
