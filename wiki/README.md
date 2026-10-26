# YoyoLoader Wiki

YoyoLoader 的完整文档。API 与事件有意向
[LegacyScriptEngine](https://docs.levimc.org/LegacyScriptEngine/zh/apis/) 对齐，
让为 LLSE 写的插件尽量不用改写就能运行。

## 目录

| 页面 | 内容 |
|---|---|
| [快速开始](getting-started.md) | 安装、启动、第一条插件 |
| [插件格式](plugin-format.md) | `manifest.json`、目录结构、依赖与循环检测 |
| [事件](events.md) | 全部事件、参数、取消规则 |
| [玩家对象](player.md) | Player 属性与方法 |
| [世界与方块](world.md) | 时间、天气、粒子、方块 |
| [命令](commands.md) | `mc.regPlayerCmd` 真实命令注册 |
| [表单](forms.md) | SimpleForm / CustomForm / ModalForm |
| [配置与文件](storage.md) | JsonConfigFile / IniConfigFile / File |
| [NBT](nbt.md) | NBT 对象与序列化 |
| [版本支持](versions.md) | 支持矩阵与降级行为 |
| [C++ / Go 插件](native-plugins.md) | C ABI v3 |

## 设计取舍

YoyoLoader 走外部注入路线，不是官方 Script API 的实现。能力边界比官方插件 API 大
（可接管 packet、多进程世界、发送真实客户端包），代价是版本绑定。

因此有一条硬规则：**崩溃比缺失功能更糟**。任何无法安全实现的能力都会抛出具名错误，
而不是静默返回错误数据。详见各页面的「未实现」小节。
