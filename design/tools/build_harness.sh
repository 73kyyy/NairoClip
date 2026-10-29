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

# core 用 `target_compile_definitions(... PUBLIC NAIROCLIP_HAS_FFMPEG=1)` 暴露该宏；
# 本脚本手工调 g++ 绕过了 CMake，不会继承它。不传的话桥里所有
# `#ifdef NAIROCLIP_HAS_FFMPEG` 分支（素材时长探测、缩略图、波形）
# 都会静默失效 —— 表现为 /api/media_library 返回空列表。
FF_DEF=""
if [ -n "$FF_CFLAGS$FF_LIBS" ]; then
    FF_DEF="-DNAIROCLIP_HAS_FFMPEG=1"
fi

EXTRA_LIBS=""
if [ -f "$ORT_ROOT/lib/libonnxruntime.so" ]; then
    EXTRA_LIBS="$ORT_ROOT/lib/libonnxruntime.so"
    ORT_RPATH="-Wl,-rpath,$ORT_ROOT/lib"
else
    ORT_RPATH=""
fi

mkdir -p design/out design/screenshots
g++ -std=c++17 -O2 -g0 -Wall -Wextra \
    -I core/include $FF_CFLAGS $FF_DEF \
    design/tools/nc_design_harness.cpp \
    "$BUILD_DIR/core/libnairoclip_core.a" \
    $FF_LIBS $EXTRA_LIBS -lz -lpthread -ldl $ORT_RPATH \
    -o "$OUT"

echo "已构建：$OUT"
