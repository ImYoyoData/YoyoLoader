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
mc.listen("onTick", (event) => {
    // event.tick: cumulative server tick
});
mc.listen("onPlayerMove", (player, from, to) => {
    // from/to: { x, y, z, dimension, dimensionName }
});
mc.listen("onPlayerChangeDimension", (player, fromDimension, toDimension) => {});
mc.listen("onPlayerJump", (player, event) => {});
mc.listen("onSneak", (player, sneaking, event) => {});
mc.listen("onChangeSprinting", (player, sprinting, event) => {});
mc.listen("onStartDestroyBlock", (player, pos, event) => {});
mc.listen("onDestroyBlock", (player, pos, event) => {});
mc.listen("onBlockInteracted", (player, pos, event) => {});
mc.listen("onUseItem", (player, event) => {});
mc.listen("onUseItemOn", (player, pos, event) => {});
mc.listen("onDropItem", (player, event) => {});
mc.listen("onBedEnter", (player, pos, event) => {});
mc.listen("onBedLeave", (player, pos, event) => {});
mc.listen("onPlayerCmd", (player, command, event) => {});
mc.listen("onFormResponse", (player, formId, response, event) => {});
mc.listen("onPlayerSkinChange", (player, uuid, event) => {});
mc.listen("onPlayerEmote", (player, pieceId, xuid, event) => {});
mc.listen("onPlayerRespawn", (player, event) => {});
mc.listen("onPlayerTeleport", (player, location, event) => {});
mc.listen("onPlayerGameModeChange", (player, mode, event) => {});
mc.listen("onPlayerItemHeld", (player, slot, previousSlot, event) => {});
mc.listen("onPlayerHealthChange", (player, health, event) => {});
mc.listen("onPlayerDie", (player, event) => {});
mc.listen("onPacketReceive", (player, packetId, event) => {});
mc.listen("onPacketSend", (player, packetId, event) => {});
```

Aliases: `pre_join`, `join`, `chat`, `left`, `server_ready`, `tick`,
`player_move`, `player_change_dimension`, `onJump`/`jump`, `onSneak`/`sneak`,
`onChangeSprinting`/`sprint`, `onCommand`, `onFormResponse`, `onHealthChange`,
`onDie`, `packet_receive`, `packet_send`.

`onLeft` is driven by `ServerNetworkHandler::_onPlayerLeft` and fires for normal
quits, kicks and timeouts. `onChat` rewrites the native TextPacket string in
place; if the new text does not fit, the original is kept.

`onTick` fires every server tick (20 times per second by default) and is
intended for short, cheap work. `onPlayerMove` is polled from the same tick
loop with a 0.01-block threshold; it fires at most once per tick per player.
`onPlayerChangeDimension` is emitted when the polled dimension changes.

Block/item actions are driven by `PlayerActionPacket`. Setting
`event.cancelled` drops that action, but YoyoLoader does not implement full
client-prediction or inventory-transaction rollback; test block/item
cancellation on a real client before using it on a production server.
`onPlayerHealthChange` and `onPlayerDie` need the outgoing `SetHealth` /
`DeathInfo` packets, so `getHealth()` returns `null` until the first one is
observed. `onPacketSend` skips high-volume chunk/level-sound packets.

Other LLSE event names are accepted by `mc.listen` but only log a warning; they
do not fire in this build.

## LegacyScriptEngine compatibility

YoyoLoader exposes an LLSE-shaped surface alongside its own API, so plugins
written against
[LegacyScriptEngine](https://docs.levimc.org/LegacyScriptEngine/zh/apis/) can
usually run unmodified.

**Positions**

```js
pl.pos        // FloatPos: x, y, z, dimid, dim ("主世界"), toString()
pl.feetPos    // FloatPos
pl.blockPos   // IntPos
pl.direction  // DirectionAngle: pitch, yaw, toFacing() -> 0..3

new IntPos(x, y, z, dimid)
new FloatPos(x, y, z, dimid)
new DirectionAngle(pitch, yaw)
```

`dimid` is `0` overworld, `1` nether, `2` end. `dim` is the LLSE display string.
`dimension` / `dimensionName` are kept as aliases for older YoyoLoader plugins.

**Player methods**

| LLSE | YoyoLoader equivalent | Status |
|---|---|---|
| `pl.tell(msg[, type])` | `sendMessage` | ✅ type 0/1/4/5 mapped to raw/tip/system/popup |
| `pl.sendText(msg[, type])` | `sendMessage` | ✅ |
| `pl.setTitle(text[, type[, fadeIn, stay, fadeOut]])` | `sendTitle` | ✅ type 0 title, 1 actionbar, 2 subtitle |
| `pl.sendToast(title, msg)` | `sendToast` | ✅ |
| `pl.getPos()` / `getFeetPos()` / `getBlockPos()` | — | ✅ |
| `pl.getDirection()` | `getRotation` | ✅ |
| `pl.isOP()` | — | ✅ checks `yoyo_ops.txt` |
| `pl.kick([msg])` | `kick` | ✅ |
| `pl.transServer(host, port)` | `transfer` | ✅ |
| `pl.setGameMode(mode)` | `setGameMode` | ✅ (capability-gated) |
| `pl.runcmd(cmd)` | — | ❌ throws a named error |
| `pl.getInventory()` / `giveItem` / `getArmor` | — | ❌ throws a named error |

**Items and blocks**

```js
const it = mc.newItem("minecraft:bread", 3);
it.type; it.name; it.id; it.count; it.maxCount; it.aux; it.damage;
it.getNbt(); it.setNbt(nbt); it.clone(); it.isNull();
```

Block objects passed to block events carry `name`, `type`, `id`, `pos`
(IntPos), `tileData`, `isAir`, `getType()`, `getPos()`.

**Cancellation**

LLSE cancels an interceptable event by **returning `false`** from the
listener. YoyoLoader supports exactly that, and also keeps the older
`event.cancelled = true` form:

```js
mc.listen("onChat", (pl, msg) => {
    if (msg === "!cancelme") return false;   // LLSE style — event is dropped
});
```

Verified on a real server: returning `false` drops the event, returning
anything else lets it through.

**Event name aliases**

`onServerStarted` → `onServerReady`, `onChangeDim` → `onPlayerChangeDimension`,
`onexit` → `onLeft`, `onMobDie` → `onPlayerDie` (player deaths only).

**Unimplemented LLSE APIs** (`mc.runcmd`, `mc.getBlock`, `mc.setBlock`,
`mc.spawnParticle`, `mc.getTime`, `mc.setTime`, `mc.getWeather`, `mc.setWeather`,
`mc.getPlayerNbt`, `mc.setPlayerNbt`, entity spawning, …) throw
`this LegacyScriptEngine API is not implemented by YoyoLoader yet` rather than
being silently absent.

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
player.isSneaking();
player.isSprinting();
player.isFlying();
player.getHeldItemSlot();           // null until PlayerEquipment is observed
player.getHealth();                 // null until SetHealth is observed

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

## Worlds / multiverse

```js
mc.worlds.list();          // [{ name, port, level, loaded, current }]
mc.worlds.current();       // current instance name
mc.worlds.isController();  // true on the main server
mc.worlds.load("world2");  // controller only
mc.worlds.unload("world2");// controller only
mc.worlds.switch(player, "world2"); // sends a real TransferPacket
mc.worlds.isOp(player);    // XUID is listed in yoyo_ops.txt
```

See `yoyo_worlds.ini` and `yoyo_ops.txt` in the server directory. The sample
`plugins/world_commands.js` implements the op-only `!world` chat commands.
`player.transfer(host, port)` is also available for custom transfer logic.

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
mc.getBDSVersion();                  // "1.26.52.3"
mc.getDimensionId("nether");         // 1, or null
mc.getDimensionName(2);              // "the_end", or null
mc.getOnlinePlayerNum();
```

## Timers and script helpers

```js
log("plain console line");            // same as mc.log
colorLog("§a", "colored line");
fastLog("asynchronous-style log");    // same sink, safe from any listener

const t1 = setTimeout(() => player.sendMessage("hi"), 1000);
const t2 = setInterval(() => mc.broadcast("tick"), 5000);
clearInterval(t1);                    // cancels both one-shot and repeating ids
clearTimeout(t2);                     // alias
```

Timers are drained from `Level::tick` on the game thread, so a timer callback
may send packets or touch world state. Delays are milliseconds; resolution is
one server tick (50 ms by default).

## Chat commands

```js
mc.regPlayerCmd("spawn", "Teleport to spawn", (player, args, output) => {
    player.teleport({ x: 0, y: 64, z: 0 });
    output.success("§aWelcome back");
    return "§bTeleported";       // returned string is sent to the player
}, 0);                           // 0 = everyone, 1 = OPs in yoyo_ops.txt
```

A matching chat line (with or without a leading `/` or `!`) is removed from
chat and routed to the callback. `args` is everything after the command name.
This is a YoyoLoader chat-command layer, not a vanilla command registration:
tab completion and `/help` integration are not provided.

## Config files

```js
const cfg = new JsonConfigFile("plugins/config.json", '{"enabled":true}');
cfg.set("greeting", "hello");
cfg.get("greeting", "default");
cfg.init("version", 1);       // returns the stored value or writes the default
cfg.delete("greeting");
cfg.getPath();
cfg.read();                   // whole file as text
cfg.write("{}");
cfg.reload();

const ini = new IniConfigFile("plugins/config.ini");
ini.set("main", "port", "19132");
ini.getStr("main", "port", "0");
ini.getInt("main", "port", 0);
ini.getFloat("main", "ratio", 0);
ini.getBool("main", "debug", false);
ini.delete("main", "port");
```

Paths are resolved inside the server directory. Absolute paths and `..`
escapes are rejected, so a plugin cannot touch arbitrary host files.

## File objects

```js
const file = new File("plugins/data/log.txt", "a");
file.writeLineSync("line");
file.close();

const reader = new File("plugins/data/log.txt", "r");
reader.readLineSync();        // "line", or null at the end
reader.readSync(16);          // at most 16 bytes as text
reader.readAllSync();
reader.seekTo(0, false);
reader.isEOF();
reader.flush();
reader.close();

const writer = new File("plugins/data/raw.bin", "w", true);  // binary mode
```

`read`, `readLine`, `readAll`, `write` and `writeLine` accept a callback and
run it synchronously with the same result, so LLSE-style async code works
without a worker thread.

## Form callbacks

```js
const form = mc.newSimpleForm("Title", "Content");
form.addButton("OK");
form.addButton("Cancel");

player.sendForm(form, (player, id, reason) => {
    // id is the clicked button index, or null when the player closed the form
    // reason: 0 = user closed, 1 = user busy
});

player.sendModalForm("Confirm", "Continue?", "Yes", "No",
    (player, result, reason) => { /* result is true/false or null */ });
```

`form.sendTo(player, callback)` is equivalent. `onFormResponse` still fires
globally for every form response.

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
