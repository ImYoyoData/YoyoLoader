// Verifies LLSE cancellation semantics: returning false from a listener must
// stop the event.  mc.__simulateChat() feeds the same emit_chat() path the
// real TextPacket hook uses, and returns null when the event was cancelled.
mc.listen("onChat", (pl, msg) => {
    if (msg === "!cancelme") {
        log("[cancel] blocked " + msg);
        return false;
    }
    log("[cancel] allowed " + msg);
});

mc.listen("onServerStarted", () => {
    const blocked = mc.__simulateChat("Probe", "!cancelme");
    const allowed = mc.__simulateChat("Probe", "hello");
    log("[cancel] blocked_result=" + (blocked === null ? "null(cancelled)" : String(blocked)));
    log("[cancel] allowed_result=" + (allowed === null ? "null(cancelled)" : String(allowed)));
});
