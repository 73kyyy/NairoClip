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

  # 深度图：Depth-Anything V2 Small（Apache-2.0）
  #
  # **2026-11-05 变更：从 dpt-hybrid-midas（509MB）换成 Depth-Anything V2 small（95MB）。**
  # 理由：体积降到 1/5.4，效果反而更好（V2 是 MiDaS 的后续工作），
  # 且推理更快。
  #
  # 与 MiDaS 的两处差异已由代码处理（depth_ai.cpp 的模型族探测）：
  #   · 归一化：V2 用 ImageNet 统计量，MiDaS 用 mean=std=0.5；
  #   · 输入尺寸：V2 的 patch=14，输入必须是 14 的倍数（用 392=14×28）。
  "depth-anything/Depth-Anything-V2-Small-hf/resolve/main/onnx/model.onnx|depth_midas/model.onnx"

  # 人声分离：UVR-MDX-NET（MIT）
  # **2026-11-05 修正**：原 kara2 模型是 [1,4,**2048**,256]，
  # 而 stem_ai.cpp 固定按 kFftSize=6144 / kDimF=3072 构造 STFT 张量 ——
  # 形状不匹配，加载必然失败，人声分离一直是坏的。
  # 换成 3072 频点的 UVR-MDX-NET 权重（锁定清单要求的 Voc_FT 类）。
  "uvr-mdx-net/UVR-MDX-NET-Voc_FT/resolve/main/model.onnx|stem_sep/model.onnx"

  # 人像抠像：MODNet（Apache-2.0）
  #
  # 选它而不是 MediaPipe Selfie Segmentation 的原因：
  #   · 边缘质量高（发丝级），MediaPipe 只有"中"
  #   · Apache-2.0 可商用
  #   · 25MB / 256x256 输入，手机上跑得动
  # 代价：2021 年后无更新。但权重本身不会"过期"，
  # 且推理代码 matting_ai.cpp 已按 MODNet 的预处理约定实现
  # （RGB 通道序 + [-1,1]，见 preprocessModnet 注释）。
  #
  # ⚠️ docs/MASTER_FRAMEWORK.md §3.1 记录过「Xenova/modnet 导出实测恒 0」。
  # 那是特定导出的问题，不是模型本身。落地后必须跑
  # MattingAI.RealInferenceProducesAlpha 断言输出非恒定。
  "Xenova/modnet/resolve/main/onnx/model.onnx|matting_basic/model.onnx"

  # 字幕翻译：m2m100-418M（MIT；全本地运行，不接云端）
  #
  # **2026-11-05 变更：移除 OPUS-MT（translate_zh_en），只保留 m2m100。**
  # 原因：OPUS-MT 存在严重复读退化（输出 "park park park"），
  # 修了 4 个叠加缺陷后仍有问题；m2m100 实测 14 句抽样全部无复读
  # （详见 docs/ROADMAP.md §5.6、docs/TRANSLATE_FRAMEWORK.md §4）。
  # 两个模型同时分发没有意义 —— 用户只会选好的那个，
  # 而差的那个多占 1.1GB 下载量。
  #
  # 必须用**分体导出**三件：merged 版在 cache 分支下 encoder KV 会收缩，
  # 导致译文退化（OPUS-MT 已踩过这个坑）。

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
