const HOME_CFG = new JsonConfigFile("plugins/data/homes.json");
const KEY = "players";

function keyOf(pl) { return pl.xuid || pl.name; }

function loadAll() {
    const data = HOME_CFG.get(KEY, null);
    if (!data || typeof data !== "object") return {};
    return data;
}

function homesOf(pl) {
    const all = loadAll();
    const mine = all[keyOf(pl)];
    return mine && typeof mine === "object" ? mine : {};
}

function persist(all) { HOME_CFG.set(KEY, all); }

function addHome(pl, name) {
    const all = loadAll();
    const mine = all[keyOf(pl)] && typeof all[keyOf(pl)] === "object" ? all[keyOf(pl)] : {};
    if (Object.keys(mine).length >= 16) { pl.tell("§c最多 16 个家"); return false; }
    const loc = pl.pos;
    mine[name] = { x: loc.x, y: loc.y, z: loc.z, dimid: loc.dimid, at: Date.now() };
    all[keyOf(pl)] = mine;
    persist(all);
    pl.tell("§a已设置家: §f" + name);
    return true;
}

function delHome(pl, name) {
    const all = loadAll();
    const mine = all[keyOf(pl)];
    if (!mine || !mine[name]) { pl.tell("§c没有这个家: " + name); return false; }
    delete mine[name];
    all[keyOf(pl)] = mine;
    persist(all);
    pl.tell("§7已删除家: " + name);
    return true;
}

function goHome(pl, name) {
    const home = homesOf(pl)[name];
    if (!home) { pl.tell("§c没有这个家: " + name); return false; }
    pl.teleport({ x: home.x, y: home.y, z: home.z, dimid: home.dimid });
    pl.tell("§a已传送到家: §f" + name);
    return true;
}

function openHomeForm(pl) {
    const names = Object.keys(homesOf(pl));
    if (names.length === 0) {
        pl.tell("§e你还没有设置任何家");
        pl.tell("§e使用 §f/home add <名字> §e添加");
        return;
    }
    const form = mc.newSimpleForm();
    form.setTitle("我的家");
    form.setContent("选择一个家传送过去");
    for (const n of names) form.addButton(n);
    form.addButton("§c取消");
    pl.sendForm(form, (player, id) => {
        if (id === null || id === undefined || id === names.length) return;
        goHome(player, names[id]);
    });
}

function handleCommand(pl, cmd, ev) {
    const parts = String(cmd).trim().split(/\s+/);
    if (parts[0] !== "home") return;
    ev.cancelled = true;
    const sub = parts[1];

    if (!sub) { openHomeForm(pl); return; }

    if (sub === "list") {
        const homes = homesOf(pl);
        const names = Object.keys(homes);
        if (names.length === 0) { pl.tell("§e你还没有设置任何家"); return; }
        pl.tell("§e你的家：");
        for (const n of names) {
            const h = homes[n];
            pl.tell("§7- §f" + n + " §8(" + h.x.toFixed(0) + ", " + h.y.toFixed(0) +
                    ", " + h.z.toFixed(0) + ") dimid=" + h.dimid);
        }
        return;
    }
    if (sub === "add") {
        addHome(pl, parts[2] || ("home" + (Object.keys(homesOf(pl)).length + 1)));
        return;
    }
    if (sub === "del" || sub === "delete" || sub === "remove") {
        if (!parts[2]) { pl.tell("§c用法: /home del <名字>"); return; }
        delHome(pl, parts[2]);
        return;
    }
    goHome(pl, sub);
}

mc.regPlayerCmd("home", "家传送", (pl, args, out) => {
    try { handleCommand(pl, "home " + (args || ""), { cancelled: false }); }
    catch (e) { pl.tell("§c命令出错"); mc.log("[home] " + e.message); }
    out.success();
});

// Chat fallback keeps "/home" working for clients that send it as chat.
mc.listen("onChat", (pl, msg, ev) => {
    const text = String(msg || "").trim();
    if (text.charAt(0) !== "/") return;
    if (text.slice(1).split(/\s+/)[0] !== "home") return;
    try { handleCommand(pl, text.slice(1), ev); } catch (e) {}
    ev.cancelled = true;
});

mc.log("[home] 已加载 v" + __plugin.version);
