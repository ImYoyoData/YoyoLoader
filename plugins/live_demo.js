// YoyoLoader live API demo.
//
// Copy this file to <server>/plugins/live_demo.js.  It exercises the live
// Player bridge that is implemented in the runtime; every call below was
// tested against BDS 1.26.52.3 on Windows and Linux.

mc.log("live_demo loaded, server " + mc.getServerVersion());

mc.listen("onJoin", (player) => {
    const location = player.getLocation();
    mc.log(`[live_demo] ${player.name} joined at ` +
           `${location.x.toFixed(1)} ${location.y.toFixed(1)} ${location.z.toFixed(1)} ` +
           `dimension=${location.dimensionName} mode=${player.getGameModeName()}`);

    player.sendMessage("§a[YoyoLoader] welcome, " + player.name);
    player.sendTitle("§eYoyoLoader", "§bLive API is online", 10, 70, 20);
    player.sendTip("§dTip from the live bridge");
    player.sendPopup("§6Popup from the live bridge");
    player.sendToast("YoyoLoader", "Toast packet received");
    player.sendActionBar("§aActionbar");

    const form = mc.newSimpleForm("YoyoLoader", "Choose an action");
    form.addButton("Stay here");
    form.addButton("Teleport +3 blocks");
    form.sendTo(player);

    mc.log("[live_demo] online players: " + mc.getOnlinePlayerCount());
    mc.broadcast("§e[YoyoLoader] " + player.name + " joined the server");
});

mc.listen("onChat", (player, message, event) => {
    // Set event.message to rewrite the TextPacket before BDS broadcasts it.
    mc.log(`[live_demo] chat ${player.name}: ${message}`);
});

mc.listen("onLeft", (player, reason) => {
    mc.log(`[live_demo] ${player.name} left: ${reason}`);
});
