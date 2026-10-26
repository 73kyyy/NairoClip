#!/usr/bin/env bash
# 下载 8 个 AI 模型的真实 ONNX 权重
# 来源：HuggingFace 镜像 hf-mirror.com（官方站部分网络不可达）
# 产物：models/<model_id>/<file>.onnx（不入 git，体积 GB 级）
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${NC_MODELS_DIR:-$ROOT/models}"
MIRROR="${NC_MODEL_MIRROR:-https://hf-mirror.com}"
ONLY="${1:-}"
log() { printf '  %s\n' "$*"; }

JOBS=(
  # ---- 锁定清单（《NairoClip 所有模型协议和其它要求》§1.2）----
  # 说明：每项标注「协议 / 状态」。未接入的功能不下载占位模型，
  #       避免"有文件但跑不通"的假可用状态（见 docs/ROADMAP.md §5）。

  # 语音转字幕：whisper.cpp 同源权重（MIT）
  "onnx-community/whisper-small/resolve/main/onnx/encoder_model.onnx|whisper_zh/encoder_model.onnx"
  "onnx-community/whisper-small/resolve/main/onnx/decoder_model_merged.onnx|whisper_zh/decoder_model_merged.onnx"
  "onnx-community/whisper-small/resolve/main/config.json|whisper_zh/config.json"
  "onnx-community/whisper-small/resolve/main/tokenizer.json|whisper_zh/tokenizer.json"

  # 深度图：MiDaS（MIT；上游 Intel/dpt-hybrid-midas 为 Apache-2.0）
  # 实测可用：输入 [1,3,384,384]，预处理 mean/std = 0.5
  "Xenova/dpt-hybrid-midas/resolve/main/onnx/model.onnx|depth_midas/model.onnx"
  "Xenova/dpt-hybrid-midas/resolve/main/preprocessor_config.json|depth_midas/preprocessor_config.json"

  # 人声分离：UVR-MDX-NET（MIT）
  # ⚠️ 待核对：锁定清单要求 Voc_FT 权重，此处为 kara2 变体
  "musetric/uvr-mdxnet-kara2-onnx/resolve/main/kara2.onnx|stem_sep/model.onnx"

  # 字幕翻译：OPUS-MT-ZH-EN（Apache-2.0；全本地运行，不接云端）
  # 必须用**分体导出**三件：merged 版在 cache 分支下 encoder KV 会收缩，
  # 导致译文退化（详见 docs/ROADMAP.md §5.6）。
  "Xenova/opus-mt-zh-en/resolve/main/onnx/encoder_model.onnx|translate_zh_en/encoder_split.onnx"
  "Xenova/opus-mt-zh-en/resolve/main/onnx/decoder_model.onnx|translate_zh_en/decoder_split.onnx"
  "Xenova/opus-mt-zh-en/resolve/main/onnx/decoder_with_past_model.onnx|translate_zh_en/decoder_with_past.onnx"
  "Xenova/opus-mt-zh-en/resolve/main/tokenizer.json|translate_zh_en/tokenizer.json"

  # 字幕翻译（首选）：m2m100-418M（MIT，核查记录 docs/licenses/m2m100_418M.md）
  # 相对 OPUS-MT 的关键优势：无短语复读，短句收尾稳定（14 句抽样 0 退化）。
  # 注意：BPE 词表，推理侧必须在源序列前加 __zh__、末尾加 </s>，
  # 否则输出错语种并陷入复读（详见 docs/TRANSLATE_FRAMEWORK.md §4.1）。
  "Xenova/m2m100_418M/resolve/main/onnx/encoder_model.onnx|translate_zh_en_m2m/encoder.onnx"
  "Xenova/m2m100_418M/resolve/main/onnx/decoder_model.onnx|translate_zh_en_m2m/decoder.onnx"
  "Xenova/m2m100_418M/resolve/main/onnx/decoder_with_past_model.onnx|translate_zh_en_m2m/decoder_with_past.onnx"
  "Xenova/m2m100_418M/resolve/main/tokenizer.json|translate_zh_en_m2m/tokenizer.json"
)
# ---- 暂不下载（原因见 docs/ROADMAP.md §5.1 / §5.4）----
# 人像抠像 MediaPipe Selfie Segmentation：
#   Qualcomm 导出版实测对真实人像输出恒 0（sigmoid 后 0.5000），不可用；
#   官方为 .tflite，需要 TensorFlow 转换（当前环境无法安装）。
#   在拿到可用 ONNX 前不下载，避免假可用。
# 光流补帧 RIFE：HF 上无可用 ONNX 导出（多个仓库 401）。
# 超分辨率 Real-ESRGAN：ai-forever/Real-ESRGAN 仅提供 .pth，无 ONNX。
# TTS VITS 官方权重：HF 上 Xenova/vits-* 均不可达（401）。
# MADLAD-400（3B）：按项目要求改为全本地运行后，
#   3B 规模不适合端上实时推理（ONNX 转换亦需 torch + 大显存），
#   故改用 OPUS-MT-ZH-EN 分体导出（见上方 JOBS）。


total=0; ok=0; skip=0; fail=0; failed_list=()

for job in "${JOBS[@]}"; do
  url="${job%%|*}"; rel="${job##*|}"; mid="${rel%%/*}"
  if [ -n "$ONLY" ] && [ "$mid" != "$ONLY" ]; then continue; fi
  dest="$OUT/$rel"
  mkdir -p "$(dirname "$dest")"

  if [ -s "$dest" ]; then
    sz=$(stat -c%s "$dest" 2>/dev/null || echo 0)
    if [ "$sz" -gt 1024 ]; then
      log "跳过 ${rel}（已存在 $((sz/1048576))MB）"
      skip=$((skip+1)); total=$((total+1)); continue
    fi
  fi

  log "下载 ${rel} …"
  if curl -fL --retry 3 --retry-delay 2 -C - --connect-timeout 20 --max-time 3600 \
        -o "$dest" "$MIRROR/$url" 2>/dev/null && [ -s "$dest" ]; then
    sz=$(stat -c%s "$dest")
    if [ "$sz" -lt 1024 ]; then
      log "  x 内容过小（${sz}B），疑似错误页"
      rm -f "$dest"; fail=$((fail+1)); failed_list+=("$rel")
    else
      log "  ok $((sz/1048576))MB"; ok=$((ok+1))
    fi
  else
    log "  x 下载失败"; rm -f "$dest"
    fail=$((fail+1)); failed_list+=("$rel")
  fi
  total=$((total+1))
done

echo
echo "模型目录：$OUT"
echo "成功 $ok / 跳过 $skip / 失败 $fail（共 $total）"
if [ "$fail" -gt 0 ]; then
  printf '失败项：\n'; printf '  %s\n' "${failed_list[@]}"
  exit 1
fi

echo
echo "各模型占用："
for d in "$OUT"/*/; do
  [ -d "$d" ] || continue
  n=$(basename "$d"); s=$(du -sb "$d" 2>/dev/null | cut -f1)
  printf '  %-18s %6s MB\n' "$n" "$((s/1048576))"
done
