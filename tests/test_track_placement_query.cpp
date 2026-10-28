#include "test_framework.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/commands.h"

#include <cstring>
#include <string>

using namespace nairoclip::core;

// =============================================================================
// B2-8 拖放相容性查询
// =============================================================================
//
// UI 要在**拖动过程中**显示「可放/禁止」，因此需要在不构造 Clip、不尝试插入的
// 前提下查询相容性。原先 `trackAcceptsClipType` 藏在 commands.cpp 的匿名
// namespace 里，UI 根本调不到。
//
// 这些用例双向验证：相容表本身正确 + C ABI 映射正确。

namespace {

bool contains(const char* h, const char* n) {
    return h != nullptr && std::strstr(h, n) != nullptr;
}

void freeIfNotNull(char* p) {
    if (p != nullptr) nc_free_string(p);
}

}  // namespace

NC_TEST(PlacementQuery, VideoTrackAcceptsVideoAndImage) {
    NC_CHECK(trackAcceptsClipType(TrackType::Video, MediaType::Video) ==
             PlacementCompat::Allowed);
    NC_CHECK(trackAcceptsClipType(TrackType::Video, MediaType::Image) ==
             PlacementCompat::Allowed);
    // 音频进视频轨 = 黑屏，拒绝
    NC_CHECK(trackAcceptsClipType(TrackType::Video, MediaType::Audio) ==
             PlacementCompat::Rejected);
}

NC_TEST(PlacementQuery, AudioTrackAcceptsAudioAndVideo) {
    NC_CHECK(trackAcceptsClipType(TrackType::Audio, MediaType::Audio) ==
             PlacementCompat::Allowed);
    // 视频进音频轨 = 取声音（音画分离的标准操作）
    NC_CHECK(trackAcceptsClipType(TrackType::Audio, MediaType::Video) ==
             PlacementCompat::Allowed);
    // 图片进音频轨 = 读不出 PCM
    NC_CHECK(trackAcceptsClipType(TrackType::Audio, MediaType::Image) ==
             PlacementCompat::Rejected);
}

NC_TEST(PlacementQuery, SubtitleAndAdjustmentRejectEverything) {
    for (MediaType mt : {MediaType::Video, MediaType::Audio, MediaType::Image}) {
        NC_CHECK(trackAcceptsClipType(TrackType::Subtitle, mt) == PlacementCompat::Rejected);
        NC_CHECK(trackAcceptsClipType(TrackType::Adjustment, mt) == PlacementCompat::Rejected);
    }
}

NC_TEST(PlacementQuery, UnknownMediaIsTolerated) {
    // 未 probe 的素材一律放行，否则「刚导入就拖不进时间轴」
    for (TrackType tt : {TrackType::Video, TrackType::Audio, TrackType::Subtitle,
                         TrackType::Adjustment}) {
        NC_CHECK(trackAcceptsClipType(tt, MediaType::Unknown) ==
                 PlacementCompat::AllowedBecauseUnknown);
    }
}

// ---- C ABI 层 ----

NC_TEST(PlacementQuery, CapiAcceptsMatchesCoreTable) {
    // 0=Video 1=Audio 2=Image 3=Unknown；track: 0=Video 1=Audio 2=Subtitle 3=Adj
    NC_CHECK_EQ(nc_track_accepts_media(0, 0), 1);  // 视频 → 视频轨
    NC_CHECK_EQ(nc_track_accepts_media(0, 2), 1);  // 图片 → 视频轨
    NC_CHECK_EQ(nc_track_accepts_media(0, 1), 0);  // 音频 → 视频轨 ✗
    NC_CHECK_EQ(nc_track_accepts_media(1, 1), 1);  // 音频 → 音频轨
    NC_CHECK_EQ(nc_track_accepts_media(1, 0), 1);  // 视频 → 音频轨（取声音）
    NC_CHECK_EQ(nc_track_accepts_media(1, 2), 0);  // 图片 → 音频轨 ✗
    NC_CHECK_EQ(nc_track_accepts_media(2, 0), 0);  // 任何 → 字幕轨 ✗
    NC_CHECK_EQ(nc_track_accepts_media(3, 0), 0);  // 任何 → 调整图层 ✗
    // Unknown 放行
    NC_CHECK_EQ(nc_track_accepts_media(0, 3), 1);
    NC_CHECK_EQ(nc_track_accepts_media(2, 3), 1);
}

NC_TEST(PlacementQuery, CapiRejectsInvalidEnums) {
    NC_CHECK_EQ(nc_track_accepts_media(9, 0), -1);
    NC_CHECK_EQ(nc_track_accepts_media(0, 9), -1);
    NC_CHECK_EQ(nc_track_accepts_media(-1, -1), -1);
    // 应留下原因（B0-3）
    const char* last = nc_last_error();
    NC_CHECK(last != nullptr);
    NC_CHECK(contains(last, "超出枚举范围"));
}

NC_TEST(PlacementQuery, ReasonExplainsWhyRejected) {
    char* reason = nullptr;
    char* err = nullptr;
    // 音频进视频轨
    NC_CHECK_EQ(nc_track_placement_reason(0, 1, &reason, &err), 0);
    NC_CHECK(reason != nullptr);
    if (reason != nullptr) {
        NC_CHECK(contains(reason, "黑屏"));
        NC_CHECK(contains(reason, "音频"));
        nc_free_string(reason);
        reason = nullptr;
    }
    freeIfNotNull(err);

    // 图片进音频轨
    NC_CHECK_EQ(nc_track_placement_reason(1, 2, &reason, nullptr), 0);
    NC_CHECK(reason != nullptr);
    if (reason != nullptr) {
        NC_CHECK(contains(reason, "声音"));
        nc_free_string(reason);
        reason = nullptr;
    }
}

NC_TEST(PlacementQuery, ReasonForAllowedIsNull) {
    char* reason = nullptr;
    // 相容时不写 reason（保持 NULL）
    NC_CHECK_EQ(nc_track_placement_reason(0, 0, &reason, nullptr), 1);
    NC_CHECK(reason == nullptr);
}

NC_TEST(PlacementQuery, ReasonForUnknownExplainsTolerance) {
    char* reason = nullptr;
    NC_CHECK_EQ(nc_track_placement_reason(0, 3, &reason, nullptr), 1);
    NC_CHECK(reason != nullptr);
    if (reason != nullptr) {
        NC_CHECK(contains(reason, "未知"));
        nc_free_string(reason);
    }
}

NC_TEST(PlacementQuery, ReasonRejectsInvalidEnums) {
    char* err = nullptr;
    NC_CHECK_EQ(nc_track_placement_reason(99, 0, nullptr, &err), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(err != nullptr);
    freeIfNotNull(err);
}

NC_TEST(PlacementQuery, LabelsAreReadable) {
    NC_CHECK(std::string(trackTypeLabel(TrackType::Video)).find("视频") != std::string::npos);
    NC_CHECK(std::string(trackTypeLabel(TrackType::Audio)).find("音频") != std::string::npos);
    NC_CHECK(std::string(trackTypeLabel(TrackType::Subtitle)).find("字幕") != std::string::npos);
    NC_CHECK(std::string(mediaTypeLabel(MediaType::Video)).find("视频") != std::string::npos);
    NC_CHECK(std::string(mediaTypeLabel(MediaType::Image)).find("图片") != std::string::npos);
}

NC_TEST(PlacementQuery, ConsistentWithActualInsert) {
    // 关键一致性：查询说「不可放」时，真的插入也必须失败。
    // 否则 UI 显示禁止但实际能放（或反之），用户会完全失去信任。
    NcEditor* e = nc_editor_create();
    NC_CHECK(e != nullptr);
    // 建一条视频轨 + 一条音频轨
    const uint64_t vidTrack = nc_add_track(e, "V", 0);
    const uint64_t audTrack = nc_add_track(e, "A", 1);
    NC_CHECK(vidTrack != 0);
    NC_CHECK(audTrack != 0);

    // 音频素材
    const uint64_t audioAsset = nc_import_asset(e, "a.wav", "/tmp/a.wav", 1, 5000000);
    NC_CHECK(audioAsset != 0);

    // 查询说音频不能进视频轨
    NC_CHECK_EQ(nc_track_accepts_media(0, 1), 0);
    // 真的插也必须失败
    const uint64_t clipId = nc_insert_clip(e, 0, audioAsset, 0, 1000000);
    NC_CHECK_EQ(clipId, 0u);

    // 查询说音频可以进音频轨
    NC_CHECK_EQ(nc_track_accepts_media(1, 1), 1);
    const uint64_t ok = nc_insert_clip(e, 1, audioAsset, 0, 1000000);
    NC_CHECK(ok != 0u);

    nc_editor_destroy(e);
}
