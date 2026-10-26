# NBT

## 创建与读写

```js
const nbt = new NBT();
nbt.set("name", "Yoyo");
nbt.set("count", 3);
nbt.set("ratio", 1.5);
nbt.set("enabled", true);
nbt.set("values", [1, 2, 3]);
nbt.set("nested", { inner: "value" });

nbt.get("name");       // "Yoyo"
nbt.getType("count");  // "int"
nbt.keys();            // ["name", "count", ...]
nbt.remove("name");
```

## 嵌套标签

```js
nbt.setTag("child", new NBT());
nbt.getTag("child");
```

## 序列化

```js
nbt.toJSON();        // JS 对象
nbt.toString();      // JSON 字符串
nbt.toBinaryHex();   // Bedrock 二进制 NBT 的十六进制
```

## 解析

```js
NBT.parse(objectOrJsonString);
NBT.fromJSON(jsonString);
NBT.parseBinary(hexString);
NBT.fromHex(hexString);
```

## 数组类型

```js
nbt.setByteArray("bytes", [1, 2, 3]);
nbt.setIntArray("ints", [1, 2, 3]);
nbt.setLongArray("longs", [1, 2, 3]);
```

## 类型

| 方法返回 | 说明 |
|---|---|
| `"byte"` | 1 字节整数 |
| `"short"` | 2 字节整数 |
| `"int"` | 4 字节整数 |
| `"long"` | 8 字节整数 |
| `"float"` / `"double"` | 浮点 |
| `"string"` | 字符串 |
| `"byteArray"` / `"intArray"` / `"longArray"` | 数组 |
| `"compound"` | 嵌套 |
| `"list"` | 列表 |

解析与序列化使用 Bedrock 小端 NBT 格式。

## 未实现

`player.getNbt()` / `player.setNbt()` 未实现：完整玩家 NBT 序列化是 BDS 中
版本差异最大的部分，硬解析会产生错乱数据。调用会抛出具名错误。

`NBT` 对象本身完全可用，可以用于自定义数据存储与二进制序列化。
