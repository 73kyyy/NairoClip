#pragma once
// =============================================================================
// 智能成片（多版本）+ 重新生成影响分析 + 参考风格权益门控
//   —— 文档第 7 部分 4.1「智能成片」/ 4.4「重新生成」/ 六、七（权益与边界）
// =============================================================================
// 产品对应（原文）：
//   4.1 L3 基础参数：「素材范围、成片时长、风格预设、剪辑节奏滑块、自动添加内容
//       勾选、生成版本数」；失败态：「第3个版本生成时中断，已成功生成的2个版本不受
//       影响…[跳过该片段重试] [仅保留已生成版本]」
//   4.4 重新生成：「⚠️ 您选择了"整个项目"重新生成 / 将会丢失：• 3处手动调整的转场
//       • 2条手动编辑的字幕 / 将会保留：• 已锁定的音乐轨道 / [取消] [确认重新生成]」
//   7.6 权益矩阵：「参考视频风格提取：免费版不可用 / Pro 可用」
//   7.7 边界：「免费用户参考视频功能 → 显示🔒，弹付费墙」
//
// 本模块定位（纯本地算法 + 事务化落盘，零网络）：
//   · planSmartMovie：把「风格画像 + 素材 + 目标时长 + 版本数」展开为多版本计划
//     （每版本按变体策略生成不同的剪辑方案；版本差异真实可测）；
//   · applySmartMovieVersion(s)：落盘版本到指定空轨道（一条事务；逐个失败不阻断）；
//   · analyzeRegenerationImpact：重新生成前的影响分析（将丢失/将保留，真实扫描）；
//   · clearForRegeneration：按分析执行清理（保留锁定轨道/其它序列）；
//   · 权益门控：参考风格（Pro 专属）与多版本（1 版 vs 1-5 版）的可用性判定。
//
// 合规（文档铁律，与本模块文案一致）：
//   · 对外表述只能「参考节奏与风格生成」，不可「复刻同款视频」；
//   · 参考素材仅贡献统计特征，不复制画面/人物/Logo/字幕文案；
//   · 全部计算在设备本地完成，不联网（隐私政策 3.1/3.2）。

#include "nairoclip/core/editor.h"
#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/reference_style.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 权益门控（文档 7.6 / 7.7）
// =============================================================================

/// 参考风格功能的可用性（Free 不可用 → 弹付费墙）。
struct ReferenceStyleAccess {
    bool allowed = false;
    /// 不可用原因的展示文案（含 🔒 语义，可直接用于 UI）。
    std::string reason;
    /// 是否应弹付费墙。
    bool showPaywall = false;
};

/// 参考风格（参考仿剪）门控：Pro 生效才可用（fail-closed）。
/// - nowUnix：用于 proActiveAt 到期判定（纵深防御，与导出门控一致）。
ReferenceStyleAccess referenceStyleAccess(const Entitlement& entitlement, std::int64_t nowUnix);

/// 生成版本数的门控结果。
struct VersionCountAccess {
    bool allowed = false;
    /// 实际允许的版本数（Free 固定 1；Pro 1..5）。
    int allowedCount = 1;
    /// 用户请求的版本数（原样回显，便于 UI 提示）。
    int requestedCount = 1;
    /// 提示文案（如「免费版固定生成 1 个版本，升级 Pro 可选 1-5 个」）。
    std::string reason;
    bool showPaywall = false;
};

/// 生成版本数门控：Free 固定 1 个；Pro 可选 1..5（文档 7.6「生成版本数量」）。
/// - 请求 <= 0 按 1 处理。
VersionCountAccess versionCountAccess(const Entitlement& entitlement, std::int64_t nowUnix,
                                      int requestedCount);

// =============================================================================
// 多版本智能成片（文档 4.1）
// =============================================================================

/// 版本变体策略（决定每个版本与其它版本的真实差异）。
enum class MovieVariant {
    Reference = 0,  ///< 忠实参考节奏（按画像典型镜头时长）
    Fast = 1,       ///< 快切版（镜头更短，节奏更密）
    Relaxed = 2,    ///< 舒缓版（镜头更长，节奏更慢）
    Dynamic = 3,    ///< 动感版（快切 + 每对转场）
    Calm = 4,       ///< 沉静版（舒缓 + 极少转场，适合口播/教程）
};

/// 变体名称（"reference"/"fast"/"relaxed"/"dynamic"/"calm"）。
const char* movieVariantName(MovieVariant variant);
/// 变体展示名（中文，供 UI 列表）。
const char* movieVariantLabel(MovieVariant variant);

/// 智能成片配置（对应文档 4.1「L3 基础参数」）。
struct SmartMovieConfig {
    /// 风格预设名（展示用；空 = 未命名）。
    std::string stylePreset;
    /// 目标成片时长（秒；<= 0 = 按参考节奏自然长度）。
    double targetSeconds = 0.0;
    /// 剪辑节奏滑块（0..1；0.5 = 忠实参考；< 0.5 更慢；> 0.5 更快）。
    double paceFactor = 0.5;
    /// 生成版本数（1..5；由 versionCountAccess 门控后传入）。
    int versionCount = 1;
    /// 自动添加内容：应用参考色调（映射为调色特效）。
    bool applyColor = true;
    /// 自动添加内容：应用转场。
    bool applyTransitions = true;
    /// 每版本最大转场数（防止过度添加）。
    std::size_t maxTransitionsPerVersion = 12;
    /// 参考色调画像（applyColor 时使用；来自 StyleProfile::color）。
    ColorProfile color;
    /// 素材范围（可用素材段；空 = 配置无效）。
    std::vector<MaterialSegment> material;

    bool valid() const {
        return versionCount >= 1 && versionCount <= 5 && paceFactor >= 0.0 &&
               paceFactor <= 1.0 && targetSeconds >= 0.0 && !material.empty();
    }
};

/// 单个版本的剪辑计划（落盘前可预览）。
struct MovieVersionPlan {
    int index = 0;                 ///< 版本序号（1 起）
    MovieVariant variant = MovieVariant::Reference;
    std::string label;             ///< 展示名（「版本 1 · 忠实参考」）
    std::vector<CutSuggestion> cuts;  ///< 剪辑建议（真实素材区间）
    Time totalDuration;            ///< 成片总时长
    Time shotDuration;             ///< 该版本采用的镜头时长
    std::size_t transitionCount = 0;  ///< 计划转场数
    /// 该版本是否可落盘（无建议内容时为 false，可跳过且不影响其它版本）。
    bool ok = false;
    /// 不可用原因（ok=false 时说明）。
    std::string note;
};

/// 多版本成片计划。
struct SmartMoviePlan {
    std::vector<MovieVersionPlan> versions;
    /// 参考节奏画像（供 UI 展示「参考节奏：平均 2.3 秒/镜头」）。
    RhythmProfile rhythm;
    /// 说明（可直接展示）。
    std::string summary;
    std::vector<std::string> warnings;

    /// 可落盘的版本数。
    std::size_t okCount() const;
    bool valid() const { return okCount() > 0; }
};

/// 生成多版本计划（不修改工程）。
/// - profile：参考风格画像（决定节奏）；config.material：用户素材；
/// - 每版本按变体策略调整镜头时长与转场密度（真实差异）；
/// - targetSeconds > 0 时按目标时长推导镜头数（仍受素材容量约束）；
/// - 素材不足以生成任意版本时返回 valid()=false 并给出 warnings。
SmartMoviePlan planSmartMovie(const StyleProfile& profile, const SmartMovieConfig& config);

/// 落盘结果（单版本）。
struct SmartMovieApplyResult {
    Result result;
    std::size_t clipsInserted = 0;
    std::size_t effectsAdded = 0;
    std::size_t transitionsAdded = 0;
    std::vector<std::string> warnings;
};

/// 落盘单个版本到指定轨道（一条事务可整体回退）。
/// - 前置条件：轨道存在、为空、未锁定；版本计划 ok；
/// - 需要转场的片段对之间真实预留间隙（转场需间隙的项目约束）；
/// - 事务名默认「智能成片 · 版本 N」。
SmartMovieApplyResult applySmartMovieVersion(TimelineEditor& editor, Project& project,
                                             SequenceId sequenceId, TrackId trackId,
                                             const MovieVersionPlan& version,
                                             const SmartMovieConfig& config,
                                             const std::string& transactionName = "");

/// 多版本批量落盘（文档 4.1 失败态：某版本失败不阻断其余版本）。
struct SmartMovieBatchResult {
    Result result;
    std::size_t versionsApplied = 0;
    std::vector<int> failedVersionIndexes;
    std::vector<std::string> failures;
    std::vector<SmartMovieApplyResult> perVersion;

    /// 是否部分成功（文档「已成功生成的版本不受影响」）。
    bool partialSuccess() const { return versionsApplied > 0 && !failedVersionIndexes.empty(); }
};

/// 批量落盘：每个可落盘版本对应一个空轨道（trackIds 数量不足时，剩余版本记录为失败）。
SmartMovieBatchResult applySmartMovieVersions(TimelineEditor& editor, Project& project,
                                              SequenceId sequenceId,
                                              const std::vector<TrackId>& trackIds,
                                              const SmartMoviePlan& plan,
                                              const SmartMovieConfig& config);

// =============================================================================
// 重新生成影响分析（文档 4.4）
// =============================================================================

/// 重新生成范围（文档「整个项目 / 单个序列」）。
enum class RegenerationScope {
    WholeProject = 0,  ///< 整个项目（含字幕轨与各序列的非保护内容）
    Sequence = 1,      ///< 单个序列（其它序列不动）
};

/// 影响分析中的一项（「将会丢失：• 3处手动调整的转场」）。
struct ImpactItem {
    /// 类别代码（"transitions" / "subtitles" / "effects" / "keyframes" /
    /// "masks" / "audio-adjust" / "speed" / "compound" / "markers"）。
    std::string code;
    /// 展示文案（可直接用于 UI 列表，如「3 处手动调整的转场」）。
    std::string label;
    /// 涉及数量。
    std::size_t count = 0;
};

/// 影响分析报告（落盘前的强制确认依据）。
struct RegenerationImpactReport {
    RegenerationScope scope = RegenerationScope::WholeProject;
    /// 将会丢失（重新生成会覆盖的内容）。
    std::vector<ImpactItem> willLose;
    /// 将会保留（锁定轨道等受保护内容）。
    std::vector<ImpactItem> willKeep;
    /// 受影响的轨道数（将被清空并重建的轨道）。
    std::size_t affectedTracks = 0;
    /// 受保护的轨道数（锁定 → 跳过重建）。
    std::size_t protectedTracks = 0;
    /// 说明（可直接展示）。
    std::string summary;

    /// 是否有任何将丢失内容（无 → UI 可不弹二次确认）。
    bool hasLosses() const;
    /// 将丢失总条目数。
    std::size_t lossCount() const;
    /// 生成「将会丢失 / 将会保留」清单文案（逐行；供弹窗直接展示）。
    std::vector<std::string> detailLines() const;
};

/// 分析重新生成的影响范围（只读；UI 在用户点击「重新生成」时调用）。
RegenerationImpactReport analyzeRegenerationImpact(const Project& project, SequenceId sequenceId,
                                                   RegenerationScope scope);

/// 重新生成执行结果。
struct RegenerationResult {
    Result result;
    /// 清空的轨道数（重建范围）。
    std::size_t tracksCleared = 0;
    /// 保留的轨道数（锁定轨道跳过）。
    std::size_t tracksProtected = 0;
    /// 清理的标记数（含章节标记）。
    std::size_t markersCleared = 0;
    /// 清理的字幕数（scope=WholeProject 时）。
    std::size_t subtitlesCleared = 0;
    /// 说明（可直接展示）。
    std::string summary;
};

/// 执行「重新生成」的清理阶段（一条事务可整体回退）：
/// - 按报告与范围清空内容，但**保留**：锁定轨道、其它序列（scope=Sequence）；
/// - 调用方在成功后重新执行 planSmartMovie + applySmartMovieVersions 填充内容
///   （两阶段设计：清理与重建分别可撤销，用户「取消」时可只回退清理）；
/// - 事务名默认「重新生成」；失败不产生副作用（先校验后执行）。
RegenerationResult clearForRegeneration(TimelineEditor& editor, Project& project,
                                        SequenceId sequenceId,
                                        const RegenerationImpactReport& report,
                                        const std::string& transactionName = "重新生成");

}  // namespace core
}  // namespace nairoclip
