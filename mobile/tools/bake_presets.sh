#!/usr/bin/env bash
# 用真实引擎逐预设渲染宫格缩略图，落盘为 JPEG。
# 产出：static/img/preset/{filter|effect|mask}_{id}.jpg
# 用途：三级面板宫格的效果预览（静态版内嵌，联机版走 /api/preset_frame）
set -u
cd "$(dirname "$0")/.."
ROOT="$(cd .. && pwd)"
PORT="${1:-9201}"
OUT="static/img/preset"

"$ROOT/design/tools/nc_design_harness" "$PORT" "$ROOT/design" >/tmp/bake.log 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT

for i in $(seq 1 40); do
  curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/api/state" && break
  sleep 0.5
done

mkdir -p "$OUT"
RAW="$(mktemp -d)"

fetch() {
  local kind="$1" id="$2"
  curl -s -m 30 -o "$RAW/${kind}_${id}.png" \
    "http://127.0.0.1:$PORT/api/preset_frame?kind=${kind}&style=${id}&t=1600000&w=200"
}

# 滤镜 1..13（FilterStyle）
for id in $(seq 1 13); do fetch filter "$id"; done
# 特效（EffectType：画面/氛围/动感/人物）
for id in 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 37; do
  fetch effect "$id"
done
# 蒙版 1..6（MaskShape）
for id in $(seq 1 6); do fetch mask "$id"; done

python3 - "$RAW" "$OUT" <<'PY'
import os, sys
from PIL import Image

raw, out = sys.argv[1], sys.argv[2]
ok, bad, total = 0, [], 0
for f in sorted(os.listdir(raw)):
    if not f.endswith('.png'):
        continue
    src = os.path.join(raw, f)
    if os.path.getsize(src) == 0:
        bad.append(f)
        continue
    try:
        im = Image.open(src).convert('RGB')
    except Exception:
        bad.append(f)
        continue
    dst = os.path.join(out, f[:-4] + '.jpg')
    im.save(dst, 'JPEG', quality=82, optimize=True)
    ok += 1
    total += os.path.getsize(dst)

print(f'BAKED {ok} BAD {len(bad)} KB {total // 1024}')
if bad:
    print('  failed:', ', '.join(bad[:8]))
PY

rm -rf "$RAW"
ls "$OUT" | wc -l
