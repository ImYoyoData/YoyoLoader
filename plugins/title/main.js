// 称号系统  ——  /title
//
// 命令（聊天输入，以 / 开头）：
//   /title              打开称号表单，选择并佩戴
//   /title list         列出全部称号
//   /title off          卸下称号
//   /title add <名字>    （仅 OP）给自己添加自定义称号
//
// 称号以聊天前缀形式显示在玩家名字前，保存在 plugins/data/titles.json。

const TITLE_CFG = new JsonConfigFile("plugins/data/titles.json");
const KEY = "players";

// 内置称号池。玩家可用 /title 直接选择。
const BUILTIN = [
    { id: "newbie",  name: "§7新手",   desc: "刚来到这个世界" },
    { id: "builder", name: "§a建筑师", desc: "喜欢搭建" },
    { id: "miner",   name: "§6矿工",   desc: "地下是我的家" },
    { id: "hunter",  name: "§c猎人",   desc: "野外生存专家" },
    { id: "explorer",name: "§b探险家", desc: "走过每一片土地" },
    { id: "admin",   name: "§4§l管理员", desc: "服务器管理者" },
];

function loadAll() {
    const data = TITLE_CFG.get(KEY, null);
    return data && typeof data === "object" ? data : {};
}

function saveAll(data) {
    TITLE_CFG.set(KEY, data);
}

function titleOf(key) {
    const data = loadAll();
    const id = data[key];
    if (!id) return null;
    return BUILTIN.find((t) => t.id === id) || { id, name: id, desc: "自定义称号" };
}

function setTitle(pl, id) {
    const data = loadAll();
    const key = pl.xuid || pl.name;
    if (!id) {
        delete data[key];
    } else {
        data[key] = id;
    }
    saveAll(data);
}

// 广播时把称号拼在名字前。onChat 里改写 event.message 即可。
mc.listen("onChat", (pl, msg, ev) => {
    const title = titleOf(pl.xuid || pl.name);
    if (title && msg && String(msg).charAt(0) !== "/") {
        ev.message = title.name + " §f" + msg;
    }
});

// "/xxx" arrives as a normal chat line from a real client, so the command
// router below is shared by chat and the native command packet.
function handleCommand(pl, cmd, ev) {
    const parts = String(cmd).trim().split(/\s+/);
    if (parts[0] !== "title") return;

    const sub = parts[1] || "";

    if (sub === "list") {
        pl.tell("§e可用称号：");
        for (const t of BUILTIN) {
            pl.tell("§7- §f" + t.id + " §7" + t.name + " §8" + t.desc);
        }
        ev.cancelled = true;
        return;
    }

    if (sub === "off") {
        setTitle(pl, null);
        pl.tell("§a已卸下称号");
        ev.cancelled = true;
        return;
    }

    if (sub === "add") {
        if (!pl.isOP()) {
            pl.tell("§c只有 OP 可以添加称号");
            ev.cancelled = true;
            return;
        }
        const name = parts.slice(2).join(" ");
        if (!name) {
            pl.tell("§c用法: /title add <名字>");
            ev.cancelled = true;
            return;
        }
        const id = "custom_" + Date.now();
        BUILTIN.push({ id, name: "§d" + name, desc: "自定义" });
        setTitle(pl, id);
        pl.tell("§a已添加并佩戴称号: §d" + name);
        ev.cancelled = true;
        return;
    }

    // 无参数：弹出表单
    const form = mc.newSimpleForm();
    form.setTitle("称号");
    form.setContent("选择一个要佩戴的称号");
    for (const t of BUILTIN) {
        form.addButton(t.name + " §8" + t.desc);
    }
    form.addButton("§c卸下称号");

    pl.sendForm(form, (player, id) => {
        if (id === null || id === undefined) return;
        if (id === BUILTIN.length) {
            setTitle(player, null);
            player.tell("§a已卸下称号");
            return;
        }
        const chosen = BUILTIN[id];
        if (!chosen) return;
        setTitle(player, chosen.id);
        player.tell("§a已佩戴称号: " + chosen.name);
    });

    ev.cancelled = true;
}

// Real command registration: BDS routes "/title ..." here before its own
// parser runs, so an unknown command never reaches the vanilla dispatcher.
mc.regPlayerCmd("title", "称号系统", (pl, args, out) => {
    handleCommand(pl, "title " + (args || ""), { cancelled: false });
    out.success();
});

mc.log("[title] 称号插件已加载");
