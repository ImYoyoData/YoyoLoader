mc.listen("onJoin", (pl) => {
    // Only `say` — no `time set` — to isolate whether the crash is the packet
    // lifecycle or a specific command.
    const ok = mc.runcmd("say single-runcmd");
    mc.log("[rc] runcmd=" + ok);
});
mc.log("[rc] loaded");
