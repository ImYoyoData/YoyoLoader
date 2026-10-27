# 更新日志

所有版本的主要变化。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

## [0.2.0] — 未发布

本版重点是把"能不能用"讲清楚：新增原生对象访问层，并把已经验证做不到的部分
明确关闭并写进文档，避免插件在崩溃和静默失败之间踩雷。

### 新增

- **组件类型哈希可从类型名推导**。BDS 的 EnTT 组件 id 就是 C++ 类型名的
  FNV-1a/32 哈希，在真实服务器上遍历 467 个活组件池、与 381 个类型名比对，
  433 个精确命中。新增组件不再需要扫描二进制，跨版本也能用。
  （`components.h::component_hash`）
- **跨平台 STL 读取器**。`std::vector` 在 MSVC 与 libc++ 下都是三个指针，
  一个读取器通用；`std::string` 两者不同，仍走 `native_string.{h,cpp}`。
  （`stl_layout.h`）
- **`ContainerComponent` / `ItemStack` 布局**，按平台分别记录进版本表：
  - Windows：组件 512 字节，槽位数组 `+0x1a8`
  - Linux：组件 416 字节，槽位数组 `+0x150`
  - `ItemStack` 两边都是 152 字节，数量 `+0x22`、有效标志 `+0x23`、aux `+0x20`
  - 以上用于**方块实体容器**（箱子、漏斗等）。
- **内存映射可读范围查询**（`memory_span.{h,cpp}`）。变长数据包与游戏内指针
  在解引用前先问操作系统"这段能读多长"，不再用固定长度硬切。
- **动态内容独立模块**（`dynamic_content.{h,cpp}`），与原生核心完全分离，
  启动时单独打印能力报告。
- **启动能力报告**新增两行：原生容器访问状态、动态内容能力。
- 新增 `docs/NATIVE_ACCESS.md`：说明原生栈能做什么、为什么某些事做不了。

### 修复

- **EnTT 组件查找越界（会崩服）**。`entt_try_get` 拿 bucket 里的下标直接索引
  entries 数组，从不校验上界；探测链上遇到脏 bucket 就是野读。现在每个版本都记录
  数组末尾指针，越界按未命中处理。
- **`native_inventory_discover` 扫内存致崩（已移除）**。原先想靠扫 `ServerPlayer`
  找 `vector<ItemStack>`，假阳性候选解引用野指针把服务器打崩。按项目规则
  "崩溃比缺失功能更糟"删掉扫描，函数只输出结论。
- **`dynamic_content_report` 自死锁**。写报告时持锁，内部又调用了要加锁的探测函数，
  导致注入后卡在启动阶段。改为递归锁。
- **`mc.runcmd` / `pl.runcmd` 报错信息**改为说明具体原因并指向正确替代方案。

### 变更

- **`mc.regPlayerCmd` 明确是假命令**。实现一直是"在 BDS 解析命令前拦下
  `CommandRequestPacket`"，本次把文档、头文件注释、能力表全部改成这个说法，
  不再出现"真实命令注册"这类会误导人的描述。客户端不会补全、`/help` 里也没有，
  这是设计如此，不是 bug。
- 客户端命令表（`client_commands.*`）保持**禁用**，并在注释里写明禁用原因是
  改写 `AvailableCommandsPacket` 会破坏包内的 `std::vector` 成员、导致加入时崩服。

### 已知限制（有据可查的"做不到"）

- **玩家背包不是 EnTT 组件**。真实玩家上遍历 458 个池，`ContainerComponent` 不在其中；
  背包在 `PlayerInventory` 里，是 `ServerPlayer` 的成员对象，字段偏移未确定。
  因此 `getInventory` / `getArmor` / `getEnderChest` / `giveItem` 的原生路径不可用。
- **背包数据包解析结果不采用**。线格式按权威协议实现且有边界检查，但实测没有任何
  payload 起点能让槽位数等于该窗口规定值（背包 36 / 盔甲 4 / 副手 1），说明 payload
  成员偏移尚未定位。解析器保留，暂不供数据给插件。
- **注册新的物品 / 生物 / 材质 / 合成表**运行时不可能，只能通过行为包。
  `dynamic_content` 把这四项固定为 `false` 并在提示里说明原因。
- `pl.runcmd`、玩家 NBT 仍不可用（`CommandOrigin` 与玩家 NBT 序列化布局未验证）。
- 1.26.70.21 仍为部分支持：join/leave/chat 的 hook 目标已定位，
  对象布局与数据包字段偏移尚未在该版本上验证。

### 文档

- `docs/RE_NOTES.md` 新增第 11 节：哈希推导、EnTT 查找路径、容器布局、
  以及"玩家背包不是组件"这一负面结论的完整证据。
- `docs/NATIVE_ACCESS.md` 新增：两条路径的边界、能力矩阵、对插件作者的建议。
- `docs/API_COVERAGE.md`：把假命令、背包、NBT、runcmd 的状态写准确。
- `wiki/commands.md` 重写，标题由"真实命令注册"改为"假命令（聊天拦截）"。

## [0.1.0] — 已发布

首个可用版本。

- 注入器（Windows `CreateProcess` 挂起注入 / Linux `LD_PRELOAD`），自动定位服务器二进制
- 内联 hook 引擎：14 字节绝对跳转，12 字节短跳转 + 短 jcc 提升
- 三级跨版本符号解析：精确 RVA + 序言校验 → 字节特征扫描 → 结构探针
- 版本能力矩阵，未验证的能力默认关闭并在启动时打印
- 事件：`onPreJoin`、`onJoin`、`onLeft`、`onChat`、`onTick` 及若干数据包级事件
- JS 插件：每插件独立 `JSContext`（崩溃隔离）、`manifest.json`、依赖拓扑排序与环检测
- Go / C++ 插件（C ABI）
- `mc` / `nbt` / `forms` 命名空间，LLSE 兼容的位置/方块/方向对象
- 配置与文件类：`JsonConfigFile`、`IniConfigFile`、`File`（目录沙箱）
- 定时器（`setTimeout` / `setInterval`），由 `Level::tick` 驱动
- 官方 Script API 桥接：部署行为包，暴露 `@minecraft/server`
- 多世界（多进程）与 `TransferPacket` 切换
- 发布流水线：GitHub Actions 构建 Windows/Linux 压缩包并校验内容
