#include "nairoclip/core/filters.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"

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
    if (frames_.size() >= kFrameCacheCapacity && !lru_.empty()) {
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
        if (source.width == canvasW && source.height == canvasH) {
            frame.rgba = std::move(source.pixels);
            frame.width = source.width;
            frame.height = source.height;
        } else {
            // 源尺寸与画布不一致：按序列画幅适配策略渲染（与导出管线保持一致）。
            std::vector<std::uint8_t> canvas(
                static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH) * 4, 0);
            const int stride = source.stride > 0 ? source.stride : source.width * 4;
            renderFrameFit(source.pixels.data(), source.width, source.height, stride,
                           canvas.data(), canvasW, canvasH, fit, background);
            frame.rgba = std::move(canvas);
            frame.width = canvasW;
            frame.height = canvasH;
        }
        // 应用片段特效栈（颜色变换 + 空间效果 + 画面特效）
        // 预览用行内帧序号（时间量化到 30fps 网格）作为时变种子，保证同一时刻
        // 预览稳定（静止画面不闪噪点）；导出用真实帧序号（噪点随时间流动）。
        // 质量分级（文档 4.4）：预览路径按 sampleBudget 简化高采样效果
        // （旋转模糊/甩镜等）、减少粒子数量，不改变导出质量上限（三端对等）。
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
    return frame;
}

}  // namespace core
}  // namespace nairoclip
