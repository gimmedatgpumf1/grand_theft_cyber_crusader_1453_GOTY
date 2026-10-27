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

mkdir -p "$ROOT/build/obj/native-client"
CLIENT_FLAGS=("${FLAGS[@]}" -pthread)
CLIENT_SOURCES=(native/client/client.cpp native/client/raster.cpp native/client/hud.cpp
  native/client/human_renderer.cpp native/client/world_renderer.cpp native/client/interactive.cpp
  native/client/window_x11.cpp native/client/main.cpp native/client/smoke.cpp src/net/platform.cpp src/net/websocket.cpp src/net/sha1.cpp)
CLIENT_OBJECTS=()
for SOURCE in "${CLIENT_SOURCES[@]}"; do
  OBJECT="$ROOT/build/obj/native-client/$(basename "${SOURCE%.cpp}").o"
  if [ -n "$CCACHE_BIN" ]; then
    "$CCACHE_BIN" "$COMPILER" "${CLIENT_FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  else
    "$COMPILER" "${CLIENT_FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  fi
  CLIENT_OBJECTS+=("$OBJECT")
done
CLIENT_COMMON=("$ROOT/build/obj/native-client/client.o" "$ROOT/build/obj/native-client/raster.o"
  "$ROOT/build/obj/native-client/hud.o" "$ROOT/build/obj/native-client/human_renderer.o"
  "$ROOT/build/obj/native-client/world_renderer.o" "$ROOT/build/obj/native/human.o"
  "$ROOT/build/obj/native/hair.o" "$ROOT/build/obj/native/json.o"
  "$ROOT/build/obj/native-client/platform.o" "$ROOT/build/obj/native-client/websocket.o"
  "$ROOT/build/obj/native-client/sha1.o")
"$COMPILER" -pthread "$ROOT/build/obj/native-client/main.o" "$ROOT/build/obj/native-client/interactive.o" \
  "$ROOT/build/obj/native-client/window_x11.o" "${CLIENT_COMMON[@]}" -ldl -o "$ROOT/build/bin/gtc-native-client"
"$COMPILER" -pthread "$ROOT/build/obj/native-client/smoke.o" "${CLIENT_COMMON[@]}" -o "$ROOT/build/bin/gtc-native-smoke"
if [ "$(uname -s)-$(uname -m)" = "Linux-x86_64" ]; then
  cp "$ROOT/build/bin/gtc-native-fit" "$ROOT/prebuilt/linux-x86_64/gtc-native-fit"
  cp "$ROOT/build/bin/gtc-native-client" "$ROOT/prebuilt/linux-x86_64/gtc-native-client"
  cp "$ROOT/build/bin/gtc-native-smoke" "$ROOT/prebuilt/linux-x86_64/gtc-native-smoke"
  (cd "$ROOT/prebuilt" && sha256sum linux-x86_64/gtc-native-fit linux-x86_64/gtc-native-client linux-x86_64/gtc-native-smoke > NATIVE_SHA256SUMS)
fi
echo "[native] Success: $ROOT/build/bin/gtc-native-fit"
echo "[native] Success: $ROOT/build/bin/gtc-native-client"
echo "[native] Success: $ROOT/build/bin/gtc-native-smoke"
if [ -n "$CCACHE_BIN" ]; then "$CCACHE_BIN" --show-stats; fi
