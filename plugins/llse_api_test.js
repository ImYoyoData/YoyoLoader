// Final verification of the LLSE API surface we implemented, plus the precise
// refusal for the ones we deliberately do not implement.
mc.listen("onJoin", (pl) => {
    const report = [];
    const step = (label, fn) => {
        try { report.push(label + "=" + fn()); }
        catch (e) { report.push(label + "=REFUSED"); }
    };
    step("setTime", () => mc.setTime(6000));
    step("setWeather", () => mc.setWeather(1));
    step("spawnParticle", () => mc.spawnParticle(pl.pos, "minecraft:heart_particle"));
    step("runcmd", () => mc.runcmd("say nope"));
    mc.log("[api] RESULT " + report.join(" | "));
});
mc.log("[api] loaded");
