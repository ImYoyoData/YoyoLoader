// YoyoLoader multiverse commands.
//
// Chat commands (not slash commands):
//   !world list
//   !world current
//   !world load <name>     (controller/op only)
//   !world unload <name>   (controller/op only)
//   !world switch <name>   (op only)
//
// OP is checked against yoyo_ops.txt (one XUID per line).  The controller is
// the main server; child worlds can switch players back to main.

mc.log("[worlds] command plugin loaded, current=" + mc.worlds.current());

mc.listen("onChat", (player, message, event) => {
    if (!message.startsWith("!world")) {
        return;
    }
    event.cancelled = true;

    if (!mc.worlds.isOp(player)) {
        player.sendMessage("§c只有 OP 可以执行世界命令");
        return;
    }

    const parts = message.trim().split(/\s+/);
    const sub = parts[1] || "help";
    const name = parts[2] || "";

    if (sub === "list") {
        for (const world of mc.worlds.list()) {
            player.sendMessage(
                `§e${world.current ? "*" : " "} ${world.name} §7port=${world.port} ` +
                `loaded=${world.loaded} level=${world.level}`
            );
        }
        return;
    }

    if (sub === "current") {
        player.sendMessage("§e当前世界: §f" + mc.worlds.current());
        return;
    }

    if (sub === "load" || sub === "unload") {
        if (!mc.worlds.isController()) {
            player.sendMessage("§c只能在主世界执行 load/unload");
            return;
        }
        if (!name) {
            player.sendMessage("§c用法: !world " + sub + " <name>");
            return;
        }
        const ok = sub === "load" ? mc.worlds.load(name) : mc.worlds.unload(name);
        player.sendMessage(ok ? `§a世界 ${name} ${sub === "load" ? "已加载" : "已卸载"}` :
                               `§c操作失败: ${name}`);
        return;
    }

    if (sub === "switch") {
        if (!name) {
            player.sendMessage("§c用法: !world switch <name>");
            return;
        }
        const ok = mc.worlds.switch(player, name);
        player.sendMessage(ok ? `§a正在切换到世界 ${name}...` : `§c无法切换到 ${name}`);
        return;
    }

    player.sendMessage("§e用法: !world list|current|load <name>|unload <name>|switch <name>");
});
