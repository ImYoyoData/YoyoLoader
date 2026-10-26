# 配置与文件

## JsonConfigFile

```js
const cfg = new JsonConfigFile("plugins/data/config.json", '{"seed":1}');
```

| 方法 | 说明 |
|---|---|
| `init(name, default)` | 不存在则写入默认值，返回存储值 |
| `set(name, value)` | 写入，返回 Boolean |
| `get(name[, default])` | 读取，缺失返回 default |
| `delete(name)` | 删除键 |
| `getPath()` | 文件路径 |
| `read()` | 整个文件文本 |
| `write(content)` | 覆盖写入 |
| `reload()` | 重新读取 |
| `close()` | 关闭 |

值可以是整数、浮点、字符串、布尔、数组、对象（可嵌套）。

```js
const all = cfg.get("players", {});
all[pl.xuid] = { home: { x: 0, y: 64, z: 0 } };
cfg.set("players", all);
```

**注意**：`get` 每次返回新的 JS 对象。要修改嵌套结构，必须取出 → 修改 → `set` 写回。

## IniConfigFile

```js
const ini = new IniConfigFile("plugins/config.ini");
ini.set("main", "port", "19132");
ini.getStr("main", "port", "0");
ini.getInt("main", "port", 0);
ini.getFloat("main", "ratio", 0);
ini.getBool("main", "debug", false);
ini.init("main", "version", 1);
ini.delete("main", "port");
```

## File

```js
const f = new File("plugins/data/log.txt", "w");
f.writeLineSync("第一行");
f.close();

const r = new File("plugins/data/log.txt", "r");
r.readLineSync();      // "第一行"，末尾返回 null
r.readSync(16);        // 最多 16 字节
r.readAllSync();       // 全部
r.seekTo(0, false);
r.isEOF();
r.flush();
r.close();
```

模式：`"r"` 读、`"w"` 写、`"a"` 追加。第三个参数 `true` 为二进制模式。
带回调的 `read/write/readLine/writeLine` 同步执行回调。

## 路径安全

所有路径都限制在服务器目录内：

- 绝对路径被拒绝
- `..` 逃逸被拒绝

```js
new JsonConfigFile("../../../etc/passwd");  // 抛出错误
```

## 每个插件的数据目录

约定把插件数据放在 `plugins/data/<插件名>/`。`plugins/data` 不会当作插件目录扫描。

```js
const cfg = new JsonConfigFile("plugins/data/home/homes.json");
```

## 插件元信息

每个插件可以读取自己的身份：

```js
__plugin.name;     // "home"
__plugin.version;  // "1.0.0"
__plugin.engine;   // "javascript"
```
