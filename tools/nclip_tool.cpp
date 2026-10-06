// nclip_tool —— .nclip 工程文件命令行工具
//
// 用法：
//   nclip_tool info <file.nclip>     # 显示工程摘要
//   nclip_tool validate <file.nclip> # 校验工程文件（CI 用）
//   nclip_tool version               # 显示工具与格式版本
//
// 退出码：0 = 成功，1 = 文件错误，2 = 参数错误

#include "nairoclip/core/project_doctor.h"
#include "nairoclip/core/serialization.h"

#include <cstdio>
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
        std::printf("    轨道=%zu  标记=%zu  字幕=%zu\n", s.tracks.size(), s.markers.size(),
                    s.subtitles.size());
        for (const auto& t : s.tracks) {
            std::printf("    - %s（%s）片段=%zu 转场=%zu%s%s%s\n", t.name.c_str(),
                        t.type == TrackType::Video    ? "视频"
                        : t.type == TrackType::Audio  ? "音频"
                        : t.type == TrackType::Subtitle ? "字幕"
                                                        : "特效",
                        t.clips.size(), t.transitions.size(),
                        t.locked ? " [锁定]" : "", t.muted ? " [静音]" : "",
                        t.hidden ? " [隐藏]" : "");
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

void printUsage() {
    std::printf("用法：\n");
    std::printf("  nclip_tool info <file.nclip>      显示工程摘要\n");
    std::printf("  nclip_tool validate <file.nclip>  校验工程文件\n");
    std::printf("  nclip_tool doctor <file.nclip> [--fix] 修复工程文件\n");
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
    std::fprintf(stderr, "错误：未知命令 %s\n", command.c_str());
    printUsage();
    return 2;
}
