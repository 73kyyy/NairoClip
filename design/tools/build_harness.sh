#!/usr/bin/env bash
# 构建设计验证桥（真实引擎驱动 UI 原型）。
# 依赖：已构建的 core 静态库（build/core/libnairoclip_core.a）+ FFmpeg + ONNX Runtime（可选）
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD_DIR="${1:-build}"
ORT_ROOT="${NAIROCLIP_ORT_ROOT:-/workspace/deps/onnxruntime-linux-aarch64-1.23.0}"
OUT="design/tools/nc_design_harness"

FF_LIBS="$(pkg-config --libs libavformat libavcodec libavutil libswscale libswresample 2>/dev/null || true)"
FF_CFLAGS="$(pkg-config --cflags libavformat libavcodec libavutil libswscale libswresample 2>/dev/null || true)"

EXTRA_LIBS=""
if [ -f "$ORT_ROOT/lib/libonnxruntime.so" ]; then
    EXTRA_LIBS="$ORT_ROOT/lib/libonnxruntime.so"
    ORT_RPATH="-Wl,-rpath,$ORT_ROOT/lib"
else
    ORT_RPATH=""
fi

mkdir -p design/out design/screenshots
g++ -std=c++17 -O2 -g0 -Wall -Wextra \
    -I core/include $FF_CFLAGS \
    design/tools/nc_design_harness.cpp \
    "$BUILD_DIR/core/libnairoclip_core.a" \
    $FF_LIBS $EXTRA_LIBS -lz -lpthread -ldl $ORT_RPATH \
    -o "$OUT"

echo "已构建：$OUT"
