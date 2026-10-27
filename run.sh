#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
PORT="${GTC_PORT:-1453}"

server_ready() {
  (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && exec 3>&-
}

SERVER_RUNNING=0
if server_ready; then SERVER_RUNNING=1; fi
if [ "$SERVER_RUNNING" -ne 1 ]; then bash "$ROOT/build.sh"; fi
bash "$ROOT/native/build.sh"

SERVER_PID=""
if [ "$SERVER_RUNNING" -ne 1 ]; then
  "$ROOT/build/bin/gtc1453" --port "$PORT" --web "$ROOT/web" &
  SERVER_PID=$!
fi
trap 'if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; fi' EXIT
READY=0
for _ in {1..50}; do
  if server_ready; then
    READY=1
    break
  fi
  sleep 0.1
done
if [ "$READY" -ne 1 ]; then
  echo "[run] The authoritative server did not open port $PORT." >&2
  exit 1
fi
"$ROOT/build/bin/gtc-native-client" --host 127.0.0.1 --port "$PORT" --name "${USER:-Native Citizen}" "$@"
