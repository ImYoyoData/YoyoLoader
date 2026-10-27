#include "script_bridge.h"

#include "log.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace yoyo {
namespace {

namespace fs = std::filesystem;

constexpr const char* kPackId = "3f7c2a10-9b4e-4d21-8a55-6c1e2f9d7b30";
constexpr const char* kModuleId = "b8e4d1c6-2a3f-4e7b-9d10-5f8c6b2e4a91";
constexpr const char* kPackName = "YoyoLoader Bridge";

std::atomic<bool> g_installed{false};

bool write_if_changed(const fs::path& path, const std::string& content) {
    std::error_code error;
    if (fs::exists(path, error)) {
        std::ifstream existing(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << existing.rdbuf();
        if (buffer.str() == content) {
            return true;
        }
    }
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << content;
    return file.good();
}

std::string manifest_json() {
    std::ostringstream out;
    out << "{\n"
        << "  \"format_version\": 2,\n"
        << "  \"header\": {\n"
        << "    \"name\": \"" << kPackName << "\",\n"
        << "    \"description\": \"Exposes the official Script API to YoyoLoader plugins\",\n"
        << "    \"uuid\": \"" << kPackId << "\",\n"
        << "    \"version\": [0, 0, 1],\n"
        << "    \"min_engine_version\": [1, 21, 0]\n"
        << "  },\n"
        << "  \"modules\": [\n"
        << "    {\n"
        << "      \"type\": \"script\",\n"
        << "      \"language\": \"javascript\",\n"
        << "      \"uuid\": \"" << kModuleId << "\",\n"
        << "      \"version\": [0, 0, 1],\n"
        << "      \"entry\": \"scripts/main.js\"\n"
        << "    }\n"
        << "  ],\n"
        << "  \"dependencies\": [\n"
        << "    { \"module_name\": \"@minecraft/server\", \"version\": \"1.17.0\" },\n"
        << "  ],\n"
        << "  \"capabilities\": [\"script_eval\"]\n"
        << "}\n";
    return out.str();
}

/*
 * Bridge entry script.
 *
 * It imports the official API and installs a global `yoyoScript` object.  The
 * objects exposed here are the real Endstone-equivalent ones (Player, Container,
 * ItemStack, Block, Entity, Scoreboard) that the injector cannot construct.
 */
const char* kBridgeScript = R"JS(
import * as mc from "@minecraft/server";
/*
 * server-net is optional: it exists on dedicated servers but not on every
 * build, and a hard import would stop the whole bridge from loading.  The
 * inventory bridge registers its handler only when the module is present.
 */
let net = null;
try {
    net = await import("@minecraft/server-net");
} catch (e) {
    net = null;
    console.warn("[YoyoLoader] server-net unavailable: " + String(e && e.message));
}
console.warn("[YoyoLoader] server-net " + (net ? "loaded" : "MISSING"));

/*
 * The bridge publishes the official API plus a few helpers that make it behave
 * more like LegacyScriptEngine.  Plugin code can use either the raw API
 * (`yoyoScript.mc.world`) or the helpers (`yoyoScript.api`).
 */
const api = {
    /* --- world / level --- */
    getPlayers: () => mc.world.getAllPlayers(),
    getPlayer: (name) => mc.world.getAllPlayers().find(p => p.name === name),
    getDimension: (name) => mc.world.getDimension(name || "overworld"),
    broadcast: (text) => mc.world.sendMessage(String(text)),
    getTime: () => mc.world.getTimeOfDay(),
    setTime: (ticks) => mc.world.setTimeOfDay(ticks),
    getAllEntities: (dim) => (dim ? mc.world.getDimension(dim) : mc.world.getDimension("overworld")).getEntities(),
    getBlock: (x, y, z, dim) => {
        const d = mc.world.getDimension(dim || "overworld");
        return d.getBlock({ x, y, z });
    },
    setBlock: (x, y, z, type, dim) => {
        const d = mc.world.getDimension(dim || "overworld");
        return d.getBlock({ x, y, z }).setType(type);
    },
    runCommand: (cmd) => mc.world.getDimension("overworld").runCommand(String(cmd)),
    spawnParticle: (name, x, y, z, dim) => {
        const d = mc.world.getDimension(dim || "overworld");
        d.spawnParticle(String(name), { x, y, z });
    },
    getScoreboard: () => mc.world.scoreboard,

    /* --- player helpers --- */
    playerHealth: (p) => p.getComponent("minecraft:health"),
    playerInventory: (p) => p.getComponent("minecraft:inventory").container,
    playerEnderChest: (p) => p.getComponent("minecraft:ender_inventory").container,
    playerEquipment: (p) => p.getComponent("minecraft:equippable"),
    playerGameMode: (p) => p.getGameMode(),
    setPlayerGameMode: (p, mode) => p.setGameMode(mode),
    playerRunCommand: (p, cmd) => p.runCommand(String(cmd)),
    playerSendMessage: (p, text) => p.sendMessage(String(text)),
    playerTeleport: (p, x, y, z, dim) => {
        if (dim) {
            p.teleport({ x, y, z }, { dimension: mc.world.getDimension(dim) });
        } else {
            p.teleport({ x, y, z });
        }
    },
    playerTags: (p) => p.getTags(),

    /* --- item helpers --- */
    newItem: (type, count) => new mc.ItemStack(String(type), count || 1),
    itemType: (item) => item.typeId,
    itemCount: (item) => item.amount,
    cloneItem: (item) => item.clone(),

    /*
     * --- container helpers ---
     *
     * The injector cannot reach the player's slot array: it is not an EnTT
     * component, and walking ServerPlayer for it crashes.  These helpers are
     * therefore the supported path for inventory access, and they return plain
     * data so nothing script-owned escapes the bridge.
     */
    containerSize: (c) => c.size,
    containerGet: (c, slot) => {
        const item = c.getItem(slot);
        if (!item) return null;
        return { typeId: item.typeId, amount: item.amount, slot };
    },
    containerAll: (c) => {
        const out = [];
        for (let i = 0; i < c.size; i++) {
            const item = c.getItem(i);
            out.push(item ? { typeId: item.typeId, amount: item.amount, slot: i } : null);
        }
        return out;
    },
    containerSet: (c, slot, typeId, amount) => {
        if (amount <= 0) {
            c.setItem(slot, undefined);
            return true;
        }
        const stack = new mc.ItemStack(String(typeId), amount);
        c.setItem(slot, stack);
        return true;
    },
    containerEmpty: (c) => {
        for (let i = 0; i < c.size; i++) c.setItem(i, undefined);
        return true;
    },
    /* Put a stack in the first slot that can take it. */
    containerAdd: (c, typeId, amount) => {
        const stack = new mc.ItemStack(String(typeId), amount);
        const left = c.addItem(stack);
        return { added: amount - (left ? left.amount : 0), leftover: left ? left.amount : 0 };
    },
    playerContainer: (p, which) => {
        if (which === "ender") return p.getComponent("minecraft:ender_inventory").container;
        if (which === "armor") return p.getComponent("minecraft:equippable");
        return p.getComponent("minecraft:inventory").container;
    },
    /* Armor slots are named, so expose them as an array-like view. */
    equipmentAll: (eq) => {
        const out = [];
        for (const slot of ["Head", "Chest", "Legs", "Feet", "Offhand"]) {
            let item = null;
            try { item = eq.getEquipment(slot); } catch (e) { item = null; }
            out.push(item ? { typeId: item.typeId, amount: item.amount, slot } : null);
        }
        return out;
    },
    equipmentSet: (eq, slot, typeId, amount) => {
        eq.setEquipment(slot, amount > 0 ? new mc.ItemStack(String(typeId), amount) : undefined);
        return true;
    },
};

mc.system.run(() => {
    globalThis.yoyoScript = {
        mc,
        world: mc.world,
        system: mc.system,
        api,
        ready: true,
    };
    console.warn("[YoyoLoader] script bridge ready");
});

/*
 * Announce join/leave to the injector through the log channel; YoyoLoader reads
 * its own log file, so this is how the bridge and the injector stay in sync
 * without sharing memory.
 */
mc.world.afterEvents.playerSpawn.subscribe((ev) => {
    console.warn("[YoyoLoader:bridge] playerSpawn " + ev.player.name);
});
mc.world.afterEvents.playerLeave.subscribe((ev) => {
    console.warn("[YoyoLoader:bridge] playerLeave " + ev.playerName);
});

/*
 * Push player state to the injector.
 *
 * The bridge runs in BDS's own script engine while YoyoLoader plugins run in
 * QuickJS: the two cannot share objects.  HTTP is the only channel the official
 * API offers for getting data out of the game, so the bridge posts a snapshot
 * and the injector keeps it in a per-player cache.  Everything is optional: if
 * server-net or the local server is unavailable the bridge keeps working and
 * plugins fall back to packet-derived data.
 */
const BRIDGE_PORT = 8765;
const BRIDGE_URL = "http://127.0.0.1:" + BRIDGE_PORT + "/state";

async function pushState() {
    if (!net) return;
    try {
        const players = [];
        for (const p of mc.world.getAllPlayers()) {
            const entry = { name: p.name, id: p.id };
            try { entry.gameMode = p.getGameMode(); } catch (e) {}
            try {
                const hp = p.getComponent("minecraft:health");
                entry.health = hp ? hp.currentValue : undefined;
            } catch (e) {}
            try {
                const inv = p.getComponent("minecraft:inventory").container;
                const slots = [];
                for (let i = 0; i < inv.size; i++) {
                    const it = inv.getItem(i);
                    slots.push(it ? { slot: i, typeId: it.typeId, amount: it.amount } : null);
                }
                entry.inventory = { size: inv.size, slots };
            } catch (e) {
                entry.inventoryError = String(e && e.message);
            }
            try {
                const ec = p.getComponent("minecraft:ender_inventory").container;
                const slots = [];
                for (let i = 0; i < ec.size; i++) {
                    const it = ec.getItem(i);
                    slots.push(it ? { slot: i, typeId: it.typeId, amount: it.amount } : null);
                }
                entry.enderChest = { size: ec.size, slots };
            } catch (e) {}
            try {
                const eq = p.getComponent("minecraft:equippable");
                const slots = api.equipmentAll(eq);
                entry.equipment = { size: slots.length, slots };
            } catch (e) {}
            try {
                const loc = p.location;
                entry.location = { x: loc.x, y: loc.y, z: loc.z };
                entry.dimension = p.dimension.id;
            } catch (e) {}
            players.push(entry);
        }
        const body = JSON.stringify({ players, tick: mc.system.currentTick });
        const req = new net.HttpRequest(BRIDGE_URL);
        req.method = net.HttpRequestMethod.Post;
        req.body = body;
        req.headers = [new net.HttpHeader("Content-Type", "application/json")];
        await net.http.request(req);
    } catch (e) {
        /* A failed push must never affect gameplay, but it is logged once so a
         * misconfigured bridge is diagnosable. */
        if (!globalThis.__yoyoPushWarned) {
            globalThis.__yoyoPushWarned = true;
            console.warn("[YoyoLoader] state push failed: " + String(e && e.message));
        }
    }
}

/* Push on a fixed cadence rather than every tick. */
let pushCounter = 0;
mc.system.runInterval(() => {
    if (++pushCounter < 10) return;
    pushCounter = 0;
    pushState();
}, 5);

mc.world.afterEvents.playerSpawn.subscribe(() => { pushState(); });
)JS";

std::string read_world_level_name(const fs::path& root) {
    std::ifstream file(root / "server.properties");
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("level-name=", 0) == 0) {
            return line.substr(std::strlen("level-name="));
        }
    }
    return "Bedrock level";
}

/* Merge our pack into world_behavior_packs.json without dropping existing ones. */
bool register_pack(const fs::path& world_dir) {
    const fs::path file = world_dir / "world_behavior_packs.json";
    std::string existing = "[]";
    std::error_code error;
    if (fs::exists(file, error)) {
        std::ifstream input(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        if (!buffer.str().empty()) {
            existing = buffer.str();
        }
    }
    if (existing.find(kPackId) != std::string::npos) {
        return true; /* already registered */
    }

    /* Strip the closing bracket and append our entry.  The file is a JSON array
     * of {pack_id, version} objects. */
    const auto close = existing.find_last_of(']');
    if (close == std::string::npos) {
        existing = "[";
    } else {
        existing = existing.substr(0, close);
        /* Remove a trailing comma left by the previous element. */
        while (!existing.empty() && (existing.back() == '\n' || existing.back() == ' ' || existing.back() == '\r')) {
            existing.pop_back();
        }
        if (!existing.empty() && existing.back() != '[') {
            existing += ',';
        }
    }
    existing += "\n  {\"pack_id\": \"";
    existing += kPackId;
    existing += "\", \"version\": [0, 0, 1]}\n]\n";
    return write_if_changed(file, existing);
}

} // namespace

ScriptBridgeStatus script_bridge_install(const std::string& server_root) {
    ScriptBridgeStatus status;
    const fs::path root(server_root);
    const fs::path pack = root / "behavior_packs" / "yoyo_bridge";
    status.pack_directory = pack.string();

    if (!write_if_changed(pack / "manifest.json", manifest_json())) {
        status.reason = "cannot write bridge manifest";
        log_write(LogLevel::Error, "script bridge: %s", status.reason.c_str());
        return status;
    }
    if (!write_if_changed(pack / "scripts" / "main.js", kBridgeScript)) {
        status.reason = "cannot write bridge script";
        log_write(LogLevel::Error, "script bridge: %s", status.reason.c_str());
        return status;
    }

    const std::string level = read_world_level_name(root);
    const fs::path world_dir = root / "worlds" / level;
    std::error_code error;
    if (!fs::is_directory(world_dir, error)) {
        status.reason = "world directory not found: " + world_dir.string();
        log_write(LogLevel::Warn, "script bridge: %s", status.reason.c_str());
        return status;
    }
    if (!register_pack(world_dir)) {
        status.reason = "cannot update world_behavior_packs.json";
        log_write(LogLevel::Error, "script bridge: %s", status.reason.c_str());
        return status;
    }

    status.available = true;
    g_installed.store(true);
    log_write(LogLevel::Info, "script bridge deployed: %s (world '%s')", pack.string().c_str(),
              level.c_str());
    return status;
}

bool script_bridge_available() {
    return g_installed.load();
}

} // namespace yoyo
