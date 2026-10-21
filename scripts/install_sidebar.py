#!/usr/bin/env python3
"""Install the Yoyo Sidebar behavior pack into a Bedrock server directory."""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

PACK_NAME = "yoyo_sidebar"


def read_property(text: str, key: str) -> str | None:
    for line in text.splitlines():
        line = line.strip()
        if line.startswith(key + "="):
            return line.split("=", 1)[1]
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", default=".", help="Bedrock server directory (default: current directory)")
    parser.add_argument("--world", default=None, help="World name (default: level-name from server.properties)")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    source = repo / "plugins" / "sidebar_pack"
    server = Path(args.server).resolve()
    if not (source / "manifest.json").is_file():
        print(f"sidebar pack source not found: {source}", file=sys.stderr)
        return 1
    if not server.is_dir():
        print(f"server directory not found: {server}", file=sys.stderr)
        return 1

    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    pack_uuid = manifest["header"]["uuid"]
    version = manifest["header"]["version"]

    destination = server / "behavior_packs" / PACK_NAME
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "manifest.json", destination / "manifest.json")
    shutil.copytree(source / "scripts", destination / "scripts", dirs_exist_ok=True)
    print(f"installed behavior pack: {destination}")

    world_name = args.world
    properties_path = server / "server.properties"
    if world_name is None:
        if properties_path.is_file():
            world_name = read_property(properties_path.read_text(encoding="utf-8", errors="ignore"), "level-name")
        world_name = world_name or "Bedrock level"

    world_dir = server / "worlds" / world_name
    world_dir.mkdir(parents=True, exist_ok=True)
    packs_path = world_dir / "world_behavior_packs.json"
    packs = []
    if packs_path.is_file():
        try:
            packs = json.loads(packs_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            packs = []
    packs = [entry for entry in packs if entry.get("pack_id") != pack_uuid]
    packs.append({"pack_id": pack_uuid, "version": version})
    packs_path.write_text(json.dumps(packs, indent=2) + "\n", encoding="utf-8")
    print(f"enabled in world: {packs_path}")

    if properties_path.is_file():
        text = properties_path.read_text(encoding="utf-8", errors="ignore")
        changed = False
        for key in ("content-log-console-output-enabled", "content-log-file-enabled"):
            if read_property(text, key) is None:
                text += f"\n{key}=true\n"
                changed = True
            elif read_property(text, key).lower() != "true":
                text = "\n".join(
                    f"{key}=true" if line.strip().startswith(key + "=") else line
                    for line in text.splitlines()
                ) + "\n"
                changed = True
        if changed:
            properties_path.write_text(text, encoding="utf-8")
            print("enabled BDS content logging in server.properties")

    print("Restart the server; the sidebar is updated once per second.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
