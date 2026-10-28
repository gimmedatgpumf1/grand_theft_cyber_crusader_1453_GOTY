#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
bash tools/nagger.sh
# The skeletons are checked against human osteometry before anything is built on
# them. Both are scanned bodies, so each is judged against its own sex's series
# and against the tolerance a real person allows for bilateral asymmetry.
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-female.obj \
  --sex female --scanned
echo '[test] both skeletons pass their anatomical checks.'
# The shaders are generated, not compiled, so the tree is checked against the
# generator: a stale .spv or header fails here rather than at pipeline creation.
python3 native/vulkan/spirv.py --check
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
  tests/vulkan_plumbing.cpp native/vulkan/vk_device.cpp native/vulkan/vk_renderer.cpp \
  -o build/bin/gtc-vulkan-plumbing
LD_LIBRARY_PATH="$ROOT/build/vulkan-stub" ./build/bin/gtc-vulkan-plumbing
if [ -x "$ROOT/build/bin/gtc-native-smoke" ]; then
  # A machine without a Vulkan driver must say so and keep going; this asserts
  # the fallback rather than the frame rate, because the frame rate is not
  # something this machine can answer for.
  "$ROOT/build/bin/gtc-native-smoke" --vulkan-report
fi
if [ -x "$ROOT/build/bin/gtc1453" ] && [ -x "$ROOT/build/bin/gtc-native-smoke" ]; then
  "$ROOT/tests/native_smoke.sh" "$ROOT/build/native-smoke-tests"
fi
