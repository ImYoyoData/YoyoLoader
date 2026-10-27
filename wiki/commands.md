# 命令

## mc.regPlayerCmd — 假命令（聊天拦截）

**这不是真的把命令注册进 BDS。** BDS 的命令表由 `CommandRegistry` 在服务端构建，
并通过 `AvailableCommandsPacket` 下发给客户端——YoyoLoader **不往那张表里写东西**，
也不会让客户端的命令补全列表里多出 `/home`。

它做的是"假命令"：客户端发来的 `CommandRequestPacket` 在到达 BDS 命令解析器**之前**
被拦下，命令名匹配插件注册表就执行回调、丢弃这个包。对玩家来说输入 `/home` 有反应，
但对 BDS 来说这条命令从未存在过。

这个区别有实际影响：客户端不会补全、`/help` 里看不到、其它插件/命令块也调不到。

```js
mc.regPlayerCmd(name, description, callback[, level])
```

| 参数 | 类型 | 说明 |
|---|---|---|
| `name` | String | 命令名，不带 `/` |
| `description` | String | 说明（仅用于插件自己展示，不会出现在客户端列表中） |
| `callback` | Function | `function(player, args, output)` |
| `level` | Number | 可选，`0` = 所有人，`>0` = 仅 OP（查 `yoyo_ops.txt`） |

### 回调参数

| 参数 | 类型 | 说明 |
|---|---|---|
| `player` | Player | 执行命令的玩家 |
| `args` | String | 命令名之后的全部内容 |
| `output` | Object | `output.success([msg])` / `output.error(msg)` |

**返回值**：返回字符串会作为消息发给玩家。

### 示例

```js
mc.regPlayerCmd("home", "家传送", (pl, args, out) => {
    const parts = args.split(/\s+/);
    if (parts[0] === "add") {
        pl.tell("§a已设置家");
        out.success();
    }
});
```

## 命令如何到达插件

真实客户端输入 `/home add` 时：

1. 客户端发送 `CommandRequestPacket`（id 77）
2. YoyoLoader 在包的 handler 层拦截（早于 BDS 的 `MinecraftCommands`）
3. 若命令名已通过 `mc.regPlayerCmd` 注册 → 执行插件回调，**丢弃该包**
4. 未注册 → 原样交给 BDS 自己的命令解析器

因此插件命令不会触发 vanilla 的 "Unknown command"，也不需要 `runcmd`。

## 聊天回退

部分客户端/测试工具把 `/home` 当普通聊天发送。插件可以同时监听 `onChat` 兜底：

```js
mc.listen("onChat", (pl, msg, ev) => {
    const text = String(msg || "").trim();
    if (text.charAt(0) !== "/") return;
    const name = text.slice(1).split(/\s+/)[0];
    if (name !== "home") return;
    handleHomeCommand(pl, text.slice(1), ev);
    ev.cancelled = true;   // 不再广播这条聊天
});
```

## 权限

`level > 0` 时检查 `yoyo_ops.txt`：

```text
2535405997872704
```

被拒绝时插件收到统一提示，不会执行回调。

## 为什么不做成真命令

真命令需要写入 BDS 的 `CommandRegistry`，有两处硬障碍：

1. **注册接口不可靠**。BDS 的命令注册是模板化的 `CommandRegistrar::getOrCreateCommand(...)`
   （LeviLamina 靠完整头文件/PDB 才能调用）。YoyoLoader 没有 PDB，模板签名无法凭空重建，
   二进制里也找不到一个可安全调用的 `registerCommand` 符号。
2. **向客户端下发命令表会崩服**。`AvailableCommandsPacket` 对象自身持有 `std::vector` 成员，
   直接往 payload 里写我们的字节会破坏这些成员，服务器在玩家加入时崩溃
   （见 `client_commands_ready()` 的注释）。

按 `PROJECT_RULES.md`（崩溃比缺失功能更糟），这条路径保持关闭。假命令没有这些问题：
它只是拦一个包，不碰引擎状态。

## 未实现

| API | 原因 |
|---|---|
| `mc.runcmd` / `mc.runcmdEx` | 需要 `CommandOrigin` 内核结构。用命令包注入能执行，但随后服务器崩溃（引擎持有无法从外部复现的状态），因此拒绝而不是交付会崩服的功能 |
| `mc.newCommand` | 同上，依赖 `CommandRegistry` |
| `mc.regConsoleCmd` | 已注册但需要控制台输入 hook 才会触发 |

**假命令（`mc.regPlayerCmd`）完全可用，是推荐的命令方式。** 需要真正的、对客户端可见的命令时，
用行为包（Script API 的 `system.beforeEvents.startup` 自定义命令），不要指望 YoyoLoader 去改
BDS 的命令表。
