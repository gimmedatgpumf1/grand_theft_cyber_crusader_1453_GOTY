#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
bash "$ROOT/tools/nagger.sh"
mkdir -p "$ROOT/build/bin" "$ROOT/build/obj" "$ROOT/.cache/ccache"
export CCACHE_DIR="$ROOT/.cache/ccache"
export CCACHE_CONFIGPATH="$ROOT/tools/ccache.conf"
COMPILER="${CXX:-g++}"
if ! command -v "$COMPILER" >/dev/null 2>&1; then
  if command -v clang++ >/dev/null 2>&1; then
    COMPILER=clang++
  else
    echo 'No C++ compiler found. Install g++ or clang++.' >&2
    exit 1
  fi
fi
COMMAND=("$COMPILER")
if command -v ccache >/dev/null 2>&1; then
  COMMAND=(ccache "$COMPILER")
  echo '[build] ccache enabled: content hashes only, persistent local cache.'
else
  echo '[build] WARNING: ccache not found; compiling directly. Install ccache for faster rebuilds.'
fi
mapfile -d '' SOURCES < <(find "$ROOT/src" -name '*.cpp' -print0 | sort -z)
if [ "${#SOURCES[@]}" -eq 0 ]; then
  echo '[build] No sources found.' >&2
  exit 1
fi
"${COMMAND[@]}" -std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic \
  -I"$ROOT/src" "${SOURCES[@]}" -o "$ROOT/build/bin/gtc1453"
echo "[build] Success: $ROOT/build/bin/gtc1453"
if [ "${1:-}" = "--run" ]; then
  exec "$ROOT/build/bin/gtc1453"
fi
