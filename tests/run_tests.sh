#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
bash tools/nagger.sh
# Check each body's actual triangles and registered source, not standing-pose assumptions.
python3 tests/openskeleton_source_pose.py female
python3 tests/openskeleton_source_pose.py male
python3 tests/openskeleton_extract.py
python3 tests/skeleton_line_graph.py
python3 tests/skeleton_mobility.py
# The Windows and Linux builds keep separate source lists by hand; this checks
# they have not drifted and that the object lists are still well formed.
python3 tests/build_scripts.py
# The shaders are generated, not compiled, so the tree is checked against the
# generator: a stale .spv or header fails here rather than at pipeline creation.
python3 native/vulkan/make_shader_header.py --check
# The Khronos validator is the authority on whether a driver may be handed a
# module at all. It is not required to build, so it is used when it is present:
# on the machine that compiles the shaders.
if command -v spirv-val >/dev/null 2>&1; then
  for module in native/vulkan/shaders/*.spv; do
    spirv-val --target-env vulkan1.0 "$module" || { echo "spirv-val rejected $module"; exit 1; }
  done
  echo "spirv-val: PASS (all modules)"
else
  echo "spirv-val: skipped (validator not installed)"
fi
mkdir -p build/bin .cache/ccache
export CCACHE_DIR="$ROOT/.cache/ccache"
export CCACHE_CONFIGPATH="$ROOT/tools/ccache.conf"
COMPILER="${CXX:-g++}"
COMMAND=("$COMPILER")
if command -v ccache >/dev/null 2>&1; then COMMAND=(ccache "$COMPILER"); fi
"${COMMAND[@]}" -std=c++17 -O1 -g -pthread -Wall -Wextra -Wpedantic -Isrc \
  tests/tests.cpp src/core/collision.cpp src/core/json.cpp src/core/pathfinding.cpp src/core/projectile.cpp src/core/world.cpp \
  src/net/platform.cpp src/net/websocket.cpp \
  -o build/bin/gtc1453_tests
./build/bin/gtc1453_tests
echo '[test] C++ simulation and signed geometry checks passed.'
"${COMMAND[@]}" -std=c++17 -O2 -pthread -Isrc -Inative \
  tests/skeleton_source_room.cpp native/client/skeleton_room.cpp native/client/skeleton_body.cpp \
  native/client/raster.cpp native/client/hud.cpp native/client/ui_layer.cpp -o build/bin/gtc-skeleton-source-room
./build/bin/gtc-skeleton-source-room
"${COMMAND[@]}" -std=c++17 -O2 -pthread -Isrc -Inative \
  tests/skeleton_source_structures.cpp native/client/skeleton_body.cpp native/client/raster.cpp \
  native/client/hud.cpp native/client/ui_layer.cpp -o build/bin/gtc-skeleton-source-structures
./build/bin/gtc-skeleton-source-structures
"${COMMAND[@]}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc -Inative \
  tests/calibrated_joint.cpp -o build/bin/gtc-calibrated-joint
./build/bin/gtc-calibrated-joint
"${COMMAND[@]}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc -Inative \
  tests/calibrated_coupling.cpp -o build/bin/gtc-calibrated-coupling
./build/bin/gtc-calibrated-coupling
"${COMMAND[@]}" -std=c++17 -O2 -Isrc -Inative tests/room_freecam.cpp -o build/bin/gtc-room-freecam
./build/bin/gtc-room-freecam
# Actual App event routing and WebSocket input, including walking before/after C.
"${COMMAND[@]}" -std=c++17 -O2 -pthread -Isrc -Inative tests/interactive_room.cpp \
  native/client/{client,raster,hud,human_renderer,human_materials,blockbody,skeleton_body,skeleton_room,world_renderer,fps_counter,crosshair,cheat_menu,mode_bar,overlay_panel,interaction_wheel,status_panel,ui_layer,target_prompt,diagnostics,window_x11,opengl_shared}.cpp \
  native/{human,hair}.cpp src/core/json.cpp src/net/{platform,websocket,sha1}.cpp \
  native/vulkan/{vk_device,vk_renderer,vk_upload,vk_batches,vk_targets,vk_swapchain}.cpp \
  -ldl -o build/bin/gtc-interactive-room
./build/bin/gtc-interactive-room
python3 tests/quiet_interactive.py
# The Vulkan projection can be checked without a driver, so it is: the clip
# matrix has to put every point on the pixel the CPU rasterizer would use.
"${COMMAND[@]}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc -Inative \
  tests/vulkan_projection.cpp -o build/bin/gtc-vulkan-projection
./build/bin/gtc-vulkan-projection
# The same backend, end to end, against the stub driver in tests/fake-vulkan.
# It goes in its own directory so it can never answer for the real client.
mkdir -p build/vulkan-stub
"${COMMAND[@]}" -std=c++17 -O2 -shared -fPIC -Inative/vendor/vulkan/include \
  tests/fake-vulkan/stub.cpp -o build/vulkan-stub/libvulkan.so.1
"${COMMAND[@]}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc -Inative \
  tests/vulkan_plumbing.cpp native/vulkan/vk_device.cpp native/vulkan/vk_renderer.cpp native/vulkan/vk_upload.cpp native/vulkan/vk_batches.cpp native/vulkan/vk_targets.cpp native/vulkan/vk_swapchain.cpp \
  native/client/diagnostics.cpp native/client/raster.cpp native/client/hud.cpp native/client/ui_layer.cpp \
  -o build/bin/gtc-vulkan-plumbing
LD_LIBRARY_PATH="$ROOT/build/vulkan-stub" ./build/bin/gtc-vulkan-plumbing
GTC_FAKE_STAGED=1 LD_LIBRARY_PATH="$ROOT/build/vulkan-stub" ./build/bin/gtc-vulkan-plumbing
# A real Vulkan ICD is optional here. This test submits the UI shaders and
# checks the pixels; the stub test above cannot establish rendering correctness.
"${COMMAND[@]}" -std=c++17 -O2 -Isrc -Inative \
  tests/vulkan_ui.cpp native/vulkan/vk_device.cpp native/vulkan/vk_renderer.cpp native/vulkan/vk_upload.cpp native/vulkan/vk_batches.cpp native/vulkan/vk_targets.cpp native/vulkan/vk_swapchain.cpp \
  native/client/diagnostics.cpp native/client/raster.cpp native/client/hud.cpp native/client/ui_layer.cpp \
  native/client/crosshair.cpp native/client/cheat_menu.cpp native/client/mode_bar.cpp native/client/fps_counter.cpp native/client/status_panel.cpp native/client/target_prompt.cpp \
  -ldl -o build/bin/gtc-vulkan-ui-check
if [ "${GTC_VULKAN_UI_TEST:-0}" = 1 ]; then
  ./build/bin/gtc-vulkan-ui-check
  # Actual Vulkan rendering through the C room, with repeated target resizes.
  "${COMMAND[@]}" -std=c++17 -O2 -pthread -Isrc -Inative \
    tests/skeleton_resize_vulkan.cpp native/client/skeleton_room.cpp native/client/skeleton_body.cpp \
    native/client/diagnostics.cpp native/client/raster.cpp native/client/hud.cpp native/client/ui_layer.cpp \
    native/vulkan/vk_device.cpp native/vulkan/vk_renderer.cpp native/vulkan/vk_upload.cpp \
    native/vulkan/vk_batches.cpp native/vulkan/vk_targets.cpp native/vulkan/vk_swapchain.cpp \
    -ldl -o build/bin/gtc-skeleton-resize-vulkan
  ./build/bin/gtc-skeleton-resize-vulkan
  python3 tests/skeleton_headless_wsi.py
else
  echo 'Vulkan UI pixel check: skipped (set GTC_VULKAN_UI_TEST=1 with a Vulkan ICD)'
fi
if [ -x "$ROOT/build/bin/gtc-native-smoke" ]; then
  # A machine without a Vulkan driver must say so and keep going; this asserts
  # the fallback rather than the frame rate, because the frame rate is not
  # something this machine can answer for.
  "$ROOT/build/bin/gtc-native-smoke" --vulkan-report
fi
# The room -- the thing the C key opens -- through the same stub, so its reuse
# logic and the client's own options are exercised and not just the renderer's.
if [ -x "$ROOT/build/bin/gtc-native-client" ]; then
  LD_LIBRARY_PATH="$ROOT/build/vulkan-stub" "$ROOT/build/bin/gtc-native-client" \
    --room-benchmark 4 --width 640 --height 480 --renderer vulkan \
    --output "$ROOT/build/native-smoke/vk-room.png"
fi
# The room animates bones as rest geometry moved by one matrix each, while the
# software path transforms every vertex. If those two disagree the same scene
# looks different depending on the backend, so they are compared directly.
if [ -x "$ROOT/build/bin/gtc-native-client" ]; then
  "$ROOT/build/bin/gtc-native-client" --verify-pose
fi
# The interface is one box with a frame rate in it, drawn on the host into the
# finished picture. That means it can be checked exactly here: every glyph block,
# both byte orders, four window sizes, and the box in the place the header names.
mkdir -p build/native-client
if [ -x "$ROOT/build/bin/gtc-interface-check" ]; then
  "$ROOT/build/bin/gtc-interface-check"
fi
if [ -x "$ROOT/build/bin/gtc1453" ] && [ -x "$ROOT/build/bin/gtc-native-smoke" ]; then
  "$ROOT/tests/native_smoke.sh" "$ROOT/build/native-smoke-tests"
fi
