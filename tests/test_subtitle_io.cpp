#include "test_framework.h"
#include "nairoclip/core/subtitle_io.h"
using namespace nairoclip::core;
NC_TEST(SubtitleIo, ParseBasicSrt) {
    const std::string srt =
        "1\n"
        "00:00:01,000 --> 00:00:03,500\n"
        "第一条字幕\n"
        "\n"
        "2\n"
        "00:00:04,000 --> 00:00:06,000\n"
        "第二条字幕\n"
        "第二行\n"
        "\n";
    const auto result = parseSrt(srt);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.cues.size(), std::size_t(2));
    NC_CHECK(result.cues[0].start == fromSeconds(1));
    NC_CHECK(result.cues[0].duration == fromMilliseconds(2500));
    NC_CHECK_EQ(result.cues[0].text, std::string("第一条字幕"));
    NC_CHECK(result.cues[1].start == fromSeconds(4));
    NC_CHECK_EQ(result.cues[1].text, std::string("第二条字幕\n第二行"));
    NC_CHECK(result.warnings.empty());
}
NC_TEST(SubtitleIo, ParseTolerantFormats) {
    // 点号毫秒 + 无序号 + 带 BOM 样式行
    const std::string srt =
        "\n"
        "00:00:00.500 --> 00:00:02.000\n"
        "无序号字幕\n";
    const auto result = parseSrt(srt);
    NC_CHECK_EQ(result.cues.size(), std::size_t(1));
    if (result.cues.empty()) return;  // 防御：断言失败时不越界
    NC_CHECK(result.cues[0].start == fromMilliseconds(500));
}
NC_TEST(SubtitleIo, ParseRejectsBadLines) {
    const std::string srt =
        "1\n"
        "NOT A TIMESTAMP\n"
        "坏字幕\n\n"
        "2\n"
        "00:00:05,000 --> 00:00:04,000\n"   // 结束 < 开始
        "倒置字幕\n\n"
        "3\n"
        "00:00:06,000 --> 00:00:07,000\n"
        "好字幕\n";
    const auto result = parseSrt(srt);
    NC_CHECK_EQ(result.cues.size(), std::size_t(1));  // 只有第三条有效
    NC_CHECK_EQ(result.cues[0].text, std::string("好字幕"));
    NC_CHECK(result.warnings.size() >= 1);
}
NC_TEST(SubtitleIo, ExportSrtRoundTrip) {
    std::vector<SubtitleCue> cues;
    SubtitleCue a;
    a.id = SubtitleId{1};
    a.start = fromSeconds(1);
    a.duration = fromMilliseconds(2500);
    a.text = "测试一";
    SubtitleCue b;
    b.id = SubtitleId{2};
    b.start = fromSeconds(5);
    b.duration = fromSeconds(2);
    b.text = "测试二";
    cues = {b, a};  // 乱序输入
    const std::string srt = exportSrt(cues);
    // 重新解析
    const auto parsed = parseSrt(srt);
    NC_CHECK_EQ(parsed.cues.size(), std::size_t(2));
    // 应已排序
    NC_CHECK(parsed.cues[0].start == fromSeconds(1));
    NC_CHECK_EQ(parsed.cues[0].text, std::string("测试一"));
    NC_CHECK(parsed.cues[1].start == fromSeconds(5));
}
NC_TEST(SubtitleIo, ExportVttFormat) {
    std::vector<SubtitleCue> cues;
    SubtitleCue a;
    a.id = SubtitleId{1};
    a.start = fromMilliseconds(1500);
    a.duration = fromSeconds(2);
    a.text = "VTT 字幕";
    cues.push_back(a);
    const std::string vtt = exportVtt(cues);
    NC_CHECK(vtt.find("WEBVTT") == 0);
    NC_CHECK(vtt.find("00:00:01.500 --> 00:00:03.500") != std::string::npos);
}
NC_TEST(SubtitleIo, ParseVtt) {
    const std::string vtt =
        "WEBVTT\n"
        "\n"
        "00:00:01.000 --> 00:00:02.000\n"
        "VTT 测试字幕\n";
    const auto result = parseVtt(vtt);
    NC_CHECK_EQ(result.cues.size(), std::size_t(1));
    NC_CHECK_EQ(result.cues[0].text, std::string("VTT 测试字幕"));
    NC_CHECK(result.cues[0].start == fromSeconds(1));
}
