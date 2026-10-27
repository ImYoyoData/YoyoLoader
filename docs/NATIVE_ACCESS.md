# 原生对象访问与动态内容

本文说明 YoyoLoader 如何在不修改 `bedrock_server` 的前提下访问游戏内部数据，
以及哪些能力是**确实可用**、哪些是**已验证不可行**。后者同样重要：一份"做不到"的
结论有据可查，比一个会崩溃的实现有价值。

逆向细节（哈希推导、布局偏移、反汇编地址）见 [`RE_NOTES.md`](RE_NOTES.md) 第 11 节。

---

## 1. 两条互不交叉的路径

| | 原生栈 | 动态内容模块 |
|---|---|---|
| 入口 | `src/runtime/components.*`、`native_inventory.*`、`live.*` | `src/runtime/dynamic_content.*`、`script_bridge.*` |
| 手段 | 直接读 BDS 对象内存（逆向出的布局） | 官方 `@minecraft/server` Script API |
| 前提 | 类名（算哈希）+ 已验证的偏移 | 已部署的行为包 |
| 能做什么 | 读组件、读方块实体容器、发包 | 生成/给予已有物品、生成生物、改方块、跑命令 |
| 不能做什么 | 玩家背包（不是组件） | 运行时注册新的物品/生物/材质/合成表 |

两者不共享状态，能力报告也是分开打印的，插件的报错信息会指明是哪条路径不可用。

---

## 2. 原生栈：确实可用的部分

### 2.1 组件读取

BDS 的 EnTT 组件 id 是**类型名的 FNV-1a/32 哈希**：

```cpp
// components.h
component_hash("RuntimeIDComponent") == 0xFC0DBBB5
component_get(actor, "StateVectorComponent", sizeof(...));
```

这一点在真实服务器上验证过 433 个匹配项，因此**新增组件不需要扫描二进制**，
知道 C++ 类名即可。

已在真实玩家上确认可用的组件：

| 组件 | 用途 |
|---|---|
| `RuntimeIDComponent` | `pl.getRuntimeId()` |
| `ActorUniqueIDComponent` | `pl.getUniqueId()` |
| `StateVectorComponent` | `pl.getPosition()` / `getFeetPos()` |
| `ActorRotationComponent` | `pl.getRotation()` |
| `DimensionTypeComponent` | `pl.getDimension()` |
| `ActorGameTypeComponent` | `pl.getGameMode()` |
| `PlayerComponent` / `ServerPlayerComponent` | 判定玩家身份 |

### 2.2 方块实体容器

`ContainerComponent` **存在且布局已验证**：

- Windows：组件 512 字节，槽位数组在 `+0x1a8`
- Linux：组件 416 字节，槽位数组在 `+0x150`
- `ItemStack` 两边都是 152 字节，`+0x22` 数量、`+0x23` 有效标志、`+0x20` aux

它属于**方块实体**（箱子、漏斗、熔炉这类），不属于玩家。

---

## 3. 原生栈：已验证不可行的部分

### 3.1 玩家背包不是组件

用真实玩家（entity id = 1）遍历全部 458 个已注册组件池：

- `PlayerComponent`、`ServerPlayerComponent`、`UserEntityIdentifierComponent`、
  `EquippableComponent`、`StateVectorComponent`、`AttributesComponent` → **存在**
- `ContainerComponent` → **不存在**（任何池里都没有 `0x1779E2C7`）

玩家背包在 `PlayerInventory`（`SimplePlayerContainer` 等）里，是 `ServerPlayer` 的成员对象。
它的字段偏移尚未确定。

**为什么不继续扫内存找它**：我试过按 `vector<ItemStack>` 特征扫描 `ServerPlayer` 及一层成员对象，
每次都在假阳性候选上解引用野指针把服务器打崩。按 `PROJECT_RULES.md`
"崩溃比缺失功能更糟"，这条路被明确放弃，代码里 `native_inventory_discover()` 只打印结论、不扫描。

### 3.2 数据包路径读不到

`InventoryContent` 的线格式是权威的（取自 `minecraft-data` 1.26.51 `protocol.json`）：

```
window_id(varint) + ItemV4s(varint count + ItemV4[])
```

解析器已实现且有完整边界检查。但**找不到 payload 的真实起点**：

- 以固定的 `Packet 头 48 字节` 为起点，解出的 window id 巧合地像 120/124/119，
  但后面的 count 和内容明显是相邻的字符串数据；
- 对 0..128 每个起点做尝试，并要求**槽位数严格等于该窗口规定值**
  （背包 36、盔甲 4、副手 1）→ **零个起点通过**。

因此数据包解析结果当前**不用于背包读取**。解析器代码保留，等定位到真实 payload 成员再接。

### 3.3 玩家命令 / NBT

- `pl.runcmd()` 需要 `CommandOrigin`（BDS 的命令来源结构），布局未验证。
- 玩家 NBT 不是组件，序列化布局未验证。

两者的插件 API 都会抛出**指明原因**的错误，而不是静默失败。

---

## 4. 动态内容模块

`dynamic_content.*` 是独立模块，只在需要"改内容"时使用。

### 4.1 能力矩阵

启动时会打印（`dynamic_content: spawn mob=... spawn item=...`）：

| 操作 | 运行时可用 | 说明 |
|---|---|---|
| 生成生物（已有类型） | 需桥接执行器 | Script API 的 `dimension.spawnEntity` |
| 生成掉落物 | 需桥接执行器 | `dimension.spawnItem` |
| 给玩家物品 | 需桥接执行器 | `container.addItem` |
| 改方块 | 需桥接执行器 | `block.setType` |
| 跑命令 | 需桥接执行器 | `runCommand` |
| **注册新物品类型** | ❌ 永远不行 | 行为包专属 |
| **注册新生物类型** | ❌ 永远不行 | 行为包专属 |
| **注册新材质** | ❌ 永远不行 | 资源包/行为包专属 |
| **注册合成表** | ❌ 永远不行 | 行为包专属 |

最后四行是**游戏引擎的限制**，不是 YoyoLoader 的偷懒：Script API 只能操作"已经存在"的内容，
新的 id / 类型 / 合成表必须在世界加载前由行为包提供。这个事实写进能力报告，
免得插件去承诺一个永远做不到的功能。

### 4.2 官方桥接

`script_bridge.*` 部署 `behavior_packs/yoyo_bridge/`，内容包括：

- 导入 `@minecraft/server`，把 `world` / `system` 挂到 `globalThis.yoyoScript`
- 提供与 LegacyScriptEngine 更接近的辅助函数（`api.playerInventory`、`api.containerAdd` 等）
- 通过 `mc.system.runInterval` 周期性把玩家状态推给注入器

**当前状态**：桥接本体可用（真实服务器日志有 `script bridge ready`），
但把它和 QuickJS 插件连起来的执行通道（`bridge_state`）尚未完成，
所以动态内容操作目前报告"需要桥接执行器"。

> 曾经尝试用 `@minecraft/server-net` 做这条通道：模块确实随 BDS 发布，
> 但需要 `permissions.json` 白名单 + manifest 声明，且版本号校验会拒绝
> `1.0.0` / `0.1.0` / `1.0.0-beta` 等实测值。该路线暂时搁置，
> 相关代码已从部署流程移除，不留半成品。

---

## 5. 对插件作者的建议

1. 需要**读状态**（位置、生命、维度、身份、游戏模式）→ 直接用 `pl.*`，这些是原生读取，
   快且不依赖任何包。
2. 需要**读背包** → 目前没有可靠路径。请自己用 `InventoryContent` 逻辑或等桥接完成，
   不要假设 `pl.getInventory()` 会成功。
3. 需要**改内容**（给物品、生成生物、改合成表）→ 先查 `dynamic_content_capabilities()`；
   返回 `false` 时按其 `note` 提示用户，不要把失败当成功。
4. 任何 API 失败都会抛异常并说明原因，**不要用 try/catch 后静默continue**，
   否则玩家会看到"命令没反应"。
