#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SEED="$ROOT/cache/ccache-linux-x86_64.tar.gz"
mkdir -p "$ROOT/build" "$ROOT/cache"
TEMP="$ROOT/build/ccache-seed.tmp.$$"
BACKUP="$ROOT/build/ccache-seed.backup.$$"
trap 'rm -f "$TEMP" "$BACKUP"' EXIT
if [ -f "$SEED" ]; then cp "$SEED" "$BACKUP"; fi
rm -rf "$ROOT/.cache/ccache"
rm -f "$SEED"
if ! "$ROOT/build.sh"; then
  if [ -f "$BACKUP" ]; then cp "$BACKUP" "$SEED"; fi
  echo '[cache] Build failed; previous seed restored.' >&2
  exit 1
fi
find "$ROOT/.cache/ccache" -type f ! -name stats ! -name CACHEDIR.TAG \
  ! -path '*/tmp/*' ! -path '*/lock/*' -printf '%P\n' | sort | \
  tar -czf "$TEMP" -C "$ROOT/.cache/ccache" -T -
mv "$TEMP" "$SEED"
(cd "$ROOT/cache" && sha256sum ccache-linux-x86_64.tar.gz > SHA256SUMS)
echo "[cache] Portable seed rebuilt: $SEED"
