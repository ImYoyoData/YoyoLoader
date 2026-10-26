#!/usr/bin/env bash
# Cut a release: merge dev into main, tag it, and let GitHub Actions publish.
#
# The workflow (.github/workflows/release.yml) builds and packages both platforms
# on the tag push and attaches the archives to a GitHub Release.
#
# Usage: scripts/release.sh 0.1.0
set -euo pipefail

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
    echo "usage: scripts/release.sh <version>    e.g. scripts/release.sh 0.1.0"
    exit 1
fi
VERSION="${VERSION#v}"
TAG="v$VERSION"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "[release] not a git repository"
    exit 1
fi

if [[ -n "$(git status --porcelain)" ]]; then
    echo "[release] working tree is not clean; commit or stash first"
    git status --short
    exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "[release] tag $TAG already exists"
    exit 1
fi

echo "[release] fetching origin"
git fetch origin

CURRENT="$(git rev-parse --abbrev-ref HEAD)"
if [[ "$CURRENT" != "dev" ]]; then
    echo "[release] expected to run from dev (currently $CURRENT)"
    exit 1
fi

echo "[release] merging dev into main"
git checkout main
git pull --ff-only origin main
git merge --no-ff dev -m "Release $TAG"

echo "[release] tagging $TAG"
git tag -a "$TAG" -m "YoyoLoader $VERSION"

echo "[release] pushing main and $TAG"
git push origin main
git push origin "$TAG"

echo "[release] done."
echo "  main is at $(git rev-parse --short HEAD)"
echo "  watch the run: https://github.com/ImYoyoData/YoyoLoader/actions"
echo "  back to dev: git checkout dev"
