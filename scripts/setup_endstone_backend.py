#!/usr/bin/env python3
"""Install the Endstone live-API backend and the Yoyo plugins."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import venv
import zipfile
from pathlib import Path

ENDSTONE_VERSION = "0.11.14"
MINECRAFT_VERSION = "26.52"


def run(command: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True, env=env)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", default="endstone_server", help="Server directory (default: endstone_server)")
    parser.add_argument("--venv", default=".venv-endstone", help="Virtual environment directory")
    parser.add_argument("--bds-zip", default=None, help="Optional local BDS zip to extract instead of downloading")
    parser.add_argument("--run", action="store_true", help="Start Endstone after setup")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    server = Path(args.server).resolve()
    venv_dir = Path(args.venv).resolve()
    server.mkdir(parents=True, exist_ok=True)
    (server / "plugins").mkdir(parents=True, exist_ok=True)

    if not (venv_dir / ("Scripts" if os.name == "nt" else "bin") / "python").exists():
        print(f"creating virtual environment: {venv_dir}")
        venv.EnvBuilder(with_pip=True).create(venv_dir)

    python = venv_dir / ("Scripts" if os.name == "nt" else "bin") / ("python.exe" if os.name == "nt" else "python")
    run([str(python), "-m", "pip", "install", f"endstone=={ENDSTONE_VERSION}"])

    if args.bds_zip:
        print(f"extracting {args.bds_zip} -> {server}")
        with zipfile.ZipFile(args.bds_zip) as archive:
            archive.extractall(server)
        executable = server / ("bedrock_server.exe" if os.name == "nt" else "bedrock_server")
        if executable.exists():
            if os.name != "nt":
                executable.chmod(executable.stat().st_mode | 0o111)
            (server / "version.txt").write_text(MINECRAFT_VERSION, encoding="utf-8")
        print("BDS extracted; version.txt set to 26.52")

    wheels = sorted((repo / "dist" / "endstone").glob("*.whl"))
    if not wheels:
        print("no plugin wheels found in dist/endstone", file=sys.stderr)
        return 1
    for wheel in wheels:
        shutil.copy2(wheel, server / "plugins" / wheel.name)
        print(f"plugin wheel: {server / 'plugins' / wheel.name}")

    print()
    print("Setup complete.")
    print(f"Start with: {python} -m endstone -s {server} -y")
    if not args.bds_zip:
        print("If BDS is missing, Endstone will download Minecraft 26.52 automatically.")

    if args.run:
        run([str(python), "-m", "endstone", "-s", str(server), "-y"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
