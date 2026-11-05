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
    /// 目标视频编码格式（"h264" / "av1" / "vp9"；HEVC 按协议清单禁用）。
    ///
    /// **默认 h264，但含义是「h264 硬件编码」** —— 发布构建里软件 H.264
    /// 已被完全移除（AVC 专利费，见 docs/COMPLIANCE.md §1.2.1），
    /// 所以实际走的是 MediaCodec / VideoToolbox / NVENC 等系统硬件编码器。
    ///
    /// 为什么默认仍是 h264：兼容性。用户导出的视频要发给别人、传平台，
    /// 而 H.264 是唯一「到处都能放」的格式（iOS 全版本、旧安卓、
    /// 微信/抖音后台转码都接受）。VP9/AV1 体积更小但兼容性差得多，
    /// 应作为**可选格式**由用户显式选择，不能做默认。
    ///
    /// 设备无 H.264 硬件编码器时：writeVideoFrame 前会 fail-closed 报错，
    /// 平台层应捕获并提示用户改用 VP9/AV1（或换设备）。
    /// 不静默降级到软件实现 —— 那会把专利风险带回产品里。
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
