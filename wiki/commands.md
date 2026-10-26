# 命令

## mc.regPlayerCmd — 真实命令注册

```js
mc.regPlayerCmd(name, description, callback[, level])
```

| 参数 | 类型 | 说明 |
|---|---|---|
| `name` | String | 命令名，不带 `/` |
| `description` | String | 说明 |
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
2. YoyoLoader 在包的 handler 层拦截
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

## 未实现

| API | 原因 |
|---|---|
| `mc.runcmd` / `mc.runcmdEx` | 需要 `CommandOrigin` 内核结构。用命令包注入能执行命令，但随后服务器崩溃（引擎持有无法从外部复现的状态），因此拒绝而不是交付会崩服的功能 |
| `mc.newCommand` | 同上，依赖 `CommandRegistry` |
| `mc.regConsoleCmd` | 已注册但需要控制台输入 hook 才会触发 |

插件级命令（`mc.regPlayerCmd`）**完全可用**，是推荐的命令方式。
