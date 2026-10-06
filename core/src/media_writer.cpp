#include "nairoclip/core/media_writer.h"

namespace nairoclip {
namespace core {

Result MockMediaWriter::open(const std::string& path, const WriterConfig& config) {
    if (path.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "路径为空");
    }
    if (config.width <= 0 || config.height <= 0) {
        return Result::fail(ErrorCode::InvalidArgument, "输出尺寸非法");
    }
    open_ = true;
    finished_ = false;
    aborted_ = false;
    videoFrames_ = 0;
    audioBlocks_ = 0;
    hasTimestamp_ = false;
    lastPath_ = path;
    config_ = config;
    return Result::ok();
}

Result MockMediaWriter::writeVideoFrame(const OutputVideoFrame& frame) {
    if (!open_) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (frame.rgba.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "帧数据为空");
    }
    if (!hasTimestamp_) {
        firstTimestamp_ = frame.timestamp;
        hasTimestamp_ = true;
    }
    lastTimestamp_ = frame.timestamp;
    ++videoFrames_;
    return Result::ok();
}

Result MockMediaWriter::writeAudioBlock(const OutputAudioBlock& block) {
    if (!open_) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (block.samples.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "音频块为空");
    }
    ++audioBlocks_;
    return Result::ok();
}

Result MockMediaWriter::finish() {
    if (!open_) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    finished_ = true;
    open_ = false;
    return Result::ok();
}

void MockMediaWriter::abort() {
    aborted_ = true;
    open_ = false;
}

}  // namespace core
}  // namespace nairoclip
