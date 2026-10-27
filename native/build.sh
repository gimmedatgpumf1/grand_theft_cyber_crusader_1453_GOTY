#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
bash "$ROOT/tools/nagger.sh"
mkdir -p "$ROOT/build/bin" "$ROOT/build/obj/native" "$ROOT/.cache/ccache" "$ROOT/prebuilt/linux-x86_64"
export CCACHE_DIR="$ROOT/.cache/ccache"
export CCACHE_CONFIGPATH="$ROOT/tools/ccache.conf"
COMPILER="${CXX:-g++}"
if ! command -v "$COMPILER" >/dev/null 2>&1; then
  if command -v clang++ >/dev/null 2>&1; then COMPILER=clang++; else echo 'No C++ compiler found.' >&2; exit 1; fi
fi
CCACHE_BIN="${CCACHE:-}"
if [ -z "$CCACHE_BIN" ] && [ -x "$ROOT/tools/ccache/linux-x86_64/ccache" ]; then
  CCACHE_BIN="$ROOT/tools/ccache/linux-x86_64/ccache"
elif [ -z "$CCACHE_BIN" ] && command -v ccache >/dev/null 2>&1; then
  CCACHE_BIN="$(command -v ccache)"
fi
if [ -n "$CCACHE_BIN" ] && ! "$CCACHE_BIN" --version >/dev/null 2>&1; then CCACHE_BIN=""; fi
FLAGS=(-std=c++17 -O3 -Wall -Wextra -Wpedantic -Isrc -Inative)
SOURCES=(native/human.cpp native/io.cpp native/hair.cpp native/fit.cpp native/render.cpp native/main.cpp src/core/json.cpp)
OBJECTS=()
for SOURCE in "${SOURCES[@]}"; do
  OBJECT="$ROOT/build/obj/native/$(basename "${SOURCE%.cpp}").o"
  if [ -n "$CCACHE_BIN" ]; then
    "$CCACHE_BIN" "$COMPILER" "${FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  else
    "$COMPILER" "${FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  fi
  OBJECTS+=("$OBJECT")
done
"$COMPILER" "${OBJECTS[@]}" -o "$ROOT/build/bin/gtc-native-fit"
if [ "$(uname -s)-$(uname -m)" = "Linux-x86_64" ]; then
  cp "$ROOT/build/bin/gtc-native-fit" "$ROOT/prebuilt/linux-x86_64/gtc-native-fit"
  (cd "$ROOT/prebuilt" && sha256sum linux-x86_64/gtc-native-fit > NATIVE_SHA256SUMS)
fi
echo "[native] Success: $ROOT/build/bin/gtc-native-fit"
if [ -n "$CCACHE_BIN" ]; then "$CCACHE_BIN" --show-stats; fi
