// 数据模型不变量测试：Clip / Track / Sequence。
//
// 为什么单独测这三者：
//   `Track::invariantsHold()` / `hasOverlap()` / `Sequence::duration()` 是
//   全库最底层的不变量守卫——一旦失效，症状不是崩溃，而是**成片错位**
//   （片段重叠、时长算短、导出丢帧），排查成本极高。
//   此前 tests/ 下没有任何 test_clip.cpp / test_track.cpp / test_sequence.cpp，
//   这些不变量仅被编辑器测试间接覆盖，路径不全。
//
// 覆盖：正例（合法状态）与反例（必须被拒绝的非法状态）成对出现。

#include "test_framework.h"

#include "nairoclip/core/clip.h"
#include "nairoclip/core/sequence.h"
#include "nairoclip/core/track.h"

using namespace nairoclip::core;

namespace {

/// 构造一个满足 `Clip::valid()` 的最小片段。
Clip makeValidClip(ClipId id, const Time& start, const Time& duration) {
    Clip c;
    c.id = id;
    c.start = start;
    c.duration = duration;
    c.sourceIn = Rational::make(0);
    c.speed = Rational::make(1);
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// Clip
// ---------------------------------------------------------------------------

NC_TEST(ModelInvariants, ClipValidAcceptsWellFormed) {
    const Clip c = makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5));
    NC_CHECK(c.valid());
    NC_CHECK(c.end() == Rational::make(5));
}

NC_TEST(ModelInvariants, ClipEndIsStartPlusDuration) {
    const Clip c = makeValidClip(ClipId{1}, Rational::make(3), Rational::make(7));
    NC_CHECK(c.end() == Rational::make(10));
}

NC_TEST(ModelInvariants, ClipRejectsZeroDuration) {
    const Clip c = makeValidClip(ClipId{1}, Rational::make(0), Rational::make(0));
    NC_CHECK(!c.valid());  // duration 必须为正
}

NC_TEST(ModelInvariants, ClipRejectsNegativeDuration) {
    const Clip c = makeValidClip(ClipId{1}, Rational::make(0), Rational::make(-1));
    NC_CHECK(!c.valid());
}

NC_TEST(ModelInvariants, ClipRejectsNegativeStart) {
    const Clip c = makeValidClip(ClipId{1}, Rational::make(-1), Rational::make(5));
    NC_CHECK(!c.valid());
}

NC_TEST(ModelInvariants, ClipRejectsNonPositiveSpeed) {
    Clip c = makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5));
    c.speed = Rational::make(0);
    NC_CHECK(!c.valid());

    c.speed = Rational::make(-2);
    NC_CHECK(!c.valid());
}

NC_TEST(ModelInvariants, ClipRejectsNegativeSourceIn) {
    Clip c = makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5));
    c.sourceIn = Rational::make(-1);
    NC_CHECK(!c.valid());
}

// ---------------------------------------------------------------------------
// Track
// ---------------------------------------------------------------------------

NC_TEST(ModelInvariants, TrackEmptyIsValid) {
    Track t;
    NC_CHECK(t.invariantsHold());  // 空轨道合法
    NC_CHECK(t.clips.empty());
    NC_CHECK(t.findClip(ClipId{1}) == nullptr);
    NC_CHECK(t.clipAt(Rational::make(0)) == nullptr);
}

NC_TEST(ModelInvariants, TrackAcceptsOrderedNonOverlappingClips) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(2)));
    t.clips.push_back(makeValidClip(ClipId{2}, Rational::make(2), Rational::make(3)));
    NC_CHECK(t.invariantsHold());
}

NC_TEST(ModelInvariants, TrackAcceptsTouchingClips) {
    // 首尾相接（非重叠）必须合法——这是最常见的正常剪辑状态。
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(2)));
    t.clips.push_back(makeValidClip(ClipId{2}, Rational::make(2), Rational::make(2)));
    NC_CHECK(t.invariantsHold());
}

NC_TEST(ModelInvariants, TrackRejectsOverlappingClips) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5)));
    t.clips.push_back(makeValidClip(ClipId{2}, Rational::make(3), Rational::make(5)));
    NC_CHECK(!t.invariantsHold());  // 3 < 5，重叠
}

NC_TEST(ModelInvariants, TrackRejectsUnsortedClips) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(10), Rational::make(2)));
    t.clips.push_back(makeValidClip(ClipId{2}, Rational::make(0), Rational::make(2)));
    // 排序被破坏时，即使实际不重叠也必须判非法（不变量含"按 start 升序"）。
    NC_CHECK(!t.invariantsHold());
}

NC_TEST(ModelInvariants, TrackRejectsInvalidClip) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(0)));
    NC_CHECK(!t.invariantsHold());  // 零时长片段非法
}

NC_TEST(ModelInvariants, TrackSortClipsRestoresOrder) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{2}, Rational::make(10), Rational::make(2)));
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(2)));
    NC_CHECK(!t.invariantsHold());  // 乱序
    t.sortClips();
    NC_CHECK(t.invariantsHold());  // 排序后恢复
    NC_CHECK(t.clips[0].id == ClipId{1});
    NC_CHECK(t.clips[1].id == ClipId{2});
}

NC_TEST(ModelInvariants, TrackHasOverlapDetectsIntersection) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5)));

    NC_CHECK(t.hasOverlap(Rational::make(1), Rational::make(3)));   // 完全在内
    NC_CHECK(t.hasOverlap(Rational::make(-1), Rational::make(1)));  // 左侧相交
    NC_CHECK(t.hasOverlap(Rational::make(4), Rational::make(10)));  // 右侧相交
    NC_CHECK(t.hasOverlap(Rational::make(-1), Rational::make(10))); // 完全覆盖

    NC_CHECK(!t.hasOverlap(Rational::make(5), Rational::make(8)));  // 首尾相接不算重叠
    NC_CHECK(!t.hasOverlap(Rational::make(-3), Rational::make(0))); // 左侧相邻
}

NC_TEST(ModelInvariants, TrackHasOverlapCanIgnoreSelf) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5)));
    // 忽略自身后，同一区间应判定为不重叠（编辑移动片段时的核心语义）。
    NC_CHECK(!t.hasOverlap(Rational::make(0), Rational::make(5), ClipId{1}));
    // 忽略不存在的 id 不应改变结果
    NC_CHECK(t.hasOverlap(Rational::make(0), Rational::make(5), ClipId{99}));
}

NC_TEST(ModelInvariants, TrackFindClipAndIndexOf) {
    Track t;
    t.clips.push_back(makeValidClip(ClipId{7}, Rational::make(0), Rational::make(2)));
    t.clips.push_back(makeValidClip(ClipId{8}, Rational::make(2), Rational::make(2)));

    NC_CHECK(t.findClip(ClipId{8}) != nullptr);
    NC_CHECK(t.findClip(ClipId{8})->id == ClipId{8});
    NC_CHECK(t.findClip(ClipId{99}) == nullptr);
    NC_CHECK(t.indexOf(ClipId{8}) == 1);
    NC_CHECK(t.indexOf(ClipId{99}) == -1);
}

NC_TEST(ModelInvariants, TrackGainBounds) {
    Track t;
    NC_CHECK(t.gainValid());  // 默认 0 dB
    t.gainDb = -60.0;
    NC_CHECK(t.gainValid());  // 下界
    t.gainDb = 12.0;
    NC_CHECK(t.gainValid());  // 上界
    t.gainDb = -60.01;
    NC_CHECK(!t.gainValid());
    t.gainDb = 12.01;
    NC_CHECK(!t.gainValid());
}

NC_TEST(ModelInvariants, AutoDuckDefaultsAndBounds) {
    AutoDuckSettings s;
    NC_CHECK(!s.enabled);        // 默认关闭
    NC_CHECK(s.valid());         // 默认参数合法
    NC_CHECK(s.duckAmountDb == 12.0);

    s.sensitivity = 1.5;
    NC_CHECK(!s.valid());        // 灵敏度越界
    s.sensitivity = 0.0;
    s.duckAmountDb = 30.0;
    NC_CHECK(!s.valid());        // 压低幅度越界（上限 24dB）
}

NC_TEST(ModelInvariants, AutoDuckVoiceThresholdMapsInversely) {
    AutoDuckSettings s;
    s.sensitivity = 0.0;  // 最低灵敏度 → 最高阈值（最难触发）
    const double low = s.voiceThreshold();
    s.sensitivity = 1.0;  // 最高灵敏度 → 最低阈值（最易触发）
    const double high = s.voiceThreshold();
    NC_CHECK(low > high);
    NC_CHECK(high >= 0.02);   // 阈值下界
    NC_CHECK(low <= 0.20);    // 阈值上界（0.02 + 0.18）
}

// ---------------------------------------------------------------------------
// Sequence
// ---------------------------------------------------------------------------

NC_TEST(ModelInvariants, SequenceEmptyDurationIsZero) {
    Sequence s;
    NC_CHECK(s.duration() == Rational::make(0));
    NC_CHECK(s.invariantsHold());
}

NC_TEST(ModelInvariants, SequenceDurationTakesMaxClipEnd) {
    Sequence s;
    Track a;
    a.id = TrackId{1};
    a.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(3)));
    Track b;
    b.id = TrackId{2};
    b.clips.push_back(makeValidClip(ClipId{2}, Rational::make(0), Rational::make(10)));
    s.tracks.push_back(a);
    s.tracks.push_back(b);
    // 序列时长 = 所有轨道最末片段终点的最大值（不是求和，也不是首条轨道）。
    NC_CHECK(s.duration() == Rational::make(10));
}

NC_TEST(ModelInvariants, SequenceDurationHandlesGap) {
    Sequence s;
    Track t;
    t.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(2)));
    t.clips.push_back(makeValidClip(ClipId{2}, Rational::make(5), Rational::make(1)));
    s.tracks.push_back(t);
    // 中间有 3 秒空隙时，时长仍按最后片段终点算（含空隙）。
    NC_CHECK(s.duration() == Rational::make(6));
}

NC_TEST(ModelInvariants, SequenceFindTrack) {
    Sequence s;
    Track t;
    t.id = TrackId{42};
    t.name = "视频轨";
    s.tracks.push_back(t);

    NC_CHECK(s.findTrack(TrackId{42}) != nullptr);
    NC_CHECK(s.findTrack(TrackId{42})->name == "视频轨");
    NC_CHECK(s.findTrack(TrackId{7}) == nullptr);

    // const 重载
    const Sequence& cs = s;
    NC_CHECK(cs.findTrack(TrackId{42}) != nullptr);
    NC_CHECK(cs.findTrack(TrackId{7}) == nullptr);
}

NC_TEST(ModelInvariants, SequencePropagatesTrackInvariantFailure) {
    Sequence s;
    Track bad;
    bad.clips.push_back(makeValidClip(ClipId{1}, Rational::make(0), Rational::make(5)));
    bad.clips.push_back(makeValidClip(ClipId{2}, Rational::make(1), Rational::make(5)));  // 重叠
    s.tracks.push_back(bad);
    // 单条轨道非法 → 整个序列非法（不变量必须向上传播）。
    NC_CHECK(!s.invariantsHold());
}

NC_TEST(ModelInvariants, SequenceDefaultsAreSane) {
    Sequence s;
    NC_CHECK(s.width == 1080);
    NC_CHECK(s.height == 1920);      // 默认竖屏（短视频场景）
    NC_CHECK(s.frameRate == fps30());
    NC_CHECK(s.frameFit == FrameFit::Fit);
    NC_CHECK(s.masterGainDb == 0.0);
    NC_CHECK(s.tracks.empty());
    NC_CHECK(s.markers.empty());
}
