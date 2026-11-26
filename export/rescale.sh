#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$#" -ne 2 ]; then
  echo "Use: ./export/rescale.sh <male|female> <height-in-metres>" >&2
  exit 2
fi
SEX="$1"
HEIGHT="$2"
if [[ "$SEX" != male && "$SEX" != female ]]; then
  echo "Sex must be male or female." >&2
  exit 2
fi
TAG="$(printf '%s' "$HEIGHT" | sed 's/[^0-9A-Za-z.-]/_/g')"
OUT="$ROOT/export/standard_blue_${SEX}_${TAG}m"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
CXX="${CXX:-g++}"
SOURCES=(
  native/human.cpp native/io.cpp native/hair.cpp native/fit.cpp native/render.cpp src/core/json.cpp
  native/client/client.cpp native/client/raster.cpp native/client/raster_frame.cpp native/client/hud.cpp
  native/client/human_renderer.cpp native/client/human_renderer_draw.cpp native/client/human_materials.cpp
  native/client/blockbody.cpp native/client/skeleton_body.cpp native/client/skeleton_body_reference.cpp
  native/client/skeleton_reference_toe.cpp native/client/skeleton_room.cpp native/client/skeleton_comparison_load.cpp
  native/client/world_renderer.cpp native/client/world_renderer_room.cpp native/client/world_renderer_effects.cpp
  native/client/fps_counter.cpp native/client/crosshair.cpp native/client/cheat_menu.cpp native/client/mode_bar.cpp
  native/client/overlay_panel.cpp native/client/interaction_wheel.cpp native/client/status_panel.cpp
  native/client/ui_layer.cpp native/client/target_prompt.cpp native/client/interactive.cpp
  native/client/interactive_app.cpp native/client/opengl_shared.cpp native/client/diagnostics.cpp
  native/client/window_x11.cpp native/client/benchmark.cpp native/client/benchmark_scene.cpp
  native/client/smoke_motion.cpp src/net/platform.cpp src/net/websocket.cpp src/net/sha1.cpp
  native/vulkan/vk_device.cpp native/vulkan/vk_device_pipeline.cpp native/vulkan/vk_renderer.cpp
  native/vulkan/vk_renderer_entry.cpp native/vulkan/vk_upload.cpp native/vulkan/vk_batches.cpp
  native/vulkan/vk_targets.cpp native/vulkan/vk_swapchain.cpp
)
"$CXX" -std=c++20 -O2 -pthread -Isrc -Inative "${SOURCES[@]}" \
  export/export_standard_blue.cpp -ldl -o "$TMP/blue-export"
"$TMP/blue-export" "$SEX" "$HEIGHT" "$OUT"
echo "Wrote $OUT.obj and $OUT.json"
