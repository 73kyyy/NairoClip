#include "nairoclip/core/filters.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/shake_effect.h"
#include "nairoclip/core/lut.h"

#include <algorithm>
#include <utility>

namespace nairoclip {
namespace core {

namespace {

/// 在主序列中查找 t 时刻活跃的视频片段（第一个未隐藏视频轨）。
const Clip* activeVideoClip(const Project& project, TrackId* outTrack, const Time& t) {
    const Sequence* seq = project.mainSequence();
    if (seq == nullptr) {
        return nullptr;
    }
    for (const auto& track : seq->tracks) {
        if (track.type != TrackType::Video || track.hidden) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (clip.enabled && clip.contains(t)) {
                if (outTrack != nullptr) {
                    *outTrack = track.id;
                }
                return &clip;
            }
        }
        // 只取第一个可见视频轨
        break;
    }
    return nullptr;
}

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

PreviewFrame PreviewRenderer::renderUncached(const Time& timelineTime) {
    PreviewFrame frame;
    frame.timelineTime = timelineTime;
    const Sequence* seq = project_.mainSequence();
    if (seq != nullptr) {
        frame.width = seq->width;
        frame.height = seq->height;
    }

    // 字幕
    frame.subtitles = subtitlesAt(timelineTime);

    // ---- 转场检测：时刻落入转场区间时混合两路画面 ----
    const Sequence* seq2 = project_.mainSequence();
    if (seq2 != nullptr) {
        for (const auto& track : seq2->tracks) {
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
                    // 双路读取 + 混合
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

    // 视频片段
    TrackId trackId;
    const Clip* clip = activeVideoClip(project_, &trackId, timelineTime);
    if (clip == nullptr) {
        // 无活跃片段：黑场（由 UI 决定如何显示）
        frame.opacity = 0.0;
        return frame;
    }
    frame.clipId = clip->id.value;

    // 动画属性采样（本地时间）
    const Time local = timelineTime - clip->start;
    frame.opacity = clip->animation.sample(ClipProperty::Opacity, local);
    frame.scale = clip->animation.sample(ClipProperty::Scale, local);
    frame.posX = clip->animation.sample(ClipProperty::PositionX, local);
    frame.posY = clip->animation.sample(ClipProperty::PositionY, local);
    frame.rotation = clip->animation.sample(ClipProperty::Rotation, local);

    // 读取源帧：解码器由 readerFor 复用（同一素材只 open 一次）
    MediaReader* reader = readerFor(clip->asset);
    if (reader == nullptr) {
        return frame;
    }
    const Time sourceTime = clip->sourceTimeAtTimeline(timelineTime);
    if (!reader->seek(sourceTime).isOk()) {
        return frame;
    }
    VideoFrame source;
    if (reader->readVideoFrame(source)) {
        const int canvasW = seq != nullptr && seq->width > 0 ? seq->width : source.width;
        const int canvasH = seq != nullptr && seq->height > 0 ? seq->height : source.height;
        const FrameFit fit = seq != nullptr ? seq->frameFit : FrameFit::Fit;
        const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;
        // 几何变换（「转换」8 项）与画幅适配合并为**单次重采样**：
        // 走 applyXformStack，避免「先 fit 再变换」的二次插值。
        const bool needFit = (source.width != canvasW || source.height != canvasH);
        if (clip->hasXform() || needFit) {
            std::vector<std::uint8_t> canvas(
                static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH) * 4, 0);
            const int stride = source.stride > 0 ? source.stride : source.width * 4;
            const EdgeMode edge = firstXformEdge(clip->xforms, background);
            applyXformStack(source.pixels.data(), source.width, source.height, stride,
                            canvas.data(), canvasW, canvasH, clip->xforms,
                            timelineTime.toDouble(), fit, edge, background, true);
            frame.rgba = std::move(canvas);
            frame.width = canvasW;
            frame.height = canvasH;
        } else {
            // 尺寸已匹配且无几何变换：直接接管像素（零拷贝快速路径）。
            frame.rgba = std::move(source.pixels);
            frame.width = source.width;
            frame.height = source.height;
        }
        // 应用片段特效栈（颜色变换 + 空间效果 + 画面特效）
        // 预览用行内帧序号（时间量化到 30fps 网格）作为时变种子，保证同一时刻
        // 预览稳定（静止画面不闪噪点）；导出用真实帧序号（噪点随时间流动）。
        // 质量分级（文档 4.4）：预览路径按 sampleBudget 简化高采样效果
        // （旋转模糊/甩镜等）、减少粒子数量，不改变导出质量上限（三端对等）。
        /* 片段摇晃（B1-5 / B2-3）。
           顺序与 export_executor.cpp 保持一致：摇晃改变几何位置，
           之后的调色/特效作用在已位移的画面上（"先抖动再调色"）。

           这段之前**只在导出端存在**，预览完全没有 —— 用户调了震动，
           预览看不出来，导出却有，属于两端不一致。 */
        if (clip->shake.amplitude > 0.0) {
            applyShakeToRgba(frame.rgba.data(), frame.width, frame.height,
                             clip->shake, timelineTime.toDouble());
        }
        if (!clip->effects.empty()) {
            const std::int64_t frameIndex = static_cast<std::int64_t>(
                std::floor(timelineTime.toDouble() * 30.0));
            applyEffectStackToFrame(frame.rgba.data(), frame.width, frame.height,
                                    clip->effects, frameIndex,
                                    RenderQuality::previewQuality(),
                                    timelineTime.toDouble());
        }
        // 应用片段滤镜（文档「🎨滤镜」；预览与导出一致）
        // 顺序：先滤镜定色彩基调，再叠特效，最后蒙版/抠像裁剪范围。
        if (clip->filterStyle != FilterStyle::None && clip->filterIntensity > 0.0 &&
            frame.rgba.size() >=
                static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4) {
            applyFilterToRgba(frame.rgba.data(), frame.width, frame.height,
                              clip->filterStyle, clip->filterIntensity);
        }
        // 应用片段蒙版（文档「✂️剪辑 → 蒙版」）
        // 预览用 Background 模式：蒙版外填充序列背景色，UI 直接显示最终画面效果。
        if (clip->hasMask && clip->mask.shape != MaskShape::None &&
            frame.rgba.size() >=
                static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4) {
            const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;
            applyMaskToFrame(frame.rgba.data(), frame.width, frame.height, clip->mask,
                             MaskBlendMode::Background, background);
        }
        // 应用色度抠像（文档「🤖AI实验室 → 智能抠像 → 色度抠像(绿幕)」）
        // 与蒙版一致：预览用 Background 模式，UI 直接看到抠像后的合成画面。
        if (clip->hasChromaKey &&
            frame.rgba.size() >=
                static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4) {
            const std::uint32_t background = seq != nullptr ? seq->backgroundRgba : 0x000000FF;
            applyChromaKeyToFrame(frame.rgba.data(), frame.width, frame.height, clip->chromaKey,
                                  ChromaKeyBlendMode::Background, background);
        }
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
