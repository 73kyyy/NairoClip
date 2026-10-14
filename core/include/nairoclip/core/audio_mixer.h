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

    /// 查询某条轨道的自动闪避压低区间（供时间轴绘制「自动压低区间」、
    /// 混音台高亮联动、导出前预览）。未启用或无区间时返回空。
    std::vector<DuckRegion> duckRegions(TrackId trackId) const;

    /// 混音台：查询某条轨道当前是否参与混音（静音/独奏过滤后的结果）。
    /// 供 UI 高亮显示（文档「混音台高亮联动」）。
    bool trackAudible(TrackId trackId) const;

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
        /// 所属轨道（自动闪避联动查询用）。
        TrackId trackId;
        /// 该片段所属轨道是否启用自动闪避（启用时应用压低包络）。
        bool ducked = false;
        /// 轨道级增益（dB → 线性；混音台推子/数值输入）。
        double trackGain = 1.0;
        /// 片段降噪强度（文档「🎵音频 → 降噪 → 开关/强度(弱中强)」）。
        DenoiseStrength denoise = DenoiseStrength::Off;
    };

    /// 降噪流状态（每片段一份；保持跨块连续处理）。
    /// 设计：导出混音逐块驱动且每块可能重新定位，因此以「源帧号」为唯一
    /// 锚点维护一条**连续**降噪流：
    ///   · 输入前沿 = baseToken + fedCount（输入连续推进，不跳帧）；
    ///   · buf 为已产出输出（交错，srcChannels），覆盖
    ///     [bufFirst, bufFirst + 帧数)；请求区间直接从 buf 切片交付，
    ///     因此块间的舍入差（±1 帧）不会造成时轴漂移；
    ///   · 请求位置早于保留历史或前跳过大时整体重置（等价新片段开始，
    ///     重置处的瞬态由半窗前视与噪声估计快速跟随保护）；
    ///   · 专用读取器（denoiseReaders_）连续顺序读取，与共享读取器的
    ///     逐块 seek 互不干扰。
    struct DenoiseState {
        bool valid = false;            ///< 是否已初始化
        DenoiseStrength strength = DenoiseStrength::Off;  ///< 当前强度（变更时重建）
        AudioDenoiser denoiser{DenoiseStrength::Off, 1};
        int srcRate = 0;               ///< 源采样率
        int srcChannels = 0;           ///< 源声道数
        long long baseToken = 0;       ///< 输出流起点（源帧号；= 首次请求位置）
        long long fedCount = 0;        ///< 累计喂入帧数（输入前沿 = baseToken + fedCount）
        long long bufFirst = 0;        ///< buf[0] 的源帧号
        std::vector<float> buf;        ///< 已产出输出（交错，srcChannels）
        bool sourceExhausted = false;  ///< 源已耗尽（此后以零填充喂入）
        bool flushed = false;          ///< 已发送终块（此后仅消费 buf）
    };

    /// 混音一块到缓冲区（样本级）。
    void mixInto(std::vector<float>& buffer, const Time& blockStart);

    /// 降噪片段取源样本：填充 out（源采样率/声道；start = 源时间）。
    /// - 使用专用读取器（每片段一个，连续顺序读取，避免与共享读取器互相 seek 干扰）；
    /// - 语义与原始路径「seek + readAudioBlock」一致：返回 false 表示本块该
    ///   片段无可用源（调用方静音跳过）；源不足时返回可用部分（调用方补齐静音）。
    bool serveDenoisedBlock(const TrackedClip& clip, const Time& sourceTime,
                            std::int64_t outCount, AudioBlock& out);

    /// 构建自动闪避计划：分析触发轨（其他非静音音频轨）的语音活动，
    /// 为启用自动闪避的被压低轨生成压低区间。
    void buildDuckPlans(const ExportPlan& plan, const Project& project);

    MixFormat format_;
    MediaReaderFactory& readerFactory_;
    std::vector<TrackedClip> clips_;
    std::map<std::uint64_t, std::unique_ptr<MediaReader>> readers_;
    Time duration_;
    Time cursor_;          // 当前输出时间
    std::int64_t blockFrames_ = 1024;
    /// 主输出增益（线性；混音台「主输出」推子）。
    double masterGain_ = 1.0;
    /// 是否存在独奏轨（存在时仅独奏轨参与混音）。
    bool hasSolo_ = false;
    /// 自动闪避区间（按被压低轨分组；仅启用自动闪避的轨道有条目）。
    struct DuckPlan {
        TrackId trackId;
        std::vector<DuckRegion> regions;
    };
    std::vector<DuckPlan> duckPlans_;
    /// 降噪流状态（每片段；仅在启用降噪的片段上使用）。
    std::map<ClipId, DenoiseState> denoiseStates_;
    /// 降噪专用读取器（每片段一个；连续顺序读取，避免与共享读取器互相 seek 干扰）。
    std::map<ClipId, std::unique_ptr<MediaReader>> denoiseReaders_;
};

}  // namespace core
}  // namespace nairoclip
