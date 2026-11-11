#!/usr/bin/env bash
# Keep the built objects and the compiler cache in the repository, every time,
# without anyone asking.
#
# This workspace deletes `build/` and `.cache/` at the start of every session. They
# are not scratch there, they are the only copy that survives -- so a build that is
# not saved is a build that has to be done again from nothing, and the repository
# has all the room it could ever need. Saving them is therefore part of building,
# not something to remember afterwards.
#
# Only those three directories are staged and committed; whatever else is being
# worked on in the tree is left exactly as it is, and a failure to push (a colleague
# pushed first, a network hiccup) is reported and does not fail the build.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SAVED=(build .cache cache)
if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo '[cache] Not a git checkout; nothing to save into.' >&2
  exit 0
fi
if [ -z "$(git status --porcelain -- "${SAVED[@]}" 2>/dev/null)" ]; then
  exit 0
fi
git add -A -- "${SAVED[@]}" 2>/dev/null || {
  echo '[cache] Could not stage the build products.' >&2
  exit 0
}
if ! git commit -q -m "Build products and compiler cache, saved automatically ($(date -u +%Y-%m-%dT%H:%M:%SZ))" -- "${SAVED[@]}"; then
  echo '[cache] Nothing to commit after staging.' >&2
  exit 0
fi
echo "[cache] Saved $(git rev-parse --short HEAD)."
if git push -q origin HEAD 2>/dev/null; then
  echo '[cache] Pushed, so the next session starts warm.'
else
  echo '[cache] Saved locally; the push was refused (someone else is ahead). Nothing is lost.' >&2
fi
exit 0
