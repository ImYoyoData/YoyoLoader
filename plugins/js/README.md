# YoyoLoader JavaScript API

YoyoLoader embeds **QuickJS-ng 0.17.0** (MIT) in `yoyo_runtime`. Any `*.js` file
under `<server>/plugins/` is evaluated at startup with a global `mc` object.

```text
<server>/
  yoyo.exe / yoyo
  yoyo_runtime.dll / libyoyo_runtime.so
  plugins/
    live_demo.js
    title.js
```

## Events

```js
mc.listen("onPreJoin", (player, event) => {
    // event.cancelled = true; // reject the login
});
mc.listen("onJoin", (player) => {});
mc.listen("onChat", (player, message, event) => {
    // event.cancelled = true;               // drop the chat packet
    // event.message = "[Title] " + message; // rewrite before BDS broadcasts it
});
mc.listen("onLeft", (player, reason) => {});
mc.listen("onServerReady", (event) => {
    // event.serverVersion, event.moduleBase
});
```

Aliases: `pre_join`, `join`, `chat`, `left`.

`onLeft` is driven by `ServerNetworkHandler::_onPlayerLeft` and fires for normal
quits, kicks and timeouts. `onChat` rewrites the native TextPacket string in
place; if the new text does not fit, the original is kept.

Other LLSE event names are accepted by `mc.listen` but only log a warning; they
do not fire in this build.

## Player

Properties: `name`, `xuid`, `address`, `runtimeId`, `isOnline`.

```js
// messages (real BDS packets)
player.sendMessage("hello");
player.sendTitle("title", "subtitle", 10, 70, 20);
player.sendTip("tip");
player.sendPopup("popup");
player.sendSystemMessage("system");
player.sendToast("title", "content");
player.sendActionBar("actionbar");
player.resetTitle();

// geometry
const pos = player.getPosition();   // { x, y, z } eye position
const loc = player.getLocation();   // feet position + dimension + dimensionName + pitch + yaw
const rot = player.getRotation();   // { pitch, yaw }
player.teleport({ x: 100, y: 64, z: 100, dimension: 0 });
player.getDimension();              // 0 overworld, 1 nether, 2 the_end
player.getDimensionName();

// identity / state
player.getRuntimeId();
player.getUniqueId();               // raw ActorUniqueID (may be negative)
player.getGameMode();               // 0 survival, 1 creative, 2 adventure, 6 spectator
player.getGameModeName();
player.setGameMode("creative");     // or a number

// moderation
player.kick("reason");
```

Aliases: `send_message`, `send_title`, `send_tip`, `send_popup`, `send_toast`,
`send_actionbar`, `reset_title`, `get_position`, `get_location`,
`get_dimension`, `get_game_mode`, `set_game_mode`.

`teleport` currently supports same-dimension moves only; it returns `false` for
cross-dimension requests. `getGameMode` prefers the live GameType component and
falls back to the value set through `setGameMode` (default 0) if the component
cannot be read.

## Server / level helpers

```js
mc.getPlayer("Steve");
mc.getOnlinePlayers();
mc.getPlayers();
mc.getOnlinePlayerCount();
mc.broadcast("hello everyone");      // real TextPacket to all online players
mc.getLevel();                       // lightweight level view
mc.log("message");
mc.getServerVersion();               // build id
mc.getModuleBase();
```

## NBT

```js
const nbt = new NBT();
nbt.set("name", "Yoyo");
nbt.set("count", 3);
nbt.set("nested", { enabled: true, values: [1, 2, 3] });
nbt.get("nested").values;          // [1, 2, 3]
nbt.getType("count");              // "int"
nbt.keys();                        // ["name", "count", "nested"]
nbt.setTag("child", new NBT());
nbt.getTag("child");
nbt.remove("name");
nbt.toJSON();
nbt.toString();
nbt.toBinaryHex();
NBT.parse(objectOrJsonString);
NBT.parseBinary(hex);
NBT.fromHex(hex);
nbt.setByteArray(key, [1, 2, 3]);
nbt.setIntArray(key, [1, 2, 3]);
nbt.setLongArray(key, [1, 2, 3]);
```

Type inference for `set`: boolean -> byte, integer -> int/long, other number ->
double, string -> string, array -> list, object -> compound.

## Forms

```js
const form = mc.newSimpleForm("Title", "Content");
form.addButton("OK", "textures/ui/check");
form.setTitle("New title");
form.setContent("New content");
form.sendTo(player);          // sends a real ModalFormRequestPacket
player.sendForm(form);        // same thing

const custom = mc.newCustomForm("Title");
custom.addLabel("Label");
custom.addInput("Name", "placeholder", "default");
custom.addToggle("Flag", true);
custom.addDropdown("Pick", ["a", "b"], 0);
custom.addSlider("Amount", 0, 10, 1, 5);
custom.addStepSlider("Steps", ["a", "b", "c"], 0);
custom.sendTo(player);

const modal = mc.newModalForm("Title", "Content");
modal.setConfirm("Yes");
modal.setCancel("No");
modal.sendTo(player);
```

Global constructors `SimpleForm`, `CustomForm`, `ModalForm` are also available.
Form **responses** are not connected to JS callbacks yet.

## Item / Block / Container / Entity (local data models)

```js
const item = mc.newItem("minecraft:apple", 3, nbt);
item.getType(); item.getCount(); item.setCount(5);
item.getNBT(); item.setNBT(nbt);
item.isNull(); item.getMaxStackSize();

const block = mc.newBlock({ x: 1, y: 64, z: 2 }, "minecraft:stone", nbt);
block.getType(); block.getPos(); block.getNBT(); block.setNBT(nbt);

const container = mc.newContainer(9);
container.addItem(item);
container.getItem(0);
container.setItem(0, item);
container.removeItem(0);
container.getAllItems();
container.isEmpty(); container.clear();

const entity = mc.newEntity("Yoyo", { x: 0, y: 64, z: 0 }, 20);
entity.getName(); entity.getHealth(); entity.setHealth(10); entity.getPos();
```

These are **local data models**. `mc.getBlock`, `mc.getContainer` and
`mc.getEntity` are not connected to the live world yet and throw a descriptive
error. `mc.runcmd` and `mc.runcmdEx` are also not implemented.

## Threading

Event callbacks run synchronously on the server/network thread that fired the
event, under the JS engine mutex. Keep callbacks short; do not sleep, perform
blocking I/O, or run long loops. The QuickJS memory limit is 64 MB.
