# 世界与方块

## 时间

```js
mc.setTime(6000);        // 设置世界时间（ticks）
mc.getTime();            // 当前时间；0 白天 / 1 游戏时间 / 2 天数
mc.getTime(1);           // 当天时间（0-23999）
mc.getTime(2);           // 天数
```

读取值来自服务器实际发送的 `SetTimePacket`，包括 YoyoLoader 自己发的包，
因此永远和客户端显示一致。**从未收到过时间包时返回错误**，不会猜。

## 天气

```js
mc.setWeather(0);   // 晴
mc.setWeather(1);   // 雨
mc.setWeather(2);   // 雷雨
mc.getWeather();    // 0 / 1 / 2
```

## 粒子

```js
mc.spawnParticle(pl.pos, "minecraft:heart_particle");
mc.spawnParticle(x, y, z, dimid, "minecraft:heart_particle");
```

向所有在线玩家发送粒子效果包。

## 方块读取

```js
const block = mc.getBlock(pos);           // pos 是 IntPos
const block = mc.getBlock(x, y, z, dimid);

if (block !== null) {
    block.name;      // 显示名
    block.type;      // 类型名
    block.pos;       // IntPos
    block.tileData;  // 数据值
    block.isAir;     // 是否空气
    block.runtimeId; // 运行时 ID
    block.getType();
    block.getPos();
}
```

数据来自服务器发给客户端的 `UpdateBlockPacket`。**客户端从未收到过该位置的方块更新时返回 `null`**。

## 方块写入

```js
mc.setBlock(pos, name, runtimeId);
mc.setBlock(x, y, z, dimid, name, runtimeId);
```

- 必须显式提供 `runtimeId`：方块名→ID 映射表是版本相关的，无法安全推断。
- 发送 `UpdateBlockPacket` 给所有玩家，**只改变客户端显示**，不修改服务器世界数据。
- 重新进入区块会恢复原方块。

## 为什么不用 BlockSource

`BlockSource::getBlock/setBlock` 需要每个版本重新逆向对象布局，且是 BDS 里最容易变化的部分。
当前实现选择基于包层（跨版本稳定），代价是覆盖范围受限（见上）。

## 未实现

| API | 原因 |
|---|---|
| `mc.getAllEntities()` | 需要 `Level` 的 actor 迭代入口 |
| `mc.getEntities(pos[, range])` | 同上 |
| `mc.spawnMob()` / `mc.spawnItem()` | 需要 `ActorFactory` |
| `mc.explode()` | 需要 `Explosion` 内部结构 |
| `mc.setBlock` 修改服务端 | 需要 `BlockSource` |
