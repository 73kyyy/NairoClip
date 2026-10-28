#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/lut.h"

#include <cstring>

#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/reframe.h"
#include "nairoclip/core/transition_render.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/shake_effect.h"
#include "nairoclip/core/filters.h"
#include "nairoclip/core/background_fill.h"
#include "nairoclip/core/mask.h"
#include "nairoclip/core/ai_label.h"
#include "nairoclip/core/audio_mixer.h"

#include <algorithm>
#include <map>

namespace nairoclip {
namespace core {

namespace {

/// 解析 #RRGGBB / #RRGGBBAA 为 RGBA8 打包（0xRRGGBBAA）。
std::uint32_t parseColorRgba(const std::string& text) {
    const char* str = text.c_str();
    if (text.empty() || text[0] != '#') {
        return 0xFFFFFFFF;
    }
    const std::size_t hexLen = text.size() - 1;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    unsigned long value = 0;
    for (std::size_t i = 0; i < hexLen && i < 8; ++i) {
        const int digit = hexVal(str[i + 1]);
        if (digit < 0) return 0xFFFFFFFF;
        value = (value << 4) | static_cast<unsigned long>(digit);
    }
    if (hexLen == 6) {
        value = (value << 8) | 0xFFu;  // 补 alpha
    } else if (hexLen == 3) {
        // #RGB 简写扩展
        const unsigned int r = (value >> 8) & 0xF;
        const unsigned int g = (value >> 4) & 0xF;
        const unsigned int b = value & 0xF;
        value = ((r << 4 | r) << 24) | ((g << 4 | g) << 16) | ((b << 4 | b) << 8) | 0xFFu;
    }
    return static_cast<std::uint32_t>(value);
}

/// 从工程收集当前时间戳的可见字幕（含样式）。
std::vector<SubtitleRenderItem> subtitlesAt(const Project& project, SequenceId sequenceId,
                                            const Time& t) {
    std::vector<SubtitleRenderItem> items;
    const Sequence* seq = project.findSequence(sequenceId);
    if (seq == nullptr) {
        return items;
    }
    for (const auto* cue : seq->subtitles.cuesAt(t)) {
        if (!cue->enabled) {
            continue;
        }
        SubtitleRenderItem item;
        item.text = cue->text;
        item.anchorX = cue->style.anchorX;
        item.anchorY = cue->style.anchorY;
        item.alignment = cue->style.alignment;
        item.fontSize = cue->style.fontSize;
        item.scale = 1.0;
        // 镜头追踪（文档 Part 4-A 3.3）：按时间插值覆盖锚点；
        // trackingMode = 1（位置+缩放）时同时覆盖缩放。
        // 说明：此前追踪数据只存不消费（UI 拖拽锚点后文字并不跟随），
        //       此处接入渲染路径使其真实生效。
        if (cue->hasTracking()) {
            const auto& points = cue->tracking;
            const Time now = t;
            // 找到包含 now 的区间并线性插值（区间外取端点）
            SubtitleCue::TrackingPoint sampled = points.front();
            if (!(now <= points.front().time)) {
                sampled = points.back();
                for (std::size_t i = 0; i + 1 < points.size(); ++i) {
                    if (points[i].time <= now && now <= points[i + 1].time) {
                        const double span = (points[i + 1].time - points[i].time).toDouble();
                        const double u = span > 1e-9
                                             ? (now - points[i].time).toDouble() / span
                                             : 0.0;
                        sampled.time = now;
                        sampled.centerX =
                            points[i].centerX + (points[i + 1].centerX - points[i].centerX) * u;
                        sampled.centerY =
                            points[i].centerY + (points[i + 1].centerY - points[i].centerY) * u;
                        sampled.scale =
                            points[i].scale + (points[i + 1].scale - points[i].scale) * u;
                        break;
                    }
                }
            }
            item.anchorX = sampled.centerX;
            item.anchorY = sampled.centerY;
            if (cue->trackingMode == 1) {
                item.scale = sampled.scale;
            }
        }
        item.fillRgba = parseColorRgba(cue->style.color);
        item.outlineRgba = parseColorRgba(cue->style.outlineColor);
        item.outlineWidth = cue->style.outlineWidth;
        items.push_back(std::move(item));
    }
    return items;
}

}  // namespace

/// 公开包装（测试/平台层预览用；与导出路径同一实现）。
std::vector<SubtitleRenderItem> subtitlesForTest(const Project& project, SequenceId sequenceId,
                                                 const Time& t) {
    return subtitlesAt(project, sequenceId, t);
}




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

/// 时间轴时刻 → 源素材时间。
/// - 倒放（reversed）：sourceIn + (duration − local) × speed（速度曲线不参与）；
/// - 无速度曲线：线性映射 sourceIn + (t - timelineStart) * speed；
/// - 有速度曲线：数值积分 d(source) = speed(source) * d(t)（固定 64 步，跨平台确定性）。
Time mapSourceTime(const RenderClip& clip, const Time& t) {
    const Time localTime = t - clip.timelineStart;
    // 定格（B3-2）：整段复用同一源时刻的帧。放在最前，语义优先于倒放/曲线。
    if (clip.freeze) {
        return clip.sourceIn;
    }
    if (clip.reversed) {
        const Time total = clip.timelineEnd - clip.timelineStart;
        const Time remain = total - localTime;
        return clip.sourceIn + Time{remain * clip.speed};
    }
    if (clip.speedCurve.empty()) {
        return clip.sourceIn + Time{localTime * clip.speed};
    }
    constexpr int kSteps = 64;
    const double localSec = localTime.toDouble();
    const double step = localSec / static_cast<double>(kSteps);
    double sourceSec = clip.sourceIn.toDouble();
    for (int i = 0; i < kSteps; ++i) {
        const Time cursor = Rational::make(
            static_cast<std::int64_t>(sourceSec * 1000000.0), 1000000);
        const double speed = clip.speedCurve.speedAt(cursor, clip.speed);
        sourceSec += speed * step;
    }
    return Rational::make(static_cast<std::int64_t>(sourceSec * 1000000.0 + 0.5), 1000000);
}

}  // namespace

ExportStats executeExport(
    const ExportPlan& plan, MediaReaderFactory& readerFactory,
    const std::function<void(const OutputVideoFrame&)>& videoSink,
    const std::function<void(const OutputAudioBlock&)>& audioSink,
    const std::function<bool(double)>& progress, const ExportExecutorOptions& options,
    const Project* project) {
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

    // 视频轨列表（合成顺序：索引小的在下层）+ 主音频轨
    std::vector<const RenderTrack*> videoTracks;
    const RenderTrack* audioTrack = nullptr;
    for (const auto& track : plan.tracks) {
        if (track.type == TrackType::Video && !track.hidden) {
            videoTracks.push_back(&track);
        }
        if (audioTrack == nullptr && track.type == TrackType::Audio && !track.muted) {
            audioTrack = &track;
        }
    }
    // 调整图层（B2-6）：按轨道顺序收集，合成完成后再依次应用。
    // 只收「有实际效果」的（effects 或 LUT 非空）——空调整图层不参与运算，
    // 因此在没有调整图层的工程里，下面的循环一次都不会执行，
    // 输出与改动前逐像素一致（这是 B2-6 的验收条件）。
    std::vector<const RenderTrack*> adjustmentLayers;
    for (const auto& track : plan.tracks) {
        if (track.type != TrackType::Adjustment || track.hidden) {
            continue;
        }
        const bool hasEffects = !track.adjustmentEffects.empty();
        const bool hasLut = !track.adjustmentLutPath.empty();
        if (hasEffects || hasLut) {
            adjustmentLayers.push_back(&track);
        }
    }
    // LUT 只加载一次（每帧加载会带来大量重复 IO）
    Lut3D adjustmentLut;
    bool adjustmentLutLoaded = false;
    std::string adjustmentLutError;
    for (const RenderTrack* layer : adjustmentLayers) {
        if (!layer->adjustmentLutPath.empty()) {
            if (!loadCubeLutFile(layer->adjustmentLutPath, adjustmentLut,
                                 adjustmentLutError)) {
                stats.warnings.push_back("调整图层 LUT 加载失败：" + adjustmentLutError);
            } else {
                adjustmentLutLoaded = true;
            }
            break;  // 目前只支持一个 LUT（多调整图层的 LUT 叠加留待后续）
        }
    }

    // 主视频轨（保留变量兼容旧代码路径：转场/解码逻辑使用第一个视频轨）
    const RenderTrack* videoTrack = videoTracks.empty() ? nullptr : videoTracks.front();

    const FrameRate fps = options.overrideFps > 0 ? FrameRate::of(options.overrideFps)
                                                  : plan.frameRate;
    const Time totalDuration = plan.duration;

    // 帧收尾（所有输出帧路径统一走此处，避免遗漏）：水印（会员门控）。
    // 说明：动画不透明度产生的 alpha 有意保留（供平台层/上层合成语义使用）；
    // 蒙版外的透明像素在「蒙版应用点」就地与序列背景合成（见下方蒙版分支），
    // 从而不影响动画透明度的既有行为。
    bool invisibleCapacityWarned = false;
    auto finalizeFrame = [&](OutputVideoFrame& frame) {
        if (frame.rgba.empty() || frame.width <= 0 || frame.height <= 0) {
            return;
        }
        // 调整图层（B2-6）：作用于「下方所有轨道合成后的画面」。
        // 放在水印之前：调整图层是画面调色/特效，水印属于最终叠加物，
        // 不应被调整图层的调色改变（否则水印颜色会随 LUT 变化）。
        // 注意：这里处理的是**已合成好的整帧**，因此不需要知道图层数量，
        // 也不会影响既有的逐层混合逻辑（无调整图层时本循环不执行）。
        // 帧索引由时间戳换算（finalizeFrame 定义在 frameIndex 声明之前，
        // 直接引用会编译失败）。时间戳是整数微秒，换算稳定且可复现。
        const std::int64_t adjFrameIndex =
            fps.value.isPositive()
                ? static_cast<std::int64_t>(frame.timestamp.toDouble() *
                                            fps.value.toDouble() + 0.5)
                : 0;
        for (const RenderTrack* layer : adjustmentLayers) {
            if (!layer->adjustmentEffects.empty()) {
                applyEffectStackToFrame(frame.rgba.data(), frame.width, frame.height,
                                       layer->adjustmentEffects, adjFrameIndex,
                                       RenderQuality::exportQuality(),
                                       frame.timestamp.toDouble());
            }
            if (adjustmentLutLoaded && !layer->adjustmentLutPath.empty()) {
                applyLutToRgba(frame.rgba.data(), frame.width, frame.height, adjustmentLut,
                               layer->adjustmentLutStrength);
            }
        }
        if (options.watermark.enabled) {
            applyWatermark(frame.rgba.data(), frame.width, frame.height, options.watermark,
                           &frame.subtitles);
        }
        // 隐形水印（Pro 内容泄露溯源；文档 8.3.1）：容量不足时记一次警告并跳过
        if (options.invisibleWatermark.usable()) {
            if (invisibleWatermarkFits(frame.width, frame.height,
                                       options.invisibleWatermark.config)) {
                embedInvisibleWatermark(frame.rgba.data(), frame.width, frame.height,
                                        options.invisibleWatermark.payload,
                                        options.invisibleWatermark.config);
            } else if (!invisibleCapacityWarned) {
                invisibleCapacityWarned = true;
                stats.warnings.push_back(
                    "隐形水印容量不足（输出 " + std::to_string(frame.width) + "x" +
                    std::to_string(frame.height) + "），本工程未嵌入溯源水印");
            }
        }
    };

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

            // 收集当前时刻所有活动层（从顶层到底层）
            // 合成策略：
            //  - 仅一层：快路径（直接输出）；
            //  - 多层：从底层往上做 alpha 混合（上层 opacity < 1 时透出下层）；
            //    上层 opacity == 1 时遮挡下层（仍逐层解码，性能后续优化）。
            struct ActiveLayer {
                const RenderClip* clip;
            };
            std::vector<ActiveLayer> layers;
            for (auto it = videoTracks.rbegin(); it != videoTracks.rend(); ++it) {
                const RenderClip* candidate = clipAt(**it, t);
                if (candidate != nullptr) {
                    layers.push_back(ActiveLayer{candidate});
                }
            }
            const RenderClip* active = layers.empty() ? nullptr : layers.front().clip;
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
                        const Time srcTime =
                            rc->reversed
                                ? rc->sourceIn +
                                      Time{(rc->timelineEnd - rc->timelineStart - local) * rc->speed}
                                : rc->sourceIn + Time{local * rc->speed};
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
                    // 源帧按画幅适配渲染到画布尺寸（信源缺失时用背景色）
                    auto toCanvas = [&](const VideoFrame& source, bool present) {
                        std::vector<std::uint8_t> buffer(pixelBytes, 0);
                        const std::uint32_t bg = plan.backgroundRgba;
                        for (std::size_t i = 0; i + 3 < buffer.size(); i += 4) {
                            buffer[i] = static_cast<std::uint8_t>((bg >> 24) & 0xFF);
                            buffer[i + 1] = static_cast<std::uint8_t>((bg >> 16) & 0xFF);
                            buffer[i + 2] = static_cast<std::uint8_t>((bg >> 8) & 0xFF);
                            buffer[i + 3] = static_cast<std::uint8_t>(bg & 0xFF);
                        }
                        if (present && !source.pixels.empty() && source.width > 0 &&
                            source.height > 0) {
                            const int stride =
                                source.stride > 0 ? source.stride : source.width * 4;
                            if (source.width == w && source.height == h) {
                                std::memcpy(buffer.data(), source.pixels.data(),
                                            std::min(pixelBytes, source.pixels.size()));
                            } else {
                                renderFrameFit(source.pixels.data(), source.width,
                                               source.height, stride, buffer.data(), w, h,
                                               plan.frameFit, bg);
                            }
                        }
                        return buffer;
                    };
                    std::vector<std::uint8_t> bufA = toCanvas(frameA, hasA);
                    std::vector<std::uint8_t> bufB = toCanvas(frameB, hasB);
                    // 确保 alpha 不透明
                    for (std::size_t i = 3; i < bufA.size(); i += 4) bufA[i] = 255;
                    for (std::size_t i = 3; i < bufB.size(); i += 4) bufB[i] = 255;
                    // 先应用滤镜（文档 Part 4-B 第 7 节：整体色彩风格），再应用特效栈。
                    // 顺序说明：滤镜改变色调基底，特效在其上叠加视觉元素。
                    if (fromClip->filterStyle != FilterStyle::None) {
                        applyFilterToRgba(bufA.data(), w, h, fromClip->filterStyle,
                                          fromClip->filterIntensity);
                    }
                    if (toClip->filterStyle != FilterStyle::None) {
                        applyFilterToRgba(bufB.data(), w, h, toClip->filterStyle,
                                          toClip->filterIntensity);
                    }
                    // 对两路源帧分别应用各自片段的特效栈（颜色 + 空间/画面/氛围/动感）：
                    // 修复——此前转场帧完全不吃片段特效（与单层路径不一致）。
                    if (!fromClip->effects.empty()) {
                        applyEffectStackToFrame(bufA.data(), w, h, fromClip->effects,
                                                frameIndex, RenderQuality::exportQuality(),
                                                t.toDouble());
                    }
                    if (!toClip->effects.empty()) {
                        applyEffectStackToFrame(bufB.data(), w, h, toClip->effects,
                                                frameIndex, RenderQuality::exportQuality(),
                                                t.toDouble());
                    }
                    frame.rgba.assign(pixelBytes, 0);
                    blendDone = TransitionRenderer::renderFrame(
                        static_cast<TransitionType>(frame.transitionType), bufA.data(),
                        bufB.data(), frame.rgba.data(), w, h, frame.transitionProgress);
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
                if (project != nullptr) {
                    frame.subtitles = subtitlesAt(*project, plan.sequenceId, t);
                }
                if (options.attachAiLabel) {
                    AiLabelConfig labelConfig;
                    labelConfig.text = options.aiLabelText;
                    frame.subtitles.push_back(buildAiLabelItem(labelConfig));
                }
                finalizeFrame(frame);
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
                if (project != nullptr) {
                    frame.subtitles = subtitlesAt(*project, plan.sequenceId, t);
                }
                if (options.attachAiLabel) {
                    AiLabelConfig labelConfig;
                    labelConfig.text = options.aiLabelText;
                    frame.subtitles.push_back(buildAiLabelItem(labelConfig));
                }
                finalizeFrame(frame);
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
            // 映射时间轴时间 → 源素材时间（速度曲线感知）
            const Time sourceTime = mapSourceTime(*active, t);
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
                        // 输出帧尺寸以计划为准；源尺寸不同时按画幅适配策略处理
                        // （Fit 信箱 / Fill 铺满裁切 / Crop 居中精确裁切）。
                        OutputVideoFrame frame;
                        frame.timestamp = t;
                        frame.width = plan.width;
                        frame.height = plan.height;
                        const std::size_t outBytes = static_cast<std::size_t>(plan.width) *
                                                     static_cast<std::size_t>(plan.height) * 4;
                        frame.rgba.assign(outBytes, 0);
                        const int srcStride = source.stride > 0 ? source.stride : source.width * 4;
                        if (plan.reframe.valid()) {
                            // 智能重构图：按主体跟踪轨迹裁剪 + 缩放到画布
                            // （「横转竖/竖转横」的真实渲染路径）
                            reframeRenderFrameAt(plan.reframe, t, source.pixels.data(),
                                                 source.width, source.height, srcStride,
                                                 frame.rgba.data(), plan.width, plan.height);
                        } else if (source.width == plan.width && source.height == plan.height) {
                            // 尺寸一致：直接拷贝（零缩放损失快路径）
                            const int rows = std::min(source.height, plan.height);
                            for (int row = 0; row < rows; ++row) {
                                const std::uint8_t* src = source.pixels.data() +
                                    static_cast<std::size_t>(row) *
                                        static_cast<std::size_t>(srcStride);
                                std::uint8_t* dst = frame.rgba.data() +
                                    static_cast<std::size_t>(row) *
                                        static_cast<std::size_t>(plan.width) * 4;
                                std::memcpy(dst, src,
                                            static_cast<std::size_t>(plan.width) * 4);
                            }
                        } else {
                            renderFrameFit(source.pixels.data(), source.width, source.height,
                                           srcStride, frame.rgba.data(), plan.width, plan.height,
                                           plan.frameFit, plan.backgroundRgba);
                        }
                        // 应用滤镜（文档 Part 4-B 第 7 节：整体色彩风格；先于特效栈）
                        if (active->filterStyle != FilterStyle::None) {
                            applyFilterToRgba(frame.rgba.data(), plan.width, plan.height,
                                              active->filterStyle, active->filterIntensity);
                        }
                        // 背景填充（文档 Part 4-C 第 11 节）：留白区域填色/模糊/纹理。
                        // 说明：素材区域由画幅适配的几何决定（Fit 模式有留白）；
                        //       此处按 plan.frameFit 计算留白蒙版后填充。
                        if (active->backgroundMode != BackgroundMode::None) {
                            std::vector<std::uint8_t> fitMask(
                                static_cast<std::size_t>(plan.width) *
                                    static_cast<std::size_t>(plan.height),
                                255);
                            // 若为 Fit 模式：用 masked 渲染重建同一几何的留白蒙版
                            if (plan.frameFit == FrameFit::Fit && source.width > 0 &&
                                source.height > 0) {
                                std::vector<std::uint8_t> tmp(
                                    static_cast<std::size_t>(plan.width) *
                                        static_cast<std::size_t>(plan.height) * 4,
                                    0);
                                const int srcStride2 =
                                    source.stride > 0 ? source.stride : source.width * 4;
                                renderFrameFitMasked(
                                    source.pixels.data(), source.width, source.height,
                                    srcStride2, tmp.data(), plan.width, plan.height,
                                    plan.frameFit, plan.backgroundRgba, fitMask.data());
                            }
                            applyBackgroundFill(
                                frame.rgba.data(), frame.rgba.data(), fitMask.data(),
                                plan.width, plan.height, active->backgroundMode,
                                active->backgroundColorRgba, active->backgroundIntensity,
                                RenderQuality::exportQuality());
                        }
                        // 应用片段摇晃（B1-5 / B2-3）。
                        // 放在特效栈之前：摇晃改变的是几何位置，随后调色/特效
                        // 作用在已位移的画面上，与「先抖动再调色」的直觉一致。
                        if (active->shake.amplitude > 0.0) {
                            applyShakeToRgba(frame.rgba.data(), plan.width, plan.height,
                                             active->shake, t.toDouble());
                        }
                        // 应用片段特效栈（颜色变换 + 空间/画面/氛围/动感特效）
                        if (!active->effects.empty()) {
                            applyEffectStackToFrame(frame.rgba.data(), plan.width,
                                                    plan.height, active->effects,
                                                    frameIndex,
                                                    RenderQuality::exportQuality(),
                                                    t.toDouble());
                        }
                        // 应用片段蒙版（文档「✂️剪辑 → 蒙版」）：
                        // 多层场景 → Transparent（蒙版外 alpha=0，供下方逐像素混合透出下层）；
                        // 单层场景 → Background（直接合成序列背景色，导出像素有效）。
                        if (active->hasMask && active->mask.shape != MaskShape::None) {
                            const bool hasLowerLayer = layers.size() > 1;
                            applyMaskToFrame(
                                frame.rgba.data(), plan.width, plan.height, active->mask,
                                hasLowerLayer ? MaskBlendMode::Transparent
                                              : MaskBlendMode::Background,
                                plan.backgroundRgba);
                        }
                        // 应用色度抠像（文档「🤖AI实验室 → 智能抠像 → 色度抠像(绿幕)」）：
                        // 与蒙版同构——多层场景 Transparent（alpha 缩放，供下层混合）；
                        // 单层场景 Background（合成序列背景色，不残留键控色）。
                        if (active->hasChromaKey) {
                            const bool hasLowerLayer = layers.size() > 1;
                            applyChromaKeyToFrame(
                                frame.rgba.data(), plan.width, plan.height, active->chromaKey,
                                hasLowerLayer ? ChromaKeyBlendMode::Transparent
                                              : ChromaKeyBlendMode::Background,
                                plan.backgroundRgba);
                        }
                        // 多层合成：当前帧是顶层；若存在下层且顶层透明度 < 1，
                        // 则解码下层并将两层按 alpha 混合。（多层 > 2 递归处理为后续优化）
                        if (layers.size() > 1) {
                            // 顶层透明度（无动画视为 1）
                            double topAlpha = 1.0;
                            if (active->hasAnimation) {
                                topAlpha = active->animation.sample(ClipProperty::Opacity,
                                                                   t - active->timelineStart);
                                if (topAlpha < 0.0) topAlpha = 0.0;
                                if (topAlpha > 1.0) topAlpha = 1.0;
                            }
                            // 注意：除动画不透明度外，蒙版与色度抠像也会产生逐像素
                            // alpha；画中画混合模式（非 Normal）也必须进入混合路径
                            // （否则不透明的上层直接遮挡下层、混合公式不生效——
                            // 真实缺陷，端到端验证发现）。
                            const bool topHasMask =
                                active->hasMask && active->mask.shape != MaskShape::None;
                            const bool topHasChromaKey = active->hasChromaKey;
                            const bool topHasBlendFormula =
                                active->blendMode != BlendMode::Normal;
                            if (topAlpha < 0.999 || topHasMask || topHasChromaKey ||
                                topHasBlendFormula) {
                                const RenderClip* lower = layers[1].clip;
                                // 解码下层
                                MediaReader* lowerReader = getReader(lower->asset);
                                bool lowerDecoded = false;
                                if (lowerReader != nullptr) {
                                    const auto lowerOpenedIt = readers.find(lower->asset.value);
                                    if (lowerOpenedIt != readers.end() &&
                                        !lowerOpenedIt->second.opened) {
                                        if (lowerReader->open(lower->assetPath).isOk()) {
                                            lowerOpenedIt->second.opened = true;
                                        }
                                    }
                                    if (lowerReader->isOpen()) {
                                        const Time lowerSource = mapSourceTime(*lower, t);
                                        if (lowerReader->seek(lowerSource).isOk()) {
                                            VideoFrame lowerFrame;
                                            if (lowerReader->readVideoFrame(lowerFrame)) {
                                                lowerDecoded = true;
                                                const int lowerStride =
                                                    lowerFrame.stride > 0
                                                        ? lowerFrame.stride
                                                        : lowerFrame.width * 4;
                                                // 下层按画幅适配渲染到画布尺寸后再混合
                                                std::vector<std::uint8_t> lowerCanvas;
                                                const std::uint8_t* lowerPixels =
                                                    lowerFrame.pixels.data();
                                                if (lowerFrame.width != plan.width ||
                                                    lowerFrame.height != plan.height) {
                                                    lowerCanvas.assign(
                                                        static_cast<std::size_t>(plan.width) *
                                                            static_cast<std::size_t>(plan.height) *
                                                            4,
                                                        0);
                                                    renderFrameFit(
                                                        lowerFrame.pixels.data(),
                                                        lowerFrame.width, lowerFrame.height,
                                                        lowerStride, lowerCanvas.data(),
                                                        plan.width, plan.height, plan.frameFit,
                                                        plan.backgroundRgba);
                                                    lowerPixels = lowerCanvas.data();
                                                }
                                                // 下层帧同样吃自己的滤镜与特效栈
                                                // （缺陷修复：此前只处理上层，下层按原始
                                                //  像素参与混合——与转场帧同类不一致）。
                                                // 说明：仅在需要可变缓冲时拷贝（无滤镜/特效
                                                //       且已在上层画布路径时零拷贝）。
                                                if ((lower->filterStyle != FilterStyle::None ||
                                                     !lower->effects.empty())) {
                                                    if (lowerPixels != lowerCanvas.data()) {
                                                        lowerCanvas.assign(
                                                            lowerPixels,
                                                            lowerPixels +
                                                                static_cast<std::size_t>(plan.width) *
                                                                    static_cast<std::size_t>(
                                                                        plan.height) *
                                                                    4);
                                                        lowerPixels = lowerCanvas.data();
                                                    }
                                                    {
                                                        std::uint8_t* writable =
                                                            const_cast<std::uint8_t*>(lowerPixels);
                                                        if (lower->filterStyle !=
                                                            FilterStyle::None) {
                                                            applyFilterToRgba(
                                                                writable, plan.width, plan.height,
                                                                lower->filterStyle,
                                                                lower->filterIntensity);
                                                        }
                                                        if (!lower->effects.empty()) {
                                                            applyEffectStackToFrame(
                                                                writable, plan.width, plan.height,
                                                                lower->effects, frameIndex,
                                                                RenderQuality::exportQuality(),
                                                                t.toDouble());
                                                        }
                                                    }
                                                }
                                                const int lowerCanvasStride = plan.width * 4;
                                                const int w = plan.width;
                                                const int h = plan.height;
                                                // 画中画混合模式（文档 5.9）：
                                                //  - Normal：逐像素 alpha 混合（既有快路径，
                                                //    权重 = 动画不透明度 × 蒙版覆盖率）；
                                                //  - Screen/Overlay/Darken/HardLight：
                                                //    以「下层为基、上层按模式公式合成」实现。
                                                //    流程：保存上层副本（含蒙版 alpha）→
                                                //    帧缓冲填入下层 → blendFrameRgba 把上层
                                                //    按模式与 alpha 权重混合到下层上。
                                                if (active->blendMode != BlendMode::Normal) {
                                                    // 1) 保存上层（帧缓冲当前内容；alpha 已含
                                                    //    蒙版覆盖率）
                                                    std::vector<std::uint8_t> topLayer(
                                                        frame.rgba.data(),
                                                        frame.rgba.data() +
                                                            static_cast<std::size_t>(w) * h * 4);
                                                    // 2) 帧缓冲填入下层像素
                                                    for (int row = 0; row < h; ++row) {
                                                        const std::uint8_t* src =
                                                            lowerPixels +
                                                            static_cast<std::size_t>(row) *
                                                                static_cast<std::size_t>(
                                                                    lowerCanvasStride);
                                                        std::uint8_t* dst =
                                                            frame.rgba.data() +
                                                            static_cast<std::size_t>(row) *
                                                                static_cast<std::size_t>(w) * 4;
                                                        std::memcpy(dst, src,
                                                                    static_cast<std::size_t>(w) * 4);
                                                    }
                                                    // 3) 按模式公式 + alpha 权重合成
                                                    //    （opacity = 动画不透明度；alpha 通道
                                                    //     已含蒙版覆盖率）
                                                    blendFrameRgba(frame.rgba.data(),
                                                                   topLayer.data(), w, h,
                                                                   active->blendMode, topAlpha);
                                                } else {
                                                    for (int row = 0; row < h; ++row) {
                                                        const std::uint8_t* src =
                                                            lowerPixels +
                                                            static_cast<std::size_t>(row) *
                                                                static_cast<std::size_t>(
                                                                    lowerCanvasStride);
                                                        std::uint8_t* dst =
                                                            frame.rgba.data() +
                                                            static_cast<std::size_t>(row) *
                                                                static_cast<std::size_t>(w) * 4;
                                                        for (int col = 0; col < w; ++col) {
                                                            const std::size_t base =
                                                                static_cast<std::size_t>(col) * 4;
                                                            // 混合权重 = 动画不透明度 × 蒙版覆盖率：
                                                            //  - 蒙版已在此之前把 alpha 缩放为
                                                            //    255×覆盖率（Transparent 模式）；
                                                            //  - 动画不透明度（topAlpha）在混合之后
                                                            //    才应用到整帧（既有语义，保持不变）。
                                                            const double maskCoverage =
                                                                static_cast<double>(dst[base + 3]) /
                                                                255.0;
                                                            const double mixWeight =
                                                                topAlpha * maskCoverage;
                                                            const double bottomWeight =
                                                                1.0 - mixWeight;
                                                            for (int ch = 0; ch < 3; ++ch) {
                                                                const std::size_t idx = base +
                                                                    static_cast<std::size_t>(ch);
                                                                const double top = dst[idx] * mixWeight;
                                                                const double bot =
                                                                    src[idx] * bottomWeight;
                                                                dst[idx] = static_cast<std::uint8_t>(
                                                                    top + bot + 0.5);
                                                            }
                                                            // 已与下层完成合成 → 结果不透明
                                                            dst[base + 3] = 255;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                (void)lowerDecoded;
                            }
                        }
                        // 应用动画透明度（alpha 缩放）
                        if (active->hasAnimation) {
                            const Time localTime = t - active->timelineStart;
                            const double opacity =
                                active->animation.sample(ClipProperty::Opacity, localTime);
                            if (opacity < 0.999) {
                                const std::size_t pixelCount =
                                    static_cast<std::size_t>(plan.width) *
                                    static_cast<std::size_t>(plan.height);
                                const double clamped = opacity < 0.0 ? 0.0 : opacity;
                                for (std::size_t i = 0; i < pixelCount; ++i) {
                                    const std::size_t alphaIndex = i * 4 + 3;
                                    const double a = frame.rgba[alphaIndex] * clamped;
                                    frame.rgba[alphaIndex] = static_cast<std::uint8_t>(a + 0.5);
                                }
                            }
                        }
                        if (activeTransition != nullptr) {
                            frame.transitionType = static_cast<int>(activeTransition->type);
                            frame.transitionProgress = TransitionRenderer::transitionProgress(
                                t, activeTransition->start,
                                activeTransition->end - activeTransition->start);
                        }
                        if (project != nullptr) {
                            frame.subtitles = subtitlesAt(*project, plan.sequenceId, t);
                        }
                        if (options.attachAiLabel) {
                            AiLabelConfig labelConfig;
                            labelConfig.text = options.aiLabelText;
                            frame.subtitles.push_back(buildAiLabelItem(labelConfig));
                        }
                        finalizeFrame(frame);
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
                if (project != nullptr) {
                    frame.subtitles = subtitlesAt(*project, plan.sequenceId, t);
                }
                if (options.attachAiLabel) {
                    AiLabelConfig labelConfig;
                    labelConfig.text = options.aiLabelText;
                    frame.subtitles.push_back(buildAiLabelItem(labelConfig));
                }
                finalizeFrame(frame);
                videoSink(frame);
                ++stats.framesWritten;
            }
            ++frameIndex;
        }
    }

    // ---- 音频遍历：真实混音（AudioMixer 采样精确 + 解码 + 增益/淡入淡出）----
    if (options.outputAudio && audioSink) {
        MixFormat mixFormat;
        mixFormat.sampleRate = 48000;
        mixFormat.channels = 2;
        AudioMixer mixer(mixFormat, readerFactory);
        const bool mixerReady = project != nullptr && mixer.prepare(plan, *project) &&
                                mixer.duration().isPositive();
        if (mixerReady) {
            OutputAudioBlock block;
            while (mixer.readBlock(block)) {
                audioSink(block);
                ++stats.audioBlocksWritten;
                if (progress != nullptr && mixer.duration().isPositive()) {
                    const double p = block.start.toDouble() / mixer.duration().toDouble();
                    if (!progress(std::min(1.0, std::max(0.0, p)))) {
                        stats.cancelled = true;
                        return stats;
                    }
                }
            }
        } else if (audioTrack != nullptr) {
            // 混音器无可用输入（如全部静音/无音频素材）：输出静音流保持时长
            const int sampleRate = mixFormat.sampleRate;
            const int channels = mixFormat.channels;
            const int blockFrames = 1024;
            Time cursor;
            for (const auto& clip : audioTrack->clips) {
                if (clip.timelineStart > cursor) {
                    cursor = clip.timelineStart;  // 对齐（间隙静音的简化处理）
                }
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
    }

    if (progress != nullptr) {
        progress(1.0);
    }
    return stats;
}

}  // namespace core
}  // namespace nairoclip
