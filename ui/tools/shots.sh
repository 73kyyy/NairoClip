#!/bin/bash
# 批量生成剪辑页各状态截图（WebKitGTK + Xvfb，无 GPU 环境可用）
set -u
cd "$(dirname "$0")/.."
BASE="file://$(pwd)/pages/03-editor.html?capture=viewport"
run() {
  local out="$1"; local url="$2"
  timeout 120 xvfb-run -a -s "-screen 0 900x1000x24" \
    python3 tools/shot.py "$url" "screenshots/$out" 390 844 2 900 2>&1 | tail -1
}
run 03-editor.png            "$BASE"
run 03-editor-l2.png         "$BASE#cat-edit"
run 03-editor-panel.png      "$BASE#speed"
run 03-editor-filter.png     "$BASE#filter"
run 03-editor-adjust.png     "$BASE#adjust"
run 03-editor-transition.png "$BASE#trans"
run 03-editor-mask.png       "$BASE#mask"
run 03-editor-sheet.png      "$BASE#sheet"
run 03-editor-multi.png      "$BASE#multi"
run 03-editor-collapsed.png  "$BASE#collapsed"
run 03-editor-perf.png       "$BASE#perf"
