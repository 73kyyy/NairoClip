#include "test_framework.h"
#include "nairoclip/core/subtitle.h"
using namespace nairoclip::core;
namespace {
SubtitleCue makeCue(std::uint64_t id, const Time& start, const Time& duration,
                    const std::string& text) {
    SubtitleCue c;
    c.id = SubtitleId{id};
    c.start = start;
    c.duration = duration;
    c.text = text;
    return c;
}
}  // namespace
NC_TEST(Subtitle, InsertSortsAndFinds) {
    SubtitleTrack track;
    NC_CHECK(track.insert(makeCue(2, Rational::make(4), Rational::make(2), "第二句")).isOk());
    NC_CHECK(track.insert(makeCue(1, Rational::make(0), Rational::make(2), "第一句")).isOk());
    NC_CHECK(track.insert(makeCue(3, Rational::make(2), Rational::make(2), "中间")).isOk());
    NC_CHECK(track.invariantsHold());
    NC_CHECK_EQ(track.size(), std::size_t(3));
    NC_CHECK(track.cues()[0].text == std::string("第一句"));
    NC_CHECK(track.cues()[1].text == std::string("中间"));
    NC_CHECK(track.cues()[2].text == std::string("第二句"));
    NC_CHECK(track.findCue(SubtitleId{2}) != nullptr);
    NC_CHECK(track.findCue(SubtitleId{99}) == nullptr);
}
NC_TEST(Subtitle, RejectsInvalidCue) {
    SubtitleTrack track;
    NC_CHECK(!track.insert(makeCue(1, Rational::make(-1), Rational::make(2), "x")).isOk());
    NC_CHECK(!track.insert(makeCue(2, Rational::make(0), Rational::make(0), "x")).isOk());
    NC_CHECK(track.empty());
}
NC_TEST(Subtitle, CuesAtMoment) {
    SubtitleTrack track;
    NC_CHECK(track.insert(makeCue(1, Rational::make(0), Rational::make(2), "A")).isOk());
    NC_CHECK(track.insert(makeCue(2, Rational::make(1), Rational::make(2), "B")).isOk());
    // 双语叠放：t=1.5 同时可见 A、B
    const auto at = track.cuesAt(fromMilliseconds(1500));
    NC_CHECK_EQ(at.size(), std::size_t(2));
    // t=5 无字幕
    NC_CHECK_EQ(track.cuesAt(Rational::make(5)).size(), std::size_t(0));
    // 终点不含（半开区间）
    NC_CHECK_EQ(track.cuesAt(Rational::make(2)).size(), std::size_t(1));
}
NC_TEST(Subtitle, RemoveAndClear) {
    SubtitleTrack track;
    NC_CHECK(track.insert(makeCue(1, Rational::make(0), Rational::make(2), "A")).isOk());
    NC_CHECK(track.remove(SubtitleId{1}));
    NC_CHECK(!track.remove(SubtitleId{1}));
    NC_CHECK(track.empty());
    NC_CHECK(track.invariantsHold());
}
NC_TEST(Subtitle, ReplaceExistingId) {
    SubtitleTrack track;
    NC_CHECK(track.insert(makeCue(1, Rational::make(0), Rational::make(2), "A")).isOk());
    NC_CHECK(track.insert(makeCue(1, Rational::make(5), Rational::make(2), "A2")).isOk());
    NC_CHECK_EQ(track.size(), std::size_t(1));
    NC_CHECK(track.cues()[0].text == std::string("A2"));
    NC_CHECK(track.cues()[0].start == Rational::make(5));
}
NC_TEST(Subtitle, DisabledCueInvisible) {
    SubtitleTrack track;
    SubtitleCue c = makeCue(1, Rational::make(0), Rational::make(2), "A");
    c.enabled = false;
    NC_CHECK(track.insert(c).isOk());
    NC_CHECK_EQ(track.cuesAt(Rational::make(1)).size(), std::size_t(0));
}
NC_TEST(Subtitle, SplitTextByLines) {
    const auto lines = splitSubtitleText("第一行\n第二行\n第三行", 10);
    NC_CHECK_EQ(lines.size(), std::size_t(3));
    NC_CHECK(lines[0] == std::string("第一行"));
    NC_CHECK(lines[2] == std::string("第三行"));
}
NC_TEST(Subtitle, SplitLongLineByLength) {
    // 每个中文占 3 字节；maxCharsPerLine=4 按字节窗口折行但必须在 UTF-8 边界切
    const auto lines = splitSubtitleText("一二三四五六七八", 12);
    NC_CHECK_EQ(lines.size(), std::size_t(2));
    NC_CHECK(lines[0] == std::string("一二三四"));
    NC_CHECK(lines[1] == std::string("五六七八"));
}
NC_TEST(Subtitle, SplitZeroLimitReturnsEmpty) {
    const auto lines = splitSubtitleText("abc", 0);
    NC_CHECK(lines.empty());
}
