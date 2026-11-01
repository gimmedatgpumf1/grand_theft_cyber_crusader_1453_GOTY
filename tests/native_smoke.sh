#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PORT="${GTC_NATIVE_TEST_PORT:-21453}"
OUTPUT="${1:-build/native-smoke}"
"$ROOT/build/bin/gtc1453" --port "$PORT" --web "$ROOT/web" >"$ROOT/build/native-test-server.log" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true' EXIT
sleep 0.25
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
  cat "$ROOT/build/native-test-server.log" >&2
  exit 1
fi
# The test room is a separate render path from the street, and a broken frame
# contract there closes the window rather than reporting an error, so it is
# checked before the server round trip.
"$ROOT/build/bin/gtc-native-client" --verify-room --root "$ROOT" --output "$OUTPUT/room"
"$ROOT/build/bin/gtc-native-smoke" --port "$PORT" --output "$OUTPUT"
