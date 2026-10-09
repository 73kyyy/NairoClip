#!/usr/bin/env bash
# =============================================================================
# NairoClip 测试素材生成脚本
# =============================================================================
# 背景：FFmpeg 真实编解码测试依赖 /tmp/media_gen/ 下的素材，
#       此前仓库未提供生成脚本，导致新环境/CI 上这些测试必然失败
#       （或在本机无 FFmpeg 时静默跳过，掩盖真实缺陷）。
# 用法：bash tests/fixtures/generate_media.sh
#
# 产物（/tmp/media_gen/）：
#   test_av.mp4   160x120@30fps 3s H.264 + 440Hz AAC 单声道（主测试素材）
#   two_shots.mp4 两段不同画面拼接 4s（场景检测 / 参考仿剪测试）
#   speech.wav    8s 口播模拟音频（2 段 430Hz 语音 + 4s 静音，
#                 供口播精剪 VAD 端到端测试：插入时长 2s/0s/2s/0s，
#                 语音段频率与说话基频接近）
set -euo pipefail

OUT_DIR="${1:-/tmp/media_gen}"
mkdir -p "$OUT_DIR"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "错误：未找到 ffmpeg，无法生成测试素材。" >&2
    echo "安装：apt-get install ffmpeg / brew install ffmpeg" >&2
    exit 1
fi

echo "生成测试素材到 $OUT_DIR ..."

# 主素材：160x120 彩条 + 440Hz 正弦波，3 秒（与 test_media_ffmpeg.cpp 期望一致）
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc=size=160x120:rate=30:duration=3" \
    -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=3" \
    -c:v libx264 -pix_fmt yuv420p -c:a aac -ac 1 -shortest \
    "$OUT_DIR/test_av.mp4"

# 两镜头素材：彩条 2s + SMPTE 彩条 2s 拼接（场景边界在 2.0s，供场景检测验证）
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc=size=160x120:rate=30:duration=2" \
    -c:v libx264 -pix_fmt yuv420p "$TMP_DIR/shot_a.mp4"
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "smptebars=size=160x120:rate=30:duration=2" \
    -c:v libx264 -pix_fmt yuv420p "$TMP_DIR/shot_b.mp4"
printf "file '%s'\nfile '%s'\n" "$TMP_DIR/shot_a.mp4" "$TMP_DIR/shot_b.mp4" > "$TMP_DIR/concat.txt"
ffmpeg -hide_banner -loglevel error -y \
    -f concat -safe 0 -i "$TMP_DIR/concat.txt" -c copy "$OUT_DIR/two_shots.mp4"

# 口播素材：2s 语音 + 2s 静音 + 2s 语音 + 2s 静音 = 8s
# （口播精剪：VAD 应识别两段语音、剪掉 4s 静音；保持 16-bit PCM 免编解码干扰）
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "sine=frequency=430:sample_rate=48000:duration=2" \
    -f lavfi -i "anullsrc=r=48000:cl=mono:duration=2" \
    -f lavfi -i "sine=frequency=430:sample_rate=48000:duration=2" \
    -f lavfi -i "anullsrc=r=48000:cl=mono:duration=2" \
    -filter_complex "[0:a][1:a][2:a][3:a]concat=n=4:v=0:a=1[out]" \
    -map "[out]" -c:a pcm_s16le "$OUT_DIR/speech.wav"

echo "完成："
ls -la "$OUT_DIR"/test_av.mp4 "$OUT_DIR"/two_shots.mp4 "$OUT_DIR"/speech.wav
