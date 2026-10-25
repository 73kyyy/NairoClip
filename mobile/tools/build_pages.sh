#!/usr/bin/env bash
# 同一进程内：启动设计桥 → 抓快照 → 生成首页/我的单文件版 → 关桥
set -u
cd "$(dirname "$0")/.."
ROOT="$(cd .. && pwd)"
PORT="${1:-9031}"
H=""
# 若该端口已有引擎在跑，直接复用（保留其已加载的工程数据，
# 避免构建产物退化成空工程）；否则另起一个空引擎。
if ! curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/api/state"; then
  "$ROOT/design/tools/nc_design_harness" "$PORT" "$ROOT/design" >/tmp/bp.log 2>&1 &
  H=$!
  for i in $(seq 1 40); do
    curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/api/state" && break
    sleep 0.5
  done
fi
trap 'test -n "$H" && kill $H 2>/dev/null' EXIT
curl -s -m 10 -o /tmp/state.json    "http://127.0.0.1:$PORT/api/state"
curl -s -m 10 -o /tmp/projects.json "http://127.0.0.1:$PORT/api/projects"
curl -s -m 10 -o /tmp/models.json   "http://127.0.0.1:$PORT/api/models"
NC_API_PORT="$PORT" python3 tools/make_pages_static.py
NC_API_PORT="$PORT" python3 tools/make_static.py
