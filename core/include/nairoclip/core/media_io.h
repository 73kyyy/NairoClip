#pragma once

#include "nairoclip/core/media.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 媒体探测结果（打开文件后由实现填充）。
struct MediaProbeResult {
    MediaType type = MediaType::Unknown;
    Time duration;
    FrameRate frameRate;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    int channels = 0;
    std::string codec;  // 如 "h264"（展示用）
};

/// 一帧音频采样块（交错格式，16-bit 或 float 由实现约定；
/// 本工程内部约定 float 交错，随后处理环节再转换）。
struct AudioBlock {
    /// 起始时间（源素材时间轴）。
    Time start;
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;  // 交错存储，长度 = frames * channels
    std::size_t frameCount() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
};

/// 渲染输出的一帧（导出器/混音器消费）。
struct OutputVideoFrame {
    Time timestamp;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // 交错 RGBA8
};

/// 音频输出块（多轨混音后）。
struct OutputAudioBlock {
    Time start;
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;  // 交错
};

/// 一帧视频图像（RGB/A 由实现约定；本工程内部约定 RGBA8 交错）。
struct VideoFrame {
    /// 时间戳（源素材时间轴）。
    Time timestamp;
    int width = 0;
    int height = 0;
    int stride = 0;  // 每行字节数
    std::vector<std::uint8_t> pixels;  // RGBA8
};

/// 媒体读取器接口：由平台层实现（FFmpeg / 系统 API）。
/// 线程约束：单个实例不允许多线程并发调用。
class MediaReader {
public:
    virtual ~MediaReader() = default;

    /// 打开媒体文件。
    virtual Result open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    /// 探测结果（open 成功后可用）。
    virtual const MediaProbeResult& probe() const = 0;

    /// 定位到源时间（seek 到最近关键帧或精确帧，实现决定）。
    virtual Result seek(const Time& sourceTime) = 0;

    /// 读取下一帧视频；无更多帧返回 false（out 不修改）。
    virtual bool readVideoFrame(VideoFrame& out) = 0;

    /// 读取下一块音频；无更多返回 false。
    virtual bool readAudioBlock(AudioBlock& out) = 0;
};

/// 媒体读取器工厂：平台层注册具体实现。
class MediaReaderFactory {
public:
    virtual ~MediaReaderFactory() = default;
    virtual std::unique_ptr<MediaReader> create() = 0;
};

/// 内存 Mock 读取器（测试与流水线骨架验证用）。
/// 可配置：视频若干帧（按帧率生成纯色帧）、音频若干块（正弦波）。
class MockMediaReader : public MediaReader {
public:
    /// 配置一次「打开」后可见内容。
    struct Config {
        MediaProbeResult probe;
        int videoFrameCount = 0;   // 从 0 开始逐帧、间隔 1/frameRate
        int audioBlockCount = 0;   // 每块固定 1024 帧
        int audioBlockFrames = 1024;
        std::uint8_t grayValue = 128;  // 视频帧填充灰度
        bool failOpen = false;
    };

    explicit MockMediaReader(Config config);

    Result open(const std::string& path) override;
    void close() override;
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return config_.probe; }
    Result seek(const Time& sourceTime) override;
    bool readVideoFrame(VideoFrame& out) override;
    bool readAudioBlock(AudioBlock& out) override;

    /// 测试辅助：已打开次数与最近一次的路径。
    int openCount() const { return openCount_; }
    const std::string& lastOpenedPath() const { return lastPath_; }

private:
    Config config_;
    bool open_ = false;
    int openCount_ = 0;
    std::string lastPath_;
    int nextVideoFrame_ = 0;
    int nextAudioBlock_ = 0;
    Time seekBase_;
};

}  // namespace core
}  // namespace nairoclip
