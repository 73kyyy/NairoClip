// nclip_tool —— .nclip 工程文件命令行工具
//
// 用法：
//   nclip_tool info <file.nclip>     # 显示工程摘要
//   nclip_tool validate <file.nclip> # 校验工程文件（CI 用）
//   nclip_tool version               # 显示工具与格式版本
//
// 退出码：0 = 成功，1 = 文件错误，2 = 参数错误

#include "nairoclip/core/beat_detect.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/export_runner.h"
#include "nairoclip/core/project_doctor.h"

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#endif
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/subtitle_io.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace nairoclip::core;

namespace {

int printVersion() {
    std::printf("nclip_tool %d.%d\n", 0, 1);
    std::printf("format version supported: %d\n", kNclipFormatVersion);
    std::printf("abi version: (core library)\n");
    return 0;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "错误：无法打开文件 %s\n", path.c_str());
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string formatTime(const Time& t) {
    const std::int64_t micros = toMicroseconds(t);
    const std::int64_t msPart = (micros / 1000) % 1000;
    const std::int64_t seconds = micros / 1000000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld.%03llds", static_cast<long long>(seconds),
                  static_cast<long long>(msPart));
    return buf;
}

int commandInfo(const std::string& path) {
    std::string text;
    if (!readFile(path, text)) {
        return 1;
    }
    Project project;
    const NclipLoadResult result = loadProjectFromNclip(text, project);
    if (!result.result.isOk()) {
        std::fprintf(stderr, "错误：%s\n", result.result.message.c_str());
        return 1;
    }
    std::printf("=== 工程摘要 ===\n");
    std::printf("名称：%s\n", project.name.c_str());
    std::printf("格式版本：%d\n", result.fileVersion);
    std::printf("素材：%zu 个\n", project.assets.size());
    for (const auto& a : project.assets) {
        std::printf("  - [%llu] %s (%s%s)\n", static_cast<unsigned long long>(a.id.value),
                    a.name.c_str(), a.path.c_str(), a.offline ? " [离线]" : "");
    }
    std::printf("序列：%zu 个\n", project.sequences.size());
    for (const auto& s : project.sequences) {
        std::printf("  [%llu] %s  时长=%s  分辨率=%dx%d  @%g fps\n",
                    static_cast<unsigned long long>(s.id.value), s.name.c_str(),
                    formatTime(s.duration()).c_str(), s.width, s.height,
                    s.frameRate.toDouble());
        std::printf("    轨道=%zu  标记=%zu  字幕=%zu  画幅适配=%s\n", s.tracks.size(),
                    s.markers.size(), s.subtitles.size(), frameFitName(s.frameFit));
        for (const auto& t : s.tracks) {
            std::printf("    - %s（%s）片段=%zu 转场=%zu%s%s%s", t.name.c_str(),
                        t.type == TrackType::Video    ? "视频"
                        : t.type == TrackType::Audio  ? "音频"
                        : t.type == TrackType::Subtitle ? "字幕"
                                                        : "特效",
                        t.clips.size(), t.transitions.size(),
                        t.locked ? " [锁定]" : "", t.muted ? " [静音]" : "",
                        t.hidden ? " [隐藏]" : "");
            if (t.type == TrackType::Audio && t.autoDuck.enabled) {
                std::printf(" [闪避 %.0fdB 灵敏度%.1f]", t.autoDuck.duckAmountDb,
                            t.autoDuck.sensitivity);
            }
            std::printf("\n");
        }
    }
    return 0;
}

int commandValidate(const std::string& path) {
    std::string text;
    if (!readFile(path, text)) {
        return 1;
    }
    Project project;
    const NclipLoadResult result = loadProjectFromNclip(text, project);
    if (!result.result.isOk()) {
        std::fprintf(stderr, "校验失败：%s\n", result.result.message.c_str());
        return 1;
    }
    // 额外深度校验：所有轨道不变量
    for (const auto& s : project.sequences) {
        for (const auto& t : s.tracks) {
            if (!t.invariantsHold()) {
                std::fprintf(stderr, "校验失败：轨道「%s」不变量被破坏\n", t.name.c_str());
                return 1;
            }
        }
        if (!s.subtitles.invariantsHold()) {
            std::fprintf(stderr, "校验失败：序列「%s」字幕不变量被破坏\n", s.name.c_str());
            return 1;
        }
    }
    // 深度校验：使用 project doctor
    const HealthReport health = checkProjectHealth(project);
    bool ok = true;
    for (const auto& issue : health.issues) {
        const char* level = issue.level == HealthLevel::Error     ? "错误"
                            : issue.level == HealthLevel::Warning ? "警告"
                                                                  : "信息";
        std::printf("[%s] %s：%s\n", level, issue.code.c_str(), issue.message.c_str());
        if (issue.level == HealthLevel::Error) {
            ok = false;
        }
    }
    if (!ok) {
        std::fprintf(stderr, "校验失败：发现 %zu 个错误（可用 doctor 命令修复）\n",
                     health.errorCount());
        return 1;
    }
    std::printf("校验通过：%s（%zu 序列，%zu 素材，%zu 警告）\n", path.c_str(),
                project.sequences.size(), project.assets.size(), health.warningCount());
    return 0;
}

int commandDoctor(const std::string& path, bool writeBack) {
    std::string text;
    if (!readFile(path, text)) {
        return 1;
    }
    Project project;
    const NclipLoadResult result = loadProjectFromNclip(text, project);
    if (!result.result.isOk()) {
        std::fprintf(stderr, "加载失败：%s\n", result.result.message.c_str());
        return 1;
    }
    const RepairReport repair = repairProject(project);
    if (repair.actions.empty()) {
        std::printf("未发现需要修复的问题\n");
        return 0;
    }
    for (const auto& action : repair.actions) {
        std::printf("修复：%s（影响 %zu 处）\n", action.description.c_str(), action.affected);
    }
    if (writeBack) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "错误：无法写回 %s\n", path.c_str());
            return 1;
        }
        out << saveProjectToNclip(project);
        std::printf("已写回：%s\n", path.c_str());
    } else {
        std::printf("（未写回；加 --fix 参数以保存修复结果）\n");
    }
    return repair.after.healthy() ? 0 : 1;
}

#ifdef NAIROCLIP_HAS_FFMPEG
/// 导出工程到 MP4（需要 FFmpeg 构建）。
/// 用法：nclip_tool export <file.nclip> <out.mp4> [--crf N] [--preset P] [--fit fit|fill|crop]
int commandExport(const std::string& path, const std::string& outPath, int crf,
                  const std::string& preset, FrameFit fitMode) {
    std::string text;
    if (!readFile(path, text)) {
        return 1;
    }
    Project project;
    const NclipLoadResult loadResult = loadProjectFromNclip(text, project);
    if (!loadResult.result.isOk()) {
        std::fprintf(stderr, "加载失败：%s\n", loadResult.result.message.c_str());
        return 1;
    }
    Sequence* seq = project.mainSequence();
    if (seq == nullptr) {
        std::fprintf(stderr, "工程没有主序列\n");
        return 1;
    }
    // 命令行指定的画幅适配模式覆盖工程设置（导出预设 9:16 / 16:9 / 1:1 场景）。
    seq->frameFit = fitMode;
    const ExportPlan plan = compileExportPlan(project, *seq);
    for (const auto& warning : plan.warnings) {
        std::fprintf(stderr, "警告：%s\n", warning.c_str());
    }
    FFmpegReaderFactory readerFactory;
    class FFWriterFactory : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    FFWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = outPath;
    config.writer.width = plan.width;
    config.writer.height = plan.height;
    config.writer.frameRate = plan.frameRate;
    config.writer.crf = crf;
    config.writer.preset = preset;
    config.writer.includeAudio = true;
    std::printf("开始导出：%s → %s\n", path.c_str(), outPath.c_str());
    std::printf("  分辨率：%dx%d @ %.3ffps，时长：%.2fs，画幅适配：%s\n", plan.width, plan.height,
                plan.frameRate.toDouble(), plan.duration.toDouble(),
                frameFitName(plan.frameFit));
    int lastPercent = -1;
    const ExportRunResult result = runExport(
        plan, readerFactory, writerFactory, config,
        [&](double progress) {
            const int percent = static_cast<int>(progress * 100.0);
            if (percent / 10 != lastPercent / 10) {
                lastPercent = percent;
                std::printf("  进度：%d%%\n", percent);
            }
            return true;
        },
        &project);
    if (!result.result.isOk()) {
        std::fprintf(stderr, "导出失败：%s\n", result.result.message.c_str());
        return 1;
    }
    std::printf("导出完成：%lld 帧 / %lld 音频块 → %s\n",
                static_cast<long long>(result.stats.framesWritten),
                static_cast<long long>(result.stats.audioBlocksWritten), outPath.c_str());
    for (const auto& warning : result.stats.warnings) {
        std::fprintf(stderr, "警告：%s\n", warning.c_str());
    }
    return 0;
}
#else
int commandExport(const std::string&, const std::string&, int, const std::string&,
                  FrameFit) {
    std::fprintf(stderr, "此构建未包含 FFmpeg，无法导出。\n");
    return 1;
}
#endif

/// 分析音频文件的节拍（音乐卡点前端；需 FFmpeg）。
/// 用法：nclip_tool beats <audio-file> [--json]
int commandBeats(const std::string& audioPath, bool jsonOutput) {
#ifdef NAIROCLIP_HAS_FFMPEG
    FFmpegMediaReader reader;
    const Result openResult = reader.open(audioPath);
    if (!openResult.isOk()) {
        std::fprintf(stderr, "无法打开音频：%s\n", openResult.message.c_str());
        return 1;
    }
    std::vector<AudioBlock> blocks;
    AudioBlock block;
    // 上限保护：最多读取 30 分钟音频（约 1758 块/秒 @1024 帧 48kHz）
    const std::size_t maxBlocks = 200000;
    while (reader.readAudioBlock(block) && blocks.size() < maxBlocks) {
        blocks.push_back(block);
    }
    reader.close();
    const BeatAnalysis analysis = analyzeBeats(blocks);
    if (jsonOutput) {
        // 供平台层/脚本消费（节拍点时间用微秒，避免浮点误差）
        std::printf("{\"bpm\":%.3f,\"confidence\":%.3f,\"onsets\":%zu,\"beats\":[",
                    analysis.bpm, analysis.confidence, analysis.onsets.size());
        for (std::size_t i = 0; i < analysis.beats.size(); ++i) {
            std::printf("%s%lld", i == 0 ? "" : ",",
                        static_cast<long long>(toMicroseconds(analysis.beats[i].time)));
        }
        std::printf("],\"note\":\"%s\"}\n", analysis.note.c_str());
    } else {
        std::printf("=== 节拍分析：%s ===\n", audioPath.c_str());
        std::printf("时长：%.2f s，采样率：%d，声道：%d\n",
                    analysis.analyzedDuration.toDouble(), blocks.empty() ? 0 : blocks.front().sampleRate,
                    blocks.empty() ? 0 : blocks.front().channels);
        std::printf("BPM：%.2f（置信度 %.3f）\n", analysis.bpm, analysis.confidence);
        std::printf("起音点：%zu 个，节拍：%zu 个\n", analysis.onsets.size(),
                    analysis.beats.size());
        for (std::size_t i = 0; i < analysis.beats.size() && i < 20; ++i) {
            std::printf("  节拍 %2zu: %s\n", i + 1,
                        formatClock(analysis.beats[i].time).c_str());
        }
        if (analysis.beats.size() > 20) {
            std::printf("  ...（共 %zu 个）\n", analysis.beats.size());
        }
        if (!analysis.note.empty()) {
            std::printf("诊断：%s\n", analysis.note.c_str());
        }
    }
    return analysis.valid() ? 0 : 1;
#else
    (void)audioPath;
    (void)jsonOutput;
    std::fprintf(stderr, "此构建未包含 FFmpeg，无法分析音频节拍。\n");
    return 1;
#endif
}

/// 导出序列字幕为 SRT 文件。
/// 用法：nclip_tool subs <file.nclip> <out.srt>
int commandSubtitles(const std::string& path, const std::string& outPath) {
    std::string text;
    if (!readFile(path, text)) {
        return 1;
    }
    Project project;
    const NclipLoadResult loadResult = loadProjectFromNclip(text, project);
    if (!loadResult.result.isOk()) {
        std::fprintf(stderr, "加载失败：%s\n", loadResult.result.message.c_str());
        return 1;
    }
    const Sequence* seq = project.mainSequence();
    if (seq == nullptr) {
        std::fprintf(stderr, "工程没有主序列\n");
        return 1;
    }
    const std::string srt = exportSrt(seq->subtitles.cues());
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "无法写入 %s\n", outPath.c_str());
        return 1;
    }
    out << srt;
    std::printf("已导出 %zu 条字幕 → %s\n", seq->subtitles.cues().size(), outPath.c_str());
    return 0;
}

/// 导入 SRT 字幕到工程。
/// 用法：nclip_tool subs-import <file.nclip> <in.srt> <out.nclip>
int commandSubtitlesImport(const std::string& path, const std::string& srtPath,
                           const std::string& outPath) {
    std::string text;
    if (!readFile(path, text)) {
        return 1;
    }
    Project project;
    const NclipLoadResult loadResult = loadProjectFromNclip(text, project);
    if (!loadResult.result.isOk()) {
        std::fprintf(stderr, "加载失败：%s\n", loadResult.result.message.c_str());
        return 1;
    }
    Sequence* seq = project.mainSequence();
    if (seq == nullptr) {
        std::fprintf(stderr, "工程没有主序列\n");
        return 1;
    }
    std::string srtText;
    if (!readFile(srtPath, srtText)) {
        return 1;
    }
    const SubtitleParseResult parsed = parseSrt(srtText);
    if (parsed.cues.empty()) {
        std::fprintf(stderr, "未解析到字幕\n");
        return 1;
    }
    for (const auto& cue : parsed.cues) {
        SubtitleCue withId = cue;
        withId.id = SubtitleId{project.ids.next()};
        if (!seq->subtitles.insert(withId).isOk()) {
            std::fprintf(stderr, "插入字幕失败\n");
            return 1;
        }
    }
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "无法写入 %s\n", outPath.c_str());
        return 1;
    }
    out << saveProjectToNclip(project);
    std::printf("已导入 %zu 条字幕 → %s\n", parsed.cues.size(), outPath.c_str());
    for (const auto& w : parsed.warnings) {
        std::fprintf(stderr, "警告：%s\n", w.c_str());
    }
    return 0;
}

void printUsage() {
    std::printf("用法：\n");
    std::printf("  nclip_tool info <file.nclip>      显示工程摘要\n");
    std::printf("  nclip_tool validate <file.nclip>  校验工程文件\n");
    std::printf("  nclip_tool doctor <file.nclip> [--fix] 修复工程文件\n");
    std::printf("  nclip_tool export <file.nclip> <out.mp4> [--crf N] [--preset P] [--fit fit|fill|crop] 导出视频\n");
    std::printf("  nclip_tool subs <file.nclip> <out.srt> 导出字幕\n");
    std::printf("  nclip_tool subs-import <file.nclip> <in.srt> <out.nclip> 导入字幕\n");
    std::printf("  nclip_tool beats <audio> [--json] 分析音频节拍（音乐卡点）\n");
    std::printf("  nclip_tool version                版本信息\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 2;
    }
    const std::string command = argv[1];
    if (command == "version" || command == "--version" || command == "-v") {
        return printVersion();
    }
    if (command == "help" || command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }
    if (argc < 3) {
        std::fprintf(stderr, "错误：缺少文件参数\n");
        printUsage();
        return 2;
    }
    const std::string path = argv[2];
    if (command == "info") {
        return commandInfo(path);
    }
    if (command == "validate") {
        return commandValidate(path);
    }
    if (command == "doctor") {
        const bool fix = argc >= 4 && std::string(argv[3]) == "--fix";
        return commandDoctor(path, fix);
    }
    if (command == "export") {
        if (argc < 4) {
            std::fprintf(stderr, "错误：export 需要输出路径参数\n");
            printUsage();
            return 2;
        }
        const std::string outPath = argv[3];
        int crf = 23;
        std::string preset = "veryfast";
        FrameFit fitMode = FrameFit::Fit;
        for (int i = 4; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            if (flag == "--crf") {
                crf = std::atoi(argv[i + 1]);
            } else if (flag == "--preset") {
                preset = argv[i + 1];
            } else if (flag == "--fit") {
                if (!frameFitFromString(argv[i + 1], fitMode)) {
                    std::fprintf(stderr, "错误：--fit 仅支持 fit / fill / crop\n");
                    return 2;
                }
            }
        }
        return commandExport(path, outPath, crf, preset, fitMode);
    }
    if (command == "subs") {
        if (argc < 4) {
            std::fprintf(stderr, "错误：subs 需要输出路径参数\n");
            printUsage();
            return 2;
        }
        return commandSubtitles(path, argv[3]);
    }
    if (command == "beats") {
        // beats <audio> [--json]：音频文件路径由 argv[2] 提供
        const bool jsonOutput = argc >= 4 && std::string(argv[3]) == "--json";
        return commandBeats(path, jsonOutput);
    }
    if (command == "subs-import") {
        if (argc < 5) {
            std::fprintf(stderr, "错误：subs-import 需要 SRT 输入与输出路径\n");
            printUsage();
            return 2;
        }
        return commandSubtitlesImport(path, argv[3], argv[4]);
    }
    std::fprintf(stderr, "错误：未知命令 %s\n", command.c_str());
    printUsage();
    return 2;
}
