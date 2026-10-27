# YoyoLoader

YoyoLoader 是 Minecraft Bedrock Dedicated Server 的注入式 mod loader。
它不修改原版 `bedrock_server`，而是用启动器把运行时注入进程，提供事件、
脚本插件和跨平台支持。

## 快速开始

1. 从 [Releases](https://github.com/ImYoyoData/YoyoLoader/releases) 下载对应平台的压缩包
2. **直接解压到 BDS 服务器根目录**
3. 运行 `yoyo.exe`（Windows）或 `./yoyo`（Linux）代替原版服务器程序

启动器会自动找到 `bedrock_server(.exe)` 并注入运行时。压缩包自带三个示例插件。

## 文档导航

| 页面 | 内容 |
|---|---|
| [快速开始](getting-started) | 安装、启动、第一条插件 |
| [插件格式](plugin-format) | `manifest.json`、目录结构、依赖与循环检测、隔离 |
| [事件](events) | 全部事件、参数、取消规则 |
| [玩家对象](player) | Player 属性与方法 |
| [世界与方块](world) | 时间、天气、粒子、方块 |
| [命令](commands) | 命令注册与实现原理 |
| [表单](forms) | SimpleForm / CustomForm / ModalForm |
| [配置与文件](storage) | JsonConfigFile / IniConfigFile / File |
| [NBT](nbt) | NBT 对象与序列化 |
| [版本支持](versions) | 支持矩阵、降级行为、实测记录 |
| [C++ / Go 插件](native-plugins) | C ABI v3 |
| [发布流程](releasing) | 分支约定、GitHub Actions、验证清单 |

## 与 LegacyScriptEngine 的关系

YoyoLoader 的事件与脚本 API 有意向
[LegacyScriptEngine](https://docs.levimc.org/LegacyScriptEngine/zh/apis/) 对齐：
`mc.listen`、`player.sendMessage` / `sendTitle` / `sendForm`、
`setTimeout` / `setInterval`、`JsonConfigFile` / `IniConfigFile`、`File` 等
都沿用相同命名与调用形式，目的是让为 LLSE 写的插件尽量不用改写就能运行。

这不是逐字段复制：内部实现完全独立，只对齐插件作者看到的那一层。

## 版本支持

| BDS 版本 | 状态 |
|---|---|
| 1.26.52.3 | 完整支持（Windows / Linux） |
| 1.26.70.21 | 部分支持，启动日志会列出受限功能 |
| 其他版本 | 自动探测；无法验证的功能会被禁用并报告 |

## 设计原则

外部注入路线不是官方脚本 API，能力边界更大但版本绑定。因此有一条硬规则：

> **崩溃比缺失功能更糟。**

任何无法安全实现的能力都会抛出具名错误并说明原因，而不是静默返回错误数据
或让服务器崩溃。

## 仓库

- 源码：https://github.com/ImYoyoData/YoyoLoader
- 发布：https://github.com/ImYoyoData/YoyoLoader/releases
