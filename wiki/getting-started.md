# 快速开始

## 安装

把以下文件放到 BDS 目录：

```text
bedrock_server.exe / bedrock_server
yoyo.exe / yoyo
yoyo_runtime.dll / libyoyo_runtime.so
```

Windows 启动：

```bat
yoyo.exe
```

Linux 启动：

```bash
./yoyo
```

启动器会在当前目录查找 `bedrock_server(.exe)`，找不到则回退到自身目录。

## 第一条插件

创建 `plugins/hello/main.js`：

```js
mc.log("[hello] loaded");

mc.listen("onJoin", (pl) => {
    pl.tell("§a欢迎，" + pl.name);
    pl.sendTitle("§e欢迎", pl.name, 10, 70, 20);
});
```

创建 `plugins/hello/manifest.json`：

```json
{
  "name": "hello",
  "version": "1.0.0",
  "engine": "javascript",
  "entry": "main.js"
}
```

或者直接放 `plugins/hello.js`——首次启动会自动创建目录和 manifest。

## 第一条命令

```js
mc.regPlayerCmd("ping", "测试", (pl, args, out) => {
    pl.tell("§apong " + args);
    out.success();
});
```

游戏里输入 `/ping hello` → 收到 `pong hello`。

## 查看日志

```text
yoyo.log
```

包含：版本识别、hook 安装结果、受限功能清单、插件加载结果、脚本异常。

## 下一步

- [插件格式](plugin-format.md) — 依赖、隔离、自动打包
- [事件](events.md) — 全部事件
- [命令](commands.md) — 真实命令注册
- [版本支持](versions.md) — 受限功能与降级
