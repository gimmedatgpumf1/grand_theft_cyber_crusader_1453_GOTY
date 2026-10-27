#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAILED=0
while IFS= read -r -d '' FILE; do
  case "$FILE" in
    */web/js/vendor/*|*/native/vendor/*|*/.git/*|*/build/*|*/.cache/*) continue ;;
  esac
  LINES="$(wc -l < "$FILE")"
  if [ "$LINES" -gt 1000 ]; then
    printf '[NAGGER] %s has %s lines. Split it, you absolute landfill.\n' "${FILE#"$ROOT"/}" "$LINES" >&2
    FAILED=1
  fi
done < <(find "$ROOT" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.js' -o -name '*.css' -o -name '*.html' -o -name '*.sh' -o -name '*.ps1' -o -name '*.cmd' -o -name '*.py' \) -print0)
if [ "$FAILED" -eq 0 ]; then
  echo '[nagger] All first-party files are <= 1000 lines. Readability survives.'
fi
echo '[nagger] Build successful? Push to GitHub!'
exit "$FAILED"
