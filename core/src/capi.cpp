#include "nairoclip/core/capi.h"

#include "nairoclip/core/chapter.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/ai_check.h"
#include "nairoclip/core/auto_edit.h"
#include "nairoclip/core/transition.h"
#include "nairoclip/core/track.h"
#include "nairoclip/core/json.h"
#include "nairoclip/core/export_preset.h"
#include "nairoclip/core/mask.h"
#include "nairoclip/core/speech_cut.h"
#include "nairoclip/core/proxy.h"
#include "nairoclip/core/model_manager.h"
#include "nairoclip/core/serialization.h"

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/export_runner.h"
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#endif

#include <cstring>
#include <ctime>
#include <new>
#include <string>

using namespace nairoclip::core;

// 不透明句柄的真实定义。
struct NcEditor {
    Project project;
    TimelineEditor* editor;  // 堆分配（依赖 project 引用，必须在 project 之后构造）
    /// 会员权益快照（账号级状态；由平台层在服务端校验后经
    /// nc_set_entitlement 更新；默认 Free = fail-closed）。
    Entitlement entitlement;
    /// 代理设置（平台层持久化；默认自动 + 1/2 分辨率）。
    ProxySettings proxySettings;
    /// 章节检测缓存（nc_chapter_detect 结果；nc_chapter_apply 落盘消费）。
    /// 保持「检测 → 预览 → 落盘」两段式，UI 可先展示再决定是否应用。
    ChapterDetectResult chapterCache;
    /// 检测配置（与 chapterCache 配套；标题前缀等）。
    ChapterDetectConfig chapterConfig;
};

namespace {

char* dupString(const std::string& text) {
    char* out = new (std::nothrow) char[text.size() + 1];
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

int32_t toNcError(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok: return NC_OK;
        case ErrorCode::InvalidArgument: return NC_ERROR_INVALID_ARGUMENT;
        case ErrorCode::NotFound: return NC_ERROR_NOT_FOUND;
        case ErrorCode::Overlap: return NC_ERROR_OVERLAP;
        case ErrorCode::OutOfRange: return NC_ERROR_OUT_OF_RANGE;
        case ErrorCode::Locked: return NC_ERROR_LOCKED;
        case ErrorCode::Unsupported: return NC_ERROR_UNSUPPORTED;
        case ErrorCode::Internal: return NC_ERROR_INTERNAL;
        case ErrorCode::Cancelled: return NC_ERROR_CANCELLED;
    }
    return NC_ERROR_INTERNAL;
}

void setDetail(char** detail, const std::string& message) {
    if (detail != nullptr) {
        *detail = dupString(message);
    }
}

}  // namespace

extern "C" {

int32_t nc_abi_version(void) { return NC_ABI_VERSION; }

NcEditor* nc_editor_create(void) {
    NcEditor* handle = new (std::nothrow) NcEditor();
    if (handle == nullptr) {
        return nullptr;
    }
    handle->editor = new (std::nothrow) TimelineEditor(handle->project);
    if (handle->editor == nullptr) {
        delete handle;
        return nullptr;
    }
    // 默认创建一条视频轨与主序列，保证开箱即可编辑。
    Sequence seq;
    seq.id = SequenceId{handle->project.ids.next()};
    handle->project.sequences.push_back(seq);
    return handle;
}

void nc_editor_destroy(NcEditor* editor) {
    if (editor == nullptr) {
        return;
    }
    delete editor->editor;
    delete editor;
}

char* nc_project_save(const NcEditor* editor) {
    if (editor == nullptr) {
        return nullptr;
    }
    return dupString(saveProjectToNclip(editor->project));
}

int32_t nc_project_load(NcEditor* editor, const char* nclip_text, char** detail) {
    if (editor == nullptr || nclip_text == nullptr) {
        setDetail(detail, "参数为空");
        return NC_ERROR_INVALID_ARGUMENT;
    }
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(nclip_text, loaded);
    if (!result.result.isOk()) {
        setDetail(detail, result.result.message);
        return toNcError(result.result.code);
    }
    // 替换工程后必须重建编辑器（引用失效）。
    editor->project = std::move(loaded);
    delete editor->editor;
    editor->editor = new (std::nothrow) TimelineEditor(editor->project);
    if (editor->editor == nullptr) {
        setDetail(detail, "内存分配失败");
        return NC_ERROR_INTERNAL;
    }
    return NC_OK;
}

int64_t nc_project_duration_us(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    return toMicroseconds(main->duration());
}

int32_t nc_track_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(main->tracks.size());
}

int32_t nc_clip_count(const NcEditor* editor, int32_t track_index) {
    if (editor == nullptr || track_index < 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    return static_cast<int32_t>(main->tracks[static_cast<std::size_t>(track_index)].clips.size());
}

uint64_t nc_import_asset(NcEditor* editor, const char* name, const char* path, int32_t type,
                         int64_t duration_us) {
    if (editor == nullptr || name == nullptr || path == nullptr || duration_us < 0) {
        return 0;
    }
    MediaAsset asset;
    asset.name = name;
    asset.path = path;
    switch (type) {
        case 0: asset.type = MediaType::Video; break;
        case 1: asset.type = MediaType::Audio; break;
        case 2: asset.type = MediaType::Image; break;
        default: asset.type = MediaType::Unknown; break;
    }
    asset.duration = fromMicroseconds(duration_us);
    const AssetId id = editor->editor->importAsset(std::move(asset));
    return id.value;
}

uint64_t nc_add_track(NcEditor* editor, const char* name, int32_t track_type) {
    if (editor == nullptr || name == nullptr) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    TrackType type = TrackType::Video;
    switch (track_type) {
        case 0: type = TrackType::Video; break;
        case 1: type = TrackType::Audio; break;
        case 2: type = TrackType::Subtitle; break;
        case 3: type = TrackType::Effect; break;
        default: return 0;
    }
    TrackId id;
    const Result r = editor->editor->addTrack(main->id, type, name, &id);
    if (!r.isOk()) {
        return 0;
    }
    return id.value;
}

uint64_t nc_insert_clip(NcEditor* editor, int32_t track_index, uint64_t asset_id,
                        int64_t start_us, int64_t duration_us) {
    if (editor == nullptr || track_index < 0 || start_us < 0 || duration_us <= 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    Clip clip;
    clip.asset = AssetId{asset_id};
    clip.start = fromMicroseconds(start_us);
    clip.duration = fromMicroseconds(duration_us);
    ClipId outId;
    const Result r = editor->editor->insertClip(main->id, trackId, std::move(clip), &outId);
    if (!r.isOk()) {
        return 0;
    }
    return outId.value;
}

int32_t nc_undo(NcEditor* editor) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(editor->editor->undo().code);
}

int32_t nc_redo(NcEditor* editor) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(editor->editor->redo().code);
}

uint64_t nc_upsert_subtitle(NcEditor* editor, int64_t start_us, int64_t duration_us,
                            const char* text) {
    if (editor == nullptr || text == nullptr || start_us < 0 || duration_us <= 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    SubtitleCue cue;
    cue.id = SubtitleId{editor->project.ids.next()};
    cue.start = fromMicroseconds(start_us);
    cue.duration = fromMicroseconds(duration_us);
    cue.text = text;
    const Result r = editor->editor->upsertSubtitle(main->id, std::move(cue));
    if (!r.isOk()) {
        return 0;
    }
    const SubtitleTrack& subtitles = editor->project.mainSequence()->subtitles;
    // 返回最新插入的字幕 ID（按时间匹配最后一条）
    const SubtitleCue* found = nullptr;
    for (const auto& c : subtitles.cues()) {
        if (c.start == fromMicroseconds(start_us) && c.text == std::string(text)) {
            found = &c;
        }
    }
    return found != nullptr ? found->id.value : 0;
}

int32_t nc_remove_subtitle(NcEditor* editor, uint64_t subtitle_id) {
    if (editor == nullptr || subtitle_id == 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    return toNcError(editor->editor->removeSubtitle(main->id, SubtitleId{subtitle_id}).code);
}

int32_t nc_subtitle_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    return main == nullptr ? 0 : static_cast<int32_t>(main->subtitles.size());
}

int32_t nc_subtitle_get(const NcEditor* editor, int32_t index, int64_t* start_us,
                        int64_t* duration_us, char** out_text) {
    if (editor == nullptr || index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(index) >= main->subtitles.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const SubtitleCue& cue = main->subtitles.cues()[static_cast<std::size_t>(index)];
    if (start_us != nullptr) *start_us = toMicroseconds(cue.start);
    if (duration_us != nullptr) *duration_us = toMicroseconds(cue.duration);
    if (out_text != nullptr) *out_text = dupString(cue.text);
    return NC_OK;
}

uint64_t nc_add_transition(NcEditor* editor, int32_t track_index, int32_t transition_type,
                           int64_t duration_us, uint64_t from_clip_id, uint64_t to_clip_id) {
    if (editor == nullptr || track_index < 0 || duration_us <= 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    if (transition_type < 0 || transition_type > 7) {
        return 0;
    }
    Transition tr;
    tr.id = TransitionId{editor->project.ids.next()};
    tr.type = static_cast<TransitionType>(transition_type);
    tr.duration = fromMicroseconds(duration_us);
    tr.fromClip = ClipId{from_clip_id};
    tr.toClip = ClipId{to_clip_id};
    tr.name = transitionName(tr.type);
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    const Result r = editor->editor->addTransition(main->id, trackId, std::move(tr));
    return r.isOk() ? editor->project.ids.peek() : 0;
}

int32_t nc_remove_transition(NcEditor* editor, int32_t track_index, uint64_t transition_id) {
    if (editor == nullptr || track_index < 0 || transition_id == 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(
        editor->editor->removeTransition(main->id, trackId, TransitionId{transition_id}).code);
}

int32_t nc_transition_count(const NcEditor* editor, int32_t track_index) {
    if (editor == nullptr || track_index < 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    return static_cast<int32_t>(
        main->tracks[static_cast<std::size_t>(track_index)].transitions.size());
}

int32_t nc_transition_get(const NcEditor* editor, int32_t track_index, int32_t index,
                          int32_t* out_type, int64_t* out_duration_us, uint64_t* out_from_clip,
                          uint64_t* out_to_clip, char** out_name) {
    if (editor == nullptr || track_index < 0 || index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (static_cast<std::size_t>(index) >= track.transitions.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Transition& tr = track.transitions[static_cast<std::size_t>(index)];
    if (out_type != nullptr) *out_type = static_cast<int32_t>(tr.type);
    if (out_duration_us != nullptr) *out_duration_us = toMicroseconds(tr.duration);
    if (out_from_clip != nullptr) *out_from_clip = tr.fromClip.value;
    if (out_to_clip != nullptr) *out_to_clip = tr.toClip.value;
    if (out_name != nullptr) *out_name = dupString(tr.name);
    return NC_OK;
}

int32_t nc_clip_get(const NcEditor* editor, int32_t track_index, int32_t clip_index,
                    int64_t* out_start_us, int64_t* out_duration_us, uint64_t* out_asset_id) {
    if (editor == nullptr || track_index < 0 || clip_index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (static_cast<std::size_t>(clip_index) >= track.clips.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Clip& clip = track.clips[static_cast<std::size_t>(clip_index)];
    if (out_start_us != nullptr) *out_start_us = toMicroseconds(clip.start);
    if (out_duration_us != nullptr) *out_duration_us = toMicroseconds(clip.duration);
    if (out_asset_id != nullptr) *out_asset_id = clip.asset.value;
    return NC_OK;
}

uint64_t nc_add_effect(NcEditor* editor, int32_t track_index, uint64_t clip_id, int32_t type,
                       double value) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return 0;
    }
    if (type <= 0 || type > 12) {
        return 0;  // 0 = None 无效；1..12 有效
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    Effect e;
    e.id = EffectId{editor->project.ids.next()};
    e.type = static_cast<EffectType>(type);
    e.value = value;
    const Result r = editor->editor->addEffect(main->id, trackId, ClipId{clip_id}, e);
    return r.isOk() ? e.id.value : 0;
}

int32_t nc_remove_effect(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                         uint64_t effect_id) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || effect_id == 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(
        editor->editor->removeEffect(main->id, trackId, ClipId{clip_id}, EffectId{effect_id}).code);
}

int32_t nc_effect_count(const NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    return clip == nullptr ? 0 : static_cast<int32_t>(clip->effects.size());
}

int32_t nc_effect_get(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                      int32_t index, int32_t* out_type, double* out_value,
                      int32_t* out_enabled) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    if (static_cast<std::size_t>(index) >= clip->effects.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Effect& e = clip->effects.effects()[static_cast<std::size_t>(index)];
    if (out_type != nullptr) *out_type = static_cast<int32_t>(e.type);
    if (out_value != nullptr) *out_value = e.value;
    if (out_enabled != nullptr) *out_enabled = e.enabled ? 1 : 0;
    return NC_OK;
}

int32_t nc_insert_keyframe(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t property, int64_t time_us, double value) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || time_us < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (property < 0 || property > 5) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    Keyframe k;
    k.time = fromMicroseconds(time_us);
    k.value = value;
    return toNcError(editor->editor
                         ->insertKeyframe(main->id, trackId, ClipId{clip_id},
                                          static_cast<ClipProperty>(property), k)
                         .code);
}

int32_t nc_remove_keyframe(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t property, int64_t time_us) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || time_us < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (property < 0 || property > 5) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(editor->editor
                         ->removeKeyframe(main->id, trackId, ClipId{clip_id},
                                          static_cast<ClipProperty>(property),
                                          fromMicroseconds(time_us))
                         .code);
}

int32_t nc_set_clip_audio(NcEditor* editor, int32_t track_index, uint64_t clip_id, double gain_db,
                          int64_t fade_in_us, int64_t fade_out_us) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || fade_in_us < 0 ||
        fade_out_us < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(editor->editor
                         ->setClipAudio(main->id, trackId, ClipId{clip_id}, gain_db,
                                        fromMicroseconds(fade_in_us),
                                        fromMicroseconds(fade_out_us))
                         .code);
}

int32_t nc_set_frame_fit(NcEditor* editor, int32_t mode) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    FrameFit fit;
    if (!frameFitFromInt(mode, fit)) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    main->frameFit = fit;
    return NC_OK;
}

int32_t nc_get_frame_fit(const NcEditor* editor) {
    if (editor == nullptr) {
        return -1;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return -1;
    }
    return static_cast<int32_t>(main->frameFit);
}

int32_t nc_set_background_color(NcEditor* editor, uint32_t rgba) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    main->backgroundRgba = rgba;
    return NC_OK;
}

int32_t nc_set_track_auto_duck(NcEditor* editor, int32_t track_index, int32_t enabled,
                               double sensitivity, double duck_amount_db, int64_t attack_us,
                               int64_t release_us, int64_t hold_us) {
    if (editor == nullptr || track_index < 0 || attack_us < 0 || release_us < 0 ||
        hold_us < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    AutoDuckSettings settings;
    settings.enabled = enabled != 0;
    settings.sensitivity = sensitivity;
    settings.duckAmountDb = duck_amount_db;
    settings.attackSeconds = static_cast<double>(attack_us) / 1000000.0;
    settings.releaseSeconds = static_cast<double>(release_us) / 1000000.0;
    settings.holdSeconds = static_cast<double>(hold_us) / 1000000.0;
    return toNcError(editor->editor->setTrackAutoDuck(main->id, trackId, settings).code);
}

int32_t nc_get_track_auto_duck(const NcEditor* editor, int32_t track_index, int32_t* out_enabled,
                               double* out_sensitivity, double* out_duck_amount_db) {
    if (editor == nullptr || track_index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (out_enabled != nullptr) *out_enabled = track.autoDuck.enabled ? 1 : 0;
    if (out_sensitivity != nullptr) *out_sensitivity = track.autoDuck.sensitivity;
    if (out_duck_amount_db != nullptr) *out_duck_amount_db = track.autoDuck.duckAmountDb;
    return NC_OK;
}

int32_t nc_create_compound_clip(NcEditor* editor, int32_t track_index, const uint64_t* clip_ids,
                                int32_t count, const char* name, uint64_t* out_sequence_id,
                                uint64_t* out_clip_id) {
    if (editor == nullptr || clip_ids == nullptr || count <= 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    std::vector<ClipId> ids;
    ids.reserve(static_cast<std::size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        ids.push_back(ClipId{clip_ids[i]});
    }
    const std::string compoundName = (name != nullptr && name[0] != '\0')
                                         ? std::string(name)
                                         : std::string("复合片段");
    SequenceId outSeq;
    ClipId outClip;
    const Result r =
        editor->editor->createCompoundClip(main->id, trackId, std::move(ids), compoundName,
                                           &outSeq, &outClip);
    if (!r.isOk()) {
        return toNcError(r.code);
    }
    if (out_sequence_id != nullptr) *out_sequence_id = outSeq.value;
    if (out_clip_id != nullptr) *out_clip_id = outClip.value;
    return NC_OK;
}

int32_t nc_unwrap_compound_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(
        editor->editor->unwrapCompoundClip(main->id, trackId, ClipId{clip_id}).code);
}

int32_t nc_clip_is_compound(const NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return -1;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return -1;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return -1;
    }
    return clip->isCompound() ? 1 : 0;
}

int32_t nc_set_track_solo(NcEditor* editor, int32_t track_index, int32_t solo) {
    if (editor == nullptr || track_index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(editor->editor->setTrackSolo(main->id, trackId, solo != 0).code);
}

int32_t nc_set_track_gain(NcEditor* editor, int32_t track_index, double gain_db) {
    if (editor == nullptr || track_index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return toNcError(editor->editor->setTrackGain(main->id, trackId, gain_db).code);
}

int32_t nc_get_track_mixer(const NcEditor* editor, int32_t track_index, int32_t* out_muted,
                           int32_t* out_solo, double* out_gain_db) {
    if (editor == nullptr || track_index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (out_muted != nullptr) *out_muted = track.muted ? 1 : 0;
    if (out_solo != nullptr) *out_solo = track.solo ? 1 : 0;
    if (out_gain_db != nullptr) *out_gain_db = track.gainDb;
    return NC_OK;
}

int32_t nc_set_master_gain(NcEditor* editor, double gain_db) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    return toNcError(editor->editor->setMasterGain(main->id, gain_db).code);
}

int32_t nc_get_master_gain(const NcEditor* editor, double* out_gain_db) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    if (out_gain_db != nullptr) *out_gain_db = main->masterGainDb;
    return NC_OK;
}

int32_t nc_set_entitlement(NcEditor* editor, int32_t tier, int32_t lifetime,
                           int64_t expires_at_unix) {
    if (editor == nullptr || (tier != 0 && tier != 1)) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    editor->entitlement.tier =
        tier == 1 ? MembershipTier::Pro : MembershipTier::Free;
    editor->entitlement.lifetime = lifetime != 0;
    editor->entitlement.expiresAtUnix = expires_at_unix < 0 ? 0 : expires_at_unix;
    return NC_OK;
}

int32_t nc_get_entitlement(const NcEditor* editor) {
    if (editor == nullptr) {
        return -1;
    }
    return editor->entitlement.tier == MembershipTier::Pro ? 1 : 0;
}

int32_t nc_can_add_track(const NcEditor* editor) {
    if (editor == nullptr) {
        return -1;
    }
    const Sequence* main = editor->project.mainSequence();
    const int trackCount = main == nullptr ? 0 : static_cast<int>(main->tracks.size());
    return editor->entitlement.canAddTrack(trackCount) ? 1 : 0;
}

int32_t nc_would_clamp_export(const NcEditor* editor, int32_t width, int32_t height) {
    if (editor == nullptr || width <= 0 || height <= 0) {
        return -1;
    }
    const ExportPolicy policy = makeExportPolicy(
        editor->entitlement, static_cast<std::int64_t>(std::time(nullptr)));
    if (policy.maxExportShortSide <= 0) {
        return 0;
    }
    int outW = width;
    int outH = height;
    return clampSizeToShortSide(width, height, policy.maxExportShortSide, outW, outH) ? 1 : 0;
}

// ---- AI 检查（文档 4.5）----

namespace {

/// 把检查报告序列化为 JSON（供 UI 列表渲染；时间用微秒整数避免浮点误差）。
std::string aiCheckReportToJson(const AiCheckReport& report) {
    JsonValue root = JsonValue::object();
    root.set("issueCount", JsonValue::integer(static_cast<std::int64_t>(report.issues.size())));
    root.set("scannedDurationUs",
             JsonValue::integer(toMicroseconds(report.scannedDuration)));
    root.set("summary", JsonValue::string(report.summary()));
    JsonValue issues = JsonValue::array();
    for (const auto& issue : report.issues) {
        JsonValue item = JsonValue::object();
        item.set("type", JsonValue::integer(static_cast<std::int64_t>(issue.type)));
        item.set("typeName", JsonValue::string(aiCheckIssueName(issue.type)));
        item.set("severity", JsonValue::integer(static_cast<std::int64_t>(issue.severity)));
        item.set("timeUs", JsonValue::integer(toMicroseconds(issue.time)));
        item.set("entityId", JsonValue::integer(static_cast<std::int64_t>(issue.entityId)));
        item.set("trackId", JsonValue::integer(static_cast<std::int64_t>(issue.trackId.value)));
        item.set("message", JsonValue::string(issue.message));
        item.set("autoFixable", JsonValue::boolean(issue.autoFixable));
        item.set("measuredValue", JsonValue::number(issue.measuredValue));
        issues.push(std::move(item));
    }
    root.set("issues", std::move(issues));
    JsonValue notes = JsonValue::array();
    for (const auto& note : report.notes) {
        notes.push(JsonValue::string(note));
    }
    root.set("notes", std::move(notes));
    return toJsonString(root);
}

}  // namespace

int32_t nc_ai_check(NcEditor* editor, char** out_report_json, char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    // 音频类检查需要真实解码器（含 FFmpeg 构建时可用）
    AiCheckReport report;
#ifdef NAIROCLIP_HAS_FFMPEG
    FFmpegReaderFactory readerFactory;
    report = checkSequence(editor->project, seq->id, &readerFactory);
#else
    report = checkSequence(editor->project, seq->id, nullptr);
#endif
    if (out_report_json != nullptr) {
        *out_report_json = dupString(aiCheckReportToJson(report));
    }
    if (out_error != nullptr) *out_error = nullptr;
    return static_cast<int32_t>(report.issues.size());
}

int32_t nc_ai_check_fix_all(NcEditor* editor, char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    // 固定输入：对当前工程重新检查后修复（避免调用方传入过期问题列表）
    AiCheckReport report;
#ifdef NAIROCLIP_HAS_FFMPEG
    FFmpegReaderFactory readerFactory;
    report = checkSequence(editor->project, seq->id, &readerFactory);
#else
    report = checkSequence(editor->project, seq->id, nullptr);
#endif
    const AiCheckFixResult fixResult =
        fixAllIssues(*editor->editor, editor->project, seq->id, report.issues);
    if (out_error != nullptr) *out_error = nullptr;
    return static_cast<int32_t>(fixResult.fixed);
}

// ---- 代理剪辑（文档第 3 部分）----

int32_t nc_proxy_check_import(NcEditor* editor, uint64_t asset_id, int32_t* out_proxy_width,
                              int32_t* out_proxy_height, double* out_estimated_seconds,
                              int64_t* out_estimated_bytes, char** out_reason,
                              char** out_error) {
    if (editor == nullptr || asset_id == 0) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const MediaAsset* asset = editor->project.findAsset(AssetId{asset_id});
    if (asset == nullptr) {
        if (out_error != nullptr) *out_error = dupString("素材不存在");
        return -1;
    }
    const ProxyImportCheck check =
        checkProxyOnImport(*asset, editor->proxySettings, &editor->project.proxies);
    if (out_proxy_width != nullptr) *out_proxy_width = check.proxyWidth;
    if (out_proxy_height != nullptr) *out_proxy_height = check.proxyHeight;
    if (out_estimated_seconds != nullptr) *out_estimated_seconds = check.estimatedSeconds;
    if (out_estimated_bytes != nullptr) *out_estimated_bytes = check.estimatedBytes;
    if (out_reason != nullptr) *out_reason = dupString(check.reason);
    if (out_error != nullptr) *out_error = nullptr;
    return check.suggestProxy ? 1 : 0;
}

int32_t nc_proxy_set_record(NcEditor* editor, uint64_t asset_id, const char* path,
                            int32_t status, int32_t width, int32_t height,
                            int64_t size_bytes, const char* note) {
    if (editor == nullptr || asset_id == 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    ProxyStatus proxyStatus;
    if (status < 0 || status > 3) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    proxyStatus = static_cast<ProxyStatus>(status);
    if (editor->project.findAsset(AssetId{asset_id}) == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    ProxyRecord record;
    record.asset = AssetId{asset_id};
    if (path != nullptr) {
        record.path = path;
    }
    record.status = proxyStatus;
    record.width = width < 0 ? 0 : width;
    record.height = height < 0 ? 0 : height;
    record.sizeBytes = size_bytes < 0 ? 0 : size_bytes;
    if (note != nullptr) {
        record.note = note;
    }
    record.quality = editor->proxySettings.quality;
    editor->project.proxies.upsert(std::move(record));
    return NC_OK;
}

int32_t nc_proxy_get_record(const NcEditor* editor, uint64_t asset_id, int32_t* out_status,
                            int32_t* out_width, int32_t* out_height, int64_t* out_size_bytes,
                            char** out_path, char** out_note) {
    if (editor == nullptr || asset_id == 0) {
        return -1;
    }
    const ProxyRecord* record = editor->project.proxies.find(AssetId{asset_id});
    if (record == nullptr) {
        return 0;
    }
    if (out_status != nullptr) *out_status = static_cast<int32_t>(record->status);
    if (out_width != nullptr) *out_width = record->width;
    if (out_height != nullptr) *out_height = record->height;
    if (out_size_bytes != nullptr) *out_size_bytes = record->sizeBytes;
    if (out_path != nullptr) *out_path = dupString(record->path);
    if (out_note != nullptr) *out_note = dupString(record->note);
    return 1;
}

int32_t nc_proxy_usage(const NcEditor* editor, int32_t* out_ready_count,
                       int32_t* out_generating_count, int64_t* out_total_bytes) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (out_ready_count != nullptr) {
        *out_ready_count =
            static_cast<int32_t>(editor->project.proxies.readyCount());
    }
    if (out_generating_count != nullptr) {
        *out_generating_count =
            static_cast<int32_t>(editor->project.proxies.generatingCount());
    }
    if (out_total_bytes != nullptr) {
        *out_total_bytes = editor->project.proxies.totalSizeBytes();
    }
    return NC_OK;
}

int32_t nc_proxy_clear_records(NcEditor* editor) {
    if (editor == nullptr) {
        return -1;
    }
    const int32_t count = static_cast<int32_t>(editor->project.proxies.records.size());
    editor->project.proxies.clear();
    return count;
}

// ---- 导出预设 / 资源预估 / 性能模式 ----

int32_t nc_export_presets(int32_t* out_ids, int32_t capacity, int32_t* out_count) {
    const auto& presets = builtinExportPresets();
    if (out_count != nullptr) {
        *out_count = static_cast<int32_t>(presets.size());
    }
    if (out_ids != nullptr) {
        const int32_t limit = capacity < static_cast<int32_t>(presets.size())
                                  ? capacity
                                  : static_cast<int32_t>(presets.size());
        for (int32_t i = 0; i < limit; ++i) {
            out_ids[i] = static_cast<int32_t>(presets[static_cast<std::size_t>(i)].id);
        }
    }
    return NC_OK;
}

int32_t nc_export_preset_info(int32_t preset_id, int32_t* out_width, int32_t* out_height,
                              int32_t* out_fps, int32_t* out_requires_pro, char** out_label) {
    if (preset_id < 0 || preset_id > 5) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const ExportPreset& preset = exportPreset(static_cast<ExportPresetId>(preset_id));
    if (out_width != nullptr) *out_width = preset.width;
    if (out_height != nullptr) *out_height = preset.height;
    if (out_fps != nullptr) *out_fps = preset.fps;
    if (out_requires_pro != nullptr) *out_requires_pro = preset.requiresPro ? 1 : 0;
    if (out_label != nullptr) *out_label = dupString(preset.label);
    return NC_OK;
}

int32_t nc_export_estimate(int32_t width, int32_t height, int32_t fps, int64_t duration_us,
                           double video_bitrate_mbps, int32_t include_audio,
                           int64_t* out_file_size_bytes, double* out_render_seconds,
                           char** out_size_text, char** out_time_text) {
    const double durationSeconds = static_cast<double>(duration_us) / 1000000.0;
    const ExportEstimate estimate = estimateExport(width, height, fps, durationSeconds,
                                                   video_bitrate_mbps, include_audio != 0);
    if (out_file_size_bytes != nullptr) *out_file_size_bytes = estimate.fileSizeBytes;
    if (out_render_seconds != nullptr) *out_render_seconds = estimate.renderSeconds;
    if (out_size_text != nullptr) *out_size_text = dupString(estimate.fileSizeText());
    if (out_time_text != nullptr) *out_time_text = dupString(estimate.renderTimeText());
    return NC_OK;
}

int32_t nc_export_setup_preset(NcEditor* editor, int32_t preset_id, int32_t mode,
                               int32_t cpu_cores, int32_t crf, int32_t* out_width,
                               int32_t* out_height, int32_t* out_fps, int32_t* out_downgraded,
                               char** out_preset_name, char** out_warning) {
    if (editor == nullptr || preset_id < 0 || preset_id > 5 || mode < 0 || mode > 2) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    PerformanceMode performanceMode;
    if (!performanceModeFromInt(mode, performanceMode)) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const PresetExportSetup setup = setupPresetExport(
        static_cast<ExportPresetId>(preset_id), editor->entitlement,
        static_cast<std::int64_t>(std::time(nullptr)), performanceMode, cpu_cores,
        crf > 0 ? crf : 23);
    if (out_width != nullptr) *out_width = setup.width;
    if (out_height != nullptr) *out_height = setup.height;
    if (out_fps != nullptr) *out_fps = setup.fps;
    if (out_downgraded != nullptr) *out_downgraded = setup.downgradedForFree ? 1 : 0;
    if (out_preset_name != nullptr) *out_preset_name = dupString(setup.writer.preset);
    if (out_warning != nullptr) {
        *out_warning =
            dupString(setup.warnings.empty() ? std::string() : setup.warnings.front());
    }
    return NC_OK;
}

// ---- 口播精剪 ----

int32_t nc_speech_analyze(NcEditor* editor, uint64_t asset_id, int64_t* out_speech_us,
                          int64_t* out_silence_us, double* out_keep_ratio,
                          int32_t* out_segment_count, char** out_note, char** out_error) {
    if (editor == nullptr || asset_id == 0) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const MediaAsset* asset = editor->project.findAsset(AssetId{asset_id});
    if (asset == nullptr) {
        if (out_error != nullptr) *out_error = dupString("素材不存在");
        return -1;
    }
    SpeechActivityResult result;
#ifdef NAIROCLIP_HAS_FFMPEG
    FFmpegReaderFactory readerFactory;
    result = analyzeAssetSpeech(*asset, &readerFactory);
#else
    result = analyzeAssetSpeech(*asset, nullptr);
#endif
    if (out_speech_us != nullptr) *out_speech_us = toMicroseconds(result.speechDuration);
    if (out_silence_us != nullptr) *out_silence_us = toMicroseconds(result.silenceDuration);
    if (out_keep_ratio != nullptr) *out_keep_ratio = result.keepRatio;
    if (out_segment_count != nullptr) {
        *out_segment_count = static_cast<int32_t>(result.keepSegments.size());
    }
    if (out_note != nullptr) *out_note = dupString(result.note);
    if (out_error != nullptr) *out_error = nullptr;
    if (!result.valid()) {
        return 0;  // 无有效语音：返回 0（由 note 说明）
    }
    return static_cast<int32_t>(result.keepSegments.size());
}

int32_t nc_speech_cut_apply(NcEditor* editor, int32_t track_index, char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    // 选择目标轨道（默认首个音频轨）
    int32_t index = track_index;
    if (index < 0) {
        index = -1;
        for (std::size_t i = 0; i < seq->tracks.size(); ++i) {
            if (seq->tracks[i].type == TrackType::Audio) {
                index = static_cast<int32_t>(i);
                break;
            }
        }
        if (index < 0) {
            if (out_error != nullptr) *out_error = dupString("工程没有音频轨");
            return -1;
        }
    }
    if (static_cast<std::size_t>(index) >= seq->tracks.size()) {
        if (out_error != nullptr) *out_error = dupString("轨道下标越界");
        return -1;
    }
    const Track& track = seq->tracks[static_cast<std::size_t>(index)];
    if (track.type != TrackType::Audio) {
        if (out_error != nullptr) *out_error = dupString("口播精剪仅支持音频轨");
        return -1;
    }
    if (track.clips.empty()) {
        if (out_error != nullptr) *out_error = dupString("轨道无片段");
        return -1;
    }
    // 以首个片段的素材做语音分析（多素材场景由平台层分段调用）
    const MediaAsset* asset = editor->project.findAsset(track.clips.front().asset);
    if (asset == nullptr) {
        if (out_error != nullptr) *out_error = dupString("片段引用的素材不存在");
        return -1;
    }
    SpeechActivityResult activity;
#ifdef NAIROCLIP_HAS_FFMPEG
    FFmpegReaderFactory readerFactory;
    activity = analyzeAssetSpeech(*asset, &readerFactory);
#else
    activity = analyzeAssetSpeech(*asset, nullptr);
#endif
    if (!activity.valid()) {
        if (out_error != nullptr) {
            *out_error = dupString(activity.note.empty() ? "语音分析无效" : activity.note);
        }
        return -1;
    }
    // 收集该轨道全部片段 ID
    std::vector<ClipId> clipIds;
    clipIds.reserve(track.clips.size());
    for (const auto& clip : track.clips) {
        clipIds.push_back(clip.id);
    }
    const SpeechCutApplyResult applyResult = applySpeechCut(
        *editor->editor, editor->project, seq->id, track.id, clipIds, activity);
    if (!applyResult.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(applyResult.result.message);
        return -1;
    }
    if (out_error != nullptr) *out_error = nullptr;
    return static_cast<int32_t>(applyResult.clipsInserted);
}

// ---- 蒙版 ----

int32_t nc_set_clip_mask(NcEditor* editor, int32_t track_index, uint64_t clip_id, int32_t shape,
                         double center_x, double center_y, double rotation_degrees, double width,
                         double height, double feather, double corner_radius, int32_t invert) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    MaskShape maskShape;
    if (!maskShapeFromInt(shape, maskShape)) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    Mask mask;
    mask.shape = maskShape;
    mask.centerX = center_x;
    mask.centerY = center_y;
    mask.rotationDegrees = rotation_degrees;
    mask.width = width;
    mask.height = height;
    mask.feather = feather;
    mask.cornerRadius = corner_radius;
    mask.invert = invert != 0;
    return toNcError(
        editor->editor->setClipMask(main->id, trackId, ClipId{clip_id}, mask).code);
}

int32_t nc_get_clip_mask(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                         int32_t* out_shape, double* out_center_x, double* out_center_y,
                         double* out_rotation_degrees, double* out_width, double* out_height,
                         double* out_feather, double* out_corner_radius, int32_t* out_invert) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return -1;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return -1;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return -1;
    }
    if (!clip->hasMask) {
        if (out_shape != nullptr) *out_shape = 0;
        return 0;
    }
    if (out_shape != nullptr) *out_shape = static_cast<int32_t>(clip->mask.shape);
    if (out_center_x != nullptr) *out_center_x = clip->mask.centerX;
    if (out_center_y != nullptr) *out_center_y = clip->mask.centerY;
    if (out_rotation_degrees != nullptr) *out_rotation_degrees = clip->mask.rotationDegrees;
    if (out_width != nullptr) *out_width = clip->mask.width;
    if (out_height != nullptr) *out_height = clip->mask.height;
    if (out_feather != nullptr) *out_feather = clip->mask.feather;
    if (out_corner_radius != nullptr) *out_corner_radius = clip->mask.cornerRadius;
    if (out_invert != nullptr) *out_invert = clip->mask.invert ? 1 : 0;
    return 1;
}

int32_t nc_auto_edit(NcEditor* editor, int32_t track_index, const double* profile_shots,
                     int32_t shot_count, double avg_r, double avg_g, double avg_b,
                     double avg_saturation, double avg_contrast, const uint64_t* asset_ids,
                     const double* usable_seconds, int32_t material_count, int32_t apply_color,
                     char** out_error) {
    if (editor == nullptr || profile_shots == nullptr || shot_count <= 0 ||
        asset_ids == nullptr || usable_seconds == nullptr || material_count <= 0) {
        if (out_error != nullptr) *out_error = dupString("参数为空或数量非法");
        return -1;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        if (out_error != nullptr) *out_error = dupString("轨道下标越界");
        return -1;
    }

    // 构造风格画像（只含节奏与色调）
    StyleProfile profile;
    std::vector<ShotBoundary> shots;
    shots.reserve(static_cast<std::size_t>(shot_count));
    for (int32_t i = 0; i < shot_count; ++i) {
        ShotBoundary b;
        b.start = Rational::make(static_cast<std::int64_t>(profile_shots[i * 4] * 1000.0), 1000);
        b.end = Rational::make(static_cast<std::int64_t>(profile_shots[i * 4 + 1] * 1000.0), 1000);
        shots.push_back(std::move(b));
    }
    profile.rhythm = analyzeRhythm(shots);
    profile.color = analyzeColor(avg_r, avg_g, avg_b, avg_saturation, avg_contrast);

    // 用户素材列表
    std::vector<MaterialSegment> material;
    material.reserve(static_cast<std::size_t>(material_count));
    for (int32_t i = 0; i < material_count; ++i) {
        MaterialSegment m;
        m.asset = AssetId{asset_ids[i]};
        m.usableDuration = Rational::make(
            static_cast<std::int64_t>(usable_seconds[i] * 1000.0 + 0.5), 1000);
        material.push_back(std::move(m));
    }

    AutoEditOptions options;
    options.applyColor = apply_color != 0;

    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    const AutoEditResult result = applyAutoEdit(*editor->editor, editor->project, main->id,
                                                trackId, profile, material, options);
    if (!result.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(result.result.message);
        return -1;
    }
    if (out_error != nullptr) *out_error = nullptr;
    return static_cast<int32_t>(result.clipsInserted);
}

#ifdef NAIROCLIP_HAS_FFMPEG
int64_t nc_export_run(NcEditor* editor, const char* out_path, int32_t crf, int32_t include_audio,
                      int32_t attach_ai_label, const char* ai_label_text,
                      int32_t (*progress_cb)(double), char** out_error) {
    if (editor == nullptr || out_path == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    const ExportPlan plan = compileExportPlan(editor->project, *seq);
    FFmpegReaderFactory readerFactory;
    class FFWriterFactory : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    FFWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = out_path;
    config.writer.width = plan.width;
    config.writer.height = plan.height;
    config.writer.frameRate = plan.frameRate;
    config.writer.crf = crf > 0 ? crf : 23;
    config.writer.includeAudio = include_audio != 0;
    config.executor.attachAiLabel = attach_ai_label != 0;
    if (ai_label_text != nullptr && ai_label_text[0] != '\0') {
        config.executor.aiLabelText = ai_label_text;
    }
    // 会员门控：免费/过期会员 → 输出降级 1080p + 附加水印（文档 3.10）。
    // 使用系统时钟做到期判定；tier 由平台层经服务端心跳维护（文档第 8 部分）。
    config.policy = makeExportPolicy(editor->entitlement,
                                     static_cast<std::int64_t>(std::time(nullptr)));
    const ExportRunResult result = runExport(
        plan, readerFactory, writerFactory, config,
        [progress_cb](double p) {
            if (progress_cb == nullptr) return true;
            return progress_cb(p) != 0;
        },
        &editor->project);
    if (!result.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(result.result.message);
        return -1;
    }
    if (out_error != nullptr) *out_error = nullptr;
    return static_cast<int64_t>(result.stats.framesWritten);
}

#else

int64_t nc_export_run(NcEditor*, const char*, int32_t, int32_t, int32_t, const char*,
                      int32_t (*)(double), char** out_error) {
    if (out_error != nullptr) *out_error = dupString("此构建未包含 FFmpeg");
    return -1;
}

#endif

// ---- AI 模型管理（文档第 5 部分 + 第 8 部分）----

namespace {

/// 模型记录表 + 下载状态机的进程级存储（设备级资产；一个进程一份）。
ModelStore& modelStore() {
    static ModelStore store;
    return store;
}

ModelDownloadManager& modelDownloads() {
    static ModelDownloadManager manager;
    return manager;
}

}  // namespace

int32_t nc_model_catalog(char*** out_ids, int32_t capacity, int32_t* out_count) {
    const auto& catalog = builtinModelCatalog();
    if (out_count != nullptr) {
        *out_count = static_cast<int32_t>(catalog.size());
    }
    if (out_ids == nullptr) {
        return NC_OK;
    }
    if (capacity <= 0) {
        *out_ids = nullptr;
        return NC_OK;
    }
    const int32_t count = static_cast<int32_t>(
        std::min<std::size_t>(static_cast<std::size_t>(capacity), catalog.size()));
    char** items = new (std::nothrow) char*[static_cast<std::size_t>(count)];
    if (items == nullptr) {
        *out_ids = nullptr;
        return NC_ERROR_INTERNAL;
    }
    for (int32_t i = 0; i < count; ++i) {
        items[i] = dupString(catalog[static_cast<std::size_t>(i)].id);
    }
    *out_ids = items;
    return NC_OK;
}

int32_t nc_model_info(const char* model_id, char** out_name, char** out_description,
                      int32_t* out_size_bytes, char** out_version, int32_t* out_requires_pro,
                      int32_t* out_capability) {
    if (model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    if (out_name != nullptr) *out_name = dupString(info->name);
    if (out_description != nullptr) *out_description = dupString(info->description);
    if (out_size_bytes != nullptr) {
        *out_size_bytes = static_cast<int32_t>(info->sizeBytes);
    }
    if (out_version != nullptr) *out_version = dupString(info->version);
    if (out_requires_pro != nullptr) *out_requires_pro = info->requiresPro ? 1 : 0;
    if (out_capability != nullptr) {
        *out_capability = static_cast<int32_t>(info->capability);
    }
    return NC_OK;
}

int32_t nc_model_status(const NcEditor* editor, const char* model_id) {
    if (editor == nullptr || model_id == nullptr) {
        return -1;
    }
    // 运行态优先（下载中/校验中）。
    const DownloadState* state = modelDownloads().find(model_id);
    if (state != nullptr) {
        if (state->phase == DownloadPhase::Downloading) return static_cast<int32_t>(ModelStatus::Downloading);
        if (state->phase == DownloadPhase::Verifying) return static_cast<int32_t>(ModelStatus::Verifying);
    }
    return static_cast<int32_t>(modelStore().statusOf(model_id));
}

int32_t nc_model_access_allowed(const NcEditor* editor, const char* model_id) {
    if (editor == nullptr || model_id == nullptr) {
        return -1;
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return -1;
    }
    return modelAccessAllowed(*info, editor->entitlement,
                              static_cast<std::int64_t>(std::time(nullptr)))
               ? 1
               : 0;
}

int32_t nc_model_download_begin(NcEditor* editor, const char* model_id, int64_t total_bytes,
                                int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (findBuiltinModel(model_id) == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    return toNcError(modelDownloads().begin(model_id, total_bytes, now_unix).code);
}

int32_t nc_model_download_progress(NcEditor* editor, const char* model_id,
                                   int64_t downloaded_bytes, int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(modelDownloads().markProgress(model_id, downloaded_bytes, now_unix).code);
}

int32_t nc_model_download_pause(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(modelDownloads().pause(model_id, now_unix).code);
}

int32_t nc_model_download_resume(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(modelDownloads().resume(model_id, now_unix).code);
}

int32_t nc_model_download_cancel(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(modelDownloads().cancel(model_id, now_unix).code);
}

int32_t nc_model_download_finish(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    return toNcError(modelDownloads().finishDownload(model_id, now_unix).code);
}

int32_t nc_model_download_fail(NcEditor* editor, const char* model_id, const char* reason,
                               int64_t now_unix) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const std::string text = reason != nullptr ? reason : "下载失败";
    return toNcError(modelDownloads().fail(model_id, text, now_unix).code);
}

int32_t nc_model_download_state(const NcEditor* editor, const char* model_id, int32_t* out_phase,
                                int64_t* out_total_bytes, int64_t* out_downloaded_bytes,
                                char** out_message) {
    if (editor == nullptr || model_id == nullptr) {
        return -1;
    }
    const DownloadState* state = modelDownloads().find(model_id);
    if (state == nullptr) {
        return 0;
    }
    if (out_phase != nullptr) *out_phase = static_cast<int32_t>(state->phase);
    if (out_total_bytes != nullptr) *out_total_bytes = state->totalBytes;
    if (out_downloaded_bytes != nullptr) *out_downloaded_bytes = state->downloadedBytes;
    if (out_message != nullptr) *out_message = dupString(state->message);
    return 1;
}

int32_t nc_model_resume_plan(const NcEditor* editor, const char* model_id,
                             int64_t downloaded_bytes, int64_t* out_offset_bytes,
                             uint8_t* out_counter, int32_t* out_intra_block_skip) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    const DownloadResumePlan plan =
        planDownloadResume(info->id, info->version, downloaded_bytes);
    if (!plan.valid) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    if (out_offset_bytes != nullptr) *out_offset_bytes = plan.offsetBytes;
    if (out_counter != nullptr) {
        std::memcpy(out_counter, plan.counter, kAesCtrCounterSize);
    }
    if (out_intra_block_skip != nullptr) {
        *out_intra_block_skip = static_cast<int32_t>(plan.intraBlockSkip);
    }
    return NC_OK;
}

int32_t nc_model_install(NcEditor* editor, const char* model_id, const uint8_t* ciphertext,
                         int64_t size, const char* path, const char* expected_sha256_hex,
                         int64_t now_unix, int32_t* out_installed, char** out_message) {
    if (editor == nullptr || model_id == nullptr || size < 0 ||
        (size > 0 && ciphertext == nullptr)) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    std::vector<std::uint8_t> payload;
    if (size > 0) {
        payload.assign(ciphertext, ciphertext + size);
    }
    const std::string digest = expected_sha256_hex != nullptr ? expected_sha256_hex : "";
    const ModelVerifyResult verified = verifyModelPayload(*info, payload, digest);

    if (out_installed != nullptr) *out_installed = verified.ok ? 1 : 0;
    if (out_message != nullptr) *out_message = dupString(verified.message);

    ModelRecord record;
    record.modelId = info->id;
    record.version = info->version;
    record.path = path != nullptr ? path : "";
    record.sizeBytes = verified.sizeBytes;
    record.sha256 = digest;
    record.installedAtUnix = now_unix;
    if (verified.ok) {
        record.status = ModelStatus::Installed;
        // 安装成功：结束下载状态机（若存在）。
        DownloadState* state = modelDownloads().find(info->id);
        if (state != nullptr && state->phase == DownloadPhase::Verifying) {
            modelDownloads().complete(info->id, now_unix);
        }
    } else {
        record.status = ModelStatus::Corrupt;
        record.note = verified.message;
        DownloadState* state = modelDownloads().find(info->id);
        if (state != nullptr) {
            modelDownloads().fail(info->id, verified.message, now_unix);
        }
    }
    modelStore().upsert(std::move(record));
    return NC_OK;
}

int32_t nc_model_remove(NcEditor* editor, const char* model_id) {
    if (editor == nullptr || model_id == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    modelStore().remove(model_id);
    modelDownloads().remove(model_id);
    return NC_OK;
}

int32_t nc_model_storage_stats(const NcEditor* editor, int64_t device_total_bytes,
                               int64_t device_free_bytes, int64_t* out_model_bytes,
                               int32_t* out_installed_count, int32_t* out_corrupt_count,
                               char** out_usage_text, char** out_model_text) {
    if (editor == nullptr) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const ModelStorageStats stats =
        computeModelStorage(modelStore(), device_total_bytes, device_free_bytes);
    if (out_model_bytes != nullptr) *out_model_bytes = stats.modelBytes;
    if (out_installed_count != nullptr) *out_installed_count = stats.installedCount;
    if (out_corrupt_count != nullptr) *out_corrupt_count = stats.corruptCount;
    if (out_usage_text != nullptr) *out_usage_text = dupString(stats.usageText());
    if (out_model_text != nullptr) *out_model_text = dupString(stats.modelText());
    return NC_OK;
}

int32_t nc_model_decrypt(const char* model_id, const char* version, const uint8_t* key,
                         const uint8_t* ciphertext, int64_t size, int64_t offset,
                         uint8_t** out_plaintext, int64_t* out_size) {
    if (model_id == nullptr || version == nullptr || key == nullptr || size < 0 || offset < 0 ||
        (size > 0 && ciphertext == nullptr)) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    std::vector<std::uint8_t> payload;
    if (size > 0) {
        payload.assign(ciphertext, ciphertext + size);
    }
    std::vector<std::uint8_t> plain = decryptModelPayloadAt(
        key, model_id, version, static_cast<std::uint64_t>(offset), payload);
    if (plain.size() != payload.size()) {
        return NC_ERROR_INTERNAL;
    }
    if (out_size != nullptr) *out_size = static_cast<int64_t>(plain.size());
    if (out_plaintext != nullptr) {
        if (plain.empty()) {
            *out_plaintext = nullptr;
        } else {
            std::uint8_t* buffer = new (std::nothrow) std::uint8_t[plain.size()];
            if (buffer == nullptr) {
                return NC_ERROR_INTERNAL;
            }
            std::memcpy(buffer, plain.data(), plain.size());
            *out_plaintext = buffer;
        }
    }
    return NC_OK;
}

void nc_free_buffer(uint8_t* buffer) { delete[] buffer; }

// ---- 章节自动生成（文档第 7 部分「🧪AI工具 → 章节自动生成」）----

int32_t nc_chapter_detect(NcEditor* editor, double min_chapter_seconds,
                          double max_chapter_seconds, double silence_boundary_seconds,
                          int32_t use_scene_detect, int32_t use_silence_detect,
                          int32_t max_chapters, char** out_summary, char** out_note,
                          char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    ChapterDetectConfig config;
    if (min_chapter_seconds > 0.0) {
        config.minChapterSeconds = min_chapter_seconds;
    }
    if (max_chapter_seconds >= 0.0) {
        config.maxChapterSeconds = max_chapter_seconds;
    }
    if (silence_boundary_seconds > 0.0) {
        config.silenceBoundarySeconds = silence_boundary_seconds;
    }
    // -1 = 默认（启用）；0 = 禁用；其余视为启用
    config.useSceneDetect = use_scene_detect != 0;
    config.useSilenceDetect = use_silence_detect != 0;
    if (max_chapters > 0) {
        config.maxChapters = static_cast<std::size_t>(max_chapters);
    }
    if (!config.valid()) {
        if (out_error != nullptr) *out_error = dupString("章节检测参数非法");
        return -1;
    }
    // 真实采样（FFmpeg 可用时；否则给出说明性 note）
    SequenceChapterSamples samples;
#ifdef NAIROCLIP_HAS_FFMPEG
    {
        FFmpegReaderFactory factory;
        samples = sampleSequenceForChapters(editor->project, seq->id, &factory);
    }
#else
    samples = sampleSequenceForChapters(editor->project, seq->id, nullptr);
#endif
    ChapterDetectResult result;
    if (samples.usable()) {
        result = detectChaptersFromFeatures(samples.features, samples.audio,
                                            samples.timelineDuration, config);
        for (const auto& w : samples.warnings) {
            result.warnings.push_back(w);
        }
    } else {
        // 无采样数据：退化为时长约束（场景/静音均无输入）
        result = detectChapters({}, {}, seq->duration(), config);
        if (!samples.note.empty()) {
            result.note = samples.note;
        }
    }
    editor->chapterCache = result;
    editor->chapterConfig = config;
    if (out_summary != nullptr) *out_summary = dupString(result.summary);
    if (out_note != nullptr) *out_note = dupString(result.note);
    if (out_error != nullptr) *out_error = nullptr;
    if (!result.valid()) {
        return 0;  // 未检测到章节（note 说明原因）
    }
    return static_cast<int32_t>(result.chapters.size());
}

int32_t nc_chapter_apply(NcEditor* editor, char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    if (editor->chapterCache.chapters.empty()) {
        if (out_error != nullptr) {
            *out_error = dupString("没有可落盘的检测结果，请先调用 nc_chapter_detect");
        }
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    const ChapterApplyResult applied =
        applyChapters(*editor->editor, editor->project, seq->id, editor->chapterCache);
    if (!applied.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(applied.result.message);
        return -1;
    }
    if (out_error != nullptr) *out_error = nullptr;
    return static_cast<int32_t>(applied.markersAdded + applied.markersUpdated);
}

int32_t nc_chapter_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(readChaptersFromSequence(*seq).size());
}

int32_t nc_chapter_get(const NcEditor* editor, int32_t index, int64_t* out_start_us,
                       int64_t* out_end_us, char** out_title) {
    if (editor == nullptr || index < 0) {
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return NC_ERROR_NOT_FOUND;
    }
    const std::vector<Chapter> chapters = readChaptersFromSequence(*seq);
    if (static_cast<std::size_t>(index) >= chapters.size()) {
        return NC_ERROR_OUT_OF_RANGE;
    }
    const Chapter& chapter = chapters[static_cast<std::size_t>(index)];
    if (out_start_us != nullptr) *out_start_us = toMicroseconds(chapter.start);
    if (out_end_us != nullptr) *out_end_us = toMicroseconds(chapter.end);
    if (out_title != nullptr) *out_title = dupString(chapter.title);
    return NC_OK;
}

int32_t nc_chapter_rename(NcEditor* editor, int32_t index, const char* new_title,
                          char** out_error) {
    if (editor == nullptr || new_title == nullptr || index < 0) {
        if (out_error != nullptr) *out_error = dupString("参数非法");
        return NC_ERROR_INVALID_ARGUMENT;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return NC_ERROR_NOT_FOUND;
    }
    const std::vector<Chapter> chapters = readChaptersFromSequence(*seq);
    if (static_cast<std::size_t>(index) >= chapters.size()) {
        if (out_error != nullptr) *out_error = dupString("章节下标越界");
        return NC_ERROR_OUT_OF_RANGE;
    }
    std::string title = new_title;
    const std::size_t begin = title.find_first_not_of(" \t\r\n");
    const std::size_t end = title.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        if (out_error != nullptr) *out_error = dupString("标题不能为空");
        return NC_ERROR_INVALID_ARGUMENT;
    }
    title = title.substr(begin, end - begin + 1);
    const Chapter& chapter = chapters[static_cast<std::size_t>(index)];
    Marker marker;
    marker.id = chapter.markerId;
    marker.time = chapter.start;
    marker.name = title;
    marker.color = chapterMarkerColor();
    const Result r = editor->editor->upsertMarker(seq->id, std::move(marker));
    if (!r.isOk()) {
        if (out_error != nullptr) *out_error = dupString(r.message);
        return toNcError(r.code);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_chapter_clear(NcEditor* editor, char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return -1;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return -1;
    }
    const std::vector<Chapter> chapters = readChaptersFromSequence(*seq);
    if (chapters.empty()) {
        if (out_error != nullptr) *out_error = nullptr;
        return 0;  // 无章节视为已清空
    }
    editor->editor->beginTransaction("清除章节");
    int32_t removed = 0;
    for (const auto& chapter : chapters) {
        if (chapter.markerId.isValid() &&
            editor->editor->removeMarker(seq->id, chapter.markerId).isOk()) {
            ++removed;
        }
    }
    editor->editor->endTransaction();
    editor->chapterCache = ChapterDetectResult{};  // 缓存失效（避免落盘已删除的结果）
    if (out_error != nullptr) *out_error = nullptr;
    return removed;
}

void nc_free_string_array(char** items, int32_t count) {
    if (items == nullptr) {
        return;
    }
    for (int32_t i = 0; i < count; ++i) {
        delete[] items[i];
    }
    delete[] items;
}

void nc_free_string(char* text) { delete[] text; }

}  // extern "C"
