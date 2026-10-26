# 插件格式

## 目录结构

每个插件是 `plugins/` 下的一个目录：

```text
plugins/
  home/
    manifest.json
    main.js
  worldedit/
    manifest.json
    worldedit.dll      (Windows)
    worldedit.so       (Linux)
```

## manifest.json

```json
{
  "name": "home",
  "version": "1.0.0",
  "engine": "javascript",
  "entry": "main.js",
  "description": "家传送：/home add|del|list",
  "dependencies": [],
  "optionalDependencies": ["title"]
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `name` | 否 | 插件唯一名，默认取目录名。重名会被拒绝 |
| `version` | 否 | 版本号，默认 `0.0.0`，通过 `__plugin.version` 读取 |
| `engine` | 是 | `javascript` / `js` / `quickjs`，或 `native` |
| `entry` | **是** | 相对插件目录的入口文件；不存在则拒绝加载 |
| `description` | 否 | 说明文字 |
| `dependencies` | 否 | 强制依赖，缺失或失败则本插件不加载 |
| `optionalDependencies` | 否 | 可选依赖，存在时优先加载，不存在则忽略 |

## 引擎类型

| engine | 入口文件 | 说明 |
|---|---|---|
| `javascript` | `.js` | 每个插件运行在独立 QuickJS context |
| `native` | `.dll` / `.so` | C++ / Go 插件，导出 `yoyo_plugin_entry` |

## 松散 JS 文件自动打包

把一个 `.js` 直接放进 `plugins/`：

```text
plugins/my_script.js
```

首次启动时 YoyoLoader 会自动：

1. 创建 `plugins/my_script/`
2. 把脚本移动进去，重命名为 `my_script.js`
3. 生成 `manifest.json`（`name` = 文件名，`engine` = javascript）
4. 删除原来的松散文件

之后按普通插件处理。**`native` 插件必须用目录形式**，因为单个 `.dll` 无法携带 manifest。

## 依赖解析

加载顺序由拓扑排序决定：

```json
// A 依赖 B，B 依赖 C  =>  加载顺序 C, B, A
{"name": "a", "dependencies": ["b"]}
{"name": "b", "dependencies": ["c"]}
{"name": "c"}
```

### 循环依赖

```json
{"name": "x", "dependencies": ["y"]}
{"name": "y", "dependencies": ["x"]}
```

日志：

```text
[ERROR] plugin 'x' has a dependency cycle: x -> y -> x
[WARN]  plugin not loaded: x: dependency cycle (x -> y -> x)
[WARN]  plugin not loaded: y: dependency 'x' failed to load
```

**循环中的所有插件都不会加载**，其它插件不受影响。

### 缺失依赖

```text
[ERROR] plugin 'missing_dep' skipped: missing required dependency 'does_not_exist'
```

### 可选依赖

`optionalDependencies` 只影响加载顺序，永远不会阻止本插件加载：

```json
{"name": "tpa", "optionalDependencies": ["title"]}
```

`title` 存在 → 先加载 `title`；不存在 → `tpa` 照常加载。

## 插件隔离

**每个 JS 插件运行在独立的 QuickJS context。**

| 隔离项 | 效果 |
|---|---|
| 全局变量 | 各插件独立，同名函数/变量互不覆盖 |
| 异常 | 一个插件抛异常不影响其他插件，也不影响服务器 |
| 定时器 | 记录所属 context，回调在正确插件内执行 |
| 事件回调 | 逐个调用，每个都用自己 context + 独立异常处理 |

实测：一个插件在 `onTick` 里持续抛异常（687 次），服务器存活，其他插件的事件照常触发。

## 未实现 / 拒绝行为

以下情况会跳过插件并记录原因，不会导致服务器崩溃：

- `manifest.json` 不是合法 JSON
- 缺少 `entry` 或入口文件不存在
- 目录下没有 `manifest.json`（会提示）
- 插件名重复
- 依赖循环或缺失
