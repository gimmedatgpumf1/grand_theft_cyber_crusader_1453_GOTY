#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
out=build/obj/bullet
mkdir -p "$out"
if [[ -f "$out/libbullet.a" ]] && [[ -z "$(find native/vendor/bullet/src -type f -newer "$out/libbullet.a" -print -quit)" ]]; then exit 0; fi
export CXX="${CXX:-g++}"
# Source names are unique; each translation unit writes a separate object.
xargs -P "${BULLET_JOBS:-4}" -I '{}' sh -c '
  source="native/vendor/bullet/src/$1"
  object="build/obj/bullet/$(basename "${source%.cpp}").o"
  "$CXX" -std=c++17 -O2 -DNDEBUG -pthread -Inative/vendor/bullet/src -c "$source" -o "$object"
' _ '{}' < native/vendor/bullet/sources.txt
ar rcs "$out/libbullet.a" "$out"/*.o
