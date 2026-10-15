#pragma once
// =============================================================================
// AI 任务状态机（AI Task Runner）—— 文档 Part 4 各 AI 功能的**统一任务契约**
// =============================================================================
// 文档要求汇总（Part 4-A/4-B/4-C 自查规则第 4 项 + 第 30.3 节）：
//   · 所有含【进度条】的 AI 处理类功能，处理过程中面板内其他参数必须置灰禁用；
//   · AI 处理失败必须复用统一失败提示结构（**禁止**进度条卡住或无提示消失）；
//   · AI 类处理完成后计入撤销栈的 1 步操作（参考仿剪除外——用备份草稿）；
//   · 联网 AI 功能在**点击图标时**检测登录态（平台层职责；core 提供状态查询）。
//
// 本模块职责（core 层，纯状态机；零第三方依赖）：
//   1. 任务生命周期：创建 → 运行（进度 0..1）→ 完成/失败/取消；
//   2. 单活动任务约束（同一编辑器同一时刻只允许一个 AI 任务——对应文档
//      「处理期间其他参数置灰禁用」）；
//   3. 失败原因文案（统一结构；供 UI 弹「处理失败 + 重试/取消」）；
//   4. 结果交接：成功时返回「结果载荷」（各功能自定义 JSON——如生成的字幕
//      轨道引用、音轨引用；由具体功能模块填充与消费）。
//
// 实际推理（模型加载/张量计算）由各功能模块（ai_engine + 薄层）执行；
// 本模块只提供**任务簿记**，不做推理，也不伪造进度。
//
// 与平台层分工：
//   · core：任务状态机 + 结果载荷结构 + 失败语义；
//   · 平台层：判断任务是否需要登录/联网（nc_ai_feature_access）、执行真实推理、
//     按进度调用 nc_ai_task_progress。

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// AI 功能清单（文档 Part 4 逐项；**与本项目 C ABI 枚举对齐**）
// =============================================================================

/// AI 功能类型（文档 Part 4 的 9 项联网 AI + 参考仿剪）。
enum class AiFeatureType {
    None = 0,             ///< 未指定
    Matting = 1,          ///< 智能抠像（Part 4-A 1.6；文档 2.1）
    FrameInterpolation = 2, ///< 光流补帧（Part 4-A 1.7；文档 7.6）
    SuperResolution = 3,  ///< 超分辨率（Part 4-B 8.3）
    StemSeparation = 4,   ///< 人声分离（Part 3-A 音频增补第 6 项）
    SubtitleTranslate = 5,///< 字幕翻译（Part 4-A 3.4）
    Tts = 6,              ///< 文本转语音（Part 4-A 3.5）
    CameraTracking = 7,   ///< 镜头追踪（Part 4-A 3.3）
    DepthMap = 8,         ///< 深度图（Part 4-C 13.2）
    ReferenceEdit = 9,    ///< 参考仿剪（Part 4-C 13.1；**唯一不计入撤销栈**）
    SpeechToSubtitle = 10,///< 识别字幕/歌词（Part 4-A 3.2；whisper）
};

/// 功能名称（"matting"/...；序列化与 C ABI 使用）。
const char* aiFeatureName(AiFeatureType feature);
/// 功能中文展示名（文档逐字）。
const char* aiFeatureDisplayName(AiFeatureType feature);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool aiFeatureFromString(const char* text, AiFeatureType& out);
/// 从整数解析（0..10；非法返回 false）。
bool aiFeatureFromInt(int value, AiFeatureType& out);

/// 该功能是否需要联网/登录（文档 Part 5 第 26.1 节 + Part 6 第 41 节：
/// 9 项联网 AI 需要登录；**参考仿剪按文档同属联网功能**；
/// 识别字幕/歌词为本地模型（whisper.cpp）不需联网）。
bool aiFeatureRequiresNetwork(AiFeatureType feature);

/// 该功能处理结果是否计入撤销栈（文档 Part 6 第 43 节：
/// 全部计入，**参考仿剪除外**——用备份草稿方式处理）。
bool aiFeatureCountsTowardUndo(AiFeatureType feature);

// =============================================================================
// 任务状态机
// =============================================================================

/// 任务状态（文档第 30.3 节「统一失败提示结构」）。
enum class AiTaskState {
    Idle = 0,       ///< 空闲（无任务）
    Running = 1,    ///< 处理中（进度 0..1；UI 显示进度条 + 置灰其他参数）
    Succeeded = 2,  ///< 处理成功（结果可用；UI 弹「✓ 已完成」）
    Failed = 3,     ///< 处理失败（失败原因见 failureReason；UI 弹「✕ 处理失败」）
    Cancelled = 4,  ///< 用户取消（UI 静默返回）
};

/// 状态名称（"idle"/"running"/...）。
const char* aiTaskStateName(AiTaskState state);

/// AI 任务（单活动任务约束；由 NcEditor 持有一个）。
struct AiTask {
    AiFeatureType feature = AiFeatureType::None;
    AiTaskState state = AiTaskState::Idle;
    /// 进度 0..1（仅 Running 有意义；单调不减）。
    double progress = 0.0;
    /// 当前阶段说明（如「分析中 45%」的文案；可空）。
    std::string stageNote;
    /// 失败原因（仅 Failed；**必须**非空——文档禁止无提示卡死）。
    std::string failureReason;
    /// 是否可重试（失败时由启动方决定；供 UI 决定是否显示「重新处理」按钮）。
    bool retryable = false;
    /// 结果载荷（成功时填充；JSON 字符串——各功能自定义结构）。
    std::string resultPayload;
    /// 关联对象（可选；如目标片段/素材 ID——供 UI 定位）。
    std::uint64_t targetId = 0;
};

/// 任务控制器（状态机；不做推理）。
class AiTaskRunner {
public:
    /// 开始任务（要求当前空闲或已结束）。
    /// - feature：功能类型（None 拒绝）；
    /// - targetId：关联对象 ID（可空）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：已有任务在运行 / feature 非法。
    bool begin(AiFeatureType feature, std::uint64_t targetId = 0,
               std::string* outputFailure = nullptr);

    /// 更新进度（单调不减；钳制 0..1）。
    /// - stageNote：阶段说明（可空 = 保留原值）。
    /// 返回 false：当前无运行中任务。
    bool setProgress(double progress, const std::string& stageNote = std::string());

    /// 标记成功（载荷为 JSON 字符串；可空）。
    /// 返回 false：当前无运行中任务。
    bool succeed(const std::string& resultPayload = std::string());

    /// 标记失败（原因**必须**非空——空原因会被替换为通用文案）。
    /// 返回 false：当前无运行中任务。
    bool fail(const std::string& reason, bool retryable = true);

    /// 取消（用户主动；UI 静默返回）。
    /// 返回 false：当前无运行中任务。
    bool cancel();

    /// 复位到空闲（清空全部字段；UI 关闭提示后调用）。
    void reset();

    /// 当前任务（只读）。
    const AiTask& task() const { return task_; }
    /// 是否有活动任务（Running）。
    bool active() const { return task_.state == AiTaskState::Running; }

    /// 失败文案（统一结构；供 UI 直接展示）：
    /// 「[功能名]处理未能完成，请重试」或「[功能名]处理失败：<原因>」
    /// （文档第 30.3 节：必须说明具体失败原因）。
    std::string failureText() const;

private:
    AiTask task_;
};

}  // namespace core
}  // namespace nairoclip
