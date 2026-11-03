#include "nairoclip/core/filters.h"
#include "nairoclip/core/preview.h"
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
    std::size_t cap = kFrameCacheBudgetBytes / frameBytes;
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
    MediaReader* reader = readerFor(clip.asset);
    if (reader == nullptr) {
        return false;
    }
    const Time sourceTime = clip.sourceTimeAtTimeline(timelineTime);
    if (!reader->seek(sourceTime).isOk()) {
        return false;
    }
    return reader->readVideoFrame(out);
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
    // ---- 帧缓存查询 ----
    // 键用微秒整数：Time 是比，转整数避免浮点比较误差导致的假未命中。
    const std::int64_t key = static_cast<std::int64_t>(timelineTime.toDouble() * 1e6 + 0.5);
    auto hit = frames_.find(key);
    if (hit != frames_.end()) {
        // LRU 提升到队首
        lru_.erase(hit->second.lruIt);
        lru_.push_front(key);
        hit->second.lruIt = lru_.begin();
        ++stats_.frameHits;
        return hit->second.frame;
    }
    ++stats_.frameMisses;

    PreviewFrame frame = renderUncached(timelineTime);

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
    auto inserted = frames_.emplace(
        key, CacheEntry{PreviewFrame{}, lru_.begin()});
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
        if (track.type != TrackType::Video || track.hidden) {
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
    const bool needFit = (source.width != canvasW || source.height != canvasH);
    if (clip.hasXform() || needFit) {
        std::vector<std::uint8_t> canvas(
            static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH) * 4, 0);
        const int stride = source.stride > 0 ? source.stride : source.width * 4;
        const EdgeMode edge = firstXformEdge(clip.xforms, background);
        applyXformStack(source.pixels.data(), source.width, source.height, stride,
                        canvas.data(), canvasW, canvasH, clip.xforms,
                        timelineTime.toDouble(), fit, edge, background, true);
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

PreviewFrame PreviewRenderer::renderUncached(const Time& timelineTime) {
    PreviewFrame frame;
    frame.timelineTime = timelineTime;
    const Sequence* seq = project_.mainSequence();
    if (seq != nullptr) {
        frame.width = seq->width;
        frame.height = seq->height;
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
