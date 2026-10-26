# 版本支持

## 支持矩阵

| BDS 版本 | 平台 | Build ID | 状态 | 受限功能 |
|---|---|---|---|---|
| 1.26.52.3 | Windows x64 | `d9f9837c2108f3824c4c44205044422e1` | ✅ 完整 | 无 |
| 1.26.52.3 | Linux x64 | `c0ff36e2eacfc191f01e189237df71f139b5d04a` | ✅ 完整 | 无 |
| 1.26.70.21 | Windows x64 | `fd266068f00d540b4c4c44205044422e1` | 🟡 部分 | 无 `onPreJoin`、`onTick`、packet 事件；无位置/朝向/维度/游戏模式/生命值/传送 |
| 1.26.70.21 | Linux x64 | `519f9a2c3cdc31c85ef87dd2f128f79136f854d4` | 🟡 部分 | 仅 `onJoin` 与 packet 工厂 hook |
| 其它 | 任意 | 未知 | 🟡 自动探测 | 靠特征签名尝试定位 hook；对象相关能力默认关闭 |

## 启动报告

每次启动都会打印当前版本与受限功能：

```text
[INFO] version support: 1.26.70.21 (windows) build=fd266068...
[WARN] limited features on this build: pre_join event, tick event, packet receive events,
       player position/dimension, player rotation, player game mode read, player teleport, ...
[WARN] reason: 1.26.70.21 preview: join/leave/chat hook targets located;
       object layout and packet payload offsets are not verified on this build yet
```

## 三级定位机制

| 优先级 | 方式 | 适用 |
|---|---|---|
| 1 | build id 精确匹配 + RVA 表 + 序言校验 | 已知版本 |
| 2 | 特征签名扫描（唯一命中） | 已知版本序言变化、或未知版本 |
| 3 | 结构探针（断言字符串 / 指令特征） | 编译器栈帧模板变化 |

任一环节失败 → 只跳过该 hook，其它功能不受影响。

### 特征签名

每条特征以函数入口为锚点，只固定 opcode 和寄存器操作数：

- 栈帧大小、帧指针偏移、参数溢出偏移、相对位移 → 通配 `??`
- 必须唯一命中；命中后还要再验一次入口字节

实测（1.26.52.3 Windows）：7 条特征在 223MB 镜像中各自唯一，且都落在记录的 RVA 上。

### 对象布局运行时校验

函数位置能靠特征解决，字段偏移不能。所以首个真实玩家出现时会校验：

- 组件指针可读
- 位置数值有限
- pitch/yaw 合理
- 维度在 0..2

失败 → 永久禁用依赖布局的写操作（传送、设置游戏模式），读取保留。

## 降级行为

能力为 `false` 时调用会抛出具名错误：

```js
try {
    player.teleport({ x: 0, y: 64, z: 0 });
} catch (e) {
    // "teleport is not verified for this BDS build; see the startup capability report"
}
```

**不会**静默返回错误数据。

## 项目规则

完整约束见仓库根目录 [PROJECT_RULES.md](../PROJECT_RULES.md)。核心几条：

1. 禁止假设支持所有版本
2. 每个版本必须有明确条目；不确定的字段填 0 并标为不可用
3. 能力矩阵漏填视为 `false`
4. 崩溃比缺失功能更糟
