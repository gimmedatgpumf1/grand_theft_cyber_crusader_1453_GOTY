#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
bash tools/nagger.sh
mkdir -p build/bin .cache/ccache
export CCACHE_DIR="$ROOT/.cache/ccache"
export CCACHE_CONFIGPATH="$ROOT/tools/ccache.conf"
COMPILER="${CXX:-g++}"
COMMAND=("$COMPILER")
if command -v ccache >/dev/null 2>&1; then COMMAND=(ccache "$COMPILER"); fi
"${COMMAND[@]}" -std=c++17 -O1 -g -pthread -Wall -Wextra -Wpedantic -Isrc \
  tests/tests.cpp src/core/collision.cpp src/core/json.cpp src/core/pathfinding.cpp src/core/projectile.cpp src/core/world.cpp \
  -o build/bin/gtc1453_tests
./build/bin/gtc1453_tests
echo '[test] C++ simulation and signed geometry checks passed.'
if [ -x "$ROOT/build/bin/gtc1453" ] && [ -x "$ROOT/build/bin/gtc-native-smoke" ]; then
  "$ROOT/tests/native_smoke.sh" "$ROOT/build/native-smoke-tests"
fi
