#!/usr/bin/env bash
# Package a ready-to-drop-in YoyoLoader release for Linux.
#
# The tarball mirrors a BDS install, so users extract it straight into their
# server root:
#   yoyo
#   libyoyo_runtime.so
#   plugins/...
#   README.txt
#
# Usage: scripts/package_linux.sh [version]
set -euo pipefail

VERSION="${1:-dev}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dist"
STAGE="$OUT/stage-linux64"
ARCHIVE="$OUT/YoyoLoader-$VERSION-linux64.tar.gz"

if [[ ! -f "$ROOT/build-linux/bin/yoyo" ]]; then
    echo "[package] build-linux/bin/yoyo missing; run scripts/build_linux.sh first"
    exit 1
fi
if [[ ! -f "$ROOT/build-linux/bin/libyoyo_runtime.so" ]]; then
    echo "[package] build-linux/bin/libyoyo_runtime.so missing; run scripts/build_linux.sh first"
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/plugins"

echo "[package] copying binaries"
cp "$ROOT/build-linux/bin/yoyo" "$STAGE/yoyo"
cp "$ROOT/build-linux/bin/libyoyo_runtime.so" "$STAGE/libyoyo_runtime.so"
chmod +x "$STAGE/yoyo"

echo "[package] copying sample plugins"
if [[ -f "$ROOT/build-linux/bin/plugins/sample_cpp.so" ]]; then
    mkdir -p "$STAGE/plugins/sample_cpp"
    cp "$ROOT/build-linux/bin/plugins/sample_cpp.so" "$STAGE/plugins/sample_cpp/"
    printf '%s\n' '{"name":"sample_cpp","version":"1.0.0","engine":"native","entry":"sample_cpp.so","description":"YoyoLoader C++ ABI sample"}' \
        > "$STAGE/plugins/sample_cpp/manifest.json"
fi

for plugin in home title tpa; do
    mkdir -p "$STAGE/plugins/$plugin"
    cp "$ROOT/plugins/$plugin/main.js" "$STAGE/plugins/$plugin/main.js"
    cp "$ROOT/plugins/$plugin/manifest.json" "$STAGE/plugins/$plugin/manifest.json"
done

echo "[package] writing README.txt"
cat > "$STAGE/README.txt" <<'EOF'
YoyoLoader (Linux x64)

HOW TO USE
  1. Extract this archive into your BDS server root, next to bedrock_server.
  2. Make sure yoyo is executable:  chmod +x yoyo
  3. Run ./yoyo instead of ./bedrock_server.

  The launcher sets LD_PRELOAD and execs bedrock_server from the current
  directory. Plugins live in plugins/<name>/manifest.json.

WHAT IS INSIDE
  yoyo                 launcher / injector
  libyoyo_runtime.so   injected runtime (events, JS, plugins)
  plugins/home         example: /home waypoints
  plugins/title        example: /title name tags
  plugins/tpa          example: /tpa teleport requests

SUPPORTED BDS BUILDS
  1.26.52.3   full support (Windows / Linux)
  1.26.70.21  partial (see yoyo.log at startup)
  Other builds are probed automatically; unsupported features are reported in
  yoyo.log and refused rather than crashing.

Version support and limitations: see the project README / wiki.
EOF

echo "[package] creating $ARCHIVE"
tar -czf "$ARCHIVE" -C "$STAGE" .

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$ARCHIVE" | awk '{print $1}' > "$ARCHIVE.sha256"
fi

echo "[package] done: $ARCHIVE"
