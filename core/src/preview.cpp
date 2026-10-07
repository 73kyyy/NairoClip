#include "nairoclip/core/preview.h"
#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"

#include <algorithm>

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
    auto it = readers_.find(clip.asset.value);
    if (it == readers_.end()) {
        const MediaAsset* asset = project_.findAsset(clip.asset);
        if (asset == nullptr) {
            return false;
        }
        auto reader = readerFactory_.create();
        if (!reader->open(asset->path).isOk()) {
            return false;
        }
        it = readers_.emplace(clip.asset.value, std::move(reader)).first;
    }
    MediaReader* reader = it->second.get();
    const Time sourceTime = clip.sourceIn + Time{(timelineTime - clip.start) * clip.speed};
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

    // 读取源帧（简化：Mock 路径按帧时间 seek+read）
    auto it = readers_.find(clip->asset.value);
    if (it == readers_.end()) {
        const MediaAsset* asset = project_.findAsset(clip->asset);
        if (asset == nullptr) {
            return frame;
        }
        auto reader = readerFactory_.create();
        if (!reader->open(asset->path).isOk()) {
            return frame;
        }
        it = readers_.emplace(clip->asset.value, std::move(reader)).first;
    }
    MediaReader* reader = it->second.get();
    const Time sourceTime = clip->sourceIn + Time{(timelineTime - clip->start) * clip->speed};
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
        // 应用片段特效栈的颜色变换
        if (!clip->effects.empty()) {
            const ColorTransform transform = evaluateColorTransform(clip->effects);
            const std::size_t pixelCount =
                static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
            if (frame.rgba.size() >= pixelCount * 4) {
                applyColorTransformToRgba(frame.rgba.data(), pixelCount, transform);
            }
        }
    }
    return frame;
}

}  // namespace core
}  // namespace nairoclip
