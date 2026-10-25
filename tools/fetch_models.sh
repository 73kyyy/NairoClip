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
  "briaai/RMBG-1.4/resolve/main/onnx/model_quantized.onnx|matting_basic/model.onnx"
  "briaai/RMBG-1.4/resolve/main/onnx/model.onnx|matting_hq/model.onnx"
  "onnx-community/whisper-small/resolve/main/onnx/encoder_model.onnx|whisper_zh/encoder_model.onnx"
  "onnx-community/whisper-small/resolve/main/onnx/decoder_model_merged.onnx|whisper_zh/decoder_model_merged.onnx"
  "onnx-community/whisper-small/resolve/main/config.json|whisper_zh/config.json"
  "onnx-community/whisper-small/resolve/main/tokenizer.json|whisper_zh/tokenizer.json"
  "opencv/optical_flow_estimation_raft/resolve/main/optical_flow_estimation_raft_2023aug.onnx|flow_interp/model.onnx"
  "Xenova/swin2SR-classical-sr-x2-64/resolve/main/onnx/model_quantized.onnx|super_res_2x4x/model.onnx"
  "ccds/vits_onnx/resolve/main/jp_cn/model.onnx|tts_zh/model.onnx"
  "musetric/uvr-mdxnet-kara2-onnx/resolve/main/kara2.onnx|stem_sep/model.onnx"
  "Xenova/opus-mt-zh-en/resolve/main/onnx/encoder_model.onnx|translate_zh_en/encoder_model.onnx"
  "Xenova/opus-mt-zh-en/resolve/main/onnx/decoder_model_merged.onnx|translate_zh_en/decoder_model_merged.onnx"
)

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
