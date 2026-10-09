#pragma once
// =============================================================================
// 章节自动生成（Chapter Generation）—— 文档第 7 部分「🧪AI工具 → 章节自动生成」
// =============================================================================
// 产品对应（第 7 部分 2.2 AI工具 L2 列表最后一项）：
//   「章节自动生成 → 自动检测/手动编辑标题」
//
// 产品定位：长视频（课程/讲座/Vlog/访谈）导入后一键生成章节目录，
// 供播放器跳转、导出附带章节（YouTube 章节/时间戳说明）与工程内导航。
//
// 检测策略（全本地，零第三方依赖，可测试）：
//   1. 场景边界：复用 scene_detect（镜头切换点天然是章节候选边界）；
//   2. 静音边界：复用 VAD（analyzeSpeechActivity 语音保留区间的补集）——
//      长静音（段落停顿）是「话题切换」的强信号；
//   3. 时长约束：过短章节的边界被合并/丢弃（minChapterSeconds），
//      超长章节按 maxChapterSeconds 硬切（防止「整场一个章节」）；
//   4. 首章从 0 开始（第一章覆盖视频开头；末章覆盖到内容结束）。
//
// 自动标题：本地规则化命名（「第 N 章」+ 真实测量线索：亮度/运动/语音强度），
// 不调用云端、不做内容识别（合规：隐私政策「全部在设备本地完成」）；
// 标题可被用户手动编辑（renameChapter / 已落盘章节重命名）。
//
// 与标记（Marker）的关系（单一事实来源）：
//   章节落盘后即序列标记（固定章节颜色 + 标题为标记名）。用户在时间轴
//   标记面板即可看到章节结构；readChaptersFromSequence 由标记重建章节，
//   不引入第二份需要同步的持久化状态。
//
// 合规：全部计算在设备本地完成，不采集、不上传任何音视频内容
//（隐私政策 3.1/3.2）。

#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/reference_style.h"
#include "nairoclip/core/scene_detect.h"
#include "nairoclip/core/speech_cut.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 单个章节。
struct Chapter {
    /// 章节序号（从 1 开始；由列表顺序决定）。
    int index = 0;
    /// 章节起点（序列时间轴坐标）。
    Time start;
    /// 章节终点（下一章起点；末章 = 序列内容时长）。
    Time end;
    /// 章节标题（自动生成或用户手动编辑）。
    std::string title;
    /// 该章的时间轴标记 ID（落盘后写入；未落盘为无效 ID）。
    MarkerId markerId;
    /// 边界来源（供 UI 解释「为什么这里分章」）。
    enum class Boundary : std::uint8_t {
        Start = 0,      ///< 视频开头（首章）
        SceneCut = 1,   ///< 镜头切换（场景边界）
        Silence = 2,    ///< 长静音（话题停顿）
        Both = 3,       ///< 场景 + 静音同时命中
        Forced = 4,     ///< 时长硬切（超长章节切分）
        Unknown = 5,    ///< 未知（由已落盘标记重建时无边界来源信息）
    };
    Boundary boundary = Boundary::Start;
    /// 该章平均亮度（0..1；自动标题线索）。
    double averageBrightness = 0.5;
    /// 该章平均运动强度（0..1；自动标题线索）。
    double averageMotion = 0.0;
    /// 该章平均语音强度（RMS；无音频为 0）。
    double averageRms = 0.0;
    /// 是否有画面统计（无镜头数据时为 false；避免把「无数据」当成「画面平稳」）。
    bool hasVisualStats = false;

    Time duration() const { return end - start; }
    bool valid() const { return end > start && !title.empty(); }
};

/// 章节检测配置（UI「章节自动生成」面板参数）。
struct ChapterDetectConfig {
    /// 最短章节时长（秒）——短于此的候选边界被忽略/合并。
    double minChapterSeconds = 20.0;
    /// 最长章节时长（秒）——超过则硬切（0 = 不限制）。
    double maxChapterSeconds = 300.0;
    /// 长静音判定（秒）——静音持续时长达到该值视为章节边界。
    double silenceBoundarySeconds = 1.5;
    /// 场景切换阈值（直方图差异；透传给场景检测）。
    double sceneThreshold = 0.35;
    /// 是否使用场景检测（关闭则只用静音 + 时长约束）。
    bool useSceneDetect = true;
    /// 是否使用静音检测（关闭则只用场景 + 时长约束）。
    bool useSilenceDetect = true;
    /// 最大章节数（0 = 不限制；超出时按边界强度丢弃最弱边界并入相邻章）。
    std::size_t maxChapters = 0;
    /// 标题前缀（默认「第 N 章」格式）。
    std::string titlePrefix = "第";
    std::string titleSuffix = "章";

    bool valid() const {
        return minChapterSeconds > 0.0 && maxChapterSeconds >= 0.0 &&
               (maxChapterSeconds == 0.0 || maxChapterSeconds >= minChapterSeconds) &&
               silenceBoundarySeconds >= 0.0 && sceneThreshold >= 0.0 && sceneThreshold <= 1.0;
    }
};

/// 章节检测结果（供 UI 预览与落盘共用）。
struct ChapterDetectResult {
    std::vector<Chapter> chapters;
    /// 分析覆盖时长（序列内容时长）。
    Time totalDuration;
    /// 检测到的候选边界数（聚类合并前；诊断用）。
    std::size_t candidateBoundaries = 0;
    /// 说明（可直接展示，如「检测到 5 个章节（场景 3 / 静音 1 / 硬切 1）」）。
    std::string summary;
    /// 警告（如「素材过短，未生成章节」）。
    std::vector<std::string> warnings;
    /// 诊断信息（无有效输入时说明原因）。
    std::string note;

    bool valid() const { return !chapters.empty(); }
};

/// 从「镜头边界 + 语音活动」检测章节（纯算法层；不做媒体解码）。
/// - shots：镜头边界（可为空 = 不用场景检测）；
/// - speechSegments：语音保留区间（可为空 = 不用静音检测；
///   静音段由其在 [0, totalDuration] 内的补集推导）；
/// - totalDuration：序列内容时长（> 0；决定末章终点与硬切）；
/// - 返回结果恒为首章从 0 开始、按时间升序、覆盖到 totalDuration；
///   每章标题由 suggestChapterTitle 自动填充（可后续手动编辑）。
ChapterDetectResult detectChapters(const std::vector<ShotBoundary>& shots,
                                   const std::vector<SpeechActivityResult::KeepSegment>& speechSegments,
                                   const Time& totalDuration, const ChapterDetectConfig& config = {});

/// 从「逐帧特征 + 音频块」检测章节（便捷入口；测试与平台层共用）。
/// - features：逐帧特征（scene_detect 输入格式；时间戳为时间轴坐标）；
/// - audioBlocks：音频块（可为空 = 无音频）；
/// - totalDuration：序列内容时长。
ChapterDetectResult detectChaptersFromFeatures(const std::vector<FrameFeature>& features,
                                               const std::vector<AudioBlock>& audioBlocks,
                                               const Time& totalDuration,
                                               const ChapterDetectConfig& config = {});

/// 自动生成标题（本地规则化；不调用云端）。
/// 使用真实测量线索（时长/亮度/运动/语音强度），如「第 3 章 · 人声清晰 · 画面明亮」。
std::string suggestChapterTitle(const Chapter& chapter, const ChapterDetectConfig& config = {});

/// 批量润色标题（为所有章补充线索描述；已手动编辑过的标题保持不变）。
void suggestChapterTitles(ChapterDetectResult& result, const ChapterDetectConfig& config = {});

/// 重命名章节（手动编辑标题；就地修改结果对象，供落盘使用）。
/// 返回是否成功（序号越界/标题为空时失败）。
bool renameChapter(ChapterDetectResult& result, std::size_t zeroBasedIndex,
                   const std::string& newTitle);

/// ---------------------------------------------------------------------------
/// 真实素材采样（序列 → 时间轴坐标系的帧特征 + 音频块）
/// ---------------------------------------------------------------------------
///
/// 采样配置。
struct ChapterSampleConfig {
    /// 视频采样步长（秒）；<= 0 表示自适应（见 effectiveVideoStepSeconds）。
    double videoStepSeconds = 0.0;
    /// 自适应步长目标：按此时长间隔采样（约 240 个采样点覆盖全片）。
    double targetSampleCount = 240.0;
    /// 自适应步长下限/上限（秒）——短视频更细、长视频更粗。
    double minStepSeconds = 0.25;
    double maxStepSeconds = 1.0;
    /// 目标视频轨下标（-1 = 自动：第一条有片段且未隐藏的视频轨）。
    int videoTrackIndex = -1;
    /// 目标音频轨下标（-1 = 自动：第一条有片段的音频轨；
    /// 找不到时回退到视频轨片段自身的音频）。
    int audioTrackIndex = -1;
    /// 稳定性上限：视频采样点总数（超出截断并警告）。
    std::size_t maxVideoSamples = 2000;
    /// 稳定性上限：音频块总数（超出截断并警告）。
    std::size_t maxAudioBlocks = 200000;

    bool valid() const {
        return targetSampleCount > 0.0 && minStepSeconds > 0.0 &&
               maxStepSeconds >= minStepSeconds && maxVideoSamples > 0 && maxAudioBlocks > 0;
    }

    /// 实际使用的视频采样步长（显式设置则原样返回；否则按内容时长自适应）。
    double effectiveVideoStepSeconds(const Time& contentDuration) const {
        if (videoStepSeconds > 0.0) {
            return videoStepSeconds;
        }
        if (!contentDuration.isPositive()) {
            return minStepSeconds;
        }
        const double span = contentDuration.toDouble() / targetSampleCount;
        if (span < minStepSeconds) return minStepSeconds;
        if (span > maxStepSeconds) return maxStepSeconds;
        return span;
    }
};

/// 采样结果（时间轴坐标系）。
struct SequenceChapterSamples {
    std::vector<FrameFeature> features;  ///< 逐帧特征（时间戳 = 时间轴时间）
    std::vector<AudioBlock> audio;       ///< 音频块（start = 时间轴时间）
    Time timelineDuration;               ///< 序列内容时长
    std::vector<std::string> warnings;   ///< 跳过/截断说明
    std::string note;                    ///< 无法采样时的原因（如「未提供解码器」）

    bool usable() const { return !features.empty() || !audio.empty(); }
};

/// 对序列真实解码采样（逐片段 seek 采样帧特征 + 逐块解码音频）。
/// - readerFactory：平台层注入（FFmpeg/系统实现）；为空则 note 说明并返回空；
/// - 复合片段（嵌套序列）不参与采样（记入 warnings）；素材缺失的片段跳过。
/// 说明：纯本地处理，不产生任何网络请求。
SequenceChapterSamples sampleSequenceForChapters(const Project& project, SequenceId sequenceId,
                                                 MediaReaderFactory* readerFactory,
                                                 const ChapterSampleConfig& config = {});

/// ---------------------------------------------------------------------------
/// 落盘与重建
/// ---------------------------------------------------------------------------

/// 章节落盘结果。
struct ChapterApplyResult {
    Result result;
    std::size_t markersAdded = 0;
    std::size_t markersUpdated = 0;
    std::vector<std::string> warnings;
};

/// 落盘章节：把章节写入序列标记（一条撤销事务可整体回退）。
/// - 幂等语义：优先按章节的 markerId 匹配既有标记；markerId 无效时按
///   「章节颜色 + 相同起点」匹配（重复生成同一内容不产生重复标记）；
/// - 事务名默认「生成章节」（历史面板显示）；
/// - 前置条件：序列存在、章节列表非空；失败不产生副作用。
ChapterApplyResult applyChapters(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                                ChapterDetectResult& result,
                                const std::string& transactionName = "生成章节");

/// 由序列标记重建章节列表（按章节颜色筛选、按时间排序）。
/// - 每章标题 = 标记名；终点 = 下一章起点（末章 = 序列内容时长）；
/// - boundary：首章 Start，其余 Unknown（标记不携带边界来源）；
/// - 统计字段不可用（hasVisualStats = false）。
std::vector<Chapter> readChaptersFromSequence(const Sequence& sequence);

/// 章节标记颜色（时间轴标记面板展示；与章节主题色一致）。
const char* chapterMarkerColor();

/// 标题是否为自动生成格式（「第 N 章[ · 线索]」）——用于决定
/// 重新生成时是否覆盖（手动编辑过的标题不覆盖）。
bool isAutoGeneratedTitle(const std::string& title, const ChapterDetectConfig& config = {});

}  // namespace core
}  // namespace nairoclip
