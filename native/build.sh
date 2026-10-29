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
  native/client/human_renderer.cpp native/client/human_materials.cpp native/client/blockbody.cpp native/client/skeleton_body.cpp native/client/skeleton_room.cpp native/client/world_renderer.cpp native/client/fps_counter.cpp native/client/crosshair.cpp native/client/cheat_menu.cpp native/client/mode_bar.cpp native/client/overlay_panel.cpp native/client/interaction_wheel.cpp native/client/status_panel.cpp native/client/target_prompt.cpp native/client/interactive.cpp
  native/client/opengl_shared.cpp native/client/diagnostics.cpp native/client/window_x11.cpp native/client/main.cpp native/client/benchmark.cpp native/client/smoke.cpp src/net/platform.cpp src/net/websocket.cpp src/net/sha1.cpp
  native/vulkan/vk_device.cpp native/vulkan/vk_renderer.cpp native/vulkan/vk_batches.cpp native/vulkan/vk_targets.cpp native/vulkan/vk_swapchain.cpp)
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
  "$ROOT/build/obj/native-client/blockbody.o" "$ROOT/build/obj/native-client/skeleton_body.o" "$ROOT/build/obj/native-client/skeleton_room.o" "$ROOT/build/obj/native-client/human_materials.o"
  "$ROOT/build/obj/native-client/world_renderer.o" "$ROOT/build/obj/native-client/fps_counter.o" "$ROOT/build/obj/native-client/crosshair.o" "$ROOT/build/obj/native-client/cheat_menu.o" "$ROOT/build/obj/native-client/mode_bar.o" "$ROOT/build/obj/native-client/overlay_panel.o" "$ROOT/build/obj/native-client/interaction_wheel.o" "$ROOT/build/obj/native-client/status_panel.o" "$ROOT/build/obj/native-client/target_prompt.o" "$ROOT/build/obj/native/human.o"
  "$ROOT/build/obj/native/hair.o" "$ROOT/build/obj/native/json.o"
  "$ROOT/build/obj/native-client/platform.o" "$ROOT/build/obj/native-client/websocket.o"
  "$ROOT/build/obj/native-client/sha1.o" "$ROOT/build/obj/native-client/diagnostics.o"
  "$ROOT/build/obj/native-client/vk_device.o" "$ROOT/build/obj/native-client/vk_renderer.o" "$ROOT/build/obj/native-client/vk_batches.o" "$ROOT/build/obj/native-client/vk_targets.o" "$ROOT/build/obj/native-client/vk_swapchain.o")
"$COMPILER" -pthread "$ROOT/build/obj/native-client/main.o" "$ROOT/build/obj/native-client/benchmark.o" "$ROOT/build/obj/native-client/interactive.o" \
  "$ROOT/build/obj/native-client/window_x11.o" "$ROOT/build/obj/native-client/opengl_shared.o" "${CLIENT_COMMON[@]}" -ldl -o "$ROOT/build/bin/gtc-native-client"
"$COMPILER" -pthread "$ROOT/build/obj/native-client/smoke.o" "${CLIENT_COMMON[@]}" -ldl -o "$ROOT/build/bin/gtc-native-smoke"
# The counter is drawn on the host into pixels, so it can be checked as pixels:
# the box, the border, every glyph block, both byte orders, four window sizes.
"$COMPILER" -pthread -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc -Inative "$ROOT/tests/interface_check.cpp" \
  "${CLIENT_COMMON[@]}" -ldl -o "$ROOT/build/bin/gtc-interface-check"
if [ "$(uname -s)-$(uname -m)" = "Linux-x86_64" ]; then
  cp "$ROOT/build/bin/gtc-native-fit" "$ROOT/prebuilt/linux-x86_64/gtc-native-fit"
  cp "$ROOT/build/bin/gtc-native-client" "$ROOT/prebuilt/linux-x86_64/gtc-native-client"
  cp "$ROOT/build/bin/gtc-native-smoke" "$ROOT/prebuilt/linux-x86_64/gtc-native-smoke"
  (cd "$ROOT/prebuilt" && sha256sum linux-x86_64/gtc-native-fit linux-x86_64/gtc-native-client linux-x86_64/gtc-native-smoke > NATIVE_SHA256SUMS)
fi
echo "[native] Success: $ROOT/build/bin/gtc-native-fit"
echo "[native] Success: $ROOT/build/bin/gtc-native-client"
echo "[native] Success: $ROOT/build/bin/gtc-native-smoke"
echo "[native] Success: $ROOT/build/bin/gtc-interface-check"
if [ -n "$CCACHE_BIN" ]; then "$CCACHE_BIN" --show-stats; fi
