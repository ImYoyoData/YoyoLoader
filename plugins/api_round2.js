mc.listen("onJoin", (pl) => {
    setTimeout(() => {
        const report = [];
        const step = (label, fn) => {
            try { report.push(label + "=" + fn()); }
            catch (e) { report.push(label + "=ERR"); }
        };
        // Players are online now, so these packets reach the send hook.
        step("setTime", () => mc.setTime(6000));
        step("setWeather", () => mc.setWeather(1));
        setTimeout(() => {
            step("getTime", () => mc.getTime());
            step("getWeather", () => mc.getWeather());
            mc.log("[r2] " + report.join(" | "));
        }, 500);
    }, 2500);
});
mc.log("[r2] loaded");
