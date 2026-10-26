mc.listen("onJoin", (pl) => {
    setTimeout(() => {
        const report = [];
        const step = (label, fn) => {
            try { report.push(label + "=" + fn()); }
            catch (e) { report.push(label + "=REFUSED"); }
        };
        // Implemented
        step("setTime", () => mc.setTime(6000));
        step("setWeather", () => mc.setWeather(1));
        step("spawnParticle", () => mc.spawnParticle(pl.pos, "minecraft:heart_particle"));
        // Read side, from packet cache
        setTimeout(() => {
            step("getTime", () => mc.getTime());
            step("getWeather", () => mc.getWeather());
            step("getBlock", () => { const b = mc.getBlock(pl.blockPos); return b === null ? "null(not-seen)" : "block"; });
            // Refused by design
            step("runcmd", () => mc.runcmd("say x"));
            step("getInventory", () => pl.getInventory());
            step("giveItem", () => pl.giveItem(mc.newItem("minecraft:bread", 1)));
            step("getNbt", () => pl.getNbt());
            step("getAllEntities", () => mc.getAllEntities());
            mc.log("[final] " + report.join(" | "));
        }, 600);
    }, 2500);
});
mc.log("[final] loaded");
