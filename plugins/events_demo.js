// YoyoLoader packet/state event demo.
//
// This plugin is intentionally quiet: it only prints one line per state
// transition, so it is safe to leave enabled while testing.

mc.log("[events_demo] loaded");

mc.listen("onPlayerJump", (player) => {
    mc.log(`[events_demo] ${player.name} jumped`);
});

mc.listen("onSneak", (player, sneaking) => {
    mc.log(`[events_demo] ${player.name} sneak=${sneaking}`);
});

mc.listen("onChangeSprinting", (player, sprinting) => {
    mc.log(`[events_demo] ${player.name} sprint=${sprinting}`);
});

mc.listen("onPlayerCmd", (player, command) => {
    mc.log(`[events_demo] command from ${player.name}: ${command}`);
});

mc.listen("onPlayerHealthChange", (player, health) => {
    mc.log(`[events_demo] ${player.name} health=${health}`);
});

mc.listen("onPlayerDie", (player) => {
    mc.log(`[events_demo] ${player.name} died`);
});

mc.listen("onPlayerRespawn", (player) => {
    mc.log(`[events_demo] ${player.name} respawned`);
});

mc.listen("onPlayerGameModeChange", (player, mode) => {
    mc.log(`[events_demo] ${player.name} gameMode=${mode}`);
});

mc.listen("onPlayerItemHeld", (player, slot, previous) => {
    mc.log(`[events_demo] ${player.name} heldSlot ${previous} -> ${slot}`);
});

mc.listen("onFormResponse", (player, formId, response) => {
    mc.log(`[events_demo] ${player.name} form ${formId}: ${response}`);
});
