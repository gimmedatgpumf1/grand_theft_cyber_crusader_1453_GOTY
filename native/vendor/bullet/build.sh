#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
out=build/obj/bullet
mkdir -p "$out"
export CXX="${CXX:-g++}"
# Rebuild when the compiler or language mode changes, not just when sources do.
config="$(command -v "$CXX")|$("$CXX" --version | head -n 1)|c++23|-O2 -DNDEBUG -pthread"
stamp="$out/build-config.txt"
if [[ -f "$out/libbullet.a" && -f "$stamp" ]] && [[ "$(cat "$stamp")" == "$config" ]] && \
   [[ ! native/vendor/bullet/build.sh -nt "$out/libbullet.a" && ! native/vendor/bullet/sources.txt -nt "$out/libbullet.a" ]] && \
   [[ -z "$(find native/vendor/bullet/src -type f -newer "$out/libbullet.a" -print -quit)" ]]; then exit 0; fi
rm -f "$stamp"
# Source names are unique; each translation unit writes a separate object.
xargs -P "${BULLET_JOBS:-4}" -I '{}' sh -c '
  source="native/vendor/bullet/src/$1"
  object="build/obj/bullet/$(basename "${source%.cpp}").o"
  "$CXX" -std=c++23 -O2 -DNDEBUG -pthread -Inative/vendor/bullet/src -c "$source" -o "$object"
' _ '{}' < native/vendor/bullet/sources.txt
ar rcs "$out/libbullet.a" "$out"/*.o
printf '%s\n' "$config" > "$stamp"
