// =============================================================================
// 智能成片（多版本）+ 重新生成影响分析 + 参考风格门控测试
//   （M2.25，文档第 7 部分 4.1 / 4.4 / 六 / 七）
// =============================================================================
// 覆盖：
//   · 门控：参考风格（Free 拒绝 + 付费墙 / Pro 放行 / 过期降级）、版本数（Free 1 固定）；
//   · 多版本计划：5 种变体真实差异（镜头时长/转场密度）、目标时长、素材耗尽；
//   · 落盘：单版本（含转场间隙/事务/撤销）、批量（部分失败不阻断）；
//   · 影响分析：真实统计转场/特效/关键帧/蒙版/音频/速度曲线/复合/标记/字幕/锁定；
//   · 重新生成清理：锁定轨道保护、标记/字幕清理、撤销整体回退。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/smart_movie.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

Time secs(double v) { return Rational::make(static_cast<std::int64_t>(v * 1000), 1000); }

/// 构造参考风格画像（3 个镜头，median 2s）。
StyleProfile makeProfile(double shotLen = 2.0) {
    StyleProfile profile;
    std::vector<ShotBoundary> shots;
    for (int i = 0; i < 3; ++i) {
        ShotBoundary s;
        s.start = secs(i * shotLen);
        s.end = secs((i + 1) * shotLen);
        s.motionScore = 0.5;
        s.brightness = 0.5;
        shots.push_back(s);
    }
    profile.rhythm = analyzeRhythm(shots);
    profile.color = analyzeColor(0.5, 0.5, 0.5, 0.5, 0.5);
    profile.sourceNote = "test.mp4";
    return profile;
}

/// 构造镜头时长有差异的画像（p25 < median < p75；用于节奏滑块测试）。
StyleProfile makeVariedProfile() {
    StyleProfile profile;
    std::vector<ShotBoundary> shots;
    const double lengths[] = {1.0, 1.5, 2.0, 3.0, 5.0};  // p25=1.5 median=2 p75=3
    double cursor = 0.0;
    for (const double len : lengths) {
        ShotBoundary s;
        s.start = secs(cursor);
        s.end = secs(cursor + len);
        s.motionScore = 0.5;
        s.brightness = 0.5;
        shots.push_back(s);
        cursor += len;
    }
    profile.rhythm = analyzeRhythm(shots);
    profile.color = analyzeColor(0.5, 0.5, 0.5, 0.5, 0.5);
    return profile;
}

/// 素材段（导入真实素材到工程，保证 traceability 校验通过）。
std::vector<MaterialSegment> makeMaterial(TimelineEditor** editor, Project& project, int count = 3,
                                          double seconds = 10.0) {
    std::vector<MaterialSegment> material;
    for (int i = 0; i < count; ++i) {
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "mat" + std::to_string(i) + ".mp4";
        asset.type = MediaType::Video;
        asset.duration = secs(seconds);
        const AssetId id = editor != nullptr && *editor != nullptr
                               ? (*editor)->importAsset(std::move(asset))
                               : AssetId{};
        MaterialSegment m;
        m.asset = id;
        m.usableDuration = secs(seconds);
        material.push_back(m);
    }
    return material;
}

SmartMovieConfig makeConfig(TimelineEditor** editor, Project& project, int versionCount = 1) {
    SmartMovieConfig config;
    config.versionCount = versionCount;
    config.material = makeMaterial(editor, project);
    config.applyColor = false;  // 简化断言（色调特效单测）
    return config;
}

/// 工程夹具：1 序列 + 视频轨。
struct MovieFixture {
    Project project;
    TimelineEditor* editor = nullptr;
    SequenceId sequenceId;
    TrackId trackId;

    MovieFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        sequenceId = seq.id;
        project.sequences.push_back(std::move(seq));
        editor = new TimelineEditor(project);
        editor->addTrack(sequenceId, TrackType::Video, "V1", &trackId);
    }
    ~MovieFixture() { delete editor; }

    const Track* track() const { return project.findSequence(sequenceId)->findTrack(trackId); }
};

Entitlement freeTier() { return Entitlement{}; }
Entitlement proTier() {
    Entitlement e;
    e.tier = MembershipTier::Pro;
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// 门控
// ---------------------------------------------------------------------------

NC_TEST(SmartMovie, ReferenceStyleRequiresPro) {
    const ReferenceStyleAccess free = referenceStyleAccess(freeTier(), 1000);
    NC_CHECK(!free.allowed);
    NC_CHECK(free.showPaywall);
    NC_CHECK(free.reason.find("🔒") != std::string::npos);
    const ReferenceStyleAccess pro = referenceStyleAccess(proTier(), 1000);
    NC_CHECK(pro.allowed);
    NC_CHECK(!pro.showPaywall);
}

NC_TEST(SmartMovie, ReferenceStyleExpiryDowngrades) {
    Entitlement expired;
    expired.tier = MembershipTier::Pro;
    expired.expiresAtUnix = 500;  // 已过期
    NC_CHECK(!referenceStyleAccess(expired, 1000).allowed);
    NC_CHECK(referenceStyleAccess(expired, 400).allowed);  // 到期前可用
    // 永久买断不受时间影响
    Entitlement lifetime;
    lifetime.tier = MembershipTier::Pro;
    lifetime.lifetime = true;
    NC_CHECK(referenceStyleAccess(lifetime, 999999999).allowed);
}

NC_TEST(SmartMovie, VersionCountGate) {
    // Free：请求 1 → 允许（1 个）
    const VersionCountAccess free1 = versionCountAccess(freeTier(), 1000, 1);
    NC_CHECK(free1.allowed);
    NC_CHECK_EQ(free1.allowedCount, 1);
    NC_CHECK(!free1.showPaywall);
    // Free：请求 3 → 拒绝 + 付费墙
    const VersionCountAccess free3 = versionCountAccess(freeTier(), 1000, 3);
    NC_CHECK(!free3.allowed);
    NC_CHECK_EQ(free3.allowedCount, 1);
    NC_CHECK(free3.showPaywall);
    NC_CHECK_EQ(free3.requestedCount, 3);
    // Pro：请求 3 → 3
    NC_CHECK_EQ(versionCountAccess(proTier(), 1000, 3).allowedCount, 3);
    // Pro：请求 9 → 钳制到 5
    NC_CHECK_EQ(versionCountAccess(proTier(), 1000, 9).allowedCount, 5);
    // 请求 <= 0 按 1 处理
    NC_CHECK_EQ(versionCountAccess(freeTier(), 1000, 0).requestedCount, 1);
    // 过期 Pro → 按 Free 处理
    Entitlement expired;
    expired.tier = MembershipTier::Pro;
    expired.expiresAtUnix = 500;
    NC_CHECK(!versionCountAccess(expired, 1000, 2).allowed);
}

// ---------------------------------------------------------------------------
// 多版本计划
// ---------------------------------------------------------------------------

NC_TEST(SmartMovie, RejectsInvalidInput) {
    Project project;
    SmartMovieConfig config = makeConfig(nullptr, project);
    // 无效画像
    SmartMoviePlan plan = planSmartMovie(StyleProfile{}, config);
    NC_CHECK(!plan.valid());
    NC_CHECK(!plan.warnings.empty());
    // 无效配置（无素材）
    SmartMovieConfig noMaterial = config;
    noMaterial.material.clear();
    plan = planSmartMovie(makeProfile(), noMaterial);
    NC_CHECK(!plan.valid());
    // 版本数越界
    SmartMovieConfig badVersions = config;
    badVersions.versionCount = 7;
    NC_CHECK(!planSmartMovie(makeProfile(), badVersions).valid());
}

NC_TEST(SmartMovie, SingleVersionReference) {
    Project project;
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), makeConfig(nullptr, project, 1));
    NC_CHECK(plan.valid());
    NC_CHECK_EQ(plan.versions.size(), std::size_t(1));
    const MovieVersionPlan& v = plan.versions[0];
    NC_CHECK_EQ(v.index, 1);
    NC_CHECK_EQ(v.variant, MovieVariant::Reference);
    NC_CHECK(v.ok);
    // 参考节奏 3 镜头 → 3 片段
    NC_CHECK_EQ(v.cuts.size(), std::size_t(3));
    // 镜头时长 = median = 2s
    NC_CHECK(v.shotDuration.toDouble() > 1.99 && v.shotDuration.toDouble() < 2.01);
    NC_CHECK(v.totalDuration.toDouble() > 5.99 && v.totalDuration.toDouble() < 6.01);
    NC_CHECK(v.label.find("忠实参考") != std::string::npos);
}

NC_TEST(SmartMovie, FiveVersionsHaveRealDifferences) {
    Project project;
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), makeConfig(nullptr, project, 5));
    NC_CHECK(plan.valid());
    NC_CHECK_EQ(plan.versions.size(), std::size_t(5));
    NC_CHECK_EQ(plan.okCount(), std::size_t(5));
    // 变体顺序
    NC_CHECK_EQ(plan.versions[0].variant, MovieVariant::Reference);
    NC_CHECK_EQ(plan.versions[1].variant, MovieVariant::Fast);
    NC_CHECK_EQ(plan.versions[2].variant, MovieVariant::Relaxed);
    NC_CHECK_EQ(plan.versions[3].variant, MovieVariant::Dynamic);
    NC_CHECK_EQ(plan.versions[4].variant, MovieVariant::Calm);
    // 真实差异：镜头时长 Fast < Reference < Relaxed
    const double fast = plan.versions[1].shotDuration.toDouble();
    const double ref = plan.versions[0].shotDuration.toDouble();
    const double relaxed = plan.versions[2].shotDuration.toDouble();
    NC_CHECK(fast < ref);
    NC_CHECK(relaxed > ref);
    // 转场密度差异：Dynamic 每对都加 > Fast（每第 3 对）
    NC_CHECK(plan.versions[3].transitionCount >= plan.versions[1].transitionCount);
    // 总时长差异（更短的镜头 → 更短的总时长，相同镜头数）
    NC_CHECK(plan.versions[1].totalDuration < plan.versions[0].totalDuration);
    NC_CHECK(plan.versions[2].totalDuration > plan.versions[0].totalDuration);
    // 每版本标签唯一
    NC_CHECK(plan.versions[0].label != plan.versions[1].label);
    NC_CHECK(plan.summary.find("5 个版本") != std::string::npos);
}

NC_TEST(SmartMovie, PaceFactorAffectsShotDuration) {
    Project project;
    // 滑块 0.9（更快）→ 接近 p25；0.1（更慢）→ 接近 p75
    // （使用镜头时长有差异的画像：p25=1.5 / median=2 / p75=3）
    const StyleProfile varied = makeVariedProfile();
    SmartMovieConfig fast = makeConfig(nullptr, project, 1);
    fast.paceFactor = 0.9;
    SmartMovieConfig slow = makeConfig(nullptr, project, 1);
    slow.paceFactor = 0.1;
    const double fastShot = planSmartMovie(varied, fast).versions[0].shotDuration.toDouble();
    const double slowShot = planSmartMovie(varied, slow).versions[0].shotDuration.toDouble();
    NC_CHECK(fastShot < slowShot);
    // 忠实档 = median
    SmartMovieConfig faithful = makeConfig(nullptr, project, 1);
    faithful.paceFactor = 0.5;
    const double midShot = planSmartMovie(varied, faithful).versions[0].shotDuration.toDouble();
    NC_CHECK(fastShot < midShot);
    NC_CHECK(midShot < slowShot);
}

NC_TEST(SmartMovie, TargetDurationConstrainsShotCount) {
    Project project;
    SmartMovieConfig config = makeConfig(nullptr, project, 1);
    config.targetSeconds = 8.0;  // 8s / 2s = 4 个镜头
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    NC_CHECK(plan.valid());
    NC_CHECK_EQ(plan.versions[0].cuts.size(), std::size_t(4));
}

NC_TEST(SmartMovie, MaterialExhaustionSkipsVersions) {
    Project project;
    // 只给 1 秒素材 → 舒缓慢速版（2.8s/镜头）仍能用剩余收尾，但过短的可跳过
    SmartMovieConfig config;
    config.versionCount = 5;
    config.material = makeMaterial(nullptr, project, 1, 0.3);  // 0.3 秒素材
    const SmartMoviePlan plan = planSmartMovie(makeProfile(5.0), config);
    // 素材只够 1 个片段（用剩余收尾）→ 每版本 1 片段
    if (plan.valid()) {
        NC_CHECK(plan.versions[0].cuts.size() >= 1);
        // 总时长不超过素材容量
        NC_CHECK(plan.versions[0].totalDuration.toDouble() <= 0.31);
    }
    // 完全无素材 → 无效
    SmartMovieConfig empty = config;
    empty.material.clear();
    NC_CHECK(!planSmartMovie(makeProfile(5.0), empty).valid());
}

NC_TEST(SmartMovie, TransitionPlanRespectsMaxCap) {
    Project project;
    SmartMovieConfig config = makeConfig(nullptr, project, 1);
    config.maxTransitionsPerVersion = 1;  // 上限 1
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    NC_CHECK(plan.valid());
    NC_CHECK(plan.versions[0].transitionCount <= 1);
}

// ---------------------------------------------------------------------------
// 落盘
// ---------------------------------------------------------------------------

NC_TEST(SmartMovie, ApplySingleVersion) {
    MovieFixture fx;
    const SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 1);
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    const SmartMovieApplyResult applied = applySmartMovieVersion(
        *fx.editor, fx.project, fx.sequenceId, fx.trackId, plan.versions[0], config);
    NC_CHECK(applied.result.isOk());
    NC_CHECK_EQ(applied.clipsInserted, std::size_t(3));
    const Track* track = fx.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(3));
    // 片段无缝（无转场间隙版本：Reference 有转场 → 有间隙）
    // Reference 版本隔对加转场 → 第 1 对有空隙、第 2 对无
    NC_CHECK(track->clips[0].start.isZero());
    // 撤销整体回退
    NC_CHECK(fx.editor->undo().isOk());
    NC_CHECK_EQ(fx.track()->clips.size(), std::size_t(0));
    // 重做
    NC_CHECK(fx.editor->redo().isOk());
    NC_CHECK_EQ(fx.track()->clips.size(), std::size_t(3));
    // 历史名
    const auto names = fx.editor->undoHistoryNames();
    NC_CHECK(!names.empty());
    NC_CHECK(names.back().find("智能成片") != std::string::npos);
}

NC_TEST(SmartMovie, ApplyWithTransitionsCreatesRealGapsAndTransitions) {
    MovieFixture fx;
    SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 1);
    config.applyTransitions = true;
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    const MovieVersionPlan& v = plan.versions[0];
    NC_CHECK(v.transitionCount >= 1);
    const SmartMovieApplyResult applied = applySmartMovieVersion(
        *fx.editor, fx.project, fx.sequenceId, fx.trackId, v, config);
    NC_CHECK(applied.result.isOk());
    NC_CHECK_EQ(applied.transitionsAdded, v.transitionCount);
    const Track* track = fx.track();
    NC_CHECK_EQ(track->transitions.size(), v.transitionCount);
    // 验证间隙真实存在（片段对之间）
    if (track->clips.size() >= 2) {
        bool foundGap = false;
        for (std::size_t i = 0; i + 1 < track->clips.size(); ++i) {
            const Time gap = track->clips[i + 1].start - track->clips[i].end();
            if (gap.isPositive()) {
                foundGap = true;
                // 转场时长与间隙一致
                NC_CHECK(gap.toDouble() > 0.19 && gap.toDouble() < 0.31);
            }
        }
        NC_CHECK(foundGap);
    }
}

NC_TEST(SmartMovie, ApplyRejectsNonEmptyTrack) {
    MovieFixture fx;
    // 放一个片段占位
    MediaAsset asset;
    asset.id = AssetId{fx.project.ids.next()};
    asset.name = "a.mp4";
    asset.type = MediaType::Video;
    asset.duration = secs(10);
    const AssetId assetId = fx.editor->importAsset(std::move(asset));
    Clip clip;
    clip.id = ClipId{fx.project.ids.next()};
    clip.asset = assetId;
    clip.start = Time{};
    clip.duration = secs(1);
    fx.editor->insertClip(fx.sequenceId, fx.trackId, std::move(clip));
    const SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 1);
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    const SmartMovieApplyResult applied = applySmartMovieVersion(
        *fx.editor, fx.project, fx.sequenceId, fx.trackId, plan.versions[0], config);
    NC_CHECK(!applied.result.isOk());
    NC_CHECK(applied.result.message.find("非空") != std::string::npos);
}

NC_TEST(SmartMovie, ApplyRejectsInvalidInputs) {
    MovieFixture fx;
    const SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 1);
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    // 不存在的轨道
    NC_CHECK(!applySmartMovieVersion(*fx.editor, fx.project, fx.sequenceId, TrackId{9999},
                                     plan.versions[0], config)
                  .result.isOk());
    // 锁定轨道
    fx.editor->setTrackLocked(fx.sequenceId, fx.trackId, true);
    NC_CHECK(!applySmartMovieVersion(*fx.editor, fx.project, fx.sequenceId, fx.trackId,
                                     plan.versions[0], config)
                  .result.isOk());
    fx.editor->setTrackLocked(fx.sequenceId, fx.trackId, false);
    // 无效版本计划
    MovieVersionPlan invalid;
    NC_CHECK(!applySmartMovieVersion(*fx.editor, fx.project, fx.sequenceId, fx.trackId, invalid,
                                     config)
                  .result.isOk());
}

NC_TEST(SmartMovie, BatchApplyWithPartialFailure) {
    MovieFixture fx;
    // 追加两条轨道（共 3 条）
    TrackId v2, v3;
    fx.editor->addTrack(fx.sequenceId, TrackType::Video, "V2", &v2);
    fx.editor->addTrack(fx.sequenceId, TrackType::Video, "V3", &v3);
    const SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 3);
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    NC_CHECK_EQ(plan.okCount(), std::size_t(3));
    // 只提供 2 条轨道 → 第 3 个版本失败（文档「已成功生成的版本不受影响」）
    const SmartMovieBatchResult batch = applySmartMovieVersions(
        *fx.editor, fx.project, fx.sequenceId, {fx.trackId, v2}, plan, config);
    NC_CHECK(batch.result.isOk());
    NC_CHECK_EQ(batch.versionsApplied, std::size_t(2));
    NC_CHECK_EQ(batch.failedVersionIndexes.size(), std::size_t(1));
    NC_CHECK_EQ(batch.failedVersionIndexes[0], 3);
    NC_CHECK(batch.partialSuccess());
    // 前两条轨道已有内容
    NC_CHECK(!fx.project.findSequence(fx.sequenceId)->tracks[0].clips.empty());
    NC_CHECK(!fx.project.findSequence(fx.sequenceId)->tracks[1].clips.empty());
    NC_CHECK(fx.project.findSequence(fx.sequenceId)->tracks[2].clips.empty());
}

NC_TEST(SmartMovie, BatchApplyAllSucceed) {
    MovieFixture fx;
    TrackId v2, v3, v4, v5;
    fx.editor->addTrack(fx.sequenceId, TrackType::Video, "V2", &v2);
    fx.editor->addTrack(fx.sequenceId, TrackType::Video, "V3", &v3);
    fx.editor->addTrack(fx.sequenceId, TrackType::Video, "V4", &v4);
    fx.editor->addTrack(fx.sequenceId, TrackType::Video, "V5", &v5);
    SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 5);
    config.applyTransitions = true;
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    const SmartMovieBatchResult batch = applySmartMovieVersions(
        *fx.editor, fx.project, fx.sequenceId, {fx.trackId, v2, v3, v4, v5}, plan, config);
    NC_CHECK(batch.result.isOk());
    NC_CHECK_EQ(batch.versionsApplied, std::size_t(5));
    NC_CHECK(batch.failedVersionIndexes.empty());
    // 5 条轨道各有内容；转场数量符合变体差异
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    NC_CHECK_EQ(seq->tracks[0].transitions.size(), plan.versions[0].transitionCount);
    NC_CHECK_EQ(seq->tracks[3].transitions.size(), plan.versions[3].transitionCount);
    NC_CHECK(seq->tracks[3].transitions.size() >= seq->tracks[1].transitions.size());
}

NC_TEST(SmartMovie, ApplyColorEffects) {
    MovieFixture fx;
    StyleProfile profile = makeProfile(2.0);
    profile.color = analyzeColor(0.7, 0.4, 0.3, 0.8, 0.4);  // 暖色高饱和 → 应有特效
    SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 1);
    config.applyColor = true;
    config.color = profile.color;
    const SmartMoviePlan plan = planSmartMovie(profile, config);
    const SmartMovieApplyResult applied = applySmartMovieVersion(
        *fx.editor, fx.project, fx.sequenceId, fx.trackId, plan.versions[0], config);
    NC_CHECK(applied.result.isOk());
    NC_CHECK(applied.effectsAdded > 0);  // 真实色调 → 有特效
    // 特效真实落在片段上
    const Track* track = fx.track();
    std::size_t totalEffects = 0;
    for (const auto& clip : track->clips) {
        totalEffects += clip.effects.effects().size();
    }
    NC_CHECK_EQ(totalEffects, applied.effectsAdded);
}

// ---------------------------------------------------------------------------
// 重新生成影响分析
// ---------------------------------------------------------------------------

namespace {

/// 构造含各类编辑痕迹的工程（用于影响分析）。
struct ImpactFixture {
    Project project;
    TimelineEditor* editor = nullptr;
    SequenceId sequenceId;
    TrackId videoTrack;
    TrackId lockedTrack;

    ImpactFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        sequenceId = seq.id;
        project.sequences.push_back(std::move(seq));
        editor = new TimelineEditor(project);
        editor->addTrack(sequenceId, TrackType::Video, "V1", &videoTrack);
        editor->addTrack(sequenceId, TrackType::Audio, "A1", &lockedTrack);
        // 视频轨：2 片段 + 1 转场 + 特效 + 关键帧 + 蒙版
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "a.mp4";
        asset.type = MediaType::Video;
        asset.duration = secs(20);
        const AssetId assetId = editor->importAsset(std::move(asset));
        Clip a;
        a.id = ClipId{project.ids.next()};
        a.asset = assetId;
        a.start = Time{};
        a.duration = secs(2);
        editor->insertClip(sequenceId, videoTrack, std::move(a));
        Clip b;
        b.id = ClipId{project.ids.next()};
        b.asset = assetId;
        b.start = secs(2);
        b.duration = secs(2);
        ClipId bId;
        editor->insertClip(sequenceId, videoTrack, std::move(b), &bId);
        const Track* track = project.findSequence(sequenceId)->findTrack(videoTrack);
        const ClipId aId = track->clips[0].id;
        // 转场
        Transition tr;
        tr.id = TransitionId{project.ids.next()};
        tr.type = TransitionType::CrossDissolve;
        tr.duration = secs(0.3);
        tr.fromClip = aId;
        tr.toClip = bId;
        editor->addTransition(sequenceId, videoTrack, tr);
        // 特效
        Effect fx;
        fx.id = EffectId{project.ids.next()};
        fx.type = EffectType::Brightness;
        fx.value = 0.2;
        editor->addEffect(sequenceId, videoTrack, aId, fx);
        // 蒙版（真实形状；默认 Mask{} 为 None 不启用）
        Mask mask;
        mask.shape = MaskShape::Linear;
        mask.feather = 0.1;
        editor->setClipMask(sequenceId, videoTrack, aId, mask);
        // 音频轨：增益 + 锁定
        Clip audio;
        audio.id = ClipId{project.ids.next()};
        audio.asset = assetId;
        audio.start = Time{};
        audio.duration = secs(4);
        audio.audioGainDb = -6.0;
        editor->insertClip(sequenceId, lockedTrack, std::move(audio));
        editor->setTrackLocked(sequenceId, lockedTrack, true);
        // 标记 + 字幕
        Marker marker;
        marker.id = MarkerId{project.ids.next()};
        marker.time = secs(1);
        marker.name = "待调";
        marker.color = "#FF8A5B";
        editor->upsertMarker(sequenceId, marker);
        SubtitleCue cue;
        cue.id = SubtitleId{project.ids.next()};
        cue.start = Time{};
        cue.duration = secs(2);
        cue.text = "你好";
        Sequence* seqPtr = project.findSequence(sequenceId);
        seqPtr->subtitles.insert(cue);
    }
    ~ImpactFixture() { delete editor; }
};

}  // namespace

NC_TEST(SmartMovie, ImpactAnalysisCountsRealState) {
    ImpactFixture fx;
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        fx.project, fx.sequenceId, RegenerationScope::WholeProject);
    // 受影响轨道：视频轨（音频轨锁定 → 保护）
    NC_CHECK_EQ(report.affectedTracks, std::size_t(1));
    NC_CHECK_EQ(report.protectedTracks, std::size_t(1));
    // 找各类计数
    auto countOf = [&](const std::string& code) -> std::size_t {
        for (const auto& item : report.willLose) {
            if (item.code == code) {
                return item.count;
            }
        }
        return 0;
    };
    NC_CHECK_EQ(countOf("transitions"), std::size_t(1));
    NC_CHECK_EQ(countOf("effects"), std::size_t(1));
    NC_CHECK_EQ(countOf("masks"), std::size_t(1));
    NC_CHECK_EQ(countOf("markers"), std::size_t(1));
    NC_CHECK_EQ(countOf("subtitles"), std::size_t(1));
    // 锁定轨道 → 保留项
    bool hasLocked = false;
    for (const auto& item : report.willKeep) {
        if (item.code == "locked-tracks") {
            hasLocked = true;
            NC_CHECK_EQ(item.count, std::size_t(1));
        }
    }
    NC_CHECK(hasLocked);
    NC_CHECK(report.hasLosses());
    NC_CHECK(report.lossCount() >= 5);
    // 弹窗清单文案
    const auto lines = report.detailLines();
    NC_CHECK(!lines.empty());
    NC_CHECK(lines[0].find("将会丢失") != std::string::npos);
}

NC_TEST(SmartMovie, ImpactAnalysisScopeSequenceKeepsSubtitles) {
    ImpactFixture fx;
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        fx.project, fx.sequenceId, RegenerationScope::Sequence);
    // scope=Sequence：字幕不在丢失清单
    for (const auto& item : report.willLose) {
        NC_CHECK(item.code != "subtitles");
    }
    NC_CHECK(report.summary.find("当前序列") != std::string::npos);
}

NC_TEST(SmartMovie, ImpactAnalysisMissingSequence) {
    ImpactFixture fx;
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        fx.project, SequenceId{9999}, RegenerationScope::WholeProject);
    NC_CHECK(!report.summary.empty());
    NC_CHECK(!report.hasLosses());
}

// ---------------------------------------------------------------------------
// 重新生成清理
// ---------------------------------------------------------------------------

NC_TEST(SmartMovie, ClearPreservesLockedTrack) {
    ImpactFixture fx;
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        fx.project, fx.sequenceId, RegenerationScope::WholeProject);
    const RegenerationResult cleared = clearForRegeneration(*fx.editor, fx.project,
                                                            fx.sequenceId, report);
    NC_CHECK(cleared.result.isOk());
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    // 锁定轨道内容保留
    NC_CHECK_EQ(seq->findTrack(fx.lockedTrack)->clips.size(), std::size_t(1));
    // 视频轨清空
    NC_CHECK_EQ(seq->findTrack(fx.videoTrack)->clips.size(), std::size_t(0));
    // 转场/标记/字幕清空
    NC_CHECK_EQ(seq->findTrack(fx.videoTrack)->transitions.size(), std::size_t(0));
    NC_CHECK_EQ(seq->markers.size(), std::size_t(0));
    NC_CHECK_EQ(seq->subtitles.size(), std::size_t(0));
    NC_CHECK_EQ(cleared.tracksProtected, std::size_t(1));
    NC_CHECK_EQ(cleared.markersCleared, std::size_t(1));
    NC_CHECK_EQ(cleared.subtitlesCleared, std::size_t(1));
    // 撤销 → 完整还原
    NC_CHECK(fx.editor->undo().isOk());
    const Sequence* restored = fx.project.findSequence(fx.sequenceId);
    NC_CHECK_EQ(restored->findTrack(fx.videoTrack)->clips.size(), std::size_t(2));
    NC_CHECK_EQ(restored->findTrack(fx.videoTrack)->transitions.size(), std::size_t(1));
    NC_CHECK_EQ(restored->markers.size(), std::size_t(1));
    NC_CHECK_EQ(restored->subtitles.size(), std::size_t(1));
}

NC_TEST(SmartMovie, ClearScopeSequenceKeepsSubtitles) {
    ImpactFixture fx;
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        fx.project, fx.sequenceId, RegenerationScope::Sequence);
    const RegenerationResult cleared = clearForRegeneration(*fx.editor, fx.project,
                                                            fx.sequenceId, report);
    NC_CHECK(cleared.result.isOk());
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    // scope=Sequence：字幕保留
    NC_CHECK_EQ(seq->subtitles.size(), std::size_t(1));
    NC_CHECK_EQ(cleared.subtitlesCleared, std::size_t(0));
}

NC_TEST(SmartMovie, ClearMissingSequenceFails) {
    ImpactFixture fx;
    RegenerationImpactReport report;
    const RegenerationResult cleared = clearForRegeneration(*fx.editor, fx.project,
                                                            SequenceId{9999}, report);
    NC_CHECK(!cleared.result.isOk());
}

NC_TEST(SmartMovie, RegenerationWorkflowEndToEnd) {
    // 完整闭环：分析 → 清理 → 重新生成（清除后轨道可重新填充）
    ImpactFixture fx;
    const RegenerationImpactReport report = analyzeRegenerationImpact(
        fx.project, fx.sequenceId, RegenerationScope::WholeProject);
    const RegenerationResult cleared = clearForRegeneration(*fx.editor, fx.project,
                                                            fx.sequenceId, report);
    NC_CHECK(cleared.result.isOk());
    // 重新生成：plan + apply 到已清空的视频轨（素材在清理前先准备好，
    // 避免额外导入素材命令干扰撤销步数断言）
    const SmartMovieConfig config = makeConfig(&fx.editor, fx.project, 1);
    const SmartMoviePlan plan = planSmartMovie(makeProfile(2.0), config);
    const SmartMovieApplyResult applied = applySmartMovieVersion(
        *fx.editor, fx.project, fx.sequenceId, fx.videoTrack, plan.versions[0], config);
    NC_CHECK(applied.result.isOk());
    NC_CHECK_EQ(fx.project.findSequence(fx.sequenceId)
                    ->findTrack(fx.videoTrack)
                    ->clips.size(),
                std::size_t(3));
    // 回退生成（最近一条历史）
    NC_CHECK(fx.editor->undo().isOk());
    NC_CHECK_EQ(fx.project.findSequence(fx.sequenceId)
                    ->findTrack(fx.videoTrack)
                    ->clips.size(),
                std::size_t(0));
    // 继续回退：清理前的导入素材命令（每素材一条）
    // → 跳过导入命令，直接回退到清理事务
    while (fx.editor->undoCount() > 0 &&
           fx.project.findSequence(fx.sequenceId)->findTrack(fx.videoTrack)->clips.size() == 0) {
        const std::size_t before = fx.editor->undoCount();
        NC_CHECK(fx.editor->undo().isOk());
        if (fx.editor->undoCount() == before) {
            break;  // 防御：无进展
        }
        // 清理事务被回退后片段恢复（导入素材命令还会继续占用历史）
        const auto clips = fx.project.findSequence(fx.sequenceId)->findTrack(fx.videoTrack)->clips.size();
        if (clips > 0) {
            break;
        }
    }
    NC_CHECK_EQ(fx.project.findSequence(fx.sequenceId)
                    ->findTrack(fx.videoTrack)
                    ->clips.size(),
                std::size_t(2));
}
