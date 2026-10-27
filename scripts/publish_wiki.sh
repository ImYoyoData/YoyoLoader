#!/usr/bin/env bash
# Publish the docs in wiki/ to the GitHub Wiki.
#
# GitHub does not let you create the wiki repository through the API: the wiki
# only exists once a first page has been created in the web UI.  Do that once:
#
#   https://github.com/ImYoyoData/YoyoLoader/wiki  ->  "Create the first page"
#
# After that, this script clones the wiki repo, replaces its pages with the
# files in wiki/, and pushes.
#
# Usage: scripts/publish_wiki.sh "sync docs"
set -euo pipefail

MESSAGE="${1:-Update wiki}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="$ROOT/wiki"
REMOTE="https://github.com/ImYoyoData/YoyoLoader.wiki.git"
WORK="$(mktemp -d)"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

if [[ ! -d "$SOURCE" ]]; then
    echo "[wiki] source directory not found: $SOURCE"
    exit 1
fi

echo "[wiki] cloning $REMOTE"
if ! git clone "$REMOTE" "$WORK/repo" 2>/dev/null; then
    cat <<'EOF'
[wiki] clone failed.

GitHub creates the wiki repository only after the first page exists.  Open:

  https://github.com/ImYoyoData/YoyoLoader/wiki

click "Create the first page", save anything, then re-run this script.
EOF
    exit 1
fi

echo "[wiki] copying pages"
rm -f "$WORK"/repo/*.md
cp "$SOURCE"/*.md "$WORK/repo"/

cd "$WORK/repo"
if [[ -z "$(git status --porcelain)" ]]; then
    echo "[wiki] no changes to publish"
    exit 0
fi

git add -A
git -c user.name="${GIT_AUTHOR_NAME:-Yoyo}" \
    -c user.email="${GIT_AUTHOR_EMAIL:-yoyo@local}" \
    commit -q -m "$MESSAGE"

echo "[wiki] pushing"
git push origin HEAD

echo "[wiki] done: https://github.com/ImYoyoData/YoyoLoader/wiki"
