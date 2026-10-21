# Endstone live-API backend

YoyoLoader's native runtime is a small, symbol-free injector with events, JS/C++/Go
plugins, NBT, and data models. For the **complete live API set** (real Player,
Block, Item, Container, Entity, Forms, Scoreboard), the project now also ships an
Endstone backend.

Endstone is Apache-2.0, supports BDS 1.26.52 on Windows and Linux, and already
implements the live bindings using its fake headers and per-version symbol
tables. Re-implementing those bindings from scratch would duplicate years of
reverse engineering.

## What is included

| Path | Purpose |
|---|---|
| `endstone_plugins/yoyo_title` | Endstone Python plugin: intercepts `PlayerChatEvent`, prefixes a title, releases the modified message |
| `endstone_plugins/yoyo_sidebar` | Endstone Python plugin: real scoreboard sidebar with TPS/time/players/entities/items |
| `dist/endstone/*.whl` | Prebuilt wheels for both plugins |
| `scripts/setup_endstone_backend.py` | Creates a venv, installs Endstone, copies the wheels, optionally extracts BDS |

## Setup

```bash
python scripts/setup_endstone_backend.py \
    --server endstone_server \
    --bds-zip /path/to/bedrock-server-1.26.52.3-win.zip \
    --run
```

On Linux use the Linux BDS zip. Without `--bds-zip`, Endstone downloads BDS
1.26.52 itself.

Manual equivalent:

```bash
python -m venv .venv-endstone
.venv-endstone/bin/pip install endstone==0.11.14
mkdir -p endstone_server/plugins
cp dist/endstone/*.whl endstone_server/plugins/
.venv-endstone/bin/python -m endstone -s endstone_server -y
```

On Windows use `.venv-endstone\Scripts\python.exe` and
`.venv-endstone\Scripts\pip.exe`.

Server directory requirements:

- `bedrock_server` / `bedrock_server.exe`;
- `version.txt` containing `26.52` when you supply BDS yourself;
- `plugins/` containing the two wheels.

## Plugins

### yoyo_title

`PlayerChatEvent` is a standard Endstone cancelable event with a writable
`message`. The plugin does:

```python
@event_handler
def on_player_chat(self, event: PlayerChatEvent) -> None:
    title = self.titles.get(event.player.name, "[玩家]")
    event.message = f"{title} {event.message}"
```

Default titles are `YoyoTest -> [管理员]`, `Steve -> [玩家]`. To override them,
create `plugins/yoyo_title/config.toml`:

```toml
[titles]
YoyoTest = "[管理员]"
Steve = "[玩家]"
```

The plugin name is the entry point `yoyo_title`, so Endstone looks for
`plugins/yoyo_title/config.toml`.

### yoyo_sidebar

The plugin creates a dummy scoreboard objective `yoyo_sidebar` (display name
`YoyoLoader`) and sets it to `DisplaySlot.SIDE_BAR`. Every 20 ticks it updates:

- `TPS` — `server.current_tps`
- `TIME` — game time as HHMM (e.g. `630` = 06:30)
- `PLAYERS` — `server.online_players`
- `ENTITIES` — overworld actors
- `ITEMS` — actors whose type is `minecraft:item`

It uses `server.scheduler.run_task(..., period=20)` and the Endstone scoreboard
API, so the sidebar is a real BDS sidebar, not a chat-log substitute.

## Verification

Windows (Endstone 0.11.14, BDS 26.52, Windows x64):

```text
[YoyoSidebar] sidebar objective=yoyo_sidebar display_slot=DisplaySlot.SIDE_BAR
[YoyoSidebar] sidebar tps=20 time=52 players=1 entities=1 items=0
[YoyoSidebar] sidebar tps=20 time=55 players=1 entities=3 items=0
```

Linux (Docker, python:3.12-slim):

```text
[YoyoTitle] Loading yoyo_title v0.1.0
[YoyoSidebar] Loading yoyo_sidebar v0.1.0
[YoyoSidebar] sidebar objective=yoyo_sidebar display_slot=DisplaySlot.SIDE_BAR
[YoyoSidebar] sidebar tps=20 time=1 players=0 entities=0 items=0
```

The title plugin loads and enables on both platforms. Its chat rewrite could not
be exercised end-to-end with `bedrock-protocol`: BDS drops the bot's TextPacket
before any loader's chat handler (the same behavior was observed with the native
YoyoLoader hook). A real Minecraft client is required for that final chat check;
`PlayerChatEvent.message` is the standard Endstone API and the plugin code uses
only that path.

## Relationship to YoyoLoader native

- **Endstone backend**: complete live Player/Block/Item/Container/Entity/Forms/
  Scoreboard API; plugins are Python or C++.
- **YoyoLoader native runtime**: custom launcher, fast event hooks, C++/Go/JS
  plugins, full NBT and form data models; live world access remains data-only.

Do not inject both loaders into the same `bedrock_server` process; use one per
server directory. The two plugin wheels are for the Endstone backend.
