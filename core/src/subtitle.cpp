#include "nairoclip/core/subtitle.h"

#include <algorithm>

namespace nairoclip {
namespace core {

Result SubtitleTrack::insert(SubtitleCue cue) {
    if (!cue.valid()) {
        return Result::fail(ErrorCode::InvalidArgument, "字幕时间参数非法");
    }
    // 同 ID 替换
    for (auto& existing : cues_) {
        if (existing.id == cue.id) {
            existing = std::move(cue);
            std::sort(cues_.begin(), cues_.end(),
                      [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
            return Result::ok();
        }
    }
    cues_.push_back(std::move(cue));
    std::sort(cues_.begin(), cues_.end(),
              [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
    return Result::ok();
}

bool SubtitleTrack::remove(SubtitleId id) {
    const auto it = std::find_if(cues_.begin(), cues_.end(),
                                 [id](const SubtitleCue& c) { return c.id == id; });
    if (it == cues_.end()) {
        return false;
    }
    cues_.erase(it);
    return true;
}

void SubtitleTrack::clear() { cues_.clear(); }

SubtitleCue* SubtitleTrack::findCue(SubtitleId id) {
    for (auto& c : cues_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const SubtitleCue* SubtitleTrack::findCue(SubtitleId id) const {
    for (const auto& c : cues_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

std::vector<const SubtitleCue*> SubtitleTrack::cuesAt(const Time& t) const {
    std::vector<const SubtitleCue*> result;
    for (const auto& c : cues_) {
        if (c.enabled && c.contains(t)) {
            result.push_back(&c);
        }
    }
    return result;
}

bool SubtitleTrack::invariantsHold() const {
    for (std::size_t i = 0; i < cues_.size(); ++i) {
        if (!cues_[i].valid()) {
            return false;
        }
        if (i > 0 && cues_[i - 1].start > cues_[i].start) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> splitSubtitleText(const std::string& text,
                                           std::size_t maxCharsPerLine) {
    std::vector<std::string> lines;
    if (maxCharsPerLine == 0) {
        return lines;
    }
    // 先按显式换行拆段，再对每段做按长度折行（优先在标点/空格后断行）。
    std::size_t segmentStart = 0;
    auto flushSegment = [&](std::size_t segmentEnd) {
        std::size_t pos = segmentStart;
        while (pos < segmentEnd) {
            std::size_t remaining = segmentEnd - pos;
            std::size_t take = remaining <= maxCharsPerLine ? remaining : maxCharsPerLine;
            if (take < remaining) {
                // 回退到最近的 UTF-8 字符边界：切割点处的字节不能是延续字节
                // （0x80-0xBF 为 UTF-8 延续字节）。
                std::size_t cut = take;
                while (cut > 0 &&
                       (static_cast<unsigned char>(text[pos + cut]) & 0xC0) == 0x80) {
                    --cut;
                }
                if (cut == 0) {
                    cut = take;  // 防御：非法 UTF-8 时强制切
                }
                take = cut;
            }
            lines.push_back(text.substr(pos, take));
            pos += take;
        }
        segmentStart = segmentEnd;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            flushSegment(i);
            segmentStart = i + 1;  // 跳过换行符
        }
    }
    flushSegment(text.size());
    return lines;
}

}  // namespace core
}  // namespace nairoclip
