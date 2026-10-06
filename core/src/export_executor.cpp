#include "nairoclip/core/export_executor.h"

#include <cstring>

#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"

#include <algorithm>
#include <map>

namespace nairoclip {
namespace core {

namespace {

/// 每个素材的读取器缓存条目。
struct ReaderEntry {
    std::unique_ptr<MediaReader> reader;
    bool opened = false;
};

/// 找到覆盖时间点 t 的片段（单轨内）。
const RenderClip* clipAt(const RenderTrack& track, const Time& t) {
    for (const auto& clip : track.clips) {
        if (clip.timelineStart <= t && t < clip.timelineEnd) {
            return &clip;
        }
    }
    return nullptr;
}

}  // namespace

ExportStats executeExport(
    const ExportPlan& plan, MediaReaderFactory& readerFactory,
    const std::function<void(const OutputVideoFrame&)>& videoSink,
    const std::function<void(const OutputAudioBlock&)>& audioSink,
    const std::function<bool(double)>& progress, const ExportExecutorOptions& options) {
    ExportStats stats;
    stats.warnings = plan.warnings;

    if (!exportPlanValid(plan)) {
        stats.warnings.push_back("导出计划无效，已中止");
        return stats;
    }

    // 素材读取器缓存（按 asset ID 复用实例）
    std::map<std::uint64_t, ReaderEntry> readers;
    auto getReader = [&](const AssetId& asset) -> MediaReader* {
        auto it = readers.find(asset.value);
        if (it == readers.end()) {
            ReaderEntry entry;
            entry.reader = readerFactory.create();
            readers.emplace(asset.value, std::move(entry));
            it = readers.find(asset.value);
        }
        ReaderEntry& entry = it->second;
        return entry.reader.get();
    };

    // 主视频轨（第一个可见视频轨）+ 主音频轨
    const RenderTrack* videoTrack = nullptr;
    const RenderTrack* audioTrack = nullptr;
    for (const auto& track : plan.tracks) {
        if (videoTrack == nullptr && track.type == TrackType::Video && !track.hidden) {
            videoTrack = &track;
        }
        if (audioTrack == nullptr && track.type == TrackType::Audio && !track.muted) {
            audioTrack = &track;
        }
    }

    const FrameRate fps = options.overrideFps > 0 ? FrameRate::of(options.overrideFps)
                                                  : plan.frameRate;
    const Time totalDuration = plan.duration;

    // ---- 视频遍历 ----
    if (options.outputVideo && videoSink && !plan.tracks.empty()) {
        std::int64_t frameIndex = 0;
        while (true) {
            const Time t = timeAtFrame(fps, frameIndex);
            if (!(t < totalDuration)) {
                break;  // 输出与序列等长
            }
            if (options.maxFrames > 0 && stats.framesWritten >= options.maxFrames) {
                break;
            }
            if (progress != nullptr && totalDuration.isPositive()) {
                const double p = t.toDouble() / totalDuration.toDouble();
                if (!progress(std::min(1.0, std::max(0.0, p)))) {
                    stats.cancelled = true;
                    return stats;
                }
            }

            // ---- 转场检测（优先于空档判断）----
            const RenderTransition* activeTransition = nullptr;
            if (videoTrack != nullptr) {
                for (const auto& tr : videoTrack->transitions) {
                    if (tr.start <= t && t < tr.end) {
                        activeTransition = &tr;
                        break;
                    }
                }
            }

            // 找到当前可见片段（仅主视频轨；多层合成 M1.x 实现）
            const RenderClip* active = videoTrack != nullptr ? clipAt(*videoTrack, t) : nullptr;
            if (active == nullptr && activeTransition != nullptr) {
                // 转场区间内的帧：尝试读取 from/to 两路源帧并混合
                OutputVideoFrame frame;
                frame.timestamp = t;
                frame.width = plan.width;
                frame.height = plan.height;
                frame.transitionType = static_cast<int>(activeTransition->type);
                frame.transitionProgress =
                    TransitionRenderer::transitionProgress(t, activeTransition->start,
                                                           activeTransition->end -
                                                               activeTransition->start);

                // 定位 from/to 两个 RenderClip
                const RenderClip* fromClip = nullptr;
                const RenderClip* toClip = nullptr;
                for (const auto& c : videoTrack->clips) {
                    if (c.clipId == activeTransition->fromClip) fromClip = &c;
                    if (c.clipId == activeTransition->toClip) toClip = &c;
                }

                bool blendDone = false;
                if (fromClip != nullptr && toClip != nullptr) {
                    // 读取两路源帧（Mock 路径：seek + read 一帧）
                    auto readFrame = [&](const RenderClip* rc, const Time& clampTime,
                                         VideoFrame& out) -> bool {
                        MediaReader* r = getReader(rc->asset);
                        if (r == nullptr) return false;
                        auto openedIt = readers.find(rc->asset.value);
                        if (openedIt != readers.end() && !openedIt->second.opened) {
                            if (!r->open(rc->assetPath).isOk()) return false;
                            openedIt->second.opened = true;
                        }
                        const Time local = clampTime - rc->timelineStart;
                        const Time srcTime = rc->sourceIn + Time{local * rc->speed};
                        if (!r->seek(srcTime).isOk()) return false;
                        return r->readVideoFrame(out);
                    };
                    // from 取区间内（钳制到片段尾部），to 取区间内（钳制到片段头部）
                    const Time fromT = std::min(t, fromClip->timelineEnd -
                                                       Time{Rational::make(1, 1000)});
                    const Time toT = std::max(t, toClip->timelineStart);
                    VideoFrame frameA;
                    VideoFrame frameB;
                    const bool hasA = readFrame(fromClip, fromT, frameA);
                    const bool hasB = readFrame(toClip, toT, frameB);
                    (void)hasA;
                    (void)hasB;
                    // 混合（任一缺失时用占位帧填充）
                    const int w = plan.width;
                    const int h = plan.height;
                    const std::size_t pixelBytes =
                        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
                    std::vector<std::uint8_t> bufA = frameA.pixels.size() >= pixelBytes
                                                        ? frameA.pixels
                                                        : std::vector<std::uint8_t>(pixelBytes, 0);
                    std::vector<std::uint8_t> bufB = frameB.pixels.size() >= pixelBytes
                                                        ? frameB.pixels
                                                        : std::vector<std::uint8_t>(pixelBytes, 0);
                    // 确保 alpha 不透明
                    for (std::size_t i = 3; i < bufA.size(); i += 4) bufA[i] = 255;
                    for (std::size_t i = 3; i < bufB.size(); i += 4) bufB[i] = 255;
                    frame.rgba.assign(pixelBytes, 0);
                    blendDone = TransitionRenderer::renderFrame(
                        static_cast<TransitionType>(frame.transitionType), bufA.data(), bufB.data(),
                        frame.rgba.data(), w, h, frame.transitionProgress);
                }
                if (!blendDone) {
                    // 防御：无有效数据源时输出占位帧（保持管线不断）
                    frame.rgba.assign(static_cast<std::size_t>(plan.width) *
                                          static_cast<std::size_t>(plan.height) * 4,
                                      32);
                    for (std::size_t i = 3; i < frame.rgba.size(); i += 4) {
                        frame.rgba[i] = 255;
                    }
                }
                videoSink(frame);
                ++stats.framesWritten;
                ++frameIndex;
                continue;
            }
            if (active == nullptr) {
                // 空档：输出黑帧（保持时间轴节奏）
                OutputVideoFrame frame;
                frame.timestamp = t;
                frame.width = plan.width;
                frame.height = plan.height;
                frame.rgba.assign(static_cast<std::size_t>(plan.width) *
                                      static_cast<std::size_t>(plan.height) * 4,
                                  0);
                for (std::size_t i = 3; i < frame.rgba.size(); i += 4) {
                    frame.rgba[i] = 255;  // 不透明黑
                }
                videoSink(frame);
                ++stats.framesWritten;
                ++frameIndex;
                continue;
            }

            ++stats.clipsVisited;

            MediaReader* reader = getReader(active->asset);
            if (reader == nullptr) {
                stats.warnings.push_back("无法创建素材读取器");
                ++stats.clipsSkipped;
                ++frameIndex;
                continue;
            }
            // 映射时间轴时间 → 源素材时间：sourceIn + (t - timelineStart) * speed
            const Time sourceTime =
                active->sourceIn + Time{(t - active->timelineStart) * active->speed};
            // 确保素材已打开（每条素材只开一次）
            const auto openedIt = readers.find(active->asset.value);
            if (openedIt != readers.end() && !openedIt->second.opened) {
                if (!reader->open(active->assetPath).isOk()) {
                    stats.warnings.push_back("素材打开失败：" + active->assetPath);
                    ++stats.clipsSkipped;
                    ++frameIndex;
                    continue;
                }
                openedIt->second.opened = true;
            }
            // 真实解码：seek 到源时间并读取一帧
            bool decoded = false;
            if (reader->isOpen()) {
                if (reader->seek(sourceTime).isOk()) {
                    VideoFrame source;
                    if (reader->readVideoFrame(source)) {
                        decoded = true;
                        // 输出帧尺寸以计划为准；源尺寸不同则居中放置（简化：直接拷贝到左上角）
                        OutputVideoFrame frame;
                        frame.timestamp = t;
                        frame.width = plan.width;
                        frame.height = plan.height;
                        const std::size_t outBytes = static_cast<std::size_t>(plan.width) *
                                                     static_cast<std::size_t>(plan.height) * 4;
                        frame.rgba.assign(outBytes, 0);
                        for (std::size_t i = 3; i < frame.rgba.size(); i += 4) {
                            frame.rgba[i] = 255;
                        }
                        const int copyW = std::min(source.width, plan.width);
                        const int copyH = std::min(source.height, plan.height);
                        const int srcStride = source.stride > 0 ? source.stride : source.width * 4;
                        for (int row = 0; row < copyH; ++row) {
                            const std::uint8_t* src = source.pixels.data() +
                                static_cast<std::size_t>(row) * static_cast<std::size_t>(srcStride);
                            std::uint8_t* dst = frame.rgba.data() +
                                static_cast<std::size_t>(row) *
                                    static_cast<std::size_t>(plan.width) * 4;
                            std::memcpy(dst, src,
                                        static_cast<std::size_t>(copyW) * 4);
                        }
                        // 应用片段特效（颜色变换）
                        if (!active->effects.empty()) {
                            const ColorTransform transform = evaluateColorTransform(active->effects);
                            if (!transform.isIdentity()) {
                                applyColorTransformToRgba(frame.rgba.data(),
                                                         static_cast<std::size_t>(plan.width) *
                                                             static_cast<std::size_t>(plan.height),
                                                         transform);
                            }
                        }
                        if (activeTransition != nullptr) {
                            frame.transitionType = static_cast<int>(activeTransition->type);
                            frame.transitionProgress = TransitionRenderer::transitionProgress(
                                t, activeTransition->start,
                                activeTransition->end - activeTransition->start);
                        }
                        videoSink(frame);
                        ++stats.framesWritten;
                        ++frameIndex;
                        continue;
                    }
                }
            }
            if (!decoded) {
                // 解码失败：输出警告色帧（保持管线不断；统计可见）
                stats.warnings.push_back("帧解码失败 @ " + sourceTime.toString());
                OutputVideoFrame frame;
                frame.timestamp = t;
                frame.width = plan.width;
                frame.height = plan.height;
                frame.rgba.assign(static_cast<std::size_t>(plan.width) *
                                      static_cast<std::size_t>(plan.height) * 4,
                                  32);
                for (std::size_t i = 3; i < frame.rgba.size(); i += 4) {
                    frame.rgba[i] = 255;
                }
                videoSink(frame);
                ++stats.framesWritten;
            }
            ++frameIndex;
        }
    }

    // ---- 音频遍历（粗粒度：按 1 秒块驱动，真实实现为采样精确混音）----
    if (options.outputAudio && audioSink && audioTrack != nullptr) {
        const int sampleRate = 48000;
        const int channels = 2;
        const int blockFrames = 1024;
        for (const auto& clip : audioTrack->clips) {
            if (progress != nullptr && totalDuration.isPositive()) {
                const double p = clip.timelineStart.toDouble() / totalDuration.toDouble();
                if (!progress(std::min(1.0, std::max(0.0, p)))) {
                    stats.cancelled = true;
                    return stats;
                }
            }
            stats.clipsVisited += 1;
            // 逐块输出该片段覆盖的时间范围（静音填充，真实混音在平台层）
            Time cursor = clip.timelineStart;
            while (cursor < clip.timelineEnd) {
                OutputAudioBlock block;
                block.start = cursor;
                block.sampleRate = sampleRate;
                block.channels = channels;
                block.samples.assign(static_cast<std::size_t>(blockFrames) * channels, 0.0f);
                audioSink(block);
                ++stats.audioBlocksWritten;
                cursor = cursor + Time{Rational::make(blockFrames, sampleRate)};
            }
        }
    }

    if (progress != nullptr) {
        progress(1.0);
    }
    return stats;
}

}  // namespace core
}  // namespace nairoclip
