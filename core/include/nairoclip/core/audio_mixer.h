#pragma once

#include "nairoclip/core/clip.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace nairoclip {
namespace core {

/// 混音输出格式。
struct MixFormat {
    int sampleRate = 48000;
    int channels = 2;
    bool valid() const { return sampleRate > 0 && channels > 0 && channels <= 8; }
};

/// 音频混音引擎：把多条音频轨片段混为一个立体声/多声道流。
///
/// 功能：
///  - 采样精确时间映射（时间轴时间 → 源时间，含 speed）
///  - 每片段增益（dB → 线性）与淡入淡出包络
///  - 多轨求和 + 总输出限幅（hard clip 保护）
///  - 块式驱动（block-based），适合流式导出
///
/// 线程约束：单实例单线程使用。
class AudioMixer {
public:
    AudioMixer(MixFormat format, MediaReaderFactory& readerFactory);

    /// 准备混音：从导出计划提取所有音频片段。
    /// 返回 false 表示计划无可用音频（调用方应输出静音）。
    bool prepare(const ExportPlan& plan, const Project& project);

    /// 读取下一个混音块（块大小 = framesPerBlock）。
    /// 到达序列终点后返回 false。
    bool readBlock(OutputAudioBlock& out);

    /// 定位到时间（用于 seek 预览）。
    Result seek(const Time& timelineTime);

    /// 总时长。
    Time duration() const { return duration_; }

    /// dB → 线性增益转换（-60dB 及以下 → 0）。
    static double dbToLinear(double db);

private:
    struct TrackedClip {
        ClipId clipId;
        AssetId asset;
        std::string assetPath;
        Time timelineStart;
        Time timelineEnd;
        Time sourceIn;
        Rational speed = Rational::make(1);
        double linearGain = 1.0;
        Time fadeIn;
        Time fadeOut;
    };

    /// 混音一块到缓冲区（样本级）。
    void mixInto(std::vector<float>& buffer, const Time& blockStart);

    MixFormat format_;
    MediaReaderFactory& readerFactory_;
    std::vector<TrackedClip> clips_;
    std::map<std::uint64_t, std::unique_ptr<MediaReader>> readers_;
    Time duration_;
    Time cursor_;          // 当前输出时间
    std::int64_t blockFrames_ = 1024;
};

}  // namespace core
}  // namespace nairoclip
