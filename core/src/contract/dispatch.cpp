#include "nairoclip/core/contract/dispatch.h"

#include <string>

namespace nairoclip {
namespace core {
namespace contract {
namespace {

struct SupportedCommand {
    const char* type;
};

const SupportedCommand kSupported[] = {
    {"clip.move"},
    {"clip.split"},
    {"clip.remove"},
};

constexpr std::size_t kSupportedCount = sizeof(kSupported) / sizeof(kSupported[0]);

bool readUint64(const JsonValue& obj, const char* key, std::uint64_t* out) {
    if (!obj.isObject() || !obj.has(key)) return false;
    const JsonValue& v = obj.get(key);
    if (!v.isNumber()) return false;
    const std::int64_t raw = v.asInteger(-1);
    if (raw < 0) return false;
    *out = static_cast<std::uint64_t>(raw);
    return true;
}

/// 解析 "{ticks, timebase:{num,den}}" 为有理数秒。
/// 与 07 §8 的示例一致：ticks × num / den。
bool readTime(const JsonValue& obj, const char* key, Rational* out) {
    if (!obj.isObject() || !obj.has(key)) return false;
    const JsonValue& t = obj.get(key);
    if (!t.isObject()) return false;
    const JsonValue& ticks = t.get("ticks");
    if (!ticks.isNumber()) return false;
    const std::int64_t tk = ticks.asInteger(0);
    if (tk < 0) return false;
    std::int64_t num = 1;
    std::int64_t den = 1;
    if (t.has("timebase")) {
        const JsonValue& tb = t.get("timebase");
        if (!tb.isObject()) return false;
        num = tb.get("num").asInteger(0);
        den = tb.get("den").asInteger(0);
        if (num <= 0 || den <= 0) return false;
    }
    *out = Rational::make(tk * num, den);
    return true;
}

CommandResult rejected(const CommandRequest& request, ErrorDetail error) {
    CommandResult r;
    r.status = CommandStatus::Rejected;
    r.commandId = request.commandId;
    r.error = std::move(error);
    return r;
}

}  // namespace

bool isSupportedCommandType(const std::string& type) {
    for (std::size_t i = 0; i < kSupportedCount; ++i) {
        if (type == kSupported[i].type) return true;
    }
    return false;
}

std::size_t supportedCommandTypeCount() { return kSupportedCount; }

CommandDispatcher::CommandDispatcher(TimelineEditor& editor, EventBus& bus)
    : editor_(editor), bus_(bus) {}

std::uint64_t CommandDispatcher::currentRevision() const { return editor_.editRevision(); }

CommandResult CommandDispatcher::apply(const CommandRequest& request) {
    // 1) 信封校验（不执行业务语义）
    const ErrorDetail envelopeError = validateCommandRequest(request);
    if (!envelopeError.isOk()) return rejected(request, envelopeError);

    // 2) revision 乐观并发。expectedRevision == 0 表示调用方显式放弃校验
    //    （仅用于初始化/只读场景），由信封校验层保证不会误用。
    const std::uint64_t current = currentRevision();
    if (request.expectedRevision != 0) {
        const ErrorDetail revError = checkRevision(request.expectedRevision, current);
        if (!revError.isOk()) return rejected(request, revError);
    }

    // 3) 命令类型 → 真实编辑调用
    const JsonValue& p = request.payload;
    std::uint64_t sequenceId = 0;
    std::uint64_t trackId = 0;
    std::uint64_t clipId = 0;
    if (!readUint64(p, "sequenceId", &sequenceId) || !readUint64(p, "trackId", &trackId) ||
        !readUint64(p, "clipId", &clipId)) {
        return rejected(request, makeError(ErrorDomain::Timeline, "INVALID_ARGUMENT",
                                           "payload 缺少 sequenceId/trackId/clipId",
                                           "payload"));
    }
    const SequenceId seq{sequenceId};
    const TrackId trk{trackId};
    const ClipId clp{clipId};

    Result editResult;
    if (request.commandType == "clip.move") {
        Rational target;
        if (!readTime(p, "targetStart", &target)) {
            return rejected(request, makeError(ErrorDomain::Timeline, "INVALID_ARGUMENT",
                                               "clip.move 需要 targetStart{ticks,timebase}",
                                               "payload.targetStart"));
        }
        editResult = editor_.moveClip(seq, trk, clp, target);
    } else if (request.commandType == "clip.split") {
        Rational at;
        if (!readTime(p, "at", &at)) {
            return rejected(request, makeError(ErrorDomain::Timeline, "INVALID_ARGUMENT",
                                               "clip.split 需要 at{ticks,timebase}",
                                               "payload.at"));
        }
        editResult = editor_.splitClip(seq, trk, clp, at);
    } else if (request.commandType == "clip.remove") {
        editResult = editor_.removeClip(seq, trk, clp);
    } else {
        // 明确拒绝未实现类型：不做"假装成功"，也不做模糊匹配（03 §6）。
        return rejected(request, makeError(ErrorDomain::Timeline, "UNSUPPORTED_COMMAND",
                                           "未支持的命令类型：" + request.commandType,
                                           "commandType"));
    }

    if (!editResult.isOk()) {
        return rejected(request, mapCoreResult(editResult, ErrorDomain::Timeline));
    }

    // 4) 成功：封装结果 + 发布事件
    CommandResult result;
    result.status = CommandStatus::Accepted;
    result.commandId = request.commandId;
    result.newRevision = currentRevision();
    result.changeSetId = "cs-" + request.commandId;
    result.undoAvailable = editor_.canUndo();

    EventEnvelope event;
    event.schemaVersion = kSchemaVersion;
    event.eventId = "evt-" + request.commandId;
    event.eventType = eventTypeTimelineChanged();
    event.projectId = request.projectId;
    event.revision = result.newRevision;
    event.occurredAtMs = 0;  // 由平台层补真实时间戳（核心层不依赖系统时钟）
    event.correlationId = request.commandId;
    JsonValue payload = JsonValue::object();
    payload.set("commandId", JsonValue::string(request.commandId));
    payload.set("commandType", JsonValue::string(request.commandType));
    payload.set("origin", JsonValue::string(commandOriginName(request.origin)));
    event.payload = payload;

    lastEvent_ = event;
    bus_.publish(event);

    return result;
}

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
