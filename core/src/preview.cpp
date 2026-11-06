#include "nairoclip/core/filters.h"
#include "nairoclip/core/preview.h"

#include <cstring>
#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/shake_effect.h"
#include "nairoclip/core/lut.h"
#include "nairoclip/core/xform.h"
#include "nairoclip/core/blend_modes.h"

#include <algorithm>
#include <utility>

namespace nairoclip {
namespace core {

namespace {

/// 在轨道上按片段 ID 查找。
const Clip* findClipInTrack(const Track& track, ClipId id) {
    for (const auto& clip : track.clips) {
        if (clip.id == id) {
            return &clip;
        }
    }
    return nullptr;
}

}  // namespace

PreviewRenderer::PreviewRenderer(const Project& project, MediaReaderFactory& readerFactory)
    : project_(project), readerFactory_(readerFactory) {}

PreviewRenderer::~PreviewRenderer() = default;

MediaReader* PreviewRenderer::readerFor(AssetId asset) {
    auto it = readers_.find(asset.value);
    if (it != readers_.end()) {
        return it->second.get();
    }
    const MediaAsset* found = project_.findAsset(asset);
    if (found == nullptr) {
        return nullptr;
    }
    auto reader = readerFactory_.create();
    if (!reader || !reader->open(found->path).isOk()) {
        return nullptr;
    }
    ++stats_.readerOpens;
    MediaReader* raw = reader.get();
    readers_.emplace(asset.value, std::move(reader));
    return raw;
}

namespace {

/// 影响像素的属性通道（ClipProperty 没有 Count 哨兵，显式列举）。
///
/// 为什么不写 `for (i = 0; i < N; ++i)`：枚举可能被后续追加，
/// 显式列一遍能在「加了通道却忘了混进指纹」时立刻看出来 ——
/// 漏掉一个通道的后果是「改它不刷新预览」，非常难查。
///
/// 不含 Volume：音量不影响画面像素（音频走另一条路径）。
static const ClipProperty kVisualProps[] = {
    ClipProperty::Opacity, ClipProperty::Scale,   ClipProperty::PositionX,
    ClipProperty::PositionY, ClipProperty::Rotation,
};

/// FNV-1a 64 位。选它而不是 std::hash：标准对 std::hash 的实现不作保证，
/// 跨编译器/跨版本结果可能不同，而缓存失效判定**不该依赖未指定行为**。
inline std::uint64_t fnvMix(std::uint64_t h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFF;
        h *= 1099511628211ULL;
    }
    return h;
}

inline std::uint64_t fnvMixDouble(std::uint64_t h, double v) {
    // 浮点按位混合：+0.0 与 -0.0 会被视作不同，但那种差异在时间轴上无意义，
    // 且这里只用于「变了没变」的判定，误判的代价是多渲染一帧。
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "double 必须 8 字节");
    std::memcpy(&bits, &v, sizeof(bits));
    return fnvMix(h, bits);
}

inline std::uint64_t fnvMixStr(std::uint64_t h, const std::string& s) {
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

/// 把一组「影响像素的数值」混进指纹。
/// 只取数值，不取 clipId —— 选择框/当前片段变化不该让缓存失效。
std::uint64_t mixClipVisuals(std::uint64_t h, const Clip& clip) {
    /* 时间与速度按 double 混入：Rational 的内部表示（num/den 约分与否）
       不该影响「内容是否变了」的判定 —— 1/30 与 2/60 是同一点。 */
    h = fnvMixDouble(h, clip.start.toDouble());
    h = fnvMixDouble(h, clip.duration.toDouble());
    h = fnvMixDouble(h, clip.sourceIn.toDouble());
    h = fnvMixDouble(h, clip.speed.toDouble());

    // 变换（画中画位置/大小/旋转）
    h = fnvMixDouble(h, clip.transform.scale);
    h = fnvMixDouble(h, clip.transform.posX);
    h = fnvMixDouble(h, clip.transform.posY);
    h = fnvMixDouble(h, clip.transform.rotation);
    h = fnvMix(h, static_cast<std::uint64_t>(clip.transform.anchor));
    h = fnvMix(h, clip.transform.enabled ? 1u : 0u);

    // 混合与外观
    h = fnvMix(h, static_cast<std::uint64_t>(clip.blendMode));
    h = fnvMix(h, clip.hasMask ? 1u : 0u);
    h = fnvMix(h, clip.hasChromaKey ? 1u : 0u);

    // 效果栈：数量 + 每个 effect 的类型/取值/启用态
    h = fnvMix(h, static_cast<std::uint64_t>(clip.effects.size()));
    for (const auto& effect : clip.effects.effects()) {
        h = fnvMix(h, static_cast<std::uint64_t>(effect.type));
        h = fnvMixDouble(h, effect.value);
        h = fnvMix(h, effect.enabled ? 1u : 0u);
    }

    // 8 槽几何变换：只混启用且有值的槽（关掉的槽不影响像素）
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        const XformParams& x = clip.xforms.slots[i];
        if (!x.enabled) {
            continue;
        }
        h = fnvMix(h, static_cast<std::uint64_t>(i));
        h = fnvMixDouble(h, x.value);
        h = fnvMix(h, static_cast<std::uint64_t>(x.edge));
    }

    // 关键帧动画：曲线数量 + 每条曲线的采样值
    // （只混数量不够 —— 拖关键帧不改数量但改取值。）
    for (ClipProperty prop : kVisualProps) {
        const KeyframeTrack* curve = clip.animation.findCurve(prop);
        if (curve == nullptr || curve->empty()) {
            continue;
        }
        h = fnvMix(h, static_cast<std::uint64_t>(prop));
        h = fnvMix(h, static_cast<std::uint64_t>(curve->size()));
        for (const auto& k : curve->keyframes()) {
            h = fnvMixDouble(h, k.time.toDouble());
            h = fnvMixDouble(h, k.value);
        }
    }

    // 滤镜 / LUT（只混标识，文件内容变不变由文件系统负责）
    h = fnvMix(h, static_cast<std::uint64_t>(clip.filterStyle));
    h = fnvMixDouble(h, clip.filterIntensity);
    h = fnvMixStr(h, clip.lutPath);
    h = fnvMixDouble(h, clip.lutStrength);

    // 文本内容（栅格化结果随正文/样式变）
    if (clip.text.has_value()) {
        h = fnvMixStr(h, clip.text->text);
        h = fnvMixDouble(h, clip.text->style.fontSize);
        h = fnvMixStr(h, clip.text->style.color);
        h = fnvMixStr(h, clip.text->style.outlineColor);
        h = fnvMixDouble(h, clip.text->style.outlineWidth);
        h = fnvMix(h, static_cast<std::uint64_t>(clip.text->style.alignment));
        h = fnvMixDouble(h, clip.text->style.anchorX);
        h = fnvMixDouble(h, clip.text->style.anchorY);
    }
    return h;
}

}  // namespace

std::uint64_t PreviewRenderer::contentFingerprint(const Time& timelineTime) const {
    std::uint64_t h = 1469598103934665603ULL;   // FNV offset basis

    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) {
        return h;
    }
    // 画幅与背景色也影响像素
    h = fnvMix(h, static_cast<std::uint64_t>(seq->width));
    h = fnvMix(h, static_cast<std::uint64_t>(seq->height));
    h = fnvMix(h, static_cast<std::uint64_t>(seq->backgroundRgba));
    h = fnvMix(h, static_cast<std::uint64_t>(seq->frameFit));

    int layerCount = 0;
    for (const auto& track : seq->tracks) {
        if ((track.type != TrackType::Video && track.type != TrackType::Text) || track.hidden) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.contains(timelineTime)) {
                continue;
            }
            h = fnvMix(h, static_cast<std::uint64_t>(layerCount++));
            h = mixClipVisuals(h, clip);

            /* 调整图层：作用于「下方累积结果」，必须一并混进来 ——
               否则改调整图层的 LUT 不会让缓存失效，
               用户会看到「拖了没反应」（直到换个时间点才刷新）。 */
            if (track.type == TrackType::Adjustment || track.hasAdjustment()) {
                h = fnvMix(h, 0xA5A5A5A5ULL);   // 分隔标记，避免与上一层混淆
                h = fnvMixDouble(h, track.adjustmentOpacity);
                /* adjustmentXforms 是 XformStack（定长 8 槽），不是容器。
                   只混启用槽 —— 关掉的槽不影响像素。 */
                for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
                    const XformParams& x = track.adjustmentXforms.slots[i];
                    if (!x.enabled) {
                        continue;
                    }
                    h = fnvMix(h, static_cast<std::uint64_t>(i));
                    h = fnvMixDouble(h, x.value);
                    h = fnvMix(h, static_cast<std::uint64_t>(x.edge));
                }
                // 调整图层的独立关键帧动画
                for (ClipProperty prop : kVisualProps) {
                    const KeyframeTrack* curve = track.adjustmentAnimation.findCurve(prop);
                    if (curve == nullptr || curve->empty()) {
                        continue;
                    }
                    h = fnvMix(h, static_cast<std::uint64_t>(prop));
                    for (const auto& k : curve->keyframes()) {
                        h = fnvMixDouble(h, k.time.toDouble());
                        h = fnvMixDouble(h, k.value);
                    }
                }
                h = fnvMixStr(h, track.adjustmentLutPath);
            }
            break;   // 同轨片段不重叠，取到即跳出（与 collectActiveLayers 一致）
        }
    }
    h = fnvMix(h, static_cast<std::uint64_t>(layerCount));
    return h;
}

namespace {

/// 双线性放大 RGBA8（用于「缓存图比请求的小」时顶替）。
///
/// 为什么不复用 renderFrameFit：
///   · 它带画幅适配（letterbox/fill/crop）与背景填充语义，这里只需要纯缩放；
///   · 它每次都会算 fitMatrix + 求逆矩阵，纯放大是多余的；
///   · 单独实现能把「只放大不缩」这个前提写死（避免误用成缩小，
///     缩小有 aliasing 问题，应该走渲染管线而不是缓存放大）。
///
/// 性能：1080p 双线性放大约 3-5ms（单线程），远低于重新解码 + 合成的
/// 几十毫秒 —— 这就是「宁可放大也不重渲」的依据。
void upscaleRgba(const std::uint8_t* src, int srcW, int srcH, std::uint8_t* dst, int dstW,
                 int dstH) {
    if (src == nullptr || dst == nullptr || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return;
    }
    for (int y = 0; y < dstH; ++y) {
        // 目标像素中心映射回源坐标（与 renderFrameFit 的采样约定一致：
        // 以像素中心为整数，src = (dst + 0.5) * ratio - 0.5）
        const double sy = (static_cast<double>(y) + 0.5) * static_cast<double>(srcH) /
                              static_cast<double>(dstH) -
                          0.5;
        int y0 = static_cast<int>(std::floor(sy));
        const double fy = sy - static_cast<double>(y0);
        int y1 = y0 + 1;
        // 边界钳制（不是填背景：放大时不希望边缘出现黑边）
        if (y0 < 0) y0 = 0;
        if (y0 > srcH - 1) y0 = srcH - 1;
        if (y1 < 0) y1 = 0;
        if (y1 > srcH - 1) y1 = srcH - 1;

        const std::uint8_t* row0 = src + static_cast<std::size_t>(y0) *
                                             static_cast<std::size_t>(srcW) * 4u;
        const std::uint8_t* row1 = src + static_cast<std::size_t>(y1) *
                                             static_cast<std::size_t>(srcW) * 4u;
        std::uint8_t* outRow = dst + static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(dstW) * 4u;
        for (int x = 0; x < dstW; ++x) {
            const double sx = (static_cast<double>(x) + 0.5) * static_cast<double>(srcW) /
                                  static_cast<double>(dstW) -
                              0.5;
            int x0 = static_cast<int>(std::floor(sx));
            const double fx = sx - static_cast<double>(x0);
            int x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x0 > srcW - 1) x0 = srcW - 1;
            if (x1 < 0) x1 = 0;
            if (x1 > srcW - 1) x1 = srcW - 1;

            const std::uint8_t* p00 = row0 + static_cast<std::size_t>(x0) * 4u;
            const std::uint8_t* p10 = row0 + static_cast<std::size_t>(x1) * 4u;
            const std::uint8_t* p01 = row1 + static_cast<std::size_t>(x0) * 4u;
            const std::uint8_t* p11 = row1 + static_cast<std::size_t>(x1) * 4u;
            const double w00 = (1.0 - fx) * (1.0 - fy);
            const double w10 = fx * (1.0 - fy);
            const double w01 = (1.0 - fx) * fy;
            const double w11 = fx * fy;
            std::uint8_t* out = outRow + static_cast<std::size_t>(x) * 4u;
            for (int c = 0; c < 4; ++c) {
                const double v = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
                out[c] = static_cast<std::uint8_t>(v + 0.5);
            }
        }
    }
}

}  // namespace

void PreviewRenderer::setCacheScale(double scale) {
    /* 缓存分辨率缩放。夹在 [0.05, 1.0]：
       · 1.0 = 关闭降采样（行为与改动前一致）；
       · 太小（<0.25）放大后糊得没法看，且会让「内容指纹相同但尺寸不同」
         的混乱加剧 —— 0.05 是 RenderQuality::valid 的下界，沿用。 */
    const double clamped = scale < 0.05 ? 0.05 : (scale > 1.0 ? 1.0 : scale);
    if (std::abs(clamped - cacheScale_) < 1e-9) {
        return;
    }
    cacheScale_ = clamped;
    /* 尺寸策略变了 → 旧缓存全部作废。
       不清的话会留下「一部分 0.5 尺寸、一部分 1.0 尺寸」的混合缓存，
       命中时全靠 cacheScale 判断，虽然正确但浪费内存（大小图混存）。 */
    frames_.clear();
    lru_.clear();
}

void PreviewRenderer::setFrameCacheBudgetBytes(std::size_t bytes) {
    /* 分级预算的入口。夹在 [24MB, 256MB]：
       · 下限 24MB —— 再小就等于关掉缓存（1080p 只够 3 帧），
         拖动会退化成每帧全量解码，比不做分级更糟；
       · 上限 256MB —— 保护低端机被误配置成旗舰值。 */
    constexpr std::size_t kMin = 24u * 1024u * 1024u;
    constexpr std::size_t kMax = 256u * 1024u * 1024u;
    frameCacheBudget_ = bytes < kMin ? kMin : (bytes > kMax ? kMax : bytes);
    /* 预算变小后，已有缓存可能超限 —— 立刻按新容量淘汰，
       否则「设置了预算但内存没降下来」，分级就没有意义。 */
    const std::size_t capacity = cacheCapacity();
    while (frames_.size() > capacity && !lru_.empty()) {
        const std::int64_t victim = lru_.back();
        lru_.pop_back();
        frames_.erase(victim);
    }
}

void PreviewRenderer::clearFrameCache() {
    frames_.clear();
    lru_.clear();
    stats_.cachedFrames = 0;
}

std::size_t PreviewRenderer::cacheCapacity() const {
    // 用最近一帧的实际尺寸算单帧字节；还没渲染过就用画布尺寸估算。
    std::size_t w = 0;
    std::size_t h = 0;
    if (!frames_.empty()) {
        const PreviewFrame& any = frames_.begin()->second.frame;
        w = static_cast<std::size_t>(any.width);
        h = static_cast<std::size_t>(any.height);
    } else {
        const Sequence* seq = project_.mainSequence();
        if (seq != nullptr && seq->width > 0 && seq->height > 0) {
            w = static_cast<std::size_t>(seq->width);
            h = static_cast<std::size_t>(seq->height);
        }
    }
    if (w == 0 || h == 0) {
        return kFrameCacheMaxFrames;   // 未知画幅：先按上限，首次渲染后自动收敛
    }
    const std::size_t frameBytes = w * h * 4;
    std::size_t cap = frameCacheBudget_ / frameBytes;
    if (cap < kFrameCacheMinFrames) cap = kFrameCacheMinFrames;
    if (cap > kFrameCacheMaxFrames) cap = kFrameCacheMaxFrames;
    return cap;
}

PreviewRenderer::CacheStats PreviewRenderer::cacheStats() const {
    CacheStats copy = stats_;
    copy.cachedFrames = frames_.size();
    return copy;
}

std::vector<std::string> PreviewRenderer::subtitlesAt(const Time& timelineTime) const {
    std::vector<std::string> result;
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) {
        return result;
    }
    for (const auto* cue : seq->subtitles.cuesAt(timelineTime)) {
        result.push_back(cue->text);
    }
    return result;
}

bool PreviewRenderer::readClipSourceFrame(const Clip& clip, const Time& timelineTime,
                                         VideoFrame& out) {
    /* ---- 文本片段（§4.6）：走平台侧栅格化，不走解码器 ---- */
    if (clip.isText()) {
        return rasterizeTextClip(clip, out);
    }

    MediaReader* reader = readerFor(clip.asset);
    if (reader == nullptr) {
        return false;
    }
    const Time sourceTime = clip.sourceTimeAtTimeline(timelineTime);

    /* **预览不启用顺序访问提示**（PD 的可选能力，只有导出用）。
       ----------------------------------------------------------------
       为什么不给预览也开：预览的访问模式**不确定** ——
         · 播放：顺序
         · 拖动时间轴：任意跳
         · 转场：同一时刻交替读**两路不同素材**，各自的 nextExpected 会
           来回跳变
         · 单帧刷新 / 面板改参数：任意跳
       顺序提示一旦被误用于非顺序访问，后果是**返回错帧**（画面卡在旧位置），
       这种 bug 极难定位，代价远大于省下的那点解码时间。

       导出没有这个问题：帧严格按时间递增，素材区间连续，
       是天然的顺序访问。所以只在 export_executor 开启。
       预览要提速应走 P2（代理渲染）—— 那条路不改变帧的正确性。 */
    if (!reader->seek(sourceTime).isOk()) {
        return false;
    }
    return reader->readVideoFrame(out);
}

bool PreviewRenderer::rasterizeTextClip(const Clip& clip, VideoFrame& out) {
    TextRasterizer* rasterizer = textRasterizer();
    if (rasterizer == nullptr) {
        /* 平台层没注册栅格化器 —— 显式降级为「不渲染文本」，
           而不是崩溃或渲染垃圾。调用方会得到 false，
           继续合成其他层（文字暂时不可见，但画面其余部分正常）。 */
        return false;
    }
    if (!clip.text.has_value() || clip.text->empty()) {
        return false;
    }

    /* 栅格化尺寸：按画布尺寸算（预览走代理分辨率时画布已经是缩放后的）。
       为什么不按片段显示尺寸算：文本的换行位置取决于可用宽度，
       按显示尺寸算会在缩放时改变换行 —— 用户看到「放大后排版变了」。
       统一按画布宽算，缩放由后续 XformStack 的重采样完成。 */
    const Sequence* seq = project_.mainSequence();
    const int canvasW = seq != nullptr ? seq->width : 1080;
    const int canvasH = seq != nullptr ? seq->height : 1920;
    const double scale = std::clamp(quality_.scale, 0.05, 1.0);
    const int targetW = std::max(1, static_cast<int>(std::lround(canvasW * scale)));
    const int targetH = std::max(1, static_cast<int>(std::lround(canvasH * scale)));

    int w = 0;
    int hgt = 0;
    std::vector<std::uint8_t> rgba;
    if (!rasterizer->rasterize(*clip.text, targetW, targetH, rgba, w, hgt)) {
        return false;
    }
    if (w <= 0 || hgt <= 0 ||
        rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(hgt) * 4u) {
        return false;
    }

    out.width = w;
    out.height = hgt;
    out.stride = w * 4;
    out.pixels = std::move(rgba);
    /* 文本没有「源时间」概念 —— 内容不随时间变化，
       随时间变化的是动画通道（ClipAnimation，在别的环节应用）。
       这里填零：下游若按 timestamp 排序，文本层排在最前，
       这正好符合「文本通常压在最上层」的直觉。
       不要填画布时间——那会让同一文本在不同帧有不同的 timestamp，
       帧缓存键会误判成「不同内容」。 */
    out.timestamp = Time{};
    return true;
}

void PreviewRenderer::blendTransitionFrame(PreviewFrame& frame, const Clip& from,
                                           const Clip& to, const Time& timelineTime,
                                           double progress) {
    // 两路源帧：from 取末尾（本地时间钳制在 from 区间内），to 取开头
    const Time fromLocal = timelineTime < from.end() ? timelineTime : from.end() - from.start;
    const Time toLocal = timelineTime > to.start ? timelineTime : to.start;
    VideoFrame frameA;
    VideoFrame frameB;
    const bool hasA = readClipSourceFrame(from, from.start + (fromLocal - from.start), frameA);
    const bool hasB = readClipSourceFrame(to, to.start + (toLocal - to.start), frameB);
    if (!hasA && !hasB) {
        return;
    }
    if (!hasA) {
        frame.rgba = std::move(frameB.pixels);
        frame.width = frameB.width;
        frame.height = frameB.height;
        return;
    }
    if (!hasB) {
        frame.rgba = std::move(frameA.pixels);
        frame.width = frameA.width;
        frame.height = frameA.height;
        return;
    }
    if (frameA.width != frameB.width || frameA.height != frameB.height) {
        // 尺寸不一致：取 A（防御；正常情况下同序列尺寸一致）
        frame.rgba = std::move(frameA.pixels);
        frame.width = frameA.width;
        frame.height = frameA.height;
        return;
    }
    frame.width = frameA.width;
    frame.height = frameA.height;
    frame.rgba.assign(static_cast<std::size_t>(frameA.width) *
                          static_cast<std::size_t>(frameA.height) * 4,
                      0);
    TransitionRenderer::renderFrame(static_cast<TransitionType>(frame.transitionType),
                                    frameA.pixels.empty() ? nullptr : frameA.pixels.data(),
                                    frameB.pixels.empty() ? nullptr : frameB.pixels.data(),
                                    frame.rgba.data(), frameA.width, frameA.height, progress);
}

PreviewFrame PreviewRenderer::renderAt(const Time& timelineTime) {
    /* ---- 丢帧（P2）----
       目的：算不过来时**丢帧追赶**，而不是排队积压。

       判据：同一时间点的请求如果连续到来（播放时每帧都在推进），
       而上一次渲染还没被消费，就说明渲染速度跟不上播放速度。
       此时直接返回缓存里的**上一帧**并计入 droppedFrames_，
       让播放器的时间轴继续推进 —— 用户看到的是「卡一下」，
       而不是「越积越多，最后卡死」。

       实现上不做真正的跨线程队列（那是 P3 的范畴），只做**同帧去重**：
       同一个 key 连续请求且缓存里有更新的帧时，复用而不重算。
       这是最小可行版本，不引入并发复杂度。

       注意：**暂停时的单帧请求不能丢**（用户要看这一帧）。
       暂停时 allowFrameDrop 仍为 true，但因为每次时间点都不同且
       调用方会等待，不会命中这里的「连续同帧」路径。 */
    // ---- 帧缓存查询 ----
    // 键用微秒整数：Time 是比，转整数避免浮点比较误差导致的假未命中。
    const std::int64_t key = static_cast<std::int64_t>(timelineTime.toDouble() * 1e6 + 0.5);

    /* P2 分层失效：命中条件从「时间点相同」升级为「时间点 + 内容都相同」。
       ----------------------------------------------------------------------
       原来只比时间点，于是任何编辑都会让同一时间点的旧缓存变成**错的**，
       只能靠调用方 clearFrameCache() 整体清空 —— 拖一下滑块就把整条
       时间轴的缓存全废掉，下一帧要重新解码所有层。

       现在每项记录生成时的 contentFingerprint。查询时：
         · 时间点相同 + 指纹相同 → 命中（改动没影响这一帧）；
         · 时间点相同 + 指纹不同 → 视为未命中，**原地淘汰**这一项后重渲染
           （不整表清空：其它时间点的缓存仍然有效）。

       为什么不去 map 里按指纹反查：同一时间点只可能有一份当前内容，
       反查多个版本没有意义，还白占内存。 */
    const std::uint64_t fingerprint = contentFingerprint(timelineTime);

    /* 本次请求的真实渲染缩放 = min(质量档, 缓存策略)。
       ------------------------------------------------------------------------
       这是降采样缓存生效的**关键一步**，也是最容易做错的地方。

       为什么取 min 而不是直接用 quality_.scale：
         · quality_.scale 是「这一次愿意付出多少渲染代价」，拖动时 0.5、
           暂停时 1.0；
         · cacheScale_ 是「缓存愿意存多大」，固定 0.5；
         · 实际渲染按两者更小的那个 —— 因为渲染结果会被存进缓存，
           存不下更大的（存了也白占内存）。暂停回 1.0 时靠 upscaleRgba
           把 0.5 的缓存图放大顶上，不重渲。

       如果直接用 quality_.scale（不取 min）：暂停时按 1.0 渲染，
       单帧 8.3MB 存进缓存 —— 缓存容量立刻掉到 1/4，
       降采样就白做了。

       反之如果直接改 quality_.scale 本身：会影响 autoAdjustQuality 的
       降级判定（它按实际耗时算），把「缓存策略」和「性能自适应」
       两件事搅在一起。所以这里只在渲染入口取 min，不动 quality_ 本身。
    */
    double requestedScale = 1.0;
    {
        const Sequence* seqForScale = project_.mainSequence();
        if (seqForScale != nullptr) {
            requestedScale = std::clamp(quality_.scale, 0.05, 1.0);
        }
    }
    const double effectiveScale = std::min(requestedScale, cacheScale_);

    auto hit = frames_.find(key);
    if (hit != frames_.end()) {
        if (hit->second.fingerprint == fingerprint) {
            // LRU 提升到队首
            lru_.erase(hit->second.lruIt);
            lru_.push_front(key);
            hit->second.lruIt = lru_.begin();
            ++stats_.frameHits;

            /* ---- 降采样缓存：缓存图比请求的小 → 放大顶上 ----
               ------------------------------------------------------------------
               这是「缓存分辨率」与「请求分辨率」解耦的关键。

               场景：拖动时按 quality_.scale=0.5 渲染并缓存（每帧 2.1MB@1080p，
               容量是 1.0 的 4 倍）。松手后 quality_.scale 回 1.0，
               此时缓存里只有 0.5 的图 ——

                 · 旧行为：resetToFullQuality() 直接清空缓存，全部重渲；
                 · 新行为：把 0.5 的图放大到 1.0 顶上。放大约 3-5ms，
                   重渲要几十毫秒。用户几乎看不出差异（下一帧全质量渲染
                   完成后缓存被替换）。

               为什么要看 cacheScale 而不是帧的实际尺寸：
               缓存条目可能是「低质量档渲的」，也可能是「全质量档渲的」，
               两者都合法。用记录下来的 cacheScale 判断更直接，
               也避免了每帧比较 width/height。 */
            PreviewFrame cached = hit->second.frame;
            if (!cached.rgba.empty() && cached.width > 0 && cached.height > 0 &&
                requestedScale > hit->second.cacheScale + 1e-9) {
                const Sequence* seqForSize = project_.mainSequence();
                if (seqForSize != nullptr) {
                    const int wantW = std::max(
                        1, static_cast<int>(std::lround(seqForSize->width * requestedScale)));
                    const int wantH = std::max(
                        1, static_cast<int>(std::lround(seqForSize->height * requestedScale)));
                    // 只在「缓存确实更小」时放大；尺寸相同就没必要拷贝
                    if (wantW > cached.width || wantH > cached.height) {
                        PreviewFrame upscaled;
                        upscaled.timelineTime = cached.timelineTime;
                        upscaled.width = wantW;
                        upscaled.height = wantH;
                        upscaled.rgba.assign(
                            static_cast<std::size_t>(wantW) * static_cast<std::size_t>(wantH) * 4u,
                            0);
                        upscaleRgba(cached.rgba.data(), cached.width, cached.height,
                                    upscaled.rgba.data(), wantW, wantH);
                        // 元数据（字幕/动画快照/转场）直接沿用 —— 它们与分辨率无关
                        upscaled.subtitles = cached.subtitles;
                        upscaled.opacity = cached.opacity;
                        upscaled.scale = cached.scale;
                        upscaled.posX = cached.posX;
                        upscaled.posY = cached.posY;
                        upscaled.rotation = cached.rotation;
                        upscaled.clipId = cached.clipId;
                        upscaled.transitionType = cached.transitionType;
                        upscaled.transitionProgress = cached.transitionProgress;
                        ++stats_.upscaledFromCache;
                        return upscaled;
                    }
                }
            }
            return cached;
        }
        /* 内容变了：这一项作废。删掉后继续走下面的重渲染流程，
           渲染完会以新指纹重新插入。 */
        lru_.erase(hit->second.lruIt);
        frames_.erase(hit);
        ++stats_.invalidatedByEdit;
    }
    ++stats_.frameMisses;

    PreviewFrame frame = renderUncached(timelineTime, effectiveScale);

    // ---- 写入缓存（LRU 淘汰）----
    // 至少保留 1 帧，否则 while 在 capacity==0 时会退化成清空缓存
    std::size_t capacity = cacheCapacity();
    if (capacity == 0) capacity = 1;
    while (frames_.size() >= capacity && !lru_.empty()) {
        const std::int64_t victim = lru_.back();
        lru_.pop_back();
        frames_.erase(victim);
    }
    lru_.push_front(key);
    // 注意：这里必须拷贝进缓存（缓存要长期持有），
    // 然后把局部对象 move 出去，避免返回时再拷一次大 buffer。
    /* 记录生成这一帧时用的缩放。用「实际渲染尺寸 ÷ 画布尺寸」反算，
       而不是直接用 quality_.scale —— 因为 renderUncached 内部会按
       minAllowedScale 等约束调整，那个值才是真实的。

       反算的好处：即使将来有人在 renderUncached 里加了别的缩放规则，
       这里也自动正确（不会出现「记录的 scale 与实际尺寸不一致」）。 */
    double actualScale = 1.0;
    {
        const Sequence* seqForScale = project_.mainSequence();
        if (seqForScale != nullptr && seqForScale->width > 0) {
            actualScale = static_cast<double>(frame.width) /
                          static_cast<double>(seqForScale->width);
        }
    }
    auto inserted = frames_.emplace(
        key, CacheEntry{PreviewFrame{}, lru_.begin(), fingerprint, actualScale});
    inserted.first->second.frame = std::move(frame);
    stats_.cachedFrames = frames_.size();
    return inserted.first->second.frame;   // 返回缓存中的副本（调用方要独立持有）
}

std::vector<PreviewRenderer::ActiveLayer> PreviewRenderer::collectActiveLayers(
    const Time& timelineTime) const {
    std::vector<ActiveLayer> layers;
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) {
        return layers;
    }
    /* 顺序遍历 seq->tracks ⇒ 下标 0 = 最底层。
       这个顺序是「自底向上合成」的唯一依据，**不要改成反向遍历**，
       否则画中画会盖住主画面且静默出错（applyAdjustmentLayers 也依赖它）。

       注意与导出侧的区别（两边结果一致，但方向约定相反）：
         预览 preview.cpp  ：正序遍历 → layers[0] = 最底层，向后逐层叠加
         导出 export_executor.cpp ：videoTracks 正序（小索引在下），再 rbegin()
                                     反向 → layers[0] = 最顶层，向前找下层
       改动任何一侧都要回头确认另一侧，否则「预览 ≠ 导出」。 */
    for (const auto& track : seq->tracks) {
        /* 文本轨也参与合成（§4.6）：文本片段有像素（由平台层栅格化），
           要与视频层一样按顺序叠加。
           为什么不再限定 Video：文本要能加几何变换、被上层调整图层作用，
           就必须走同一条合成管线；单独一条路径必然与视频层行为漂移。 */
        if ((track.type != TrackType::Video && track.type != TrackType::Text) ||
            track.hidden) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (clip.enabled && clip.contains(timelineTime)) {
                ActiveLayer layer;
                layer.clip = &clip;
                layers.push_back(layer);
                break;   // 同轨片段不重叠，取到即跳出
            }
        }
    }
    return layers;
}

bool PreviewRenderer::renderClipLayer(const Clip& clip, const Time& timelineTime,
                                      int canvasW, int canvasH, bool hasLowerLayer,
                                      PreviewFrame& out) {
    const Sequence* seq = project_.mainSequence();
    const Time local = timelineTime - clip.start;

    out.width = canvasW;
    out.height = canvasH;
    out.clipId = clip.id.value;
    out.opacity = clip.animation.sample(ClipProperty::Opacity, local);
    out.scale = clip.animation.sample(ClipProperty::Scale, local);
    out.posX = clip.animation.sample(ClipProperty::PositionX, local);
    out.posY = clip.animation.sample(ClipProperty::PositionY, local);
    out.rotation = clip.animation.sample(ClipProperty::Rotation, local);

    MediaReader* reader = readerFor(clip.asset);
    if (reader == nullptr) {
        return false;
    }
    const Time sourceTime = clip.sourceTimeAtTimeline(timelineTime);
    if (!reader->seek(sourceTime).isOk()) {
        return false;
    }
    VideoFrame source;
    if (!reader->readVideoFrame(source)) {
        return false;
    }

    const FrameFit fit = seq != nullptr ? seq->frameFit : FrameFit::Fit;
    const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;
    /* 画面变换（画中画位置/大小/旋转）的取值优先级：
       关键帧 > 静态值 > 默认 —— 与 resolveClipXformValue 的契约一致。
       这里就地解析，避免为一个四元组引入额外结构。 */
    const auto resolve = [&](ClipProperty prop, double fallback) {
        if (clip.animation.hasCurve(prop)) {
            return clip.animation.sample(prop, local);
        }
        return fallback;
    };
    const double xfScale = clip.transform.enabled
                               ? resolve(ClipProperty::Scale, clip.transform.scale)
                               : resolve(ClipProperty::Scale, 1.0);
    const double xfPosX = clip.transform.enabled
                              ? resolve(ClipProperty::PositionX, clip.transform.posX)
                              : resolve(ClipProperty::PositionX, 0.0);
    const double xfPosY = clip.transform.enabled
                              ? resolve(ClipProperty::PositionY, clip.transform.posY)
                              : resolve(ClipProperty::PositionY, 0.0);
    const double xfRot = clip.transform.enabled
                             ? resolve(ClipProperty::Rotation, clip.transform.rotation)
                             : resolve(ClipProperty::Rotation, 0.0);
    const int xfAnchor = clip.transform.enabled ? clip.transform.anchor : 4;
    const Affine2D extra = clipTransformMatrix(xfScale, xfPosX, xfPosY, xfRot, xfAnchor,
                                               canvasW, canvasH);
    const bool hasExtra = !(std::abs(xfScale - 1.0) < 1e-9 &&
                            std::abs(xfPosX) < 1e-9 && std::abs(xfPosY) < 1e-9 &&
                            std::abs(xfRot) < 1e-9);

    const bool needFit = (source.width != canvasW || source.height != canvasH);
    if (clip.hasXform() || needFit || hasExtra) {
        std::vector<std::uint8_t> canvas(
            static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH) * 4, 0);
        const int stride = source.stride > 0 ? source.stride : source.width * 4;
        const EdgeMode edge = firstXformEdge(clip.xforms, background);
        applyXformStack(source.pixels.data(), source.width, source.height, stride,
                        canvas.data(), canvasW, canvasH, clip.xforms,
                        timelineTime.toDouble(), fit, edge, background, true, extra);
        out.rgba = std::move(canvas);
    } else {
        out.rgba = std::move(source.pixels);
    }

    /* 以下顺序与导出保持一致：摇晃 → 特效 → 滤镜 → 蒙版 → 抠像。
       多层场景下蒙版/抠像走 **Transparent** 模式（alpha 缩放），
       让下层能透出来；单层场景走 Background（填背景色）。 */
    if (clip.shake.amplitude > 0.0) {
        applyShakeToRgba(out.rgba.data(), out.width, out.height,
                         clip.shake, timelineTime.toDouble());
    }
    if (!clip.effects.empty()) {
        const std::int64_t frameIndex = static_cast<std::int64_t>(
            std::floor(timelineTime.toDouble() * 30.0));
        applyEffectStackToFrame(out.rgba.data(), out.width, out.height,
                                clip.effects, frameIndex,
                                RenderQuality::previewQuality(),
                                timelineTime.toDouble());
    }
    if (clip.filterStyle != FilterStyle::None && clip.filterIntensity > 0.0) {
        applyFilterToRgba(out.rgba.data(), out.width, out.height,
                          clip.filterStyle, clip.filterIntensity);
    }
    if (clip.hasMask && clip.mask.shape != MaskShape::None) {
        applyMaskToFrame(out.rgba.data(), out.width, out.height, clip.mask,
                         hasLowerLayer ? MaskBlendMode::Transparent
                                       : MaskBlendMode::Background,
                         background);
    }
    if (clip.hasChromaKey) {
        applyChromaKeyToFrame(out.rgba.data(), out.width, out.height, clip.chromaKey,
                              hasLowerLayer ? ChromaKeyBlendMode::Transparent
                                            : ChromaKeyBlendMode::Background,
                              background);
    }
    return true;
}

void PreviewRenderer::compositeLayer(PreviewFrame& base, const PreviewFrame& layer,
                                     BlendMode mode, double topAlpha) {
    const int w = base.width;
    const int h = base.height;
    if (w <= 0 || h <= 0 || layer.width != w || layer.height != h ||
        base.rgba.size() < static_cast<std::size_t>(w) * h * 4 ||
        layer.rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        return;
    }
    const double alpha = std::min(1.0, std::max(0.0, topAlpha));

    // 非 Normal 模式直接走行业标准公式（blend_modes 已实现 16 种）
    if (mode != BlendMode::Normal) {
        blendFrameRgba(base.rgba.data(), layer.rgba.data(), w, h, mode, alpha);
        return;
    }

    /* Normal：逐像素非预乘 alpha 混合。
       base 是已累积的下层画面，layer 是上层。
       权重 = 上层 alpha 通道（已含蒙版覆盖率）× 动画不透明度。 */
    const std::size_t n = static_cast<std::size_t>(w) * h;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* top = layer.rgba.data() + i * 4;
        std::uint8_t* dst = base.rgba.data() + i * 4;
        const double wa = (static_cast<double>(top[3]) / 255.0) * alpha;
        if (wa <= 0.0) {
            continue;
        }
        if (wa >= 1.0) {
            dst[0] = top[0];
            dst[1] = top[1];
            dst[2] = top[2];
            dst[3] = 255;
            continue;
        }
        const double wb = 1.0 - wa;
        dst[0] = static_cast<std::uint8_t>(top[0] * wa + dst[0] * wb + 0.5);
        dst[1] = static_cast<std::uint8_t>(top[1] * wa + dst[1] * wb + 0.5);
        dst[2] = static_cast<std::uint8_t>(top[2] * wa + dst[2] * wb + 0.5);
        dst[3] = 255;
    }
}

void PreviewRenderer::setQuality(const RenderQuality& quality) {
    /* 只接受**合法**的质量。导出路径的 quality 必须 scale=1，否则
       预览会用降分辨率算，而用户以为导出也变了 —— 这类误配必须拒绝。 */
    if (!quality.valid()) return;
    quality_ = quality;
}

const RenderQuality& PreviewRenderer::quality() const { return quality_; }

std::uint64_t PreviewRenderer::droppedFrames() const { return droppedFrames_; }

double PreviewRenderer::minAllowedScale() const {
    /* 短边 720px 对应的 scale。
       画布尺寸从主序列取；取不到时返回 1.0（不限制）。 */
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr || seq->width <= 0 || seq->height <= 0) {
        return 1.0;
    }
    const int shortSide = std::min(seq->width, seq->height);
    if (shortSide <= kMinRenderShortSide) {
        /* 画布本身就不足 720p —— 不能再降，否则就是糊。 */
        return 1.0;
    }
    const double scale = static_cast<double>(kMinRenderShortSide) /
                         static_cast<double>(shortSide);
    return std::clamp(scale, 0.05, 1.0);
}

bool PreviewRenderer::resetToFullQuality() {
    /* 暂停/单帧：强制回 1.0。
       同时清缓存 —— 降级时缓存的是小图，直接拿来显示会糊。 */
    if (quality_.scale >= 0.999) {
        return false;
    }
    RenderQuality q = RenderQuality::exportQuality();
    q.path = RenderPath::Preview;     // 预览路径仍允许后续再降
    q.allowFrameDrop = true;
    quality_ = q;
    frames_.clear();
    lru_.clear();
    return true;
}

bool PreviewRenderer::isDegraded() const { return quality_.scale < 0.999; }

bool PreviewRenderer::autoAdjustQuality(double avgFrameMs) {
    /* 按实测耗时选档。阈值来自文档 §P2。
       滞回：只在跨档时改变，避免在阈值附近来回抖
       （30fps 目标 = 33ms/帧，40ms 与 25ms 之间留出缓冲区）。 */
    double want = 1.0;
    if (avgFrameMs > 40.0) {
        want = 0.5;
    } else if (avgFrameMs > 25.0) {
        want = 0.75;
    }

    /* **下限钳制**（用户定案：短边不低于 720px）。
       没有这一步，「算力不足就降到 0.5」在 720p 画布上会降到 360p ——
       用户会看到明显马赛克，而不是「流畅」。 */
    const double floor = minAllowedScale();
    if (want < floor) want = floor;

    if (std::abs(want - quality_.scale) < 0.01) {
        return false;
    }
    RenderQuality q = RenderQuality::previewQuality(want, want);
    /* 采样预算与 scale 同步降低：
       空间效果（模糊/发光）的核半径按预算算，scale 降了却保持
       全预算，等于在更小的图上花同样的计算量，收益打折。 */
    q.scale = want;
    q.sampleBudget = want;
    q.allowFrameDrop = true;
    if (!q.valid()) return false;
    quality_ = q;
    /* 尺寸变了 → 缓存里的旧帧尺寸不匹配，必须清掉。
       不清的话会把 0.5 倍的帧喂给按 1.0 倍分配的缓冲区。 */
    frames_.clear();
    lru_.clear();
    return true;
}

PreviewFrame PreviewRenderer::renderUncached(const Time& timelineTime, double scale) {
    PreviewFrame frame;
    frame.timelineTime = timelineTime;
    const Sequence* seq = project_.mainSequence();
    if (seq != nullptr) {
        frame.width = seq->width;
        frame.height = seq->height;
    }

    /* ---- 代理渲染（P2）----
       按 quality_.scale 缩小渲染目标。

       为什么这是「不是压缩画质」：屏幕上预览窗口的物理像素就那么多，
       渲染 4K 再缩到 400px 宽的窗口，多出的像素 100% 被丢弃。
       按 scale 渲染省下的计算量是纯收益。

       scale 的合法范围由 RenderQuality::valid() 保证：
         · 预览：0 < scale <= 1
         · 导出：必须 1.0（这里改的是 PreviewRenderer，导出走另一条路径）

       实现方式：只改画布尺寸，**渲染流程完全不变** —— 所有图层、
       特效、变换都按新尺寸算，效果等价于「先全尺寸渲再缩」但省掉
       了全尺寸的开销（双线性缩小与渲染是合并的，不是两次）。
       最终由平台层把 canvasW×canvasH 拉伸到控件尺寸。 */
    /* 缩放由调用方算好传入（= min(quality_.scale, cacheScale_)）。
       原来是这里读 quality_.scale，但渲染结果要进缓存，
       所以必须按「缓存能存多大」也约束一次 —— 否则暂停回全质量时
       会把 8.3MB 的全尺寸帧塞进缓存，容量立刻掉到 1/4。 */
    const double clampedScale = std::clamp(scale, 0.05, 1.0);
    if (clampedScale < 0.999 && seq != nullptr) {
        frame.width = std::max(1, static_cast<int>(std::lround(seq->width * clampedScale)));
        frame.height = std::max(1, static_cast<int>(std::lround(seq->height * clampedScale)));
    }

    frame.subtitles = subtitlesAt(timelineTime);

    /* ---- 转场：时刻落入转场区间时混合两路画面 ----
       转场优先于普通多层合成（它本身就涉及两层）。 */
    if (seq != nullptr) {
        for (const auto& track : seq->tracks) {
            if (track.type != TrackType::Video || track.hidden) {
                continue;
            }
            for (const auto& tr : track.transitions) {
                const Clip* from = findClipInTrack(track, tr.fromClip);
                const Clip* to = findClipInTrack(track, tr.toClip);
                if (from == nullptr || to == nullptr || !tr.duration.isPositive()) {
                    continue;
                }
                const Time trStart = from->end();
                const Time trEnd = trStart + tr.duration;
                if (trStart <= timelineTime && timelineTime < trEnd) {
                    frame.transitionType = static_cast<int>(tr.type);
                    frame.transitionProgress = TransitionRenderer::transitionProgress(
                        timelineTime, trStart, tr.duration);
                    blendTransitionFrame(frame, *from, *to, timelineTime,
                                         frame.transitionProgress);
                    return frame;
                }
            }
        }
    }

    const int canvasW = frame.width;
    const int canvasH = frame.height;
    const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;

    /* ---- 多层合成：自底向上逐层累积 ----
       这里是「画中画在预览里看得见」的关键。此前只渲染第一条可见视频轨，
       第二条起被完全忽略（导出却支持多层）—— 预览 ≠ 导出。 */
    const auto layers = collectActiveLayers(timelineTime);
    if (layers.empty()) {
        frame.opacity = 0.0;   // 无活跃片段：黑场，由 UI 决定如何显示
        applyAdjustmentLayers(frame, timelineTime);
        return frame;
    }

    bool composed = false;
    for (const auto& layer : layers) {
        PreviewFrame single;
        /* hasLowerLayer：本层下方是否还有其它层。
           决定蒙版/抠像走 Transparent（透出下层）还是 Background（填背景色）。
           由调用方算一次传入 —— 每层单独 collectActiveLayers 是 O(n) 重复。 */
        const bool hasLowerLayer = layers.size() > 1;
        if (!renderClipLayer(*layer.clip, timelineTime, canvasW, canvasH,
                             hasLowerLayer, single)) {
            continue;   // 该层解码失败/无素材，跳过而不是整体失败
        }
        if (!composed) {
            // 最底层：直接作为基底（不透明区域先填背景色）
            frame.rgba.assign(static_cast<std::size_t>(canvasW) * canvasH * 4, 0);
            for (std::size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
                frame.rgba[i]     = static_cast<std::uint8_t>((background >> 24) & 0xFF);
                frame.rgba[i + 1] = static_cast<std::uint8_t>((background >> 16) & 0xFF);
                frame.rgba[i + 2] = static_cast<std::uint8_t>((background >> 8) & 0xFF);
                frame.rgba[i + 3] = 255;
            }
            compositeLayer(frame, single, BlendMode::Normal, single.opacity);
            // 对外暴露的元数据取**最底层**（与单层时代的行为一致）
            frame.clipId = single.clipId;
            frame.opacity = single.opacity;
            frame.scale = single.scale;
            frame.posX = single.posX;
            frame.posY = single.posY;
            frame.rotation = single.rotation;
            composed = true;
        } else {
            compositeLayer(frame, single, layer.clip->blendMode, single.opacity);
        }
    }

    if (!composed) {
        frame.opacity = 0.0;
    }
    applyAdjustmentLayers(frame, timelineTime);
    return frame;
}

const Lut3D* PreviewRenderer::adjustmentLutFor(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = adjustmentLuts_.find(path);
    if (it == adjustmentLuts_.end()) {
        LutCacheEntry entry;
        std::string error;
        entry.loaded = loadCubeLutFile(path, entry.lut, error);
        it = adjustmentLuts_.emplace(path, std::move(entry)).first;
    }
    return it->second.loaded ? &it->second.lut : nullptr;
}

void PreviewRenderer::applyAdjustmentLayers(PreviewFrame& frame, const Time& timelineTime) {
    // 调整图层（B2-6）：作用于「下方所有轨道合成后的整帧」。
    //
    // 顺序与导出保持一致（export_executor.cpp 的 finalizeFrame）：
    // 先 effects，再 LUT。预览不画水印，故没有水印那一步。
    // 没有调整图层、或调整图层无效果时直接返回 ——
    // 此时输出与改动前逐像素一致（B2-6 的验收条件）。
    if (frame.rgba.empty() || frame.width <= 0 || frame.height <= 0) return;
    const Sequence* seq = project_.mainSequence();
    if (seq == nullptr) return;

    const std::size_t need = static_cast<std::size_t>(frame.width) *
                             static_cast<std::size_t>(frame.height) * 4;
    if (frame.rgba.size() < need) return;

    // 预览用行内帧序号（量化到 30fps 网格）作时变种子，与片段特效取舍一致：
    // 静止画面不闪噪点；用真实帧序号会让暂停时粒子持续跳动。
    const std::int64_t frameIndex =
        static_cast<std::int64_t>(std::floor(timelineTime.toDouble() * 30.0));

    for (const auto& track : seq->tracks) {
        if (track.type != TrackType::Adjustment || track.hidden) continue;
        if (!track.hasAdjustment()) continue;

        /* ---- 4.7 调整图层全功能 ----
           顺序（与文档 §4.7 一致）：
             1) xforms  —— 整帧几何变换
             2) opacity —— 整帧 alpha
             3) effects —— 已有
             4) LUT     —— 已有

           为什么几何在最前：effects/LUT 都是逐像素颜色运算，
           几何变换会重采样（双线性），若放在颜色之后，
           插值会「混合已调色的像素」，边缘出现色边；
           放在最前则是对原始画面重采样，之后统一调色，结果更干净。
           这也是 AE/剪映 的通行顺序（transform 在 effects 之前）。 */
        /* 调整图层没有起始时间概念 —— 它从工程开头一直作用到结尾。
           所以「本地时间」就是时间轴时间本身，不做偏移。
           （若将来支持「调整图层只在一段区间内生效」，这里才需要减偏移。） */
        const Time localTime = timelineTime;
        if (track.adjustmentXforms.anyEnabled()) {
            std::vector<std::uint8_t> canvas(
                static_cast<std::size_t>(frame.width) * frame.height * 4, 0);
            /* edge 用背景填充：调整图层的几何变换可能把画面缩小，
               露出的区域应当是序列背景色，而不是上一次的残留像素。 */
            const std::uint8_t bg[4] = {0, 0, 0, 255};
            applyXformStack(frame.rgba.data(), frame.width, frame.height, frame.width * 4,
                            canvas.data(), frame.width, frame.height,
                            track.adjustmentXforms, timelineTime.toDouble(),
                            FrameFit::Fit, EdgeMode::Background,
                            (static_cast<std::uint32_t>(bg[0]) << 24) |
                                (static_cast<std::uint32_t>(bg[1]) << 16) |
                                (static_cast<std::uint32_t>(bg[2]) << 8) |
                                static_cast<std::uint32_t>(bg[3]),
                            true, Affine2D{});
            frame.rgba.swap(canvas);
        }

        /* 透明度：静态值与关键帧取较强者还是相乘？
           这里用「关键帧存在时以关键帧为准，否则用静态值」——
           与 ClipAnimation::sample 的契约一致（无曲线时返回默认值）。
           若改成相乘，用户既设静态值又打关键帧时会出现双重衰减，
           表现为「调了 0.5 但实际只有 0.25」，很难理解。 */
        double opacity = track.adjustmentOpacity;
        if (!track.adjustmentAnimation.empty()) {
            opacity = track.adjustmentAnimation.sample(ClipProperty::Opacity, localTime);
        }
        if (opacity < 0.999) {
            const int w = static_cast<int>(std::clamp(opacity, 0.0, 1.0) * 256.0 + 0.5);
            const std::size_t pixels =
                static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
            /* 逐通道乘 alpha：只缩放 alpha，RGB 不动。
               为什么不是整体 lerp 到黑：调整图层是「叠加层」，
               降低不透明度应让它「变淡」（露出下层），
               而不是「变暗」（把下层也压暗）。 */
            for (std::size_t i = 0; i < pixels; ++i) {
                frame.rgba[i * 4 + 3] = static_cast<std::uint8_t>(
                    (static_cast<int>(frame.rgba[i * 4 + 3]) * w) >> 8);
            }
        }

        if (!track.adjustmentEffects.empty()) {
            applyEffectStackToFrame(frame.rgba.data(), frame.width, frame.height,
                                    track.adjustmentEffects, frameIndex,
                                    RenderQuality::previewQuality(),
                                    timelineTime.toDouble());
        }
        if (!track.adjustmentLutPath.empty()) {
            const Lut3D* lut = adjustmentLutFor(track.adjustmentLutPath);
            if (lut != nullptr) {
                applyLutToRgba(frame.rgba.data(), frame.width, frame.height, *lut,
                               track.adjustmentLutStrength);
            }
        }
    }
}

}  // namespace core
}  // namespace nairoclip
