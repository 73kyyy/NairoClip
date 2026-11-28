#pragma once

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nairoclip {
namespace core {

/// 导出写入器配置。
struct WriterConfig {
    int width = 0;
    int height = 0;
    FrameRate frameRate = fps30();
    /// 目标视频编码格式（"h264" / "hevc" / "av1" / "vp9"）。
    ///
    /// Android 构建只允许系统 MediaCodec wrapper，不捆绑软件视频编码器；
    /// 实际可用格式、profile、分辨率和帧率由设备系统报告的能力决定。
    /// 设备不支持所选配置时必须 fail-closed，不得静默退回软件实现。
    ///
    /// 其他平台按其平台媒体框架及该平台单独审核过的构建策略选择编码器。
    std::string videoCodec = "h264";
    int crf = 23;              // 质量（0-51，越低越好；23 默认）
    std::string preset = "veryfast";  // 编码器 preset
    /// 编码线程数（0 = 编码器自动；>0 为显式上限）。
    int threads = 0;
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitrate = 192000;  // bps
    bool includeAudio = true;
    /// AI 隐式标识元数据键值对（《人工智能生成合成内容标识办法》要求；
    /// 由 ai_marking::buildAiMetadata 构建；非空时写入容器元数据）。
    std::vector<std::pair<std::string, std::string>> aiMetadata;
};

/// 媒体写入器接口：由平台层实现（FFmpeg / MediaCodec）。
/// 使用顺序：open → writeVideoFrame* / writeAudioBlock* → close。
/// 线程约束：单实例单线程使用。
class MediaWriter {
public:
    virtual ~MediaWriter() = default;

    /// 打开输出目标。
    virtual Result open(const std::string& path, const WriterConfig& config) = 0;
    /// 写入一帧视频（像素为 RGBA8；写入器负责转 YUV）。
    virtual Result writeVideoFrame(const OutputVideoFrame& frame) = 0;
    /// 写入一块音频（float 交错）。
    virtual Result writeAudioBlock(const OutputAudioBlock& block) = 0;
    /// 完成写入（冲刷编码器、写文件尾）。
    virtual Result finish() = 0;
    /// 放弃写入（删除半成品）。
    virtual void abort() = 0;
    virtual bool isOpen() const = 0;
};

/// 写入器工厂：平台层注册具体实现。
class MediaWriterFactory {
public:
    virtual ~MediaWriterFactory() = default;
    virtual std::unique_ptr<MediaWriter> create() = 0;
};

/// 内存 Mock 写入器（测试/CI 用）。
/// 记录帧数与音频块数，可校验尺寸/时间戳序列。
class MockMediaWriter : public MediaWriter {
public:
    Result open(const std::string& path, const WriterConfig& config) override;
    Result writeVideoFrame(const OutputVideoFrame& frame) override;
    Result writeAudioBlock(const OutputAudioBlock& block) override;
    Result finish() override;
    void abort() override;
    bool isOpen() const override { return open_; }

    // 测试观察
    int videoFramesWritten() const { return videoFrames_; }
    int audioBlocksWritten() const { return audioBlocks_; }
    bool finished() const { return finished_; }
    bool aborted() const { return aborted_; }
    const std::string& lastPath() const { return lastPath_; }
    const WriterConfig& config() const { return config_; }
    /// 第一帧和最后一帧的时间戳（用于顺序校验）。
    Time firstTimestamp() const { return firstTimestamp_; }
    Time lastTimestamp() const { return lastTimestamp_; }

private:
    bool open_ = false;
    bool finished_ = false;
    bool aborted_ = false;
    int videoFrames_ = 0;
    int audioBlocks_ = 0;
    std::string lastPath_;
    WriterConfig config_;
    Time firstTimestamp_;
    Time lastTimestamp_;
    bool hasTimestamp_ = false;
};

/// Mock 写入器工厂。
class MockWriterFactory : public MediaWriterFactory {
public:
    std::unique_ptr<MediaWriter> create() override {
        return std::make_unique<MockMediaWriter>();
    }
};

}  // namespace core
}  // namespace nairoclip
