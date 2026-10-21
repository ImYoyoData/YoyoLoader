import { world, system } from "@minecraft/server";

const OBJECTIVE = "yoyo_sidebar";
let initialized = false;
let lastTick = system.currentTick;
let lastWall = Date.now();

function run(command) {
    try {
        return world.getDimension("overworld").runCommand(command);
    } catch (e) {
        console.warn("[YoyoSidebar] command failed: " + command + " :: " + e);
        return null;
    }
}

function setup() {
    // Remove the previous objective so a server restart always starts clean.
    run(`scoreboard objectives remove ${OBJECTIVE}`);
    run(`scoreboard objectives add ${OBJECTIVE} dummy "YoyoLoader"`);
    run(`scoreboard objectives setdisplay sidebar ${OBJECTIVE}`);
    initialized = true;
    console.log("[YoyoSidebar] setup complete");
}

system.runInterval(() => {
    if (!initialized) setup();

    const now = Date.now();
    const tick = system.currentTick;
    const elapsed = Math.max(1, now - lastWall);
    const tps = Math.min(20, Math.max(0, Math.round(((tick - lastTick) * 1000) / elapsed)));
    lastTick = tick;
    lastWall = now;

    const dimension = world.getDimension("overworld");
    let entities = 0;
    let items = 0;
    try { entities = dimension.getEntities().length; } catch (e) { console.warn("[YoyoSidebar] entities: " + e); }
    try { items = dimension.getEntities({ type: "minecraft:item" }).length; } catch (e) { console.warn("[YoyoSidebar] items: " + e); }
    const players = world.getAllPlayers().length;

    let time = 0;
    try { time = world.getTimeOfDay(); } catch (e) { console.warn("[YoyoSidebar] time: " + e); }
    // 0..24000 ticks -> HHMM as an integer, e.g. 630 for 06:30.
    const hhmm = Math.floor(time / 1000) * 100 + Math.floor(((time % 1000) * 60) / 1000);

    run(`scoreboard players set TPS ${OBJECTIVE} ${tps}`);
    run(`scoreboard players set TIME ${OBJECTIVE} ${hhmm}`);
    run(`scoreboard players set PLAYERS ${OBJECTIVE} ${players}`);
    run(`scoreboard players set ENTITIES ${OBJECTIVE} ${entities}`);
    run(`scoreboard players set ITEMS ${OBJECTIVE} ${items}`);

    console.log(`[YoyoSidebar] tps=${tps} time=${hhmm} players=${players} entities=${entities} items=${items}`);
}, 20);
