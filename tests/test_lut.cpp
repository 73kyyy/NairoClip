#include "test_framework.h"
#include "nairoclip/core/lut.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 生成一个 identity LUT 的 .cube 文本（size³ 行）。
/// 数据顺序：r 最快、b 最慢。
std::string makeIdentityCube(int size) {
    std::string out;
    out += "TITLE \"identity\"\n";
    out += "LUT_3D_SIZE " + std::to_string(size) + "\n";
    out += "DOMAIN_MIN 0.0 0.0 0.0\n";
    out += "DOMAIN_MAX 1.0 1.0 1.0\n";
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const double rf = size > 1 ? double(r) / (size - 1) : 0.0;
                const double gf = size > 1 ? double(g) / (size - 1) : 0.0;
                const double bf = size > 1 ? double(b) / (size - 1) : 0.0;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f\n", rf, gf, bf);
                out += buf;
            }
        }
    }
    return out;
}

/// 生成「交换 R 与 B」的 LUT（用于验证通道映射，identity 测不出顺序错误）。
std::string makeSwapRbCube(int size) {
    std::string out;
    out += "LUT_3D_SIZE " + std::to_string(size) + "\n";
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const double rf = double(r) / (size - 1);
                const double gf = double(g) / (size - 1);
                const double bf = double(b) / (size - 1);
                char buf[96];
                // 输出 = (b, g, r)：交换红蓝
                std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f\n", bf, gf, rf);
                out += buf;
            }
        }
    }
    return out;
}

}  // namespace

// =============================================================================
// 3D LUT（B1-2）
// =============================================================================

NC_TEST(Lut, IdentityCubeRoundtrip) {
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(makeIdentityCube(17), lut, err));
    NC_CHECK(err.empty());
    NC_CHECK_EQ(lut.size, 17);
    NC_CHECK_EQ(lut.title, std::string("identity"));
    NC_CHECK(lut.valid());
    // 17³ × 3
    NC_CHECK_EQ(lut.data.size(), std::size_t(17 * 17 * 17 * 3));

    // identity：采样任意点都应原样返回
    for (double v = 0.0; v <= 1.0001; v += 0.125) {
        double r = 0, g = 0, b = 0;
        lutSample(lut, v, v, v, r, g, b);
        NC_CHECK(std::fabs(r - v) < 1e-6);
        NC_CHECK(std::fabs(g - v) < 1e-6);
        NC_CHECK(std::fabs(b - v) < 1e-6);
    }
}

NC_TEST(Lut, ChannelOrderIsRedFastest) {
    // 这是 .cube 最容易写错的地方：数据顺序必须是 r 最快、b 最慢。
    // 用「交换红蓝」的 LUT 才能测出顺序错误——identity 对顺序不敏感。
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(makeSwapRbCube(5), lut, err));

    // 输入纯红 (1,0,0) → 输出应为纯蓝 (0,0,1)
    double r = 0, g = 0, b = 0;
    lutSample(lut, 1.0, 0.0, 0.0, r, g, b);
    NC_CHECK(std::fabs(r - 0.0) < 1e-6);
    NC_CHECK(std::fabs(g - 0.0) < 1e-6);
    NC_CHECK(std::fabs(b - 1.0) < 1e-6);

    // 输入纯蓝 (0,0,1) → 输出纯红
    lutSample(lut, 0.0, 0.0, 1.0, r, g, b);
    NC_CHECK(std::fabs(r - 1.0) < 1e-6);
    NC_CHECK(std::fabs(b - 0.0) < 1e-6);
}

NC_TEST(Lut, TrilinearInterpolationIsLinearInSmoothLut) {
    // 用一个「把 R 乘 0.5」的线性 LUT：插值必须精确（线性函数的插值仍线性）
    std::string text = "LUT_3D_SIZE 2\n";
    for (int b = 0; b < 2; ++b) {
        for (int g = 0; g < 2; ++g) {
            for (int r = 0; r < 2; ++r) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f\n", r * 0.5, double(g), double(b));
                text += buf;
            }
        }
    }
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(text, lut, err));

    // R 分量：输入 0.5 → 0.25（精确，无插值误差）
    double r = 0, g = 0, b = 0;
    lutSample(lut, 0.5, 0.3, 0.7, r, g, b);
    NC_CHECK(std::fabs(r - 0.25) < 1e-6);
    NC_CHECK(std::fabs(g - 0.3) < 1e-6);
    NC_CHECK(std::fabs(b - 0.7) < 1e-6);
}

NC_TEST(Lut, Supports17And33) {
    for (int size : {17, 33}) {
        Lut3D lut;
        std::string err;
        NC_CHECK(parseCubeLut(makeIdentityCube(size), lut, err));
        NC_CHECK_EQ(lut.size, size);
        double r = 0, g = 0, b = 0;
        lutSample(lut, 0.42, 0.58, 0.91, r, g, b);
        NC_CHECK(std::fabs(r - 0.42) < 1e-5);
        NC_CHECK(std::fabs(g - 0.58) < 1e-5);
        NC_CHECK(std::fabs(b - 0.91) < 1e-5);
    }
}

NC_TEST(Lut, CommentsAndBlankLinesIgnored) {
    std::string text = "# 这是注释\n\nLUT_3D_SIZE 2\n# 另一条注释\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d %d %d\n", r, g, b);
                text += buf;
            }
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(text, lut, err));
    NC_CHECK_EQ(lut.size, 2);
}

// ---- 畸形文件：必须拒绝并给出原因 ----

NC_TEST(Lut, RejectsMissingSize) {
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut("0 0 0\n1 1 1\n", lut, err));
    NC_CHECK(!err.empty());
    NC_CHECK(err.find("LUT_3D_SIZE") != std::string::npos);
}

NC_TEST(Lut, RejectsSizeOutOfRange) {
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut("LUT_3D_SIZE 1\n0 0 0\n", lut, err));
    NC_CHECK(err.find("范围") != std::string::npos);

    Lut3D lut2;
    std::string err2;
    NC_CHECK(!parseCubeLut("LUT_3D_SIZE 128\n", lut2, err2));
    NC_CHECK(!err2.empty());
}

NC_TEST(Lut, RejectsWrongDataCount) {
    // 声明 2（需要 8 行）但只给 3 行
    std::string text = "LUT_3D_SIZE 2\n0 0 0\n0.5 0 0\n1 0 0\n";
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut(text, lut, err));
    NC_CHECK(err.find("数据行数量不符") != std::string::npos);
    // 报错要说清期望与实际，便于用户定位
    NC_CHECK(err.find("8") != std::string::npos);
    NC_CHECK(err.find("3") != std::string::npos);
}

NC_TEST(Lut, RejectsNonNumericData) {
    std::string text = "LUT_3D_SIZE 2\n0 0 0\n0.5 abc 0\n1 0 0\n0 1 0\n0 0 1\n1 1 0\n1 0 1\n0 1 1\n";
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut(text, lut, err));
    NC_CHECK(err.find("abc") != std::string::npos);
    NC_CHECK(err.find("不是数值") != std::string::npos);
    // 带行号
    NC_CHECK(err.find("第 3 行") != std::string::npos);
}

NC_TEST(Lut, RejectsWrongFieldCount) {
    std::string text = "LUT_3D_SIZE 2\n0 0 0\n0.5 0 0 0.2\n";
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut(text, lut, err));
    NC_CHECK(err.find("三个数值") != std::string::npos);
}

NC_TEST(Lut, Rejects1DLutExplicitly) {
    // 1D LUT 语义不同，必须明确拒绝而不是静默当 3D 处理
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut("LUT_1D_SIZE 16\n0 0 0\n", lut, err));
    NC_CHECK(err.find("1D") != std::string::npos);
}

NC_TEST(Lut, RejectsDuplicateSize) {
    Lut3D lut;
    std::string err;
    NC_CHECK(!parseCubeLut("LUT_3D_SIZE 2\nLUT_3D_SIZE 2\n", lut, err));
    NC_CHECK(err.find("重复") != std::string::npos);
}

NC_TEST(Lut, FailedParseLeavesOutputUntouched) {
    // 解析失败不得让调用方拿到半个 LUT
    Lut3D existing;
    std::string err;
    NC_CHECK(parseCubeLut(makeIdentityCube(2), existing, err));
    const std::size_t before = existing.data.size();
    const int beforeSize = existing.size;

    Lut3D target = existing;
    NC_CHECK(!parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n", target, err));
    NC_CHECK_EQ(target.data.size(), before);
    NC_CHECK_EQ(target.size, beforeSize);
}

NC_TEST(Lut, DomainMinMaxRespected) {
    // 回归保护：DOMAIN 声明为 0..255 的 LUT，归一化输入 1.0 必须落在立方体
    // 末端。此前按「域单位」解释输入，1.0 被当成 1/255 落在起点，
    // 整张图会被压黑——这个断言就是那次缺陷的守门人。
    std::string text = "LUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 255 255 255\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d %d %d\n", r, g, b);
                text += buf;
            }
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(text, lut, err));
    double r = 0, g = 0, b = 0;
    // 输入按 0..255 解释：255/255 = 1.0 → 末端角点
    lutSample(lut, 255.0 / 255.0, 0.0, 0.0, r, g, b);
    NC_CHECK(std::fabs(r - 1.0) < 1e-6);
    // 0 → 起点
    lutSample(lut, 0.0, 0.0, 0.0, r, g, b);
    NC_CHECK(std::fabs(r - 0.0) < 1e-6);
}

NC_TEST(Lut, StrengthBlendsBetweenOriginalAndLut) {
    const int w = 4, h = 1;
    // 灰度图
    std::vector<std::uint8_t> px = {100, 100, 100, 255, 150, 150, 150, 255,
                                    200, 200, 200, 255, 50, 50, 50, 255};
    const std::vector<std::uint8_t> original = px;

    // 反相 LUT（r → 1-r）
    std::string text = "LUT_3D_SIZE 2\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d %d %d\n", 1 - r, 1 - g, 1 - b);
                text += buf;
            }
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(text, lut, err));

    // 强度 0：完全不变
    NC_CHECK(applyLutToRgba(px.data(), w, h, lut, 0.0));
    NC_CHECK(px == original);

    // 强度 1：完全反相
    px = original;
    NC_CHECK(applyLutToRgba(px.data(), w, h, lut, 1.0));
    NC_CHECK_EQ(static_cast<int>(px[0]), 155);   // 255 - 100
    NC_CHECK_EQ(static_cast<int>(px[4]), 105);   // 255 - 150

    // 强度 0.5：居中（100 → 127 左右）
    px = original;
    NC_CHECK(applyLutToRgba(px.data(), w, h, lut, 0.5));
    NC_CHECK(std::abs(static_cast<int>(px[0]) - 127) <= 1);
}

NC_TEST(Lut, ApplyPreservesAlpha) {
    const int w = 2, h = 1;
    std::vector<std::uint8_t> px = {100, 100, 100, 77, 200, 200, 200, 33};
    Lut3D lut;
    std::string err;
    NC_CHECK(parseCubeLut(makeIdentityCube(2), lut, err));
    NC_CHECK(applyLutToRgba(px.data(), w, h, lut, 1.0));
    NC_CHECK_EQ(static_cast<int>(px[3]), 77);
    NC_CHECK_EQ(static_cast<int>(px[7]), 33);
}

NC_TEST(Lut, InvalidLutRejectedByApply) {
    const int w = 2, h = 2;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 128);
    const std::vector<std::uint8_t> original = px;
    Lut3D empty;  // size = 0，未初始化
    NC_CHECK(!applyLutToRgba(px.data(), w, h, empty, 1.0));
    NC_CHECK(px == original);
    NC_CHECK(!applyLutToRgba(nullptr, w, h, empty, 1.0));
    NC_CHECK(!applyLutToRgba(px.data(), 0, h, empty, 1.0));
}

NC_TEST(Lut, LoadMissingFileFails) {
    Lut3D lut;
    std::string err;
    NC_CHECK(!loadCubeLutFile("/nonexistent/definitely_missing.cube", lut, err));
    NC_CHECK(!err.empty());
}

// =============================================================================
// Clip 数据模型与序列化（B1-2）
// =============================================================================

#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

NC_TEST(Lut, ClipDefaultsToNoLut) {
    Clip c;
    NC_CHECK(!c.hasLut());
    NC_CHECK(c.lutValid());
    NC_CHECK_EQ(c.lutStrength, 1.0);
}

NC_TEST(Lut, ClipLutValidity) {
    Clip c;
    c.lutPath = "/luts/teal_orange.cube";
    NC_CHECK(c.hasLut());
    NC_CHECK(c.lutValid());

    c.lutStrength = 0.5;
    NC_CHECK(c.lutValid());
    c.lutStrength = 0.0;
    NC_CHECK(c.lutValid());
    c.lutStrength = 1.0;
    NC_CHECK(c.lutValid());
    // 越界不合法
    c.lutStrength = 1.5;
    NC_CHECK(!c.lutValid());
    c.lutStrength = -0.1;
    NC_CHECK(!c.lutValid());
}

NC_TEST(Lut, ClipLutSurvivesSerialization) {
    Project p;
    p.name = "lut 工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    s.name = "s";
    s.frameRate = fps30();
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    t.name = "v";
    MediaAsset a;
    a.id = AssetId{p.ids.next()};
    a.path = "/v/a.mp4";
    a.name = "a.mp4";
    a.type = MediaType::Video;
    a.duration = Rational::make(10);
    p.assets.push_back(a);
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.asset = a.id;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    c.lutPath = "/luts/teal_orange.cube";
    c.lutStrength = 0.65;
    t.clips.push_back(c);
    s.tracks.push_back(t);
    p.sequences.push_back(s);

    Project out;
    const NclipLoadResult r = loadProjectFromNclip(saveProjectToNclip(p), out);
    NC_CHECK(r.result.isOk());
    const Clip& lc = out.sequences[0].tracks[0].clips[0];
    NC_CHECK(lc.lutPath == std::string("/luts/teal_orange.cube"));
    NC_CHECK(std::fabs(lc.lutStrength - 0.65) < 1e-9);
    NC_CHECK(lc.hasLut());
}

NC_TEST(Lut, NoLutOmitsField) {
    // 未启用 LUT 时不写字段：保持文件 diff 稳定
    Project p;
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    s.name = "s";
    s.frameRate = fps30();
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    NC_CHECK(text.find("\"lut\"") == std::string::npos);
}

NC_TEST(Lut, OutOfRangeStrengthIsClampedOnLoad) {
    // 强度写坏时截断而不是让工程打不开
    const std::string text =
        "{\"format\":\"nclip\",\"version\":1,\"project\":{\"name\":\"t\"},\"assets\":[],"
        "\"sequences\":[{\"id\":1,\"name\":\"s\",\"frameRate\":[30,1],\"width\":1920,"
        "\"height\":1080,\"tracks\":[{\"id\":2,\"type\":\"video\",\"name\":\"v\",\"clips\":["
        "{\"id\":3,\"asset\":0,\"start\":[0,1],\"duration\":[1,1],\"sourceIn\":[0,1],"
        "\"speed\":[1,1],\"lut\":{\"path\":\"/x.cube\",\"strength\":5.0}}]}],\"markers\":[]}]}";
    Project out;
    const NclipLoadResult r = loadProjectFromNclip(text, out);
    NC_CHECK(r.result.isOk());
    NC_CHECK(std::fabs(out.sequences[0].tracks[0].clips[0].lutStrength - 1.0) < 1e-9);
}
