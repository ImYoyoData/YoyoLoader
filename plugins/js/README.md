# YoyoLoader JavaScript API

YoyoLoader embeds **QuickJS-ng 0.17.0** (MIT) in `yoyo_runtime`. Any `*.js` file
under `<server>/plugins/` is evaluated at startup with a global `mc` object.

```text
<server>/
  yoyo.exe / yoyo
  yoyo_runtime.dll / libyoyo_runtime.so
  plugins/
    sample.js
```

## Events

```js
mc.listen("onPreJoin", (player) => { /* return false or event.cancelled = true to reject */ });
mc.listen("onJoin", (player) => {});
mc.listen("onChat", (player, message, event) => { /* event.cancelled = true */ });
mc.listen("onLeft", (player, reason) => {});
```

Aliases: `pre_join`, `join`, `chat`, `left`.

Only these four events are currently fired. `mc.listen` accepts other LLSE event
names but logs a warning; they will never fire in this build.

## Player

Properties: `name`, `xuid`, `address`, `runtimeId`, `isOnline`.

Implemented: identity fields, `toString()`, `mc.getPlayer(nameOrXuid)`,
`mc.getOnlinePlayers()`.

Not implemented (throws a descriptive `Error`): `sendMessage`, `kick`,
`getPosition`, `getDimension`, `teleport`, `getNbt`.

The registry is populated from `onPreJoin` / chat packets. `onJoin` currently has
the runtime ID but not the player name, and `onLeft` has no player identity.

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
nbt.toJSON();                      // object
nbt.toString();                    // JSON string
nbt.toBinaryHex();                 // Bedrock little-endian NBT bytes as hex
NBT.parse(objectOrJsonString);
NBT.parseBinary(hex);
NBT.fromHex(hex);
nbt.setByteArray(key, [1,2,3]);
nbt.setIntArray(key, [1,2,3]);
nbt.setLongArray(key, [1,2,3]);
```

Type inference for `set`: boolean -> byte, integer -> int/long, other number ->
double, string -> string, array -> list, object -> compound.

## Forms (data model)

```js
const form = mc.newSimpleForm("Title", "Content");
form.addButton("OK", "textures/ui/check");
form.setTitle("New title");
form.setContent("New content");
form.toString();      // JSON
form.toJSON();        // object
form.sendTo(player);  // throws: BDS form packet bridge not implemented yet

const custom = mc.newCustomForm("Title");
custom.addLabel("Label");
custom.addInput("Name", "placeholder", "default");
custom.addToggle("Flag", true);
custom.addDropdown("Pick", ["a", "b"], 0);
custom.addSlider("Amount", 0, 10, 1, 5);

const modal = mc.newModalForm("Title", "Content");
modal.setConfirm("Yes");
modal.setCancel("No");
```

Global constructors `SimpleForm`, `CustomForm`, `ModalForm` are also available.

## Item / Block / Container / Entity (data model)

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

These are **local data models**. `mc.getBlock`, `mc.getContainer`, `mc.getEntity`
are not connected to the live world yet and throw a descriptive error.

## Other `mc` functions

| Function | Status |
|---|---|
| `mc.log(...)` | implemented |
| `mc.listen(name, cb)` | implemented for the four events above |
| `mc.getPlayer(nameOrXuid)` | implemented from the event registry |
| `mc.getOnlinePlayers()` / `mc.getPlayers()` | implemented from the event registry |
| `mc.getOnlinePlayerCount()` | implemented from the event registry |
| `mc.getServerVersion()` | implemented (BDS build id) |
| `mc.getModuleBase()` | implemented |
| `mc.broadcast`, `mc.runcmd`, `mc.runcmdEx` | not implemented; throws |
| `mc.getBlock`, `mc.getContainer`, `mc.getEntity` | not implemented; throws |

## Threading

Event callbacks run synchronously on the game/network thread that fired the
event, under the JS engine mutex. Keep callbacks short; do not sleep or perform
blocking I/O. A long callback blocks the server thread.
