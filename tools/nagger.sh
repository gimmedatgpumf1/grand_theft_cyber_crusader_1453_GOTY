#!/usr/bin/env bash
# The line guard.
#
# A file has three states here, and it says so every single time:
#
#   under 750   left alone.
#   750 - 1000  nagged. Loudly. Repeatedly. With a bar showing exactly how close
#               the file is to the wall and a reminder of what happens there.
#   1000+       the build stops. Not a warning, not a note in a log: the compiler
#               is never reached, and nothing is built until the file is split.
#
# The wall is a wall. Splitting a file is cheap; a thousand-line file is not.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HARD_LIMIT=1000
WARN_LIMIT=750
FAILED=0
NAGGED=0
BLOCKED=()
WARNED=()

bar() {  # a twenty-cell bar of how much of the wall is already used
  local filled=$(( $1 * 20 / HARD_LIMIT ))
  local out=""
  for ((i = 0; i < 20; ++i)); do
    if ((i < filled)); then out+='#'; else out+='-'; fi
  done
  printf '%s' "$out"
}

while IFS= read -r -d '' FILE; do
  case "$FILE" in
    */web/js/vendor/*|*/native/vendor/*|*/.git/*|*/build/*|*/.cache/*) continue ;;
  esac
  LINES="$(wc -l < "$FILE")"
  REL="${FILE#"$ROOT"/}"
  if [ "$LINES" -gt "$HARD_LIMIT" ]; then
    printf '[NAGGER] ################################################################\n' >&2
    printf '[NAGGER]  BLOCKED: %s is %s lines.\n' "$REL" "$LINES" >&2
    printf '[NAGGER]  %s is the wall. You did not hit it, you drove through it.\n' "$HARD_LIMIT" >&2
    printf '[NAGGER]  I will not compile a single object until you clean this up.\n' >&2
    printf '[NAGGER]  Not one. Go split it, you absolute landfill.\n' >&2
    printf '[NAGGER] ################################################################\n' >&2
    BLOCKED+=("$REL ($LINES)")
    FAILED=1
  elif [ "$LINES" -ge "$WARN_LIMIT" ]; then
    LEFT=$(( HARD_LIMIT - LINES ))
    NAGGED=$(( NAGGED + 1 ))
    WARNED+=("$REL ($LINES)")
    printf '[NAGGER] [%s] %s\n' "$(bar "$LINES")" "$REL" >&2
    printf '[NAGGER]   %s lines. %s left before I spit the dummy and stop building.\n' "$LINES" "$LEFT" >&2
    printf '[NAGGER]   Time to split or I will spit! 750 is where a file stops being\n' >&2
    printf '[NAGGER]   code and starts being a landfill with a filename.\n' >&2
    if [ "$LEFT" -le 0 ]; then
      printf '[NAGGER]   AT THE WALL. Not near it, on it. The very next line stops the
' >&2
      printf '[NAGGER]   build. Split it, you absolute landfill.
' >&2
    elif [ "$LEFT" -le 50 ]; then
      printf '[NAGGER]   %s LINES FROM THE WALL. This thing is one sneeze away from\n' "$LEFT" >&2
      printf '[NAGGER]   being a build failure. Split it NOW, you digital hoarder.\n' >&2
    elif [ "$LEFT" -le 150 ]; then
      printf '[NAGGER]   %s lines of headroom, which is not headroom, it is a countdown.\n' "$LEFT" >&2
    else
      printf '[NAGGER]   Every line you add from here is another brick in your own wall.\n' >&2
    fi
  fi
done < <(find "$ROOT" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.js' -o -name '*.css' -o -name '*.html' -o -name '*.sh' -o -name '*.ps1' -o -name '*.cmd' -o -name '*.py' \) -print0)

if [ "$NAGGED" -gt 0 ] && [ "$FAILED" -eq 0 ]; then
  printf '[nagger] ################################################################\n' >&2
  printf '[nagger]  %s file(s) in the danger zone. I am watching all of them.\n' "$NAGGED" >&2
  for entry in "${WARNED[@]}"; do
    printf '[nagger]   - %s\n' "$entry" >&2
  done
  printf '[nagger]  Split one of them and I will shut up about it. Build continues,\n' >&2
  printf '[nagger]  but I am not happy about it, and neither is the next person.\n' >&2
  printf '[nagger] ################################################################\n' >&2
fi
if [ "$FAILED" -ne 0 ]; then
  printf '[nagger] %s file(s) past the hard limit: %s\n' "${#BLOCKED[@]}" "${BLOCKED[*]}" >&2
  printf '[nagger] Nothing was compiled. Not one object. Split the file(s) above and\n' >&2
  printf '[nagger] try again -- and do not come back until you have.\n' >&2
  exit "$FAILED"
fi
if [ "$NAGGED" -eq 0 ]; then
  echo '[nagger] All first-party files are under 750 lines. Readability survives.'
fi
echo '[nagger] Build successful? Push to GitHub!'
echo "[nagger] Less yapping, more coding! Don't bother the King until you have fixed it!"
exit "$FAILED"
