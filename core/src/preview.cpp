#include "nairoclip/core/preview.h"

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
        frame.rgba = std::move(source.pixels);
        frame.width = source.width;
        frame.height = source.height;
    }
    return frame;
}

}  // namespace core
}  // namespace nairoclip
