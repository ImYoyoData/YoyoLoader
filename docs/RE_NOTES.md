# YoyoLoader 逆向笔记（BDS 1.26.52.3）

## 1. 二进制事实

| 项目 | Linux | Windows |
|---|---|---|
| 文件 | `bedrock_server` 245 MB | `bedrock_server.exe` 213 MB |
| 格式 | ELF64 x86-64 PIE，stripped | PE32+ x86-64 |
| 构建 | clang 20.1.8 + LLD 20，静态 libc++ | MSVC STL |
| 导出符号 | 只有 `.dynsym` 导入 | 只有 5 个 `stamp` 导出 |
| RTTI | 游戏类 RTTI 存在 | 只找到 std 类 RTTI，未找到游戏类 |
| 调试信息 | `.gnu_debuglink: bedrock_server.debug`（zip 中不含） | PDB 路径 + GUID；微软符号服务器 404 |

结论：不能链接，不能依赖 PDB/DWARF；运行时只能 `模块基址 + RVA`。版本识别使用构建 ID，而不是文件名。

## 2. 构建 ID

### Windows

PE 可选头 `DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]` → `IMAGE_DEBUG_DIRECTORY` → CodeView `RSDS`：

```
签名 RSDS
GUID 16 字节（前 4/2/2 字节按小端转成 canonical 字符串）
age 4 字节
```

1.26.52.3：

```text
d9f9837c2108f3824c4c44205044422e1
```

### Linux

ELF program headers 中 `PT_NOTE` → `Elf64_Nhdr`，查找 `NT_GNU_BUILD_ID`（type 3，name `GNU`），desc 的十六进制：

```text
c0ff36e2eacfc191f01e189237df71f139b5d04a
```

## 3. 事件挂载点

### pre_join

`ServerNetworkHandler::tryToLoadPlayer(ServerPlayer&, ConnectionRequest const&, PlayerAuthenticationInfo const&)`

| 平台 | RVA | 序言 |
|---|---|---|
| Linux | `0x892CD70` | `55 41 57 41 56 41 55 41 54 53 48 83 EC 58` |
| Windows | `0x9A0690` | `55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC C8 00 00 00` |

序言只是 push 寄存器 + `sub rsp`，没有 RIP-relative 指令，可以安全地整体复制到 trampoline。

`PlayerAuthenticationInfo` 的 `std::string` 成员顺序：

```text
xuid, play_fab_id, nintendo_id, psn_id, tenant_id,
mess_user_id, xbox_live_name, nintendo_name, play_station_name,
best_display_name, ...
```

所以：

- best_display_name：`9 * sizeof(std::string)`
- xbox_live_name：`6 * sizeof(std::string)`
- xuid：偏移 0

`sizeof(std::string)`：MSVC 32，libc++ 24。

### left

`ServerNetworkHandler::disconnectClientWithMessage(NetworkIdentifier const&, SubClientId, DisconnectFailReason, std::string const&, std::optional<std::string>)`

| 平台 | RVA | 序言 |
|---|---|---|
| Linux | `0x8904FE0` | `55 41 57 41 56 41 55 41 54 53 48 81 EC F8 01 00 00` |
| Windows | `0x972F40` | `55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 28 02 00 00` |

注意第 6 个参数 `std::optional<std::string>` 是按值传递的非平凡类型：

- SysV：通过不可见引用（r9）传递；
- Windows x64：在栈上传递。

detour 的签名必须保留这个参数（用 `const void*` 占位），否则调用原函数时第 6 个参数会错位。

### chat / join 的 handler 定位

这两个 handler 是虚函数，可以在 vtable 里找到；但服务器实际是**直接调用**它们（编译器 devirtualize），替换 vtable 槽位不会生效。最终实现是对函数自身做 inline hook。

Linux 主 vtable：

```text
RVA 0xF037CA8，offset_to_top = 0，共 107 槽
```

定位方法：

1. 在 `.rodata` 找到 `20ServerNetworkHandler\0`；
2. 通过 `.rela.dyn` 找到 typeinfo（name 指针 → typeinfo → 6 个引用它的 vtable）；
3. offset_to_top 为 0 的那个是主 vtable；
4. 槽位 104 的函数地址是 `0x8918B70`，与 `_validateLoginPacket` 的已知位置一致，说明“头文件声明顺序 + 1（析构函数占 2 槽）”的映射成立。

由此得到：

| 目标 | 声明序号 | 槽位 | 函数 RVA | 序言长度 |
|---|---|---|---|---|
| `SetLocalPlayerAsInitializedPacket` | 67 | 68 | `0x8911320` | 24 |
| `TextPacket` | 78 | 79 | `0x8905AD0` | 27 |

序言：

```text
join: 55 41 57 41 56 41 55 41 54 53 48 81 EC 58 03 00 00 48 89 FB 0F B6 52 10
chat: 55 41 57 41 56 41 55 41 54 53 48 81 EC D8 05 00 00 49 89 FD 80 BA 80 00 00 00 01
```

chat 的 27 字节刚好停在 `cmp byte [rdx+0x80], 1` 之后；后面紧跟一条 `jne rel8`，不能一起复制到 trampoline（相对跳转会失效）。运行时在 patch 前逐字节比对序言，不匹配就跳过。

Windows 的主 vtable 位于 RVA `0xA716310`（live dump 的 `vtable` 指针减去模块基址得到）。已验证：

| 槽位 | RVA | 说明 |
|---|---|---|
| 3 | `0x989140` | `_validateLoginPacket`，用作 vtable 基址/槽位校验锚点 |
| 90 | `0x980970` | `SetLocalPlayerAsInitialized` handler |
| 242 | `0x973B30` | `TextPacket` handler |

Windows join handler 的特征：调用 `_getServerPlayer` 后写入 `player+0xdb7 = 1`，与 Linux join 的 `player+0xcbf = 1` 对应。Ghidra 反编译结果：

```c
lVar7 = _getServerPlayer(param_1 - 0x10, param_2, *(byte *)(param_3 + 0x10));
if (lVar7 != 0) {
    *(byte *)(lVar7 + 0xdb7) = 1;
    ...
}
```

Windows chat handler 的 Ghidra 反编译结果包含：`_getServerPlayer`、大量 `std::string` 处理，以及对聊天 helper `FUN_140970de0`（含 "%s has been kicked for flooding the chat."）和 `disconnectClientWithMessage`（`FUN_140972f40`）的调用，因此确认它是 TextPacket handler。

Windows 序言：

```text
join: 41 56 56 57 53 48 81 EC 28 01 00 00 0F 29 BC 24 10 01 00 00 0F 29 B4 24 00 01 00 00
chat: 55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 18 05 00 00 48 8D AC 24 80 00 00 00
```

### 实机验证结果（1.26.52.3）

用 `bedrock-protocol` 的离线 bot 连接：

**Linux**

- `tryToLoadPlayer` inline hook 触发，`pre_join` 正确拿到 `YoyoTest`；
- `SetLocalPlayerAsInitialized` handler inline hook 触发，`join` 的 `player_id` 为 1 / 3（每次连接递增）；
- `TextPacket` hook 已安装，但 bot 发出的 chat 包没有被 BDS 接受/记录，所以 `onChat` 还需要真实客户端确认；
- bot 正常断开时没有走 `disconnectClientWithMessage`，`onLeft` 需要补 `_onPlayerLeft` / `ServerPlayer::disconnect`。

**Windows**

- 四个 inline hook 全部安装成功（tryToLoadPlayer / disconnectClientWithMessage / SetLocalPlayerAsInitialized / TextPacket）；
- `pre_join` 正确拿到 `YoyoTest`；
- `join` handler 触发，`player_id=1`，与 Linux 一致；
- 尝试了 raw / system / chat / translation 四种 TextPacket 变体，BDS 都没有把包送到 handler，因此 chat 仍需真实客户端确认；Ghidra 反编译已确认 handler 语义；
- 正常断开同样没有走 `disconnectClientWithMessage`，`onLeft` 仍需补 `_onPlayerLeft` / `ServerPlayer::disconnect`。

## 4.5 REA / Ghidra 环境

- REA：`rea-agents@6.7.0`，MCP + CLI 均已验证；
- JDK：`D:\Tools\jdk-21`（Temurin 21.0.12.1）；
- Ghidra：`D:\Tools\ghidra_12.1.4_PUBLIC`（12.1.4）；
- 环境变量：`JAVA_HOME`、`GHIDRA_INSTALL_DIR`，已写入 Yoyo Code 的 `rea` MCP 条目；
- REA 全量自动分析在 330 秒导入超时（`provider_timeout`）。可用：
  ```bash
  REA_GHIDRA_STARTUP_TIMEOUT_MS=1800000
  REA_GHIDRA_DISABLED_ANALYZERS="Decompiler Parameter ID,Stack,Aggressive Instruction Finder"
  ```
  然后重新打开目标；
- 本次确认 join/chat 语义使用的是 Ghidra 12.1.4 的 `-noanalysis` 定点反编译：
  ```bash
  analyzeHeadless <proj> BDSWin -import bedrock_server.exe -noanalysis \
      -scriptPath <scripts> -postScript DecompileAt.java 0x140980970-0x140980b79 0x140973b30-0x140974da2
  ```
  伪代码确认了 `player+0xdb7=1`（join）和聊天 helper / `disconnectClientWithMessage` 调用（chat）。

## 4. 对象布局

### std::string

MSVC x64：

```text
+0   union { char buf[16]; char* ptr; }
+16  size
+24  capacity
```

capacity >= 16 表示长字符串，数据在 `ptr`；否则在 `buf`。

libc++（LLVM 20，实测）：

```text
短字符串：byte0 = size << 1（最低位 0），数据从 +1 开始
长字符串：最低位 1，size @ +8，data pointer @ +16
```

### Packet / TextPacket

`Packet` 基类大小 48 字节（vtable + priority/reliability/subClientId/handled/timepoint/handler/compressible）。`TextPacket` 的 payload 从 +48 开始。

`TextPacketPayload`：

| 平台 | payload 大小 | body 偏移 | author/message |
|---|---|---|---|
| MSVC | 192 | 112 | author @ body+8；message @ body+40（8+32） |
| libc++ | 152 | 88 | author @ body+8；message @ body+32（8+24） |

`body` 起始字节是 `TextPacketType`：

- `1` Chat、`7` Whisper、`8` Announcement → `AuthorAndMessage`；
- `0` Raw、`3` Popup、`5` Tip、`6` SystemMessage → `MessageOnly`。

`xuid` 在 payload + 8（两种平台都是）。chat 事件用这个 xuid 回查 pre_join 缓存，拿到 `ServerPlayer*`。

MSVC 实测：

```text
sizeof(string)=32
sizeof(optional<string>)=40
sizeof(variant)=80
sizeof(payload)=192
offsetof body=112
body bytes: 01 ... 'Steve' ... 'hello world'
```

libc++ 实测：

```text
sizeof(string)=24
sizeof(optional<string>)=32
sizeof(variant)=64
sizeof(payload)=152
offsetof body=88
body bytes: 01 ... 0a 'Steve' ... 16 'hello world'
```

## 5. 为什么不用符号文件

- Windows 的 PDB 不在微软公共符号服务器上（按 RSDS GUID 查询 404）；
- Linux zip 不含 `.debug` 文件；
- 即使拿到 PDB/DWARF，也不能保证下一个版本仍有。

因此本项目只依赖：

1. 构建 ID；
2. 每个版本固化的 RVA；
3. 函数序言校验。

新增版本时，更新 `src/runtime/version.cpp` 即可，不需要改 hook 逻辑。

## 6. 验证 handler 序言

最可靠的方式是真实客户端连接一次：

1. 启动服务器，连接一个客户端；
2. `yoyo.log` 应该出现：
   ```text
   inline hook installed SetLocalPlayerAsInitialized rva=0x8911320
   inline hook installed TextPacket rva=0x8905ad0
   event join ...
   event chat ...
   ```
3. 如果出现 `failed to install ... hook`，说明序言不匹配（版本不对或函数变了），不会 patch，也不会崩服。

Windows 验证时，在 `version.cpp` 中填入两个 handler 的 RVA 和序言；序言不匹配会自动跳过。

## 7. REA / Ghidra 工作流

- 把 245 MB 的 ELF 导入 Ghidra 需要较大内存；不要试图一次反编译全部 `.text`。
- Linux：优先 RTTI 名称字符串 → typeinfo → vtable；用 vtable 槽位定位 handler，再对 handler 函数做 inline hook。
- Windows：没有游戏类 RTTI，优先用字符串 xref：
  - `ServerNetworkHandler::_onPlayerLeft`
  - `.../ServerNetworkHandler.cpp`
  - packet 名称、翻译 key 等。
- 找到函数后，记录：
  - 文件 RVA；
  - 前 14–27 字节序言，必须结束在完整指令边界；
  - 是否包含 RIP-relative 或 rel8/rel32 跳转（如果有，固定序言复制法不适用，需要换通用 detour 引擎）。

## 8. 实时桥接（1.26.52.3）

| 入口 | Linux | Windows |
|---|---|---|
| `MinecraftPackets::createPacket` | `0x89FB260` | `0x93A490` |
| `Player::sendNetworkPacket` | vtable `0x728` | vtable `0x720` |
| `Actor::teleportTo` | vtable `0xB0` | vtable `0xA8` |
| `Player::setPlayerGameType` | vtable `0x698` | vtable `0x690` |
| `Dimension::getDimensionId` | vtable `0x18` | vtable `0x10` |
| `_onPlayerLeft` | `0x89143F0` | `0x983800` |
| `Level::tick` | `0xC897700` | `0x7CBFD0` |

对象布局：

- `Actor::built_in_components_`：Linux `+0x208`（state）/`+0x218`（rotation），
  Windows `+0x218`/`+0x228`。
- `Actor::dimension_`：Linux `+0x1C0`，Windows `+0x1C8`。
- EnTT 组件在 dense page 中按值存放，组件指针 = `dense_page + index * size`。
  `RuntimeIDComponent`、`ActorUniqueIDComponent` 的大小均为 8。
- `NetworkIdentifier` 176 字节；join handler 的第二个参数可以直接复制。
- Linux `Level::tick` 的第 14 字节是 `0F B6 05 disp32`（RIP-relative），
  inline hook 必须重定位该 disp32，并把 trampoline 分配在目标 ±2 GB 内；
  Windows 前 30 字节没有 RIP-relative，可直接复制。
- `onPlayerMove` 和 `onPlayerChangeDimension` 不需要额外 packet hook，
  在 `Level::tick` 的 detour 中按玩家轮询位置/维度即可。
- `MinecraftPackets::createPacket` 是统一收包入口：hook 后把 `Packet::handler_`
  （Packet 基类 `+0x20`）换成 dispatcher，收到 packet 对象、packet id 和
  `NetworkIdentifier`，再调用原 dispatcher。包 ID 来自 createPacket 参数，
  不依赖 packet vtable。
- Linux `createPacket` 的最短安全序言是 12 字节，包含 `77 12` 短跳转；
  `install_inline_hook_short` 用 12 字节 `mov rax,imm64; jmp rax` 覆盖，并在
  trampoline 中把短跳转扩展成近跳转。Windows 用普通 14 字节序言即可。
- 出站 `Player::sendNetworkPacket` 是共享 vtable 槽位：首次登录时 patch 一次，
  detour 收到 `(ServerPlayer*, Packet*)`，用 createPacket 记录的 packet vtable→id
  表识别 `SetHealth(42)`、`DeathInfo(189)`，提供生命值/死亡/`onPacketSend`。
  高流量包（chunk、subchunk、LevelSound 等）跳过发送事件。
- `PlayerEquipment(31)` payload 尾部是 `slot, selected_slot, container_id`；
  当前用相邻 0..8 整数对做启发式解析，没有硬编码描述符大小。

## 8.1 跨版本定位（特征签名）

RVA 表是“已知版本”的快路径，不是唯一入口。`src/runtime/signatures.cpp` 为每个 hook
目标保存一条以函数入口为锚点的字节特征。

设计要点：

- 只固定 opcode 和寄存器操作数。栈帧大小（`sub rsp, imm32`）、帧指针偏移
  （`lea rbp, [rsp+imm32]`）、参数溢出偏移、`cmp reg, imm32` 里的协议号全部通配成 `??`。
  早期版本只通配了两个字节，导致 `disconnectClientWithMessage` 和 `_onPlayerLeft` 在
  22 万字节的 `.text` 里各命中 2 次和 92 次，解析直接失败。把 4 字节立即数整体通配后
  才唯一。
- 解析顺序：先按 build id 找档案；有档案就先用 RVA，并用同一套掩码校验那里的字节；
  校验不过再走特征。没有档案则全部走特征。
- 特征必须唯一命中；命中后还要用掩码再验一遍入口字节。任何一步失败 → 该 hook 跳过，
  不 patch，不影响其它功能。
- Windows（MSVC）与 Linux（clang+libc++）的序言不同，特征表按平台分开。

`.text` 里重复序言非常多（同一编译器的栈帧模板），所以特征必须覆盖到函数体开头有区分度
的那几条指令。当前 Windows 七条特征在 1.26.52.3 上都唯一，且落在记录的 RVA 上。

对象偏移无法靠入口特征保证，因此加了运行时布局校验：首个真实玩家出现时检查
`state_vector`/`rotation` 组件指针可读、位置有限、pitch/yaw 合理、维度在 0..2。
任一项失败就把布局标记为 Rejected，并永久禁用依赖布局的写操作（传送、设置游戏模式），
读取仍然可用。未知版本会显式记录 `object layout is a fallback`。

## 9. 已知未完成项

- `onChat` 的真实客户端链路仍需确认；测试 bot 的 TextPacket 在 BDS 聊天处理前被丢弃。
- 跨维度传送、背包/权限/经验、Scoreboard、命令执行、实时 Block/Item/Container/Entity
  尚未接入。
- 本轮新增 packet 事件尚未启动服务器做真实客户端回归（按要求未启动）。
- `pre_join` 的 IP 字段为空；`PlayerAuthenticationInfo` 不含客户端地址。
- `getUniqueId` 返回原始 `ActorUniqueID`，当前版本可能表现为负数。
- Linux 多世界 fork/exec 逻辑尚未在本机运行（本机无 Linux 工具链）。

## 10. 多核与多世界

- 单世界 tick 不能安全并行：`Level::tick` 和全局 Actor/Block/Inventory 状态没有为外部
  并发访问加锁。YoyoLoader 不尝试拆分单世界 tick。
- 多世界采用多进程：主世界 controller 启动额外 `yoyo.exe`，每个子世界是独立
  `bedrock_server.exe`，因此不同世界可以被 OS 调度到不同核心。
- 子世界通过硬链接复用服务器文件，拥有独立的 `worlds/<level>` 存档和端口；
  `TransferPacket` 用于客户端切换。
- 多世界命令只在主世界 controller 上执行 load/unload；所有命令用
  `yoyo_ops.txt` 做 XUID 白名单。

---

# 11. 原生对象访问（STL / EnTT）——本轮逆向结论

本轮目标：按 LeviLamina 的方式直接读 BDS 对象，跨 Windows(MSVC) 与 Linux(libc++) 两套 ABI。
以下每一条都在真实二进制与真实运行中的服务器上验证过，未验证的一律标注。

## 11.1 组件类型哈希 = FNV-1a/32(类型名)

**这是本轮最重要的发现。** 组件的 32 位 EnTT 类型 id 不是任意常量，而是组件 C++ 类型名的
FNV-1a/32 哈希：

```
h = 2166136261
for each byte b of "RuntimeIDComponent":
    h ^= b
    h *= 16777619      (mod 2^32)
```

验证方式（不是推测）：

1. 对 5 个已知哈希逐一计算，全部精确命中：
   - `RuntimeIDComponent` → `0xFC0DBBB5`
   - `ActorUniqueIDComponent` → `0x18F957AF`
   - `DimensionTypeComponent` → `0xAF993816`
   - `UserEntityIdentifierComponent` → `0x0B845379`
   - `ActorGameTypeComponent` → `0x88D3EDDF`（注意：不是 `GameTypeComponent`）
2. 从 1.26.52.3 Windows 二进制导出全部池条目（实体注册表实时遍历），得到 467 个活着的
   组件哈希；从二进制里抓出 1381 个 `*Component` 类型名做 FNV 比对，**433 个精确匹配**。
3. 同一批哈希在 1.26.70.21 Windows 与 Linux 二进制里同样存在。

结论：新增组件不需要扫描二进制，知道类名即可算出 id。这是 `components.h::component_hash()`。

## 11.2 EnTT 组件查找路径（两平台一致）

组件不是"指针数组"，而是**内联存进 dense page**：

```
registry = actor[+0x10]
entity   = actor[+0x18]              (32 位：低 18 位索引，高 14 位版本)
begin    = registry[+0x48]           (Windows) / [+0x38] (Linux 1.26.52.3)
end      = registry[+0x50]           / [+0x40]
entries  = registry[+0x68]           / [+0x50]
entries_end = registry[+0x70]        / [+0x58]     ← 必需，见下

bucket = begin[(count-1) & hash]
entry  = entries + bucket_value*32
pool   = entry[+0x10]

sparse_page = pool[+8][(entity & 0x3FFFF) >> 11]
item        = sparse_page[(entity & 0x7FF) * 4]
校验: (item ^ (entity & 0xFFFC0000)) <= 0x3FFFE

dense_pages = pool[+0x50]
page        = dense_pages[(item & 0x3FF80) >> 4]
component   = page + (item & 0x7F) * sizeof(ComponentType)
```

**踩过的坑（已修）**：`bucket_value` 是 entries 数组的下标，早期实现直接用
`entries + value*32` 而不校验上界。某些哈希走的探测链上会有脏 bucket，于是变成野读并崩溃。
现在先用 `entries_end` 算出容量，`value >= capacity` 直接当未命中返回。

## 11.3 ContainerComponent / ItemStack 布局

反汇编 `0x140a3eeb2`（Windows 1.26.52.3 / 70.21 同一份代码）确认：

| 项 | Windows | Linux |
|---|---|---|
| `ContainerComponent` 大小 | `0x200` | `0x1a0` |
| 槽位 `std::vector<ItemStack>` 偏移 | `+0x1a8` | `+0x150` |
| `Container*`（虚表对象）偏移 | `+0x10` | `+0x10` |
| `ItemStack` 大小 | `0x98` | `0x98` |

`ItemStack` 内部（两平台一致，由拷贝构造 `0x1424c3ce0` 与 `setItem` `0x1424c8b90` 确认）：

| 偏移 | 类型 | 含义 |
|---|---|---|
| `+0x08` | 指针 | `Item*`（两级间接后可读 id） |
| `+0x18` | 指针 | user-data / net-id 持有者 |
| `+0x20` | uint16 | aux value（`0x7FFF` = 未设置） |
| `+0x22` | uint8 | 数量 |
| `+0x23` | uint8 | 有效标志（1 = 有物品） |
| `+0x78` | 指针 | NBT user data |

`std::vector` 在三处 ABI 上都是 **3 个指针**（begin/end/capacity），所以一个读取器通用；
`std::string` 不是（MSVC 32 字节，libc++ 24 字节），走 `native_string.{h,cpp}`。

## 11.4 关键负面结论：玩家背包不是 EnTT 组件

在 1.26.52.3 上用真实玩家（entity id = 1）实测：

- `RuntimeIDComponent`、`ActorUniqueIDComponent`、`PlayerComponent`、`ServerPlayerComponent`、
  `UserEntityIdentifierComponent`、`EquippableComponent`、`ActorTypeComponent`、
  `StateVectorComponent`、`AttributesComponent` 全部 **PRESENT**；
- `ContainerComponent` **不在任何已注册的池里**（遍历 458 个活池，无 `0x1779E2C7`）；
- `ActorEquipmentComponent`、`ActorGameModeComponent` 同样不在池里。

即：`ContainerComponent` 是**方块实体容器**（箱子/漏斗等）用的，**玩家背包不通过它**。
玩家背包位于 `PlayerInventory`（`SimplePlayerContainer` 等），是 `ServerPlayer` 的成员对象，
不是 ECS 组件。该对象的字段偏移尚未确定。

**因此：`getInventory`/`getArmor`/`getEnderChest` 不能靠组件路径实现。**
把 `ContainerComponent` 当成玩家背包去读，会读到完全不相关的数据——这比"功能缺失"更糟。

## 11.5 数据包路径（InventoryContent / InventorySlot）

协议定义（取自 `minecraft-data` 1.26.51 的 `protocol.json`，非猜测）：

```
inventory_content:  window_id(WindowIDVarint)  ItemV4s
inventory_slot:     window_id  slot(varint)  [FullContainerName]  [ItemV4]  ItemV4
ItemV4:             li16 network_id, lu16 count, varint metadata,
                    bool has_stack_id, [zigzag32 stack_id],
                    varint block_runtime_id, encapsulated extra
ItemV4s:            varint count + count * ItemV4
```

解析器 `inventory.cpp` 已按此实现，并有完整的边界检查（`Reader`）。

**但它读不到正确的 payload 起点。** 对真实服务器抓包实测：

- 以 `kPacketBaseSize = 48` 为起点，解出的 window id 恰好是 120/124/119 等合法值，
  但随后的 count 与内容明显不是 ItemStack（是相邻的 ASCII/UUID 数据）；
- 对 0..128 的每个起点尝试解析，并**要求槽位数严格等于该窗口的规定值**
  （背包 36、盔甲 4、副手 1），结果**没有任何一个起点通过**。

结论：这些包对象的 payload 并不从固定偏移开始，且我尚未定位到真正的 payload 成员。
因此当前不启用数据包解析结果做背包读取（解析器代码保留，等定位到真实偏移再接）。

## 11.6 由此确定的能力边界

| 能力 | 结论 | 依据 |
|---|---|---|
| 读组件（生命/位置/维度/身份/游戏模式/属性） | 可用 | 11.1 + 11.2，已在真实玩家上验证 |
| 读方块实体容器（箱子等） | 可用 | 11.3，组件存在且布局已验证 |
| 读玩家背包 | **不可用**（原生） | 11.4 |
| 读玩家背包（脚本桥） | 可用，需 `@minecraft/server` | 真实服务器实测 `inv=36` |
| 动态内容注册新物品/生物/材质/合成表 | **运行时不可行** | 只能靠行为包，见 `dynamic_content.h` |

原生栈保持"只做已验证的事"：组件读、方块容器读。玩家背包与动态内容走独立模块
（`dynamic_content` / Script API 桥），两者代码与能力报告互不交叉。
