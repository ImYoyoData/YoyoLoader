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
4. 槽位 104 的函数地址是 `0x8918B70`，与 Endstone 符号表中的 `_validateLoginPacket` 一致，说明“头文件声明顺序 + 1（析构函数占 2 槽）”的映射成立。

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
- Endstone 的 `scripts/configs/*.toml` 提供了 82 个符号的字节签名和推导注释，可以直接复用或作为模板；`scripts/dump_symbols.py` 会按签名在新二进制中重新扫描。
- 找到函数后，记录：
  - 文件 RVA；
  - 前 14–27 字节序言，必须结束在完整指令边界；
  - 是否包含 RIP-relative 或 rel8/rel32 跳转（如果有，固定序言复制法不适用，需要换通用 detour 引擎）。

## 8. 已知未完成项

- Windows join（`0x980970`）与 chat（`0x973B30`）已定位并接入；join 已实机验证，chat 已用 Ghidra 确认语义但 bot 的聊天包被 BDS 丢弃。
- Linux join（`0x8911320`）与 chat（`0x8905AD0`）已接入；join 已实机验证，chat 同样需要真实客户端确认。
- `onLeft` 目前只覆盖 `disconnectClientWithMessage`（踢人/超时）；正常退出还没有走这个函数。
- join/left 的 `ServerPlayer*` 还没有解析；join 包只有 ActorRuntimeID，left 只有 NetworkIdentifier。
- `pre_join` 的 IP 字段为空。
- JS host 尚未实现。
