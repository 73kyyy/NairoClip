#include "nairoclip/core/subtitle_io.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace nairoclip {
namespace core {

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

/// 解析 "HH:MM:SS,mmm" / "HH:MM:SS.mmm" / "MM:SS.mmm" → 微秒。
bool parseTimestamp(const std::string& text, std::int64_t& micros) {
    // 支持逗号/点号毫秒分隔
    std::string normalized = text;
    for (auto& c : normalized) {
        if (c == ',') c = '.';
    }
    int h = 0, m = 0, s = 0, ms = 0;
    const int count = std::sscanf(normalized.c_str(), "%d:%d:%d.%d", &h, &m, &s, &ms);
    if (count >= 3) {
        // 有毫秒
        if (count == 3) ms = 0;
        micros = (static_cast<std::int64_t>(h) * 3600 + static_cast<std::int64_t>(m) * 60 +
                  static_cast<std::int64_t>(s)) * 1000000 + static_cast<std::int64_t>(ms) * 1000;
        return h >= 0 && m >= 0 && m < 60 && s >= 0 && s < 60;
    }
    // 退化为 "MM:SS"
    const int count2 = std::sscanf(normalized.c_str(), "%d:%d", &m, &s);
    if (count2 == 2) {
        micros = (static_cast<std::int64_t>(m) * 60 + static_cast<std::int64_t>(s)) * 1000000;
        return true;
    }
    return false;
}

std::string formatTimestampVtt(const Time& t) {
    const std::int64_t micros = toMicroseconds(t);
    const std::int64_t ms = (micros % 1000000) / 1000;
    const std::int64_t totalSeconds = micros / 1000000;
    const std::int64_t s = totalSeconds % 60;
    const std::int64_t m = (totalSeconds / 60) % 60;
    const std::int64_t h = totalSeconds / 3600;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld.%03lld",
                  static_cast<long long>(h), static_cast<long long>(m),
                  static_cast<long long>(s), static_cast<long long>(ms));
    return buf;
}

std::string formatTimestampSrt(const Time& t) {
    std::string vtt = formatTimestampVtt(t);
    // SRT 用逗号
    for (auto& c : vtt) {
        if (c == '.') c = ',';
    }
    return vtt;
}

}  // namespace

SubtitleParseResult parseSrt(const std::string& text) {
    SubtitleParseResult result;
    result.result = Result::ok();
    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    std::size_t index = 0;
    std::uint64_t nextId = 1;
    while (index < lines.size()) {
        // 跳过空行
        while (index < lines.size() && trim(lines[index]).empty()) ++index;
        if (index >= lines.size()) break;
        // 序号行（可省略）：仅当整行是纯数字时跳过；
        // 防止把 "00:00:00.500 --> ..."（以数字开头的时间行）误判为序号。
        const std::string maybeNumber = trim(lines[index]);
        bool allDigits = !maybeNumber.empty();
        for (const char c : maybeNumber) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            ++index;  // 跳过序号
        }
        if (index >= lines.size()) break;
        // 时间行："00:00:01,000 --> 00:00:02,000"
        const std::string timeLine = lines[index];
        const auto arrowPos = timeLine.find("-->");
        if (arrowPos == std::string::npos) {
            result.warnings.push_back("缺少时间轴: " + timeLine);
            ++index;
            continue;
        }
        std::int64_t startMicros = 0;
        std::int64_t endMicros = 0;
        if (!parseTimestamp(trim(timeLine.substr(0, arrowPos)), startMicros) ||
            !parseTimestamp(trim(timeLine.substr(arrowPos + 3)), endMicros)) {
            result.warnings.push_back("时间解析失败: " + timeLine);
            ++index;
            continue;
        }
        ++index;
        // 文本行（可多行，直到空行）
        std::string cueText;
        while (index < lines.size() && !trim(lines[index]).empty()) {
            if (!cueText.empty()) cueText += '\n';
            cueText += trim(lines[index]);
            ++index;
        }
        if (endMicros <= startMicros) {
            result.warnings.push_back("结束时间不大于开始时间，已跳过");
            continue;
        }
        SubtitleCue cue;
        cue.id = SubtitleId{nextId++};
        cue.start = fromMicroseconds(startMicros);
        cue.duration = fromMicroseconds(endMicros - startMicros);
        cue.text = cueText;
        result.cues.push_back(std::move(cue));
    }
    return result;
}

SubtitleParseResult parseVtt(const std::string& text) {
    // VTT 与 SRT 主体格式兼容（时间行分隔符相同箭头），
    // 差异：头部 WEBVTT、时间戳用点号、可以有 cue 设置。
    SubtitleParseResult result = parseSrt(text);
    // 移除可能混入的 WEBVTT 头部行产生的 warning
    result.warnings.erase(
        std::remove_if(result.warnings.begin(), result.warnings.end(),
                       [](const std::string& w) { return w.find("WEBVTT") != std::string::npos; }),
        result.warnings.end());
    return result;
}

std::string exportSrt(const std::vector<SubtitleCue>& cues) {
    std::vector<const SubtitleCue*> sorted;
    sorted.reserve(cues.size());
    for (const auto& cue : cues) {
        sorted.push_back(&cue);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const SubtitleCue* a, const SubtitleCue* b) { return a->start < b->start; });
    std::string out;
    int index = 1;
    for (const auto* cue : sorted) {
        if (cue->text.empty()) continue;
        out += std::to_string(index++) + "\n";
        out += formatTimestampSrt(cue->start) + " --> " + formatTimestampSrt(cue->end()) + "\n";
        out += cue->text + "\n\n";
    }
    return out;
}

std::string exportVtt(const std::vector<SubtitleCue>& cues) {
    std::string out = "WEBVTT\n\n";
    std::vector<const SubtitleCue*> sorted;
    sorted.reserve(cues.size());
    for (const auto& cue : cues) {
        sorted.push_back(&cue);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const SubtitleCue* a, const SubtitleCue* b) { return a->start < b->start; });
    for (const auto* cue : sorted) {
        if (cue->text.empty()) continue;
        out += formatTimestampVtt(cue->start) + " --> " + formatTimestampVtt(cue->end()) + "\n";
        out += cue->text + "\n\n";
    }
    return out;
}

}  // namespace core
}  // namespace nairoclip
