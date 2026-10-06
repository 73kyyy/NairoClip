#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace nairoclip {
namespace core {

/// 强类型实体 ID（不同实体类型的 ID 不可混用）。
template <typename Tag>
struct Id {
    std::uint64_t value = 0;

    bool isValid() const { return value != 0; }

    bool operator==(const Id& o) const { return value == o.value; }
    bool operator!=(const Id& o) const { return value != o.value; }
    bool operator<(const Id& o) const { return value < o.value; }
};

struct ProjectTag {};
struct SequenceTag {};
struct TrackTag {};
struct ClipTag {};
struct AssetTag {};
struct MarkerTag {};
struct TransitionTag {};
struct SubtitleTag {};
struct EffectTag {};

using ProjectId = Id<ProjectTag>;
using SequenceId = Id<SequenceTag>;
using TrackId = Id<TrackTag>;
using ClipId = Id<ClipTag>;
using AssetId = Id<AssetTag>;
using MarkerId = Id<MarkerTag>;
using TransitionId = Id<TransitionTag>;
using SubtitleId = Id<SubtitleTag>;
using EffectId = Id<EffectTag>;

/// 单调递增 ID 分配器。
class IdAllocator {
public:
    std::uint64_t next() { return ++counter_; }
    std::uint64_t peek() const { return counter_; }
    void restore(std::uint64_t value) { counter_ = value; }

private:
    std::uint64_t counter_ = 0;
};

/// 错误码（核心层不抛异常，统一通过 Result 传递）。
enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    Overlap,     // 与轨道上的已有内容重叠
    OutOfRange,  // 超出素材范围 / 时间范围
    Locked,      // 轨道已锁定
    Unsupported,
    Internal,    // 内部一致性错误（通常不应出现；见 asserts）
    Cancelled,   // 用户取消（导出等长任务）
};

/// 操作结果：错误码 + 可读信息。
struct Result {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    bool isOk() const { return code == ErrorCode::Ok; }
    explicit operator bool() const { return isOk(); }

    static Result ok() { return Result{}; }

    static Result fail(ErrorCode c, std::string msg) {
        Result r;
        r.code = c;
        r.message = std::move(msg);
        return r;
    }
};

}  // namespace core
}  // namespace nairoclip
