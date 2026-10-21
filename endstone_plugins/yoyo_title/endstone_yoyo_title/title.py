from endstone.event import PlayerChatEvent, event_handler
from endstone.plugin import Plugin

DEFAULT_TITLES = {
    "YoyoTest": "[管理员]",
    "Steve": "[玩家]",
}


class TitlePlugin(Plugin):
    api_version = "0.11"

    def on_enable(self) -> None:
        titles = DEFAULT_TITLES
        try:
            config = self.config
            if isinstance(config, dict) and isinstance(config.get("titles"), dict):
                titles = config["titles"]
        except Exception:
            pass
        self.titles = titles
        self.register_events(self)
        self.logger.info("Yoyo title plugin enabled")

    @event_handler
    def on_player_chat(self, event: PlayerChatEvent) -> None:
        title = self.titles.get(event.player.name, "[玩家]")
        event.message = f"{title} {event.message}"
        self.logger.info(f"chat {event.player.name}: {event.message}")
