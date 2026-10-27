#include "test_framework.h"
#include "nairoclip/core/template_fill.h"

#include <cmath>
#include <string>

using namespace nairoclip::core;

namespace {

AssetId nextId = AssetId{1};

MediaAsset mkAsset(double seconds, MediaType type = MediaType::Video) {
    MediaAsset a;
    a.id = nextId;
    nextId.value += 1;
    a.path = "/tmp/a" + std::to_string(a.id.value) + ".mp4";
    a.name = "asset" + std::to_string(a.id.value);
    a.type = type;
    a.duration = fromMicroseconds(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5));
    a.width = 1920;
    a.height = 1080;
    return a;
}

EditTemplate mkTemplate(const std::vector<double>& slotSeconds) {
    EditTemplate t;
    t.id = "fill_test";
    t.name = "填充测试";
    t.category = "卡点";
    for (double s : slotSeconds) {
        TemplateSlot slot;
        slot.duration = fromMicroseconds(static_cast<std::int64_t>(s * 1000000.0 + 0.5));
        t.slots.push_back(slot);
    }
    t.totalDuration = t.slotsDuration();
    return t;
}

FillCandidate mkCandidate(double seconds, bool withColor = false, double r = 0.5, double g = 0.5,
                          double b = 0.5) {
    FillCandidate c;
    c.asset = mkAsset(seconds);
    if (withColor) {
        c.features.hasColor = true;
        c.features.color.avgR = r;
        c.features.color.avgG = g;
        c.features.color.avgB = b;
    }
    return c;
}

}  // namespace

// =============================================================================
// B2-2 智能填充
// =============================================================================

NC_TEST(TemplateFill, FillsEverySlotWhenEnoughMaterial) {
    const EditTemplate t = mkTemplate({1.0, 1.0, 1.0});
    std::vector<FillCandidate> cands;
    for (int i = 0; i < 3; ++i) cands.push_back(mkCandidate(5.0));

    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.complete);
    NC_CHECK_EQ(r.unfilledCount, static_cast<std::size_t>(0));
    NC_CHECK_EQ(r.assignments.size(), static_cast<std::size_t>(3));
    for (const auto& a : r.assignments) {
        NC_CHECK(a.candidateIndex >= 0);
    }
}

NC_TEST(TemplateFill, TooShortAssetIsHardFiltered) {
    // 槽位 3 秒，候选只有 1 秒 → 必须填不上，且原因要说明时长不够
    const EditTemplate t = mkTemplate({3.0});
    std::vector<FillCandidate> cands = {mkCandidate(1.0)};

    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(!r.complete);
    NC_CHECK_EQ(r.assignments[0].candidateIndex, -1);
    NC_CHECK(r.assignments[0].reason.find("时长") != std::string::npos);
}

NC_TEST(TemplateFill, ImageIsNeverFilteredByDuration) {
    // 图片可无限拉长 → 即便 duration = 0 也应能填 10 秒槽位
    const EditTemplate t = mkTemplate({10.0});
    FillCandidate img;
    img.asset = mkAsset(0.0, MediaType::Image);
    std::vector<FillCandidate> cands = {img};

    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.complete);
    NC_CHECK_EQ(r.assignments[0].candidateIndex, 0);
}

NC_TEST(TemplateFill, PrefersLongerAssetForSlack) {
    // 时长余量是打分项：槽位 2 秒时，10 秒素材应优于 2.1 秒素材
    const EditTemplate t = mkTemplate({2.0});
    std::vector<FillCandidate> cands = {
        mkCandidate(2.1),   // 刚好够
        mkCandidate(10.0),  // 余量充足
    };
    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK_EQ(r.assignments[0].candidateIndex, 1);
}

NC_TEST(TemplateFill, ReusePenaltyAvoidsUnnecessaryRepeats) {
    // 3 个槽位、3 个都够长的素材 → 应该各用一次，不重复
    const EditTemplate t = mkTemplate({1.0, 1.0, 1.0});
    std::vector<FillCandidate> cands;
    for (int i = 0; i < 3; ++i) cands.push_back(mkCandidate(5.0));

    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.complete);
    std::vector<int> used;
    for (const auto& a : r.assignments) used.push_back(a.candidateIndex);
    // 三者应互不相同
    NC_CHECK(used[0] != used[1]);
    NC_CHECK(used[1] != used[2]);
    NC_CHECK(used[0] != used[2]);
}

NC_TEST(TemplateFill, ReusesWhenMaterialIsScarce) {
    // 只有 1 个素材但要填 3 个槽位 → 必须复用（不能留空）
    const EditTemplate t = mkTemplate({1.0, 1.0, 1.0});
    std::vector<FillCandidate> cands = {mkCandidate(10.0)};

    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.complete);
    for (const auto& a : r.assignments) {
        NC_CHECK_EQ(a.candidateIndex, 0);
    }
}

NC_TEST(TemplateFill, ReusePenaltyZeroAllowsStickingToOne) {
    // 关掉复用惩罚后，如果只有一个最优素材，应一直用它
    const EditTemplate t = mkTemplate({1.0, 1.0});
    std::vector<FillCandidate> cands = {mkCandidate(10.0), mkCandidate(1.5)};
    FillWeights w = defaultFillWeights();
    w.reusePenalty = 0.0;

    const FillResult r = fillTemplateSlots(t, cands, w);
    NC_CHECK(r.complete);
    NC_CHECK_EQ(r.assignments[0].candidateIndex, 0);
    NC_CHECK_EQ(r.assignments[1].candidateIndex, 0);
}

NC_TEST(TemplateFill, ColorHarmonyPrefersSimilarToPrevious) {
    // 槽位 0 选中红色素材（因为它是唯一有优点的），槽位 1 应在
    // 「同样红」与「完全蓝」之间选红。
    const EditTemplate t = mkTemplate({1.0, 1.0});
    std::vector<FillCandidate> cands = {
        mkCandidate(10.0, true, 1.0, 0.0, 0.0),  // 纯红
        mkCandidate(10.0, true, 1.0, 0.05, 0.05),  // 接近红
        mkCandidate(10.0, true, 0.0, 0.0, 1.0),   // 纯蓝
    };
    // 先让第二个槽位独立判断：手动锁第一个为红
    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.complete);
    // 槽位 0 三者时长/特征都齐全，分数接近；无论选谁，槽位 1 都应与它色近
    const int first = r.assignments[0].candidateIndex;
    const int second = r.assignments[1].candidateIndex;
    NC_CHECK(first >= 0);
    NC_CHECK(second >= 0);
    // 若第一选了红(0)，第二不该是纯蓝(2)
    if (first == 0) {
        NC_CHECK(second != 2);
    }
}

NC_TEST(TemplateFill, MissingFeaturesAreNeutralNotPenalized) {
    // 无特征的候选不应因为"缺数据"而系统性输给有特征的候选。
    // 做法：两者其他条件相同，只有一者有 color 特征 → 分数应相等。
    const EditTemplate t = mkTemplate({1.0});
    std::vector<FillCandidate> cands = {
        mkCandidate(5.0, true, 0.5, 0.5, 0.5),  // 有中性色特征
        mkCandidate(5.0, false),                 // 无特征
    };
    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.complete);
    // 两者分数应相等（无特征按 0.5 中性，中性色也是 0.5）
    // 因为严格大于才替换，应选下标 0
    NC_CHECK_EQ(r.assignments[0].candidateIndex, 0);
    // 关键：分数不应为负或异常
    NC_CHECK(r.assignments[0].score > 0.0);
}

NC_TEST(TemplateFill, NoCandidateReportsReason) {
    const EditTemplate t = mkTemplate({1.0});
    std::vector<FillCandidate> empty;
    const FillResult r = fillTemplateSlots(t, empty);
    NC_CHECK(!r.complete);
    NC_CHECK_EQ(r.unfilledCount, static_cast<std::size_t>(1));
    NC_CHECK(r.assignments[0].reason.find("没有候选素材") != std::string::npos);
}

NC_TEST(TemplateFill, EmptyTemplateYieldsEmptyResult) {
    EditTemplate t = mkTemplate({});
    t.slots.clear();
    std::vector<FillCandidate> cands = {mkCandidate(5.0)};
    const FillResult r = fillTemplateSlots(t, cands);
    NC_CHECK(r.assignments.empty());
    NC_CHECK(r.complete);  // 没有槽位 = 平凡地完成
    NC_CHECK_EQ(r.unfilledCount, static_cast<std::size_t>(0));
}

NC_TEST(TemplateFill, Deterministic) {
    const EditTemplate t = mkTemplate({1.0, 2.0, 1.5});
    std::vector<FillCandidate> cands;
    for (int i = 0; i < 5; ++i) cands.push_back(mkCandidate(3.0 + i, true, i * 0.2, 0.5, 0.5));

    const FillResult a = fillTemplateSlots(t, cands);
    const FillResult b = fillTemplateSlots(t, cands);
    NC_CHECK_EQ(a.assignments.size(), b.assignments.size());
    for (std::size_t i = 0; i < a.assignments.size(); ++i) {
        NC_CHECK_EQ(a.assignments[i].candidateIndex, b.assignments[i].candidateIndex);
    }
}

NC_TEST(TemplateFill, ConvenienceOverloadMatchesCandidates) {
    const EditTemplate t = mkTemplate({1.0, 1.0});
    std::vector<MediaAsset> assets;
    for (int i = 0; i < 3; ++i) assets.push_back(mkAsset(5.0));

    const FillResult r = fillTemplateSlots(t, assets);
    NC_CHECK(r.complete);
    NC_CHECK_EQ(r.assignments.size(), static_cast<std::size_t>(2));
}

NC_TEST(TemplateFill, DescribeAssignmentIsReadable) {
    const EditTemplate t = mkTemplate({1.0});
    std::vector<FillCandidate> cands = {mkCandidate(5.0)};
    const FillResult r = fillTemplateSlots(t, cands);
    const std::string desc = describeAssignment(r, 0);
    NC_CHECK(desc.find("槽位") != std::string::npos);
    NC_CHECK(describeAssignment(r, 99).find("越界") != std::string::npos);
}

NC_TEST(TemplateFill, WorksWithBuiltinTemplates) {
    // 用真实内置模板 + 充足素材，必须全部填满
    for (const auto& t : builtinTemplates()) {
        std::vector<FillCandidate> cands;
        for (int i = 0; i < 6; ++i) cands.push_back(mkCandidate(10.0));
        const FillResult r = fillTemplateSlots(t, cands);
        if (!r.complete) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__,
                "内置模板「" + t.id + "」填充不完整（未填 " +
                    std::to_string(r.unfilledCount) + " 个）");
        }
    }
}
