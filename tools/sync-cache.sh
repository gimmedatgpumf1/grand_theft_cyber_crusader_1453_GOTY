#!/usr/bin/env bash
# Every successful build is a commit, and the commit is pushed.
#
# Two things are true of this workspace and both are the reason this is not left to
# anyone's memory. `build/` and `.cache/` are deleted at the start of every session,
# so a build that is not saved is a build that has to be done again from nothing --
# and the repository is the only place they outlive the build that made them. And a
# green build is a checkpoint: it is the moment the tree is known to compile, which is
# worth a commit on its own, for every changed file and not only the products.
#
# A push that is refused (a colleague pushed first, a network hiccup) is reported and
# does not fail the build: the commit is local and nothing is lost.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo '[build] Not a git checkout; nothing to record.' >&2
  exit 0
fi
if [ -z "$(git status --porcelain 2>/dev/null)" ]; then
  exit 0
fi
git add -A 2>/dev/null || {
  echo '[build] Could not stage the tree.' >&2
  exit 0
}
if ! git commit -q -m "Build succeeded: everything that changed ($(date -u +%Y-%m-%dT%H:%M:%SZ))"; then
  echo '[build] Nothing staged to commit.' >&2
  exit 0
fi
echo "[build] Committed $(git rev-parse --short HEAD): $(git show --stat --oneline HEAD | tail -1)"
if git push -q origin HEAD 2>/dev/null; then
  echo '[build] Pushed.'
else
  echo '[build] Committed locally; the push was refused (someone else is ahead). Nothing is lost.' >&2
fi
exit 0
