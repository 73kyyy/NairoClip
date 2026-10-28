#include "test_framework.h"
#include "nairoclip/core/clip.h"
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/template_apply.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

// =============================================================================
// Clip 摇晃字段（补齐 B2-3 的能力缺口）
// =============================================================================
//
// 背景：B2-3 套用模板时，`TemplateSlot::shakeAmplitude` 原本只统计进
// `shakesApplied` 然后在 warnings 里说「片段层无对应字段」。
// 现在 `Clip::shake` 存在了，必须验证整条链路：
//   模板槽位 → Clip::shake → 序列化 → 反序列化 → RenderClip::shake

namespace {

Project makeProject() {
    Project p;
    p.name = "摇晃测试";
    Sequence seq;
    seq.id = SequenceId{1};
    seq.name = "主序列";
    seq.frameRate = fps30();
    p.sequences.push_back(seq);
    p.ids.restore(100);

    MediaAsset a;
    a.id = AssetId{1};
    a.path = "/tmp/m.mp4";
    a.name = "m";
    a.type = MediaType::Video;
    a.duration = fromMicroseconds(10000000);
    p.assets.push_back(a);
    return p;
}

EditTemplate mkShakeTemplate() {
    EditTemplate t;
    t.id = "shake_test";
    t.name = "摇晃测试";
    t.category = "卡点";
    TemplateSlot s;
    s.duration = fromMicroseconds(1000000);
    s.shakeAmplitude = 0.05;
    s.shakeFrequency = 9.0;
    t.slots.push_back(s);
    t.totalDuration = t.slotsDuration();
    return t;
}

}  // namespace

NC_TEST(ClipShake, DefaultHasNoShake) {
    Clip c;
    NC_CHECK(!c.hasShake());  // 默认 amplitude = 0.02，但语义上要显式判断
    // 注意：ShakeParams 默认 amplitude 是 0.02（B1-5 的默认），
    // 但 Clip 上「没有摇晃」应通过 amplitude <= 0 表达。
    // 因此默认构造的 Clip 需要显式归零，否则每个片段都会抖。
    c.shake.amplitude = 0.0;
    NC_CHECK(!c.hasShake());
}

NC_TEST(ClipShake, DefaultClipDoesNotShake) {
    // 关键：默认构造的 Clip 不应自带摇晃（否则所有片段都抖）
    Clip c;
    NC_CHECK(!c.hasShake());
}

NC_TEST(ClipShake, SerializationRoundtrip) {
    Clip c;
    c.shake.trajectory = ShakeTrajectory::Circular;
    c.shake.amplitude = 0.07;
    c.shake.frequency = 12.0;
    c.shake.seed = 42;
    c.shake.decaySeconds = 1.5;
    c.shake.phase = 0.3;
    NC_CHECK(c.hasShake());

    // 通过 Clip 序列化（用 Clip 级 API 间接验证：构造一个工程）
    Project p = makeProject();
    TimelineEditor editor(p);
    TrackId tid;
    NC_CHECK(editor.addTrack(SequenceId{1}, TrackType::Video, "V", &tid).isOk());
    Clip placed = c;
    placed.asset = AssetId{1};
    placed.start = Rational::make(0);
    placed.duration = fromMicroseconds(1000000);
    placed.sourceIn = Rational::make(0);
    NC_CHECK(editor.insertClip(SequenceId{1}, tid, placed).isOk());

    const std::string json = saveProjectToNclip(p);
    NC_CHECK(json.find("\"shake\"") != std::string::npos);
    NC_CHECK(json.find("\"amplitude\"") != std::string::npos);

    Project loaded;
    NC_CHECK(loadProjectFromNclip(json, loaded).result.isOk());
    const Sequence* seq = loaded.mainSequence();
    NC_CHECK(seq != nullptr);
    if (seq == nullptr) return;
    NC_CHECK_EQ(seq->tracks.size(), static_cast<std::size_t>(1));
    if (seq->tracks.empty() || seq->tracks[0].clips.empty()) return;
    const Clip& back = seq->tracks[0].clips[0];
    NC_CHECK(back.hasShake());
    NC_CHECK(back.shake.trajectory == ShakeTrajectory::Circular);
    NC_CHECK(std::fabs(back.shake.amplitude - 0.07) < 1e-9);
    NC_CHECK(std::fabs(back.shake.frequency - 12.0) < 1e-9);
    NC_CHECK_EQ(back.shake.seed, 42u);
    NC_CHECK(std::fabs(back.shake.decaySeconds - 1.5) < 1e-9);
    NC_CHECK(std::fabs(back.shake.phase - 0.3) < 1e-9);
}

NC_TEST(ClipShake, NoShakeOmitsField) {
    Project p = makeProject();
    TimelineEditor editor(p);
    TrackId tid;
    NC_CHECK(editor.addTrack(SequenceId{1}, TrackType::Video, "V", &tid).isOk());
    Clip c;
    c.asset = AssetId{1};
    c.start = Rational::make(0);
    c.duration = fromMicroseconds(1000000);
    c.sourceIn = Rational::make(0);
    c.shake.amplitude = 0.0;  // 显式关闭
    NC_CHECK(editor.insertClip(SequenceId{1}, tid, c).isOk());

    const std::string json = saveProjectToNclip(p);
    NC_CHECK(json.find("\"shake\"") == std::string::npos);
}

NC_TEST(ClipShake, OutOfRangeValuesClampedOnLoad) {
    // 越界值截断而非拒绝（与 gainDb / heightPx 同策略）。
    //
    // 注意：不能用「序列化 → 字符串替换 → 反序列化」构造越界值。
    // 序列化器输出 double 全精度（0.1 → 0.10000000000000001），
    // 按 "amplitude":0.1 查找必然找不到 —— 第一版就是这么写错的。
    // 也不能凭印象手写 JSON：时间字段是 [分子, 分母] 数组而非微秒整数，
    // 外层是 {format, version, project, assets, sequences} 而非扁平结构。
    // 下面是**照着真实序列化输出**改写的最小工程。
    const std::string json = R"({
  "format": "nclip",
  "version": 1,
  "project": {"name": "clamp_test"},
  "assets": [
    {"id": 1, "path": "/tmp/m.mp4", "name": "m", "type": "video",
     "duration": [10, 1], "frameRate": [30, 1]}
  ],
  "sequences": [
    {
      "id": 1,
      "name": "s",
      "frameRate": [30, 1],
      "width": 1080,
      "height": 1920,
      "tracks": [
        {
          "id": 10,
          "type": "video",
          "name": "V",
          "clips": [
            {
              "id": 100,
              "asset": 1,
              "start": [0, 1],
              "duration": [1, 1],
              "sourceIn": [0, 1],
              "shake": {
                "trajectory": 0,
                "amplitude": 9.9,
                "frequency": 999,
                "seed": 0,
                "decay": 99,
                "phase": 0
              }
            }
          ]
        }
      ]
    }
  ]
})";

    Project loaded;
    const NclipLoadResult r = loadProjectFromNclip(json, loaded);
    if (!r.result.isOk()) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         "加载失败: " + r.result.message);
        return;
    }
    const Sequence* seq = loaded.mainSequence();
    NC_CHECK(seq != nullptr);
    if (seq == nullptr || seq->tracks.empty() || seq->tracks[0].clips.empty()) return;
    const Clip& back = seq->tracks[0].clips[0];
    NC_CHECK(back.shake.amplitude <= 0.2);
    NC_CHECK(back.shake.frequency <= 20.0);
    NC_CHECK(back.shake.decaySeconds <= 10.0);
}

NC_TEST(ClipShake, TemplateApplyWritesShake) {
    // 之前 B2-3 只统计不落盘，现在必须真的写进 Clip
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkShakeTemplate();
    std::vector<MaterialAssignment> assigns;
    assigns.push_back(MaterialAssignment{0, AssetId{1}});
    NC_CHECK(validateEditTemplate(t).isOk());

    TemplateApplyResult out;
    const Result r = applyTemplate(editor, SequenceId{1}, t, assigns, out);
    NC_CHECK(r.isOk());
    NC_CHECK_EQ(out.shakesApplied, 1);
    // 不应再有「未落盘」警告
    for (const auto& w : out.warnings) {
        if (w.find("未落盘") != std::string::npos) {
            ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                             "摇晃仍未落盘：" + w);
        }
    }

    const Sequence* seq = p.mainSequence();
    NC_CHECK(seq != nullptr);
    if (seq == nullptr || seq->tracks.empty() || seq->tracks[0].clips.empty()) return;
    const Clip& c = seq->tracks[0].clips[0];
    NC_CHECK(c.hasShake());
    NC_CHECK(std::fabs(c.shake.amplitude - 0.05) < 1e-9);
    NC_CHECK(std::fabs(c.shake.frequency - 9.0) < 1e-9);
}

NC_TEST(ClipShake, TemplateApplyUndoRemovesShake) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkShakeTemplate();
    std::vector<MaterialAssignment> assigns;
    assigns.push_back(MaterialAssignment{0, AssetId{1}});

    const std::string before = saveProjectToNclip(p);
    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assigns, out).isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(saveProjectToNclip(p), before);
}
