// TPA 传送请求  ——  /tpa
//
//   /tpa              打开在线玩家表单，选择要传送给谁
//   /tpa <玩家>       直接向该玩家发起请求
//   /tpaccept         接受当前请求
//   /tpdeny           拒绝当前请求
//
// 被请求方会收到一张确认表单；如果表单发送失败（客户端不支持），会立刻
// 收到一条聊天消息提示用 /tpaccept 或 /tpdeny 回应。

const pending = new Map(); // 目标名字 -> { from: Player, at: ms }
const PENDING_MS = 60000;

function keyOf(pl) {
    return pl.xuid || pl.name;
}

function onlineNames() {
    return mc.getOnlinePlayers().map((p) => p.name);
}

function requestTeleport(from, toName) {
    const target = mc.getPlayer(toName);
    if (!target) {
        from.tell("§c玩家不在线: " + toName);
        return false;
    }
    if (keyOf(target) === keyOf(from)) {
        from.tell("§c不能传送给自己");
        return false;
    }

    pending.set(keyOf(target), { from: { name: from.name, xuid: from.xuid }, at: Date.now() });
    from.tell("§a已向 §f" + target.name + " §a发送传送请求");

    // 首选：弹窗确认
    const form = mc.newModalForm();
    form.setTitle("传送请求");
    form.setContent("§f" + from.name + " §7请求传送到你这里");
    form.setConfirm("接受");
    form.setCancel("拒绝");
    let delivered = false;
    try {
        target.sendForm(form, (player, result) => {
            if (result === true) accept(player);
            else if (result === false) deny(player, "对方拒绝了");
        });
        delivered = true;
    } catch (err) {
        delivered = false;
    }

    // 兜底：发消息 + 命令
    if (!delivered) {
        target.tell("§e" + from.name + " 请求传送到你这里");
        target.tell("§e输入 §f/tpaccept §e接受，§f/tpdeny §e拒绝");
    } else {
        target.tell("§e" + from.name + " 请求传送到你这里（也可输入 /tpaccept 或 /tpdeny）");
    }
    return true;
}

function takePending(player) {
    const key = keyOf(player);
    const entry = pending.get(key);
    if (!entry) return null;
    if (Date.now() - entry.at > PENDING_MS) {
        pending.delete(key);
        return null;
    }
    pending.delete(key);
    return entry;
}

function accept(player) {
    const entry = takePending(player);
    if (!entry) {
        player.tell("§c没有待处理的传送请求（或已过期）");
        return;
    }
    const from = mc.getPlayer(entry.from.name);
    if (!from) {
        player.tell("§c请求者已离线");
        return;
    }
    const loc = player.pos;
    from.teleport({ x: loc.x, y: loc.y, z: loc.z, dimid: loc.dimid });
    from.tell("§a传送成功");
    player.tell("§a已接受 §f" + entry.from.name + " §a的传送");
}

function deny(player, reason) {
    const entry = takePending(player);
    if (!entry) {
        player.tell("§c没有待处理的传送请求");
        return;
    }
    const from = mc.getPlayer(entry.from.name);
    if (from) from.tell("§c" + reason);
    player.tell("§7已拒绝传送请求");
}

// "/xxx" arrives as a normal chat line from a real client, so the command
// router below is shared by chat and the native command packet.
function handleCommand(pl, cmd, ev) {
    const parts = String(cmd).trim().split(/\s+/);
    const name = parts[0];

    if (name === "tpaccept") {
        accept(pl);
        ev.cancelled = true;
        return;
    }
    if (name === "tpdeny") {
        deny(pl, "对方拒绝了你的传送请求");
        ev.cancelled = true;
        return;
    }
    if (name !== "tpa") return;

    ev.cancelled = true;

    if (parts[1]) {
        requestTeleport(pl, parts[1]);
        return;
    }

    // 无参数：选择玩家的表单
    const others = mc.getOnlinePlayers().filter((p) => keyOf(p) !== keyOf(pl));
    if (others.length === 0) {
        pl.tell("§c没有其他在线玩家");
        return;
    }
    const form = mc.newSimpleForm();
    form.setTitle("传送请求");
    form.setContent("选择要传送过去的玩家");
    for (const p of others) form.addButton(p.name);

    pl.sendForm(form, (player, id) => {
        if (id === null || id === undefined) return;
        const chosen = others[id];
        if (chosen) requestTeleport(player, chosen.name);
    });
}

mc.listen("onLeft", (pl) => {
    pending.delete(keyOf(pl));
});

// Real command registration: BDS routes "/tpa ..." here before its own
// parser runs, so an unknown command never reaches the vanilla dispatcher.
mc.regPlayerCmd("tpa", "传送请求", (pl, args, out) => {
    handleCommand(pl, "tpa " + (args || ""), { cancelled: false });
    out.success();
});

mc.log("[tpa] 传送请求插件已加载");
