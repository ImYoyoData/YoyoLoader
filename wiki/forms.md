# 表单

## SimpleForm — 按钮列表

```js
const form = mc.newSimpleForm();
form.setTitle("选择功能");
form.setContent("请选择一个操作");
form.addButton("按钮 1");
form.addButton("按钮 2", "textures/items/apple");   // 第二个参数是图标

pl.sendForm(form, (player, id, reason) => {
    // id 是被点击的按钮索引；null 表示玩家关闭了表单
    // reason: 0 玩家关闭, 1 玩家忙碌
    if (id === 0) player.tell("你点了按钮 1");
});
```

也可以用构造函数：

```js
const form = new SimpleForm("标题", "内容");
```

## CustomForm — 输入控件

```js
const form = mc.newCustomForm();
form.setTitle("玩家设置");
form.addLabel("请填写以下内容");
form.addInput("昵称", "输入你的昵称", "默认值");
form.addToggle("开启飞行", true);
form.addDropdown("模式", ["生存", "创造", "冒险"], 0);
form.addSlider("速度", 0, 10, 1, 5);
form.addStepSlider("难度", ["简单", "普通", "困难"], 1);

pl.sendForm(form, (player, data, reason) => {
    // data 是数组，顺序与控件一致；null 表示取消
    if (data === null) return;
    player.tell("昵称=" + data[1]);
});
```

| 方法 | 参数 |
|---|---|
| `addLabel(text)` | 标签 |
| `addInput(title[, placeholder, default])` | 文本输入 |
| `addToggle(title[, default])` | 开关 |
| `addDropdown(title, options[, default])` | 下拉 |
| `addSlider(title, min, max[, step, default])` | 滑块 |
| `addStepSlider(title, options[, default])` | 分段滑块 |

## ModalForm — 确认框

```js
const form = mc.newModalForm();
form.setTitle("确认");
form.setContent("要执行这个操作吗？");
form.setConfirm("确定");
form.setCancel("取消");

pl.sendForm(form, (player, result, reason) => {
    // result: true 确认, false 取消, null 关闭
});

// 快捷方式
pl.sendModalForm("确认", "要继续吗？", "是", "否", (player, result) => {
    if (result === true) player.tell("已确认");
});
```

## 表单 ID 与回调

每次 `sendForm` 会分配新的表单 ID。回调按 ID 精确匹配，因此同一表单对象可以
同时发给多个玩家而不会串消息。

全局事件 `onFormResponse` 也会收到所有响应：

```js
mc.listen("onFormResponse", (player, formId, response) => {
    mc.log("form " + formId + " -> " + response);
});
```

## 能力要求

表单依赖 `ModalFormResponsePacket` 的字段偏移，在受限版本上会抛错：

```text
onFormResponse is not verified for this BDS build
```

## 未实现

表单回调目前通过表单 ID 映射，尚无 `form.onResponse` 对象级事件（用 `sendForm`
的第二个参数代替）。
