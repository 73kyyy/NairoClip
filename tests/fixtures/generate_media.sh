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
#   tone_shift.mp4 4s 音频频率切换素材（0-2s 440Hz → 2-4s 3520Hz）+ 视频轨，
#                 供「音频 seek 精确性」验证（seek 后内容必须对应目标时间，
#                 而非视频关键帧处的内容）
#   denoise_src.wav 4s 降噪素材（0-2s 音节突发语音模拟（300Hz，200ms 周期内
#                 150ms 发声/50ms 间隙）+ 噪声 0.08；2-4s 纯噪声 0.08），
#                 供降噪端到端验证（语音突发保留 / 噪声段能量下降 / 无时轴偏移）
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

# 高频细节素材（空间特效端到端验证：模糊/锐化需要高频内容才能测出梯度能量变化；
# test_av.mp4 是大片均匀色块，模糊对均匀区无效果，不适合该验证）
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=320x240:rate=30:duration=1,format=rgb24" \
    -vf "drawgrid=w=8:h=8:t=1:c=white@1.0" \
    -c:v libx264 -pix_fmt yuv420p -crf 10 "$OUT_DIR/detail_src.mp4"

# 色度抠像测试素材（M2.27 端到端：绿幕底 + 肤色色块；红底对照）
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=0x00C828:s=160x120:r=30:d=2,format=rgb24" \
    -vf "drawbox=x=50:y=20:w=60:h=80:color=0xDCA98C:t=fill" \
    -c:v libx264 -pix_fmt yuv420p "$OUT_DIR/chroma_src.mp4"
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=0xB22222:s=320x240:r=30:d=1" \
    -c:v libx264 -pix_fmt yuv420p "$OUT_DIR/red_bg.mp4"

# 音频 seek 精确性素材：视频轨（testsrc 4s）+ 音频轨频率在 2.0s 处切换
# （440Hz → 3520Hz）。AAC 音频帧与视频关键帧不对齐，可暴露「seek 后读到
# 视频关键帧处音频内容」的缺陷；测试通过过零率/幅度判定内容正确性。
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc=size=160x120:rate=30:duration=4" \
    -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=2" \
    -f lavfi -i "sine=frequency=3520:sample_rate=44100:duration=2" \
    -filter_complex "[1:a][2:a]concat=n=2:v=0:a=1[a]" \
    -map 0:v -map "[a]" -c:v libx264 -pix_fmt yuv420p -g 250 -sc_threshold 0 \
    -c:a aac -shortest "$OUT_DIR/tone_shift.mp4"

# 降噪素材：0-2s = 音节突发式语音模拟（300Hz 正弦，每 200ms 中 150ms 发声 +
# 50ms 间隙；真实语音的幅度起伏特征）+ 噪声；2-4s = 纯噪声。
# 噪声为确定性伪随机（anoisesrc 固定种子），保证测试可复现。
# 说明：不用「持续稳态纯音」模拟语音——谱减法（行业标准）对持续稳态音
# 有压制倾向（语音的突发/音节结构才是目标场景），素材须贴近真实语音。
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "anoisesrc=color=white:amplitude=0.08:sample_rate=48000:duration=4:seed=4242" \
    -f lavfi -i "aevalsrc='0.4*sin(2*PI*300*t)*(lt(mod(t,0.2),0.15))':s=48000:d=2" \
    -filter_complex "[0:a][1:a]amix=inputs=2:duration=first:normalize=0[out]" \
    -map "[out]" -c:a pcm_s16le "$OUT_DIR/denoise_src.wav"

echo "完成："
ls -la "$OUT_DIR"/test_av.mp4 "$OUT_DIR"/two_shots.mp4 "$OUT_DIR"/speech.wav \
      "$OUT_DIR"/detail_src.mp4 "$OUT_DIR"/chroma_src.mp4 "$OUT_DIR"/red_bg.mp4 \
      "$OUT_DIR"/tone_shift.mp4 "$OUT_DIR"/denoise_src.wav
