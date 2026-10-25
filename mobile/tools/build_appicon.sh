#!/usr/bin/env bash
# 把 static/appicon/appicon.svg 渲染成各平台图标尺寸。
# 用仓库已有的 WebKit 截图通道（shotwait.py），无需 ImageMagick/rsvg。
set -u
cd "$(dirname "$0")/.."
ROOT="$(cd .. && pwd)"
PORT="${1:-9130}"
OUT="native/assets"

mkdir -p "$OUT/android/mipmap-mdpi" "$OUT/android/mipmap-hdpi" \
         "$OUT/android/mipmap-xhdpi" "$OUT/android/mipmap-xxhdpi" \
         "$OUT/android/mipmap-xxxhdpi" "$OUT/ios"

"$ROOT/design/tools/nc_design_harness" "$PORT" "$ROOT/design" >/tmp/icon_build.log 2>&1 &
HP=$!
trap 'kill $HP 2>/dev/null' EXIT

for i in $(seq 1 40); do
  curl -s -m 1 -o /dev/null "http://127.0.0.1:$PORT/api/state" && break
  sleep 0.5
done

render() {  # $1=size $2=outpath
  local s="$1" o="$2"
  timeout 120 xvfb-run -a -s "-screen 0 1200x1200x24" python3 tools/shotwait.py \
    "http://127.0.0.1:$PORT/mobile/static/appicon/icons.html?s=$s" \
    "$o" "" 1200 "$s" "$s" >/dev/null 2>&1
  if [ -f "$o" ]; then echo "  $o  $(stat -c%s "$o") bytes"; else echo "  失败: $o"; fi
}

echo "渲染 Android 各密度…"
render 48  "$OUT/android/mipmap-mdpi/ic_launcher.png"
render 72  "$OUT/android/mipmap-hdpi/ic_launcher.png"
render 96  "$OUT/android/mipmap-xhdpi/ic_launcher.png"
render 144 "$OUT/android/mipmap-xxhdpi/ic_launcher.png"
render 192 "$OUT/android/mipmap-xxxhdpi/ic_launcher.png"

echo "渲染 iOS 1024…"
render 1024 "$OUT/ios/AppIcon-1024.png"

cp static/appicon/appicon.svg "$OUT/appicon.svg"
echo "完成。"
