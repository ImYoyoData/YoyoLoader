from endstone.plugin import Plugin
from endstone.scoreboard import Criteria, DisplaySlot, ObjectiveSortOrder


class SidebarPlugin(Plugin):
    api_version = "0.11"

    def on_enable(self) -> None:
        self.objective = None
        self.server.scheduler.run_task(self, self.update, delay=20, period=20)
        self.logger.info("Yoyo sidebar plugin enabled")

    def update(self) -> None:
        try:
            board = self.server.scoreboard
            if self.objective is None:
                try:
                    self.objective = board.get_objective("yoyo_sidebar")
                except Exception:
                    self.objective = None
                if self.objective is None:
                    self.objective = board.add_objective(
                        "yoyo_sidebar", Criteria.DUMMY, "YoyoLoader"
                    )
                if self.objective is not None:
                    self.objective.set_display(DisplaySlot.SIDE_BAR, ObjectiveSortOrder.DESCENDING)
                    self.logger.info(
                        f"sidebar objective={self.objective.name} display_slot={self.objective.display_slot}"
                    )

            tps = round(float(self.server.current_tps))
            time_ticks = int(self.server.level.time)
            hhmm = (time_ticks // 1000) * 100 + ((time_ticks % 1000) * 60 // 1000)
            players = len(self.server.online_players)
            actors = self.server.level.get_dimension("overworld").actors
            entities = len(actors)
            items = sum(1 for actor in actors if getattr(actor, "type", "") == "minecraft:item")

            values = {
                "TPS": tps,
                "TIME": hhmm,
                "PLAYERS": players,
                "ENTITIES": entities,
                "ITEMS": items,
            }
            for entry, value in values.items():
                self.objective.get_score(entry).value = int(value)

            self.logger.info(
                f"sidebar tps={tps} time={hhmm} players={players} entities={entities} items={items}"
            )
        except Exception as exc:  # keep the scheduler alive on transient world errors
            self.logger.error(f"sidebar update failed: {exc}")

    def on_disable(self) -> None:
        try:
            self.server.scheduler.cancel_tasks(self)
        except Exception:
            pass
