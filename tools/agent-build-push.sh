#!/usr/bin/env bash
# Agent-only checkpoint build. Existing user build scripts never invoke this file.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPECTED_BRANCH="arena/1ea56266-grand-theft-cyber-crusader-145"
TARGET="${1:-native}"
MESSAGE="${2:-Agent build checkpoint}"
cd "$ROOT"

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [ "$BRANCH" != "$EXPECTED_BRANCH" ]; then
  echo "[agent-build] Refusing to build/push from '$BRANCH'; expected '$EXPECTED_BRANCH'." >&2
  exit 1
fi

case "$TARGET" in
  native) bash "$ROOT/native/build.sh" ;;
  server) bash "$ROOT/build.sh" ;;
  all)
    bash "$ROOT/build.sh"
    bash "$ROOT/native/build.sh"
    ;;
  *)
    echo "Usage: bash tools/agent-build-push.sh [native|server|all] [commit message]" >&2
    exit 2
    ;;
esac

if [ "$TARGET" = all ]; then
  bash "$ROOT/tests/run_tests.sh"
fi

if [ -z "$(git status --porcelain)" ]; then
  echo '[agent-build] Build succeeded; no source or prebuilt changes to upload.'
  exit 0
fi

git add -A
git commit -m "$MESSAGE"
git push origin "$EXPECTED_BRANCH"
echo '[agent-build] Build succeeded; checkpoint uploaded to GitHub.'
