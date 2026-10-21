// YoyoLoader sample JS plugin.
// Copy this file to <server>/plugins/sample.js to load it.
// The runtime scans <server>/plugins/*.js on startup.

mc.log("sample.js loaded");

mc.listen("onPreJoin", function (player) {
    mc.log("pre_join", player.name, player.xuid);
});

mc.listen("onJoin", function (player) {
    mc.log("join", player.name, player.runtimeId,
        "online:", mc.getOnlinePlayers().length);
});

mc.listen("onChat", function (player, message, event) {
    mc.log("chat", player.name + ": " + message);
    // event.cancelled = true; // drop the chat packet
});

mc.listen("onLeft", function (player, reason) {
    mc.log("left", player.name, reason);
});

// NBT is fully local and does not require BDS world access.
const nbt = new NBT();
nbt.set("name", "Yoyo");
nbt.set("count", 3);
nbt.set("nested", { enabled: true, values: [1, 2, 3] });
mc.log("nbt:", nbt.toString());
mc.log("nbt type:", nbt.getType("count"), "name:", nbt.get("name"));

// Forms are data models in this build; sendTo() reports that the BDS
// form packet bridge is not implemented yet.
const form = mc.newSimpleForm("Yoyo", "Hello from JS");
form.addButton("OK");
form.addButton("Cancel");
mc.log("form:", form.toString());

// Data-only Item / Block / Container / Entity models.
const item = mc.newItem("minecraft:apple", 3, nbt);
const container = mc.newContainer(9);
container.addItem(item);
mc.log("item:", item.getType(), item.getCount(), "container:", container.getAllItems().length);
