#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
bash "$ROOT/tools/nagger.sh"
mkdir -p "$ROOT/build/bin" "$ROOT/build/obj" "$ROOT/.cache/ccache" "$ROOT/prebuilt/linux-x86_64"
export CCACHE_DIR="$ROOT/.cache/ccache"
export CCACHE_CONFIGPATH="$ROOT/tools/ccache.conf"

SEED="$ROOT/cache/ccache-linux-x86_64.tar.gz"
if [ -f "$SEED" ] && [ -z "$(find "$CCACHE_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  echo '[build] Restoring the committed content-hash ccache seed.'
  tar -xzf "$SEED" -C "$CCACHE_DIR"
fi

COMPILER="${CXX:-g++}"
if ! command -v "$COMPILER" >/dev/null 2>&1; then
  if command -v clang++ >/dev/null 2>&1; then
    COMPILER=clang++
  else
    echo 'No C++ compiler found. Install g++ or clang++.' >&2
    exit 1
  fi
fi
CCACHE_BIN="${CCACHE:-}"
if [ -z "$CCACHE_BIN" ] && [ -x "$ROOT/tools/ccache/linux-x86_64/ccache" ]; then
  CCACHE_BIN="$ROOT/tools/ccache/linux-x86_64/ccache"
elif [ -z "$CCACHE_BIN" ] && command -v ccache >/dev/null 2>&1; then
  CCACHE_BIN="$(command -v ccache)"
fi
if [ -n "$CCACHE_BIN" ] && ! "$CCACHE_BIN" --version >/dev/null 2>&1; then
  echo '[build] Vendored ccache is incompatible with this host; trying a direct build.'
  CCACHE_BIN=""
fi
if [ -n "$CCACHE_BIN" ]; then
  echo "[build] ccache enabled: $("$CCACHE_BIN" --version | head -n 1), content hashes, persistent cache."
else
  echo '[build] WARNING: ccache unavailable; compiling directly.'
fi

mapfile -d '' SOURCES < <(find src -name '*.cpp' -print0 | sort -z)
if [ "${#SOURCES[@]}" -eq 0 ]; then
  echo '[build] No sources found.' >&2
  exit 1
fi
FLAGS=(-std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic -Isrc)
OBJECTS=()
for SOURCE in "${SOURCES[@]}"; do
  RELATIVE="${SOURCE#src/}"
  OBJECT="$ROOT/build/obj/${RELATIVE//\//_}.o"
  OBJECT="${OBJECT%.cpp}.o"
  if [ -n "$CCACHE_BIN" ]; then
    "$CCACHE_BIN" "$COMPILER" "${FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  else
    "$COMPILER" "${FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  fi
  OBJECTS+=("$OBJECT")
done
"$COMPILER" -pthread "${OBJECTS[@]}" -o "$ROOT/build/bin/gtc1453"
if [ "$(uname -s)-$(uname -m)" = "Linux-x86_64" ]; then
  cp "$ROOT/build/bin/gtc1453" "$ROOT/prebuilt/linux-x86_64/gtc1453"
  (cd "$ROOT/prebuilt" && sha256sum linux-x86_64/gtc1453 > SHA256SUMS)
  echo "[build] Prebuilt copy: $ROOT/prebuilt/linux-x86_64/gtc1453"
fi
echo "[build] Success: $ROOT/build/bin/gtc1453"
if [ -n "$CCACHE_BIN" ]; then "$CCACHE_BIN" --show-stats; fi
if [ "${1:-}" = "--run" ]; then
  exec "$ROOT/build/bin/gtc1453"
fi
