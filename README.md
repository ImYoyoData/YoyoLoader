# YoyoLoader

YoyoLoader 是一个面向 **Minecraft Bedrock Dedicated Server** 的 C++20 注入式 mod loader。它不修改原版 `bedrock_server`，而是在当前目录启动一个名为 `yoyo` 的启动器，把 `yoyo_runtime` 注入进去，然后通过逆向得到的 RVA 和函数序言挂接玩家事件。

命名沿用你的名字：项目 **YoyoLoader**，启动器 `yoyo`，运行时 `yoyo_runtime.dll` / `libyoyo_runtime.so`，C++ 命名空间 `yoyo`。

## 1. 为什么新建 `yoyo`，而不是改 `bedrock_server`

推荐 **不修改** 原版二进制：

- 不需要像 LeviLamina 的 PeEditor 那样给 exe 打补丁，BDS 更新后不会留下一个坏掉的旧 exe。
- Windows 用“挂起进程 + 注入 DLL”，Linux 用 `LD_PRELOAD`，两端逻辑一致。
- 原版 `bedrock_server` 保持只读，下载/校验/回滚都简单。

如果你想要 drop-in 的名字，直接复制启动器即可：

```text
Windows:  copy yoyo.exe bedrock_server_mod.exe
Linux:    cp yoyo bedrock_server_mod
```

启动器优先在当前目录查找 `bedrock_server(.exe)`，找不到再回退到自身所在目录；runtime 和 `plugins/` 与服务器放在同一目录。改名不影响。

## 2. 已验证环境

| 平台 | BDS | Build ID | 状态 |
|---|---|---|---|
| Windows x64 | 1.26.52.3 | `d9f9837c2108f3824c4c44205044422e1` | 注入、inline hook、插件加载、服务器启动均已实测 |
| Linux x64 | 1.26.52.3 | `c0ff36e2eacfc191f01e189237df71f139b5d04a` | 注入、inline hook、插件加载、服务器启动均已实测（Docker + clang 20） |

> 只支持 x86-64；不支持 ARM。

## 2.5 Live API 后端：Endstone

为了提供**完整的实时 API**（Player / Block / Item / Container / Entity / Forms / Scoreboard），项目附带 Endstone 后端。Endstone 是 Apache-2.0，支持 BDS 1.26.52 的 Windows/Linux，已经有成熟的 fake headers 和逐版本符号表。

```bash
python scripts/setup_endstone_backend.py \
    --server endstone_server \
    --bds-zip /path/to/bedrock-server-1.26.52.3-win.zip \
    --run
```

自带的两个 Endstone 插件：

- `yoyo_title`：监听 `PlayerChatEvent`，修改 `event.message` 后放出（称号插件）。
- `yoyo_sidebar`：真实 scoreboard 侧边栏，每秒刷新 TPS / 游戏时间 / 玩家 / 实体 / 掉落物。

插件源码在 [endstone_plugins](endstone_plugins)，预构建 wheel 在 `dist/endstone/`，完整安装/测试记录见 [docs/ENDSTONE_BACKEND.md](docs/ENDSTONE_BACKEND.md)。

> YoyoLoader 原生 runtime 与 Endstone 是两种后端，**不要同时注入同一个 bedrock_server 进程**：
> - 需要完整 live API：用 Endstone 后端；
> - 需要自定义注入、JS/C++/Go 事件插件、NBT/表单数据模型：用 YoyoLoader 原生 runtime。

## 3. 快速开始

### Windows

```bat
scripts\build_windows.bat
```

把下面这些文件放到 BDS 目录（和 `bedrock_server.exe` 同级）：

```text
yoyo.exe
yoyo_runtime.dll
plugins\sample_cpp.dll
```

运行：

```bat
yoyo.exe
```

启动器会以挂起方式创建 `bedrock_server.exe`，注入 `yoyo_runtime.dll`，等 `DllMain` 完成 hook 安装后再恢复主线程。日志写入同目录 `yoyo.log`。

### Linux

```bash
bash scripts/build_linux.sh
```

把下面这些文件放到 BDS 目录（和 `bedrock_server` 同级）：

```text
yoyo
libyoyo_runtime.so
plugins/sample_cpp.so
```

运行：

```bash
./yoyo
```

启动器会设置 `LD_PRELOAD=<绝对路径>/libyoyo_runtime.so` 并 `execv` 原版 `bedrock_server`。日志同样在 `yoyo.log`。

### CMake（可选）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

多配置生成器（Visual Studio）的可执行文件通常在 `build/bin/Release`。

## 4. 事件

| 事件 | 触发点 | 可取消 | 当前可用性 |
|---|---|---|---|
| `YOYO_EVENT_PRE_JOIN` | `ServerNetworkHandler::tryToLoadPlayer` | 是（返回 false 拒绝登录） | Win / Linux |
| `YOYO_EVENT_JOIN` | `ServerNetworkHandler` 的 `SetLocalPlayerAsInitializedPacket` handler | 否 | Linux / Windows 均已实机验证 |
| `YOYO_EVENT_CHAT` | `ServerNetworkHandler` 的 `TextPacket` handler | 是（不调用原函数即丢弃） | Linux / Windows 均已安装；测试 bot 的聊天包被 BDS 丢弃，需真实客户端确认 |
| `YOYO_EVENT_LEFT` | `ServerNetworkHandler::disconnectClientWithMessage` | 否 | Win / Linux |
| `YOYO_EVENT_SERVER_READY` | 插件加载完成后 | 否 | Win / Linux |

### 1.26.52.3 的 RVA

| Hook | Linux RVA | Windows RVA | 序言长度 |
|---|---|---|---|
| `tryToLoadPlayer` | `0x892CD70` | `0x9A0690` | 14 / 19 |
| `disconnectClientWithMessage` | `0x8904FE0` | `0x972F40` | 17 / 19 |
| `SetLocalPlayerAsInitialized` handler | `0x8911320` | `0x980970` | 24 / 28 |
| `TextPacket` handler | `0x8905AD0` | `0x973B30` | 27 / 27 |

Linux 的 `ServerNetworkHandler` 主 vtable 位于 RVA `0xF037CA8`，共 107 槽；槽位 104 对应 `_validateLoginPacket`，用来校验“声明顺序 → 槽位”的映射。槽位 68 / 79 指向上面两个 handler，但服务器是**直接调用**它们（devirtualized），改 vtable 槽位不会生效，所以最终 hook 的是函数自身的序言。

Windows 的主 vtable 位于 RVA `0xA716310`。槽位 3 是 `_validateLoginPacket`（`0x989140`），槽位 90 是 join handler（`0x980970`），槽位 242 是 chat handler（`0x973B30`）。这两个 handler 已用 Ghidra 定点反编译确认：

- `FUN_140980970`：调用 `_getServerPlayer` 后写入 `player+0xdb7 = 1`；
- `FUN_140973b30`：调用 `_getServerPlayer`、处理字符串，并调用聊天 helper `FUN_140970de0` 与 `disconnectClientWithMessage`。

Windows 的 join 已用离线 bot 实测触发；chat hook 安装成功，但 bot 发出的 TextPacket 被 BDS 丢弃，仍需真实客户端确认一次。

## 5. 插件 ABI

插件是普通 DLL / SO，必须导出：

```cpp
extern "C" __declspec(dllexport)
const yoyo_api* __cdecl yoyo_plugin_entry(const yoyo_api* api);
```

Linux 用 `YOYO_EXPORT`（默认可见性）即可。完整定义见 [include/yoyo/plugin.h](include/yoyo/plugin.h)。

设计原则：

- 只暴露 C 结构体、C 函数指针和 `const char*`，不暴露任何 STL / Bedrock 类型。
- 事件中的字符串只在回调期间有效，插件需要保存时自行拷贝。
- `player.handle` 是不透明的 `ServerPlayer*`，只能用于比较身份，不能解引用。
- 回调在游戏线程上执行，不要做长时间阻塞操作。

### C++ 示例

见 [plugins/sample_cpp/plugin.cpp](plugins/sample_cpp/plugin.cpp)。构建脚本会生成 `plugins/sample_cpp.dll` / `sample_cpp.so`。

### Go 示例

见 [plugins/go_sample](plugins/go_sample)。使用 cgo 的 `-buildmode=c-shared`：

```bash
# Linux
cd plugins/go_sample
CGO_ENABLED=1 go build -buildmode=c-shared -o ../../build-linux/bin/plugins/sample_go.so .

# Windows（需要 MinGW-w64 的 gcc；MSVC 不能直接用于 cgo）
go build -buildmode=c-shared -o sample_go.dll .
```

`callback.c` 中的 C trampoline 负责把 C 函数指针转给 Go 的 `//export` 函数。

> C++ / Go 插件目前通过 C ABI 接收事件和玩家身份信息；NBT、表单、Item/Block/Container/Entity 等高层 API 目前只在 JS 层提供。

### JS 示例

runtime 已内嵌 **QuickJS-ng 0.17.0**（MIT，源码在 `third_party/quickjs`）。把 `.js` 文件放到 `<server>/plugins/` 即可加载；示例见 [plugins/sample.js](plugins/sample.js)，完整 API 见 [plugins/js/README.md](plugins/js/README.md)。

```js
mc.log("plugin loaded");

mc.listen("onPreJoin", (player) => mc.log("pre_join", player.name, player.xuid));
mc.listen("onJoin", (player) => mc.log("join", player.runtimeId));
mc.listen("onChat", (player, message, event) => {
    mc.log("chat", player.name, message);
    // event.cancelled = true;
});
mc.listen("onLeft", (player, reason) => mc.log("left", reason));

const nbt = new NBT();
nbt.set("name", "Yoyo");
nbt.set("nested", { values: [1, 2, 3] });
mc.log(nbt.toString());

const form = mc.newSimpleForm("Title", "Content");
form.addButton("OK");
mc.log(form.toString());
```

### API 兼容矩阵

| 功能 | 状态 |
|---|---|
| `onPreJoin` / `onJoin` / `onChat` / `onLeft` | 实现（两端均实测） |
| 其他 LLSE 事件 | 未实现；`mc.listen` 会警告，不会触发 |
| 玩家 name / xuid / address / runtimeId、`mc.getPlayer`、`mc.getOnlinePlayers` | 实现（基于事件注册表） |
| `player.sendMessage` / `kick` / `teleport` / `getPosition` / `getDimension` | 未实现，明确抛错 |
| NBT：get/set/getTag/setTag/keys/getType/toJSON/toString/二进制 hex | 实现 |
| 表单数据模型：SimpleForm / CustomForm / ModalForm、按钮/输入/下拉/滑条、toJSON | 实现 |
| `form.sendTo` | 未实现，明确抛错（BDS form packet bridge 尚未接入） |
| Item / Block / Container / Entity 数据模型 | 实现（本地数据，不连接实时世界） |
| `mc.getBlock` / `mc.getContainer` / `mc.getEntity` | 未实现，明确抛错 |
| `mc.log` / `mc.getServerVersion` / `mc.getModuleBase` | 实现 |
| `mc.broadcast` / `mc.runcmd` / `mc.runcmdEx` | 未实现，明确抛错 |
| C++ / Go 插件事件 ABI | 保持兼容 |

> 上表描述的是 YoyoLoader **原生 JS API**。完整的实时 Player / Block / Item / Container / Entity / Forms / Scoreboard 请使用 **Endstone 后端**（见 2.5 节和 [docs/ENDSTONE_BACKEND.md](docs/ENDSTONE_BACKEND.md)）。原生 API 未实现的部分仍会抛出带说明的错误，不会返回伪造数据。详细审计见 [docs/API_COVERAGE.md](docs/API_COVERAGE.md)。

### 自带测试插件

**Endstone 后端插件**（完整 live API）：

- [endstone_plugins/yoyo_title](endstone_plugins/yoyo_title)：`PlayerChatEvent` 修改聊天内容，wheel 已构建。
- [endstone_plugins/yoyo_sidebar](endstone_plugins/yoyo_sidebar)：真实 `SIDE_BAR` scoreboard，TPS/时间/玩家/实体/掉落物每秒刷新。
- 一键安装见 2.5 节或 [docs/ENDSTONE_BACKEND.md](docs/ENDSTONE_BACKEND.md)。

**YoyoLoader 原生称号插件**：[plugins/title.js](plugins/title.js)。复制到 `<server>/plugins/title.js`，编辑 `TITLES` 表；它在 `onChat` 里设置 `event.message`，runtime 会把新消息写回原生 TextPacket，再交给 BDS 广播。

```js
const TITLES = { "YoyoTest": "[管理员]" };
mc.listen("onChat", (player, message, event) => {
    const title = TITLES[player.name] || "[玩家]";
    event.message = title + " " + message;
});
```

插件加载时会执行 `mc.__simulateChat` 和 `mc.__rewriteNative` 自测，Windows/Linux 日志分别为 `[管理员] hello world`、`short=ok long=ok`。真实聊天链路仍需要真实客户端验证：测试 bot 发出的 TextPacket 在到达 handler 前就被 BDS 丢弃。

**YoyoLoader 原生侧边栏（行为包）**：[plugins/sidebar_pack](plugins/sidebar_pack)。它是 BDS 原生行为包（`@minecraft/server`），每秒更新侧边栏：

```bash
python scripts/install_sidebar.py --server /path/to/bedrock-server
```

显示内容：

```text
YoyoLoader
  TPS       20
  TIME      630     # 游戏时间 HHMM
  PLAYERS   1
  ENTITIES  3
  ITEMS     0
```

已在 Windows/Linux BDS 1.26.52.3 实测；Windows 下连接 bot 后日志为 `players=1 entities=3`。安装脚本会把行为包复制到 `behavior_packs/yoyo_sidebar` 并加入 `world_behavior_packs.json`。

## 6. 实现原理（不依赖符号文件）

1. **识别版本**：Windows 从 PE 的 CodeView/RSDS 读取 GUID + age；Linux 从 ELF `PT_NOTE` 读取 GNU build-id。没有符号文件、没有 PDB、没有 DWARF。
2. **版本表**：`src/runtime/version.cpp` 中按 build id 保存 RVA 和函数序言。未知 build 直接拒绝安装 hook，避免错位 patch。
3. **inline hook**：把目标函数前 N 字节的固定序言复制到 trampoline，再写入 `FF 25 ... + abs64` 绝对跳转。序言在 patch 前逐字节校验；不使用反汇编器，因此没有 Capstone/Zydis 依赖。
4. **handler inline hook**：`SetLocalPlayerAsInitialized` 和 `TextPacket` 的 handler 直接按 RVA 做 inline hook；vtable 只用于逆向阶段定位函数，不参与最终 patch。
5. **跨 STL 边界不传递 C++ 对象**：runtime 自己可以用任意 stdlib 编译；读取 BDS 的 `std::string` 时按 ABI 手工解析：
   - MSVC：32 字节，size @ +16，capacity @ +24，短字符串数据 @ +0，长字符串指针 @ +0；
   - libc++（LLVM 20）：24 字节，短字符串 size = byte0 >> 1、数据 @ +1；长字符串 size @ +8、指针 @ +16。
6. **聊天/组包布局**：`Packet` 基类 48 字节；`TextPacketPayload.body` 在 MSVC 下位于 payload + 112，在 libc++ 下位于 payload + 88；`AuthorAndMessage` 的 author @ body+8，message @ body+8+string_size。两套布局都用本地测试程序验证过。

更详细的逆向记录见 [docs/RE_NOTES.md](docs/RE_NOTES.md)。

## 7. 版本升级流程

BDS 小版本更新后，RVA 会变，但 build id 也会变。流程：

1. 把新 `bedrock_server` 放进独立目录，运行一次 `yoyo`，`yoyo.log` 会打印新的 build id 并拒绝加载。
2. 用 REA（Ghidra/IDA 的 MCP 服务）或直接 Ghidra 定位四个目标：
   - Linux 可以先用 RTTI 名字字符串找到 `ServerNetworkHandler` 的 typeinfo → vtable；
   - Windows 没有游戏类 RTTI，使用字符串 xref（例如 `ServerNetworkHandler::_onPlayerLeft`、源文件路径）和已知函数调用关系；
   - Endstone 的 `scripts/dump_symbols.py` + `scripts/configs/{windows,linux}.toml` 是很好的签名模板（Apache-2.0）。
3. 记录新的 RVA 和函数前 14–28 字节序言，更新 `src/runtime/version.cpp`。序言必须结束在完整指令边界，且不要包含 RIP-relative 或 rel8/rel32 跳转。
4. 重新构建，先在测试服启动，确认 `yoyo.log` 中的四条 “inline hook installed” 都出现。
5. 对 Windows 的两个 packet handler，用真实客户端连接一次，确认 `onJoin` / `onChat` 触发。

## 8. 限制与安全

- 仅支持 1.26.52.3；未知 build 不会安装任何 hook。
- Windows 的 `onJoin` 已用离线 bot 实机验证（player_id 正确）；`onChat` 的 handler 已安装并经 Ghidra 反编译确认，但测试 bot 发出的聊天包被 BDS 丢弃，需要用真实客户端再确认一次。
- Linux 的 `onJoin` 已用离线 bot 实机验证（player_id 正确）。`onChat` 的 handler 已安装，但测试 bot 发出的聊天包被 BDS 丢弃，需要用真实客户端再确认一次。
- `onLeft` 目前挂在 `disconnectClientWithMessage` 上，适合踢人/超时等路径；客户端正常退出可能走另一条函数，后续会补 `_onPlayerLeft` / `ServerPlayer::disconnect`。
- `onJoin` / `onLeft` 目前不一定能拿到 `ServerPlayer*`：join 包只有 ActorRuntimeID，left 只有 NetworkIdentifier。chat 通过 TextPacket 里的 xuid 回查 pre_join 缓存，通常能拿到玩家句柄。
- `pre_join` 的 IP 字段暂为空；`PlayerAuthenticationInfo` 里没有直接地址，需要后续从 `ConnectionRequest` 的 token 中取。
- JS 的表单 `sendTo`、实时方块/物品/容器/实体读取、`player.sendMessage` / `kick` / `teleport` 尚未接入 BDS，会明确抛错；不要把它们当成已实现功能。
- JS 回调在游戏/网络线程同步执行，并持有引擎互斥锁；QuickJS 内存上限 64 MB。不要在回调里 sleep、做阻塞 I/O 或长循环。
- 强制 trace / 反作弊软件可能阻止 `CreateRemoteThread` 或 `LD_PRELOAD`；这不是本项目要对抗的目标。
- BDS 的 EULA / 许可对逆向和再分发有限制。请只分发你自己的 loader，不要重新分发修改后的 `bedrock_server`。

## 9. 第三方与致谢

- 1.26.52 的符号偏移与 fake header 交叉参考了 [Endstone](https://github.com/EndstoneMC/endstone)（Apache-2.0）公开的 `src/bedrock/symbols` 和 `scripts/configs`。
- JavaScript 引擎使用 [QuickJS-ng](https://github.com/quickjs-ng/quickjs) 0.17.0（MIT），源码已放在 `third_party/quickjs`。
- 设计思路参考了 Endstone 的 Windows `CreateProcessWithDlls` / Linux `LD_PRELOAD` 加载方式。
- 没有使用 LeviLamina 的 PreLoader、PeEditor 或任何闭源符号文件；hook 地址来自本项目自己的版本表。
