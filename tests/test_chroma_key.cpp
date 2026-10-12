// =============================================================================
// 色度抠像测试（M2.27，文档第 2 部分「🤖AI实验室 → 智能抠像 → 色度抠像(绿幕)」
// + 权益矩阵「AI智能抠像：基础版85% | 高精度版98%+物品/色度抠像」）
// =============================================================================
// 覆盖：
//   · 键控色解析/名称（green/blue/magenta/十六进制/中文别名/非法）；
//   · 度量正确性：纯绿幕全抠、打光不完美绿幕、浅绿（白光冲淡）、深绿阴影保护、
//     黄绿衣物部分保留、反向色（肤色/红/蓝）全保留、近灰像素保留；
//   · 双阈值软判决：过渡带单调、阈值收紧/放宽的效果方向；
//   · 溢色抑制：通道法（G 超出 max(R,B) 收拢）、强度线性、无溢色不变；
//   · 帧级应用：Transparent（alpha 缩放）/ Background（背景混色、alpha 覆盖）；
//   · 非法参数防御（不改动缓冲）；
//   · 质量档位与 Pro 门控（fail-closed）。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/chroma_key.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

std::vector<std::uint8_t> solidFrame(int w, int h, std::uint8_t r, std::uint8_t g,
                                     std::uint8_t b, std::uint8_t a = 255) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
    return pixels;
}

}  // namespace

// ---------------------------------------------------------------------------
// 键控色解析与名称
// ---------------------------------------------------------------------------

NC_TEST(ChromaKey, ColorParsing) {
    NC_CHECK_EQ(chromaKeyColorFromString("green"), std::uint32_t(0x00FF00));
    NC_CHECK_EQ(chromaKeyColorFromString("GREEN"), std::uint32_t(0x00FF00));
    NC_CHECK_EQ(chromaKeyColorFromString("blue"), std::uint32_t(0x0000FF));
    NC_CHECK_EQ(chromaKeyColorFromString("magenta"), std::uint32_t(0xFF00FF));
    NC_CHECK_EQ(chromaKeyColorFromString("绿"), std::uint32_t(0x00FF00));
    NC_CHECK_EQ(chromaKeyColorFromString("蓝"), std::uint32_t(0x0000FF));
    // 十六进制写法
    NC_CHECK_EQ(chromaKeyColorFromString("#00FF00"), std::uint32_t(0x00FF00));
    NC_CHECK_EQ(chromaKeyColorFromString("0x1A2B3C"), std::uint32_t(0x1A2B3C));
    NC_CHECK_EQ(chromaKeyColorFromString("123456"), std::uint32_t(0x123456));
    // 非法
    NC_CHECK_EQ(chromaKeyColorFromString("bogus"), std::uint32_t(0xFFFFFFFF));
    NC_CHECK_EQ(chromaKeyColorFromString("#12345"), std::uint32_t(0xFFFFFFFF));
    NC_CHECK_EQ(chromaKeyColorFromString(nullptr), std::uint32_t(0xFFFFFFFF));
    // 名称
    NC_CHECK_EQ(std::string(chromaKeyColorName(0x00FF00)), std::string("green"));
    NC_CHECK_EQ(std::string(chromaKeyColorName(0x0000FF)), std::string("blue"));
    NC_CHECK_EQ(std::string(chromaKeyColorName(0xFF00FF)), std::string("magenta"));
    NC_CHECK_EQ(std::string(chromaKeyColorName(0x123456)), std::string("custom"));
}

// ---------------------------------------------------------------------------
// 度量正确性（关键场景矩阵）
// ---------------------------------------------------------------------------

NC_TEST(ChromaKey, PureGreenRemoved) {
    ChromaKey key;  // 默认绿幕
    NC_CHECK_EQ(chromaKeyCoverageAt(0, 255, 0, key), 0.0);
    NC_CHECK_EQ(chromaKeyCoverageAt(0, 200, 40, key), 0.0);   // 打光不完美
    // 白光冲淡（浅绿）：完全抠除（软判决边界，容差 0.01 内视为完全透明）
    NC_CHECK(chromaKeyCoverageAt(120, 255, 140, key) < 0.01);
}

NC_TEST(ChromaKey, OppositeColorsPreserved) {
    ChromaKey key;
    // 反向色/异色相：全部保留为前景
    NC_CHECK(chromaKeyCoverageAt(220, 170, 140, key) > 0.99);  // 肤色
    NC_CHECK(chromaKeyCoverageAt(180, 30, 40, key) > 0.99);    // 深红
    NC_CHECK(chromaKeyCoverageAt(0, 0, 255, key) > 0.99);      // 蓝
    NC_CHECK(chromaKeyCoverageAt(255, 255, 255, key) > 0.99);  // 白
    NC_CHECK(chromaKeyCoverageAt(0, 0, 0, key) > 0.99);        // 黑（近灰）
    NC_CHECK(chromaKeyCoverageAt(128, 128, 128, key) > 0.99);  // 中灰
}

NC_TEST(ChromaKey, ShadowGreenPartiallyProtected) {
    ChromaKey key;
    // 深绿阴影 (0,90,15)：相似度按幅度衰减 → 部分保留（边缘过渡带）
    const double cov = chromaKeyCoverageAt(0, 90, 15, key);
    NC_CHECK(cov > 0.05);  // 不是一刀切抠除（保留立体感）
    NC_CHECK(cov < 1.0);
    // 收紧阈值（inner 更大 → 更容易判背景）→ 深绿阴影更接近被抠除
    ChromaKey aggressive;
    aggressive.innerThreshold = 0.55;
    aggressive.outerThreshold = 0.85;
    const double cov2 = chromaKeyCoverageAt(0, 90, 15, aggressive);
    NC_CHECK(cov2 < cov);  // 方向正确
}

NC_TEST(ChromaKey, YellowGreenClothMostlyPreserved) {
    ChromaKey key;
    // 黄绿衣物 (180,230,60)：与绿幕夹角大 → 大部分保留（不误抠衣物）
    const double cov = chromaKeyCoverageAt(180, 230, 60, key);
    NC_CHECK(cov > 0.25);  // 至少部分保留（不完整抠除）
}

NC_TEST(ChromaKey, BlueScreenKeying) {
    ChromaKey key;
    key.keyColorRgb = 0x0000FF;
    NC_CHECK_EQ(chromaKeyCoverageAt(0, 0, 255, key), 0.0);   // 蓝幕全抠
    NC_CHECK_EQ(chromaKeyCoverageAt(40, 40, 200, key), 0.0);  // 打光不完美蓝幕
    NC_CHECK(chromaKeyCoverageAt(0, 255, 0, key) > 0.99);     // 绿全保留
    NC_CHECK(chromaKeyCoverageAt(220, 170, 140, key) > 0.99);  // 肤色全保留
}

NC_TEST(ChromaKey, MagentaScreenKeying) {
    ChromaKey key;
    key.keyColorRgb = 0xFF00FF;
    NC_CHECK_EQ(chromaKeyCoverageAt(255, 0, 255, key), 0.0);
    NC_CHECK(chromaKeyCoverageAt(0, 255, 0, key) > 0.99);  // 绿全保留
}

NC_TEST(ChromaKey, ThresholdBandMonotonicity) {
    // 过渡带内覆盖率随距离单调不减（无跳变）
    ChromaKey key;
    double prev = -1.0;
    for (int g = 255; g >= 120; g -= 5) {
        const double cov = chromaKeyCoverageAt(0, static_cast<std::uint8_t>(g), 0, key);
        NC_CHECK(cov >= prev - 1e-9);
        prev = cov;
    }
    // 更宽的过渡带（outer 更大）→ 同一像素覆盖率更高（边界更软）
    ChromaKey soft;
    soft.outerThreshold = 0.95;
    NC_CHECK(chromaKeyCoverageAt(0, 140, 0, soft) >= chromaKeyCoverageAt(0, 140, 0, key));
}

// ---------------------------------------------------------------------------
// 溢色抑制
// ---------------------------------------------------------------------------

NC_TEST(ChromaKey, SpillSuppressionReducesGreenCast) {
    ChromaKey key;  // 默认强度 0.6
    std::vector<std::uint8_t> px = {150, 175, 140, 255};  // 前景灰底带绿反光
    NC_CHECK(chromaKeyCoverageAt(150, 175, 140, key) > 0.5);  // 确实是前景
    applyChromaKeyToFrame(px.data(), 1, 1, key);
    // G 超出 max(R,B)=150 的部分被收拢：175 → ~160
    NC_CHECK(px[1] < 170);
    NC_CHECK(px[1] >= 150);
    NC_CHECK_EQ(px[0], std::uint8_t(150));  // R 不变
    NC_CHECK_EQ(px[2], std::uint8_t(140));  // B 不变
    // 强度单调：越强收得越狠（但不低于 max(R,B)）
    double prev = 255.0;
    for (double s : {0.0, 0.3, 0.6, 1.0}) {
        ChromaKey k = key;
        k.spillSuppression = s;
        std::vector<std::uint8_t> p = {150, 175, 140, 255};
        applyChromaKeyToFrame(p.data(), 1, 1, k);
        NC_CHECK(static_cast<double>(p[1]) <= prev + 1e-9);
        prev = static_cast<double>(p[1]);
    }
    // 无溢色的前景像素不变（肤色）
    std::vector<std::uint8_t> skin = {220, 170, 140, 255};
    applyChromaKeyToFrame(skin.data(), 1, 1, key);
    NC_CHECK_EQ(skin[0], std::uint8_t(220));
    NC_CHECK_EQ(skin[1], std::uint8_t(170));
    NC_CHECK_EQ(skin[2], std::uint8_t(140));
}

NC_TEST(ChromaKey, SpillSuppressionDisabled) {
    ChromaKey key;
    key.spillSuppression = 0.0;
    std::vector<std::uint8_t> px = {150, 175, 140, 255};
    applyChromaKeyToFrame(px.data(), 1, 1, key);
    NC_CHECK_EQ(px[1], std::uint8_t(175));  // 关闭时原样保留
}

// ---------------------------------------------------------------------------
// 帧级应用
// ---------------------------------------------------------------------------

NC_TEST(ChromaKeyApply, TransparentModeZeroesAlpha) {
    const int w = 32, h = 24;
    // 上半绿幕，下半肤色
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            if (y < h / 2) {
                frame[idx] = 0; frame[idx + 1] = 200; frame[idx + 2] = 40;
            } else {
                frame[idx] = 220; frame[idx + 1] = 170; frame[idx + 2] = 140;
            }
        }
    }
    ChromaKey key;
    NC_CHECK(applyChromaKeyToFrame(frame.data(), w, h, key, ChromaKeyBlendMode::Transparent));
    // 绿幕区 alpha=0
    NC_CHECK_EQ(frame[(static_cast<std::size_t>(2) * w + 2) * 4 + 3], std::uint8_t(0));
    // 肤色区 alpha=255
    NC_CHECK_EQ(frame[(static_cast<std::size_t>(h - 3) * w + 2) * 4 + 3], std::uint8_t(255));
}

NC_TEST(ChromaKeyApply, BackgroundModeFillsBackground) {
    const int w = 32, h = 24;
    std::vector<std::uint8_t> frame = solidFrame(w, h, 0, 200, 40);
    ChromaKey key;
    // 背景 = 不透明蓝
    NC_CHECK(applyChromaKeyToFrame(frame.data(), w, h, key, ChromaKeyBlendMode::Background,
                                   0x0000FFFF));
    // 绿幕像素被完整替换为蓝色背景
    NC_CHECK_EQ(frame[0], std::uint8_t(0));
    NC_CHECK_EQ(frame[1], std::uint8_t(0));
    NC_CHECK_EQ(frame[2], std::uint8_t(255));
    NC_CHECK_EQ(frame[3], std::uint8_t(255));
}

NC_TEST(ChromaKeyApply, RejectsInvalidArgs) {
    const int w = 8, h = 8;
    auto frame = solidFrame(w, h, 0, 200, 40);
    const auto original = frame;
    ChromaKey key;
    // 空指针/非法尺寸
    NC_CHECK(!applyChromaKeyToFrame(nullptr, w, h, key));
    NC_CHECK(!applyChromaKeyToFrame(frame.data(), 0, h, key));
    NC_CHECK(!applyChromaKeyToFrame(frame.data(), w, -1, key));
    // 非法参数（outer <= inner）
    ChromaKey bad;
    bad.innerThreshold = 0.9;
    bad.outerThreshold = 0.5;
    NC_CHECK(!applyChromaKeyToFrame(frame.data(), w, h, bad));
    // 像素未被改动
    NC_CHECK_EQ(frame.size(), original.size());
    for (std::size_t i = 0; i < frame.size(); ++i) {
        NC_CHECK_EQ(frame[i], original[i]);
    }
    // 覆盖率查询同样拒绝
    NC_CHECK(chromaKeyCoverageAt(0, 200, 40, bad) < 0.0);
    NC_CHECK(chromaKeyCoverageRatio(frame.data(), w, h, bad, 2) < 0.0);
}

NC_TEST(ChromaKeyApply, CoverageRatioComputation) {
    const int w = 40, h = 40;
    // 左半绿幕、右半肤色 → 前景比例 ~50%
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            if (x < w / 2) {
                frame[idx] = 0; frame[idx + 1] = 200; frame[idx + 2] = 40;
            } else {
                frame[idx] = 220; frame[idx + 1] = 170; frame[idx + 2] = 140;
            }
        }
    }
    ChromaKey key;
    const double ratio = chromaKeyCoverageRatio(frame.data(), w, h, key, 2);
    NC_CHECK(ratio > 0.42 && ratio < 0.58);
    // 全绿幕 → 0
    auto allGreen = solidFrame(w, h, 0, 200, 40);
    NC_CHECK(chromaKeyCoverageRatio(allGreen.data(), w, h, key, 2) < 0.02);
    // 全肤色 → 1
    auto allSkin = solidFrame(w, h, 220, 170, 140);
    NC_CHECK(chromaKeyCoverageRatio(allSkin.data(), w, h, key, 2) > 0.98);
}

NC_TEST(ChromaKeyApply, AlreadyTransparentPreserved) {
    // 原 alpha 半透明的前景像素：叠加抠像（前景）后保持原值
    const int w = 4, h = 4;
    auto frame = solidFrame(w, h, 220, 170, 140, 128);
    ChromaKey key;
    applyChromaKeyToFrame(frame.data(), w, h, key, ChromaKeyBlendMode::Transparent);
    NC_CHECK_EQ(frame[3], std::uint8_t(128));
}

// ---------------------------------------------------------------------------
// 质量档位与门控
// ---------------------------------------------------------------------------

NC_TEST(ChromaKey, QualityNamesAndParsing) {
    NC_CHECK_EQ(std::string(mattingQualityName(MattingQuality::Basic)), std::string("basic"));
    NC_CHECK_EQ(std::string(mattingQualityName(MattingQuality::HighPrecision)),
                std::string("high_precision"));
    MattingQuality q;
    NC_CHECK(mattingQualityFromInt(0, q));
    NC_CHECK(q == MattingQuality::Basic);
    NC_CHECK(mattingQualityFromInt(1, q));
    NC_CHECK(q == MattingQuality::HighPrecision);
    NC_CHECK(!mattingQualityFromInt(2, q));
    NC_CHECK(!mattingQualityFromInt(-1, q));
}

NC_TEST(ChromaKey, QualityAdjustments) {
    ChromaKey key;
    key.spillSuppression = 0.8;
    key.outerThreshold = 0.45;  // 窄过渡带
    // 基础版：溢色上限 0.4 + 过渡带加宽到 0.30
    const ChromaKey basic = chromaKeyForQuality(key, MattingQuality::Basic);
    NC_CHECK(basic.spillSuppression <= 0.4 + 1e-9);
    NC_CHECK(basic.outerThreshold - basic.innerThreshold >= 0.30 - 1e-9);
    // 高精度版：原样
    const ChromaKey hq = chromaKeyForQuality(key, MattingQuality::HighPrecision);
    NC_CHECK(std::fabs(hq.spillSuppression - 0.8) < 1e-9);
    NC_CHECK(std::fabs(hq.outerThreshold - 0.45) < 1e-9);
    // 基础版不改变单像素判决方向（同一像素：基础版的软判决 ≥ 高精度版）
    const double covBasic = chromaKeyCoverageAt(0, 240, 10, basic);
    const double covHq = chromaKeyCoverageAt(0, 240, 10, hq);
    NC_CHECK(covBasic >= covHq - 1e-9);
}

NC_TEST(ChromaKey, AccessGatingFailClosed) {
    // 非 Pro → 不允许 + 付费墙文案
    const MattingAccess denied = mattingAccessFor(false, MattingQuality::HighPrecision);
    NC_CHECK(!denied.allowed);
    NC_CHECK(denied.showPaywall);
    NC_CHECK(denied.reason.find("🔒") != std::string::npos);
    NC_CHECK(denied.quality == MattingQuality::Basic);
    // Pro → 允许，档位透传
    const MattingAccess hq = mattingAccessFor(true, MattingQuality::HighPrecision);
    NC_CHECK(hq.allowed);
    NC_CHECK(!hq.showPaywall);
    NC_CHECK(hq.quality == MattingQuality::HighPrecision);
    const MattingAccess basic = mattingAccessFor(true, MattingQuality::Basic);
    NC_CHECK(basic.allowed);
    NC_CHECK(basic.quality == MattingQuality::Basic);
    // 用途文案
    NC_CHECK(std::string(chromaKeyPurpose()).find("Pro") != std::string::npos);
}

// =============================================================================
// 管线集成（编辑器撤销/导出计划/导出执行/预览/序列化真实生效）
// =============================================================================

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/smart_movie.h"

namespace {

/// Mock 读取器：固定纯色帧（便于验证抠像生效）。
class ChromaReader : public MediaReader {
public:
    ChromaReader() {
        probe_.type = MediaType::Video;
        probe_.frameRate = fps30();
        probe_.width = 64;
        probe_.height = 48;
        probe_.duration = Rational::make(10);
    }
    Result open(const std::string& p) override {
        if (p.empty()) return Result::fail(ErrorCode::InvalidArgument, "路径为空");
        path_ = p;
        open_ = true;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time&) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& o) override {
        if (!open_) return false;
        o.timestamp = Rational::make(0);
        o.width = 64;
        o.height = 48;
        o.stride = 64 * 4;
        o.pixels.assign(static_cast<std::size_t>(64) * 48 * 4, 255);
        // 按素材路径返回不同画面（多层合成验证需要区分上下层内容）：
        //  - "red.mp4"：纯红背景（底层）；
        //  - 其他（绿幕素材）：上半绿幕、下半肤色。
        const bool isRed = path_.find("red") != std::string::npos;
        for (int y = 0; y < 48; ++y) {
            for (int x = 0; x < 64; ++x) {
                const std::size_t idx = (static_cast<std::size_t>(y) * 64 + x) * 4;
                if (isRed) {
                    o.pixels[idx] = 220;
                    o.pixels[idx + 1] = 20;
                    o.pixels[idx + 2] = 20;
                } else if (y < 24) {
                    o.pixels[idx] = 0;
                    o.pixels[idx + 1] = 200;
                    o.pixels[idx + 2] = 40;
                } else {
                    o.pixels[idx] = 220;
                    o.pixels[idx + 1] = 170;
                    o.pixels[idx + 2] = 140;
                }
            }
        }
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    MediaProbeResult probe_;
    bool open_ = false;
    std::string path_;
};

class ChromaFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<ChromaReader>(); }
};

struct ChromaFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;
    ChromaFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 64;
        seq.height = 48;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/greenscreen.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(10);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(1);
        editor.insertClip(seqId, trackId, c, &clipId);
    }
};

}  // namespace

NC_TEST(ChromaPipeline, EditorSetUndoRedo) {
    ChromaFixture f;
    ChromaKey key;
    key.innerThreshold = 0.35;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(clip->hasChromaKey);
    NC_CHECK(std::fabs(clip->chromaKey.innerThreshold - 0.35) < 1e-9);
    // 撤销
    NC_CHECK(f.editor.undo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(!clip->hasChromaKey);
    // 重做
    NC_CHECK(f.editor.redo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->hasChromaKey);
    NC_CHECK(std::fabs(clip->chromaKey.innerThreshold - 0.35) < 1e-9);
}

NC_TEST(ChromaPipeline, EditorRejectsInvalidAndLocked) {
    ChromaFixture f;
    ChromaKey bad;
    bad.innerThreshold = 0.8;
    bad.outerThreshold = 0.3;  // outer <= inner
    ChromaKey good;
    NC_CHECK(!f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, bad, true).isOk());
    NC_CHECK(!f.editor.setClipChromaKey(f.seqId, f.trackId, ClipId{9999}, good, true).isOk());
    // 锁定轨道
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    NC_CHECK(!f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, good, true).isOk());
    // 解除 → 成功
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, false).isOk());
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, good, true).isOk());
}

NC_TEST(ChromaPipeline, ClearCommand) {
    ChromaFixture f;
    ChromaKey key;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->hasChromaKey);
    // 清除
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, false).isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(!clip->hasChromaKey);
    // 撤销清除 → 恢复
    NC_CHECK(f.editor.undo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->hasChromaKey);
    // 再撤销设置 → 消除
    NC_CHECK(f.editor.undo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(!clip->hasChromaKey);
}

NC_TEST(ChromaPipeline, ExportPlanCarriesChromaKey) {
    ChromaFixture f;
    ChromaKey key;
    key.keyColorRgb = 0x0000FF;
    key.innerThreshold = 0.4;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    NC_CHECK(!plan.tracks.empty());
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(1));
    NC_CHECK(plan.tracks[0].clips[0].hasChromaKey);
    NC_CHECK_EQ(plan.tracks[0].clips[0].chromaKey.keyColorRgb, std::uint32_t(0x0000FF));
    NC_CHECK(std::fabs(plan.tracks[0].clips[0].chromaKey.innerThreshold - 0.4) < 1e-9);
}

NC_TEST(ChromaPipeline, ExportAppliesChromaKeySingleLayer) {
    ChromaFixture f;
    ChromaKey key;  // 绿幕
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    ChromaFactory factory;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                NC_CHECK_EQ(frame.width, 64);
                NC_CHECK_EQ(frame.height, 48);
                // 上半绿幕区 → 单层走 Background：合成为序列背景色（默认不透明黑）
                const std::size_t greenIdx = (4 * 64 + 4) * 4;
                NC_CHECK_EQ(frame.rgba[greenIdx], std::uint8_t(0));
                NC_CHECK_EQ(frame.rgba[greenIdx + 1], std::uint8_t(0));
                NC_CHECK_EQ(frame.rgba[greenIdx + 2], std::uint8_t(0));
                NC_CHECK_EQ(frame.rgba[greenIdx + 3], std::uint8_t(255));
                // 下半肤色区保留
                const std::size_t skinIdx = (40 * 64 + 32) * 4;
                NC_CHECK_EQ(frame.rgba[skinIdx], std::uint8_t(220));
                NC_CHECK_EQ(frame.rgba[skinIdx + 1], std::uint8_t(170));
                NC_CHECK_EQ(frame.rgba[skinIdx + 2], std::uint8_t(140));
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(checked > 0);
}

NC_TEST(ChromaPipeline, PreviewAppliesChromaKey) {
    ChromaFixture f;
    ChromaKey key;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());
    ChromaFactory factory;
    PreviewRenderer preview(f.project, factory);
    const PreviewFrame frame = preview.renderAt(Rational::make(1, 2));
    NC_CHECK_EQ(frame.width, 64);
    NC_CHECK_EQ(frame.height, 48);
    // 绿幕区 → 背景色（预览 Background 模式）
    const std::size_t greenIdx = (4 * 64 + 4) * 4;
    NC_CHECK_EQ(frame.rgba[greenIdx], std::uint8_t(0));
    NC_CHECK_EQ(frame.rgba[greenIdx + 1], std::uint8_t(0));
    // 肤色区保留
    const std::size_t skinIdx = (40 * 64 + 32) * 4;
    NC_CHECK_EQ(frame.rgba[skinIdx], std::uint8_t(220));
}

NC_TEST(ChromaPipeline, SerializationRoundtrip) {
    ChromaFixture f;
    ChromaKey key;
    key.keyColorRgb = 0x0000FF;
    key.innerThreshold = 0.25;
    key.outerThreshold = 0.80;
    key.spillSuppression = 0.75;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());

    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    const Clip* clip = loaded.sequences[0].tracks[0].findClip(f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(clip->hasChromaKey);
    NC_CHECK_EQ(clip->chromaKey.keyColorRgb, std::uint32_t(0x0000FF));
    NC_CHECK(std::fabs(clip->chromaKey.innerThreshold - 0.25) < 1e-9);
    NC_CHECK(std::fabs(clip->chromaKey.outerThreshold - 0.80) < 1e-9);
    NC_CHECK(std::fabs(clip->chromaKey.spillSuppression - 0.75) < 1e-9);
}

NC_TEST(ChromaPipeline, DefaultOmitsChromaKeyFromFile) {
    ChromaFixture f;
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"chromaKey\"") == std::string::npos);  // 未启用不写入
}

NC_TEST(ChromaPipeline, MalformedChromaKeyDegradesGracefully) {
    // 非法 JSON 抠像参数 → 降级为未启用（工程仍可打开）
    ChromaFixture f;
    const std::string text = saveProjectToNclip(f.project);
    const std::string injected = text.substr(0, text.find("\"clips\"")) +
                                 "\"chromaKey\": {\"innerThreshold\": 0.9, "
                                 "\"outerThreshold\": 0.5}";
    // 直接构造最小工程 JSON（在片段上注入非法抠像块）
    const std::string clipMarker = "\"duration\"";
    const std::size_t pos = text.find(clipMarker);
    std::string broken = text;
    broken.insert(pos, "\"chromaKey\":{\"innerThreshold\":0.9,\"outerThreshold\":0.5},");
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(broken, loaded);
    NC_CHECK(result.result.isOk());  // 仍可打开
    const Clip* clip = loaded.sequences[0].tracks[0].findClip(f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(!clip->hasChromaKey);  // 非法参数降级
    (void)injected;
}

NC_TEST(ChromaPipeline, ChromaKeyCountedInRegenerationImpact) {
    ChromaFixture f;
    ChromaKey key;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, f.trackId, f.clipId, key, true).isOk());
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        f.project, f.seqId, RegenerationScope::WholeProject);
    bool found = false;
    for (const auto& item : report.willLose) {
        if (item.code == "chroma-key") {
            found = true;
            NC_CHECK_EQ(item.count, std::size_t(1));
        }
    }
    NC_CHECK(found);
}

NC_TEST(ChromaPipeline, ExportAppliesChromaKeyMultiLayerComposite) {
    // 双层：底层纯红（背景），上层绿幕+肤色；抠像后绿幕区应透出底层红
    ChromaFixture f;
    // 再添加一个底层轨道与红色素材
    TrackId bottomTrackId;
    NC_CHECK(f.editor.addTrack(f.seqId, TrackType::Video, "V0", &bottomTrackId).isOk());
    // 注意：addTrack 追加到末尾；需要把底层放到 0 位。
    // 简化：使用现有 V1 承载红色素材（另建轨道用于绿幕素材）。
    TrackId topTrackId;
    NC_CHECK(f.editor.addTrack(f.seqId, TrackType::Video, "V2", &topTrackId).isOk());

    // 原 V1 轨道上的片段（greenScreen.mp4：上半绿幕、下半肤色）→ 移到 topTrackId
    NC_CHECK(f.editor.removeClip(f.seqId, f.trackId, f.clipId).isOk());
    // 在 V1（现有轨道，轨道顺序中位于 V2 之前 → 底层）插入红色素材
    MediaAsset red;
    red.id = AssetId{f.project.ids.next()};
    red.path = "/videos/red.mp4";
    red.type = MediaType::Video;
    red.duration = Rational::make(10);
    f.project.assets.push_back(red);
    Clip redClip;
    redClip.asset = red.id;
    redClip.start = Rational::make(0);
    redClip.duration = Rational::make(1);
    ClipId redClipId;
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, redClip, &redClipId).isOk());
    // 在 V2（上层）插入绿幕素材
    Clip topClip;
    topClip.asset = f.assetId;
    topClip.start = Rational::make(0);
    topClip.duration = Rational::make(1);
    ClipId topClipId;
    NC_CHECK(f.editor.insertClip(f.seqId, topTrackId, topClip, &topClipId).isOk());
    // 应用抠像到上层
    ChromaKey key;
    NC_CHECK(f.editor.setClipChromaKey(f.seqId, topTrackId, topClipId, key, true).isOk());

    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    // 验证计划包含两条轨道与抠像快照
    NC_CHECK(plan.tracks.size() >= 2);
    ChromaFactory factory;
    int checked = 0;
    bool compositeVerified = false;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0 && frame.width == 64) {
                // 真实断言（多层合成）：
                //  上半（上层绿幕，被抠除 → alpha 0）→ 应透出底层红色 (220,20,20)；
                //  下半（上层肤色，保留不透明）→ 应为肤色 (220,170,140)（遮挡底层红）。
                const std::size_t greenArea = (4 * 64 + 4) * 4;    // 上层绿幕区
                const std::size_t skinArea = (40 * 64 + 32) * 4;   // 上层肤色区
                const bool greenShowsRed = frame.rgba[greenArea] > 150 &&
                                           frame.rgba[greenArea + 1] < 100 &&
                                           frame.rgba[greenArea + 2] < 100;
                const bool skinKept = frame.rgba[skinArea] > 150 &&
                                      frame.rgba[skinArea + 1] > 140 &&
                                      frame.rgba[skinArea + 2] > 100;
                NC_CHECK(greenShowsRed);  // 绿幕区透出红色底层
                NC_CHECK(skinKept);       // 肤色区保留（不透出）
                compositeVerified = true;
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(checked > 0);
    NC_CHECK(compositeVerified);
}
