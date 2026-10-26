// Capability gating probe.
//
// On a build without verified object layout, offset-dependent APIs must throw
// a clear error instead of returning garbage.  This plugin reports which calls
// succeed and which are refused, so the runtime log proves the gate works.
mc.listen("onJoin", (player) => {
    const results = [];
    const probe = (label, fn) => {
        try {
            fn();
            results.push(label + "=ok");
        } catch (err) {
            results.push(label + "=refused(" + String(err.message).slice(0, 40) + ")");
        }
    };
    probe("sendMessage", () => player.sendMessage("§a[gate] hi"));
    probe("getPosition", () => player.getPosition());
    probe("getRotation", () => player.getRotation());
    probe("getDimension", () => player.getDimension());
    probe("teleport", () => player.teleport({ x: 0, y: 64, z: 0 }));
    probe("getGameMode", () => player.getGameMode());
    probe("setGameMode", () => player.setGameMode("creative"));
    probe("getHealth", () => player.getHealth());
    mc.log("[gate] " + results.join(" "));
});
mc.log("[gate] capability probe loaded");
