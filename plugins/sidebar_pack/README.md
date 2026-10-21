# Yoyo Sidebar

A Bedrock behavior pack that maintains a scoreboard sidebar once per second:

```text
YoyoLoader
  TPS       20
  TIME      630        (game time as HHMM)
  PLAYERS   1
  ENTITIES  37
  ITEMS     4          (minecraft:item entities)
```

## Install

From this repository, run:

```bash
python scripts/install_sidebar.py --server /path/to/bedrock-server
```

On Windows:

```bat
python scripts\install_sidebar.py --server "D:\path\to\bedrock-server"
```

The installer:

- copies `plugins/sidebar_pack` to `<server>/behavior_packs/yoyo_sidebar`;
- enables it in `<server>/worlds/<level-name>/world_behavior_packs.json`;
- enables BDS content logging so `[YoyoSidebar]` lines appear in the console.

Restart the server afterwards.

If you use a world name other than `level-name`:

```bash
python scripts/install_sidebar.py --server /srv/bds --world "My World"
```

## Notes

- The objective is `yoyo_sidebar`; its display name is `YoyoLoader`.
- Lines are integer scoreboard entries: `TPS`, `TIME`, `PLAYERS`, `ENTITIES`, `ITEMS`.
- `TIME` is game time encoded as `HHMM` (e.g. `630` = 06:30). Scoreboards only
  store integers, so it cannot be rendered as `06:30` without a separate plugin.
- Entity/item counts come from `@minecraft/server`'s `dimension.getEntities()`.
- Tested with BDS 1.26.52.3. The behavior pack requires `@minecraft/server`
  1.17.0, which BDS allows by default in `config/default/permissions.json`.
- This pack does not need the YoyoLoader native runtime; it uses BDS's built-in
  scripting engine.
