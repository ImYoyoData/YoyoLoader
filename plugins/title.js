// Title plugin (称号插件)
// Intercepts chat, prefixes the player's title, and lets the modified packet
// continue. The runtime rewrites the native TextPacket message in place.
//
// Copy this file to <server>/plugins/title.js.

const TITLES = {
  "YoyoTest": "[管理员]",
  "Steve": "[玩家]",
};

mc.listen("onChat", function (player, message, event) {
  const title = TITLES[player.name] || "[玩家]";
  event.message = title + " " + message;
});

// Self-test hooks: these run without a live chat packet and exercise the same
// JS listener + native string rewrite path.
mc.log("title simulate:", mc.__simulateChat("YoyoTest", "hello world"));
mc.log("title native rewrite:", mc.__rewriteNative("hello world"));
