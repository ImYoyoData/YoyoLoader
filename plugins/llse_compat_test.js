// LegacyScriptEngine compatibility test.
//
// Everything below is written the way an LLSE plugin would be written:
// LLSE property names (pos.dimid, it.type), LLSE methods (tell, sendText,
// setTitle, getPos), LLSE constructors (FloatPos, IntPos), and LLSE's
// cancellation convention (return false).  If this plugin runs unmodified,
// the compatibility layer works.

log("[llse] plugin loaded");

mc.listen("onServerStarted", () => {
    log("[llse] onServerStarted fired");
});

mc.listen("onJoin", (pl) => {
    // LLSE: pl.name / pl.xuid as properties
    log("[llse] join " + pl.name + " xuid=" + pl.xuid);

    // LLSE: pl.tell(msg) / pl.sendText(msg, type)
    try {
        pl.tell("§a[llse] tell works");
        pl.sendText("§b[llse] sendText works", 1);
    } catch (err) {
        log("[llse] tell/sendText FAILED: " + err.message);
    }

    // LLSE: pl.pos / pl.feetPos / pl.blockPos with dimid
    try {
        const p = pl.pos;
        log("[llse] pos=(" + p.x.toFixed(1) + "," + p.y.toFixed(1) + "," + p.z.toFixed(1) +
            ") dimid=" + p.dimid + " dim=" + p.dim);
    } catch (err) {
        log("[llse] pos FAILED: " + err.message);
    }
    try {
        const f = pl.feetPos;
        log("[llse] feetPos y=" + f.y.toFixed(1) + " dimid=" + f.dimid);
    } catch (err) {
        log("[llse] feetPos FAILED: " + err.message);
    }
    try {
        const b = pl.blockPos;
        log("[llse] blockPos=(" + b.x + "," + b.y + "," + b.z + ") dimid=" + b.dimid);
    } catch (err) {
        log("[llse] blockPos FAILED: " + err.message);
    }

    // LLSE: pl.direction with toFacing()
    try {
        const d = pl.direction;
        log("[llse] direction pitch=" + d.pitch.toFixed(1) + " yaw=" + d.yaw.toFixed(1) +
            " facing=" + d.toFacing());
    } catch (err) {
        log("[llse] direction FAILED: " + err.message);
    }

    // LLSE constructors
    try {
        const ip = new IntPos(10, 64, -5, 0);
        const fp = new FloatPos(1.5, 64.25, -2.75, 0);
        const ang = new DirectionAngle(15, 90);
        log("[llse] IntPos=" + ip.toString() + " FloatPos=" + fp.toString() +
            " facing=" + ang.toFacing());
    } catch (err) {
        log("[llse] position ctors FAILED: " + err.message);
    }
});

mc.listen("onChat", (pl, msg) => {
    log("[llse] chat " + pl.name + ": " + msg);
    // LLSE cancels by returning false.
    if (msg === "!cancelme") {
        pl.tell("§cblocked");
        return false;
    }
});

// This must stay silent on builds without the tick hook.
mc.listen("onTick", () => {});

mc.listen("onLeft", (pl) => {
    log("[llse] left " + pl.name);
});

// LLSE globals that we do not implement must throw a clear message, not
// "undefined is not a function".
mc.listen("onPreJoin", (pl) => {
    try {
        mc.runcmd("say hi");
        log("[llse] runcmd unexpectedly succeeded");
    } catch (err) {
        log("[llse] runcmd correctly refused: " + err.message.slice(0, 60));
    }
});
