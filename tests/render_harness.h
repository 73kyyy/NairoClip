#pragma once
// =============================================================================
// 导出级像素基线工具（供多个测试文件共用）
// =============================================================================
// 从 test_render_baseline.cpp 提取，避免在多个文件里复制 PatternReader。
//
// 为什么要用「图案」而不是纯色：纯色图无法发现「采样位置错了」，
// 因为每处像素都一样。PatternReader 的输出随 (frameIndex, x, y) 变化。
//
// mock 的 seek 语义必须与真实 FFmpeg 一致：seek 后 readVideoFrame 返回
// **目标时间对应的那一帧**。否则导出器每帧都读到第 0 帧，30 帧指纹全同，
// 所有「两次导出一致」的断言都会假通过。

#include "pixel_baseline.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace test {

using namespace nairoclip::core;

class PatternReader : public MediaReader {
public:
    explicit PatternReader(int w, int h) : width_(w), height_(h) {
        probe_.type = MediaType::Video;
        probe_.duration = Rational::make(10);
        probe_.frameRate = fps30();
        probe_.width = w;
        probe_.height = h;
    }
    Result open(const std::string&) override {
        open_ = true;
        frame_ = 0;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    /// 真实的 MediaReader（FFmpeg 实现）在 seek 后，下一次 readVideoFrame 返回
    /// **该时间对应的那一帧**。mock 必须遵循同一语义，否则导出器每次 seek 后
    /// 都读到第 0 帧——所有输出帧内容相同，基线就变成了假通过。
    /// （这个坑在写基线时真踩过：30 帧指纹全相同。）
    Result seek(const Time& t) override {
        // 按帧率换算目标帧号
        frame_ = static_cast<int>(t.toDouble() * 30.0 + 0.5);
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& out) override {
        if (!open_ || frame_ >= 60) {
            return false;
        }
        out.timestamp = Rational::make(frame_);
        out.width = width_;
        out.height = height_;
        out.stride = width_ * 4;
        out.pixels.resize(static_cast<std::size_t>(width_) * height_ * 4);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * width_ + x) * 4;
                out.pixels[i + 0] = static_cast<std::uint8_t>((x * 7 + frame_ * 13) & 0xFF);
                out.pixels[i + 1] = static_cast<std::uint8_t>((y * 11 + frame_ * 3) & 0xFF);
                out.pixels[i + 2] = static_cast<std::uint8_t>((x + y + frame_) & 0xFF);
                out.pixels[i + 3] = 255;
            }
        }
        ++frame_;
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    int width_;
    int height_;
    MediaProbeResult probe_;
    bool open_ = false;
    int frame_ = 0;
};

class PatternFactory : public MediaReaderFactory {
public:
    explicit PatternFactory(int w, int h) : w_(w), h_(h) {}
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<PatternReader>(w_, h_);
    }

private:
    int w_, h_;
};

/// 跑一次导出，收集每一帧的指纹。
/// inline：本函数在头文件中定义，被多个 .cpp 包含，必须避免重复符号。
inline std::vector<FrameDigest> renderDigests(const Project& project, int w, int h) {
    std::vector<FrameDigest> out;
    const Sequence* seq = project.mainSequence();
    if (seq == nullptr) {
        return out;
    }
    const ExportPlan plan = compileExportPlan(project, *seq);
    PatternFactory factory(w, h);
    ExportExecutorOptions opts;
    executeExport(
        plan, factory,
        [&out](const OutputVideoFrame& f) {
            FrameDigest d;
            d.timestampMicros = static_cast<std::int64_t>(f.timestamp.toDouble() * 1000000.0);
            d.width = f.width;
            d.height = f.height;
            d.hash = fnv1a64(f.rgba.data(), f.rgba.size());
            out.push_back(d);
        },
        [](const OutputAudioBlock&) {}, nullptr, opts, &project);
    return out;
}

}  // namespace test
}  // namespace nairoclip
