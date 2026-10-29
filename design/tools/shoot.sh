#!/usr/bin/env bash
# 单次：起桥 → 截图 → 收桥（避免后台进程跨轮次被杀导致截图空白）
set -uo pipefail
PAGE="${1:-/pages/editor.html}"
OUT="${2:-design/screenshots/editor-desktop.png}"
W="${3:-1440}"; H="${4:-900}"; ZOOM="${5:-1}"; WAIT="${6:-4200}"
PORT="${PORT:-8787}"
cd "$(dirname "$0")/../.."

pkill -f "nc_design_harness $PORT" 2>/dev/null
sleep 0.4
./design/tools/nc_design_harness "$PORT" design > /tmp/harness_$PORT.log 2>&1 &
BRIDGE=$!
for i in $(seq 1 40); do
  if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$PORT/api/state"; then break; fi
  sleep 0.25
done
mkdir -p "$(dirname "$OUT")"
xvfb-run -a -s "-screen 0 $((W*2))x$((H*2))x24" \
  python3 design/tools/shot_debug.py "http://127.0.0.1:$PORT$PAGE" "$OUT" "$W" "$H" "$ZOOM" "$WAIT"
STATUS=$?
kill $BRIDGE 2>/dev/null
wait $BRIDGE 2>/dev/null
echo "--- bridge log tail ---"; tail -5 /tmp/harness_$PORT.log
exit $STATUS
