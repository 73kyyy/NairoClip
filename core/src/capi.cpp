#include "nairoclip/core/capi.h"

#include "nairoclip/core/chapter.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/ai_check.h"
#include "nairoclip/core/ai_marking.h"
#include "nairoclip/core/auto_edit.h"
#include "nairoclip/core/export_queue.h"
#include "nairoclip/core/heartbeat.h"
#include "nairoclip/core/invisible_watermark.h"
#include "nairoclip/core/media_offline.h"
#include "nairoclip/core/smart_movie.h"
#include "nairoclip/core/crypto.h"
#include "nairoclip/core/transition.h"
#include "nairoclip/core/track.h"
#include "nairoclip/core/json.h"
#include "nairoclip/core/export_preset.h"
#include "nairoclip/core/mask.h"
#include "nairoclip/core/chroma_key.h"
#include "nairoclip/core/speech_cut.h"
#include "nairoclip/core/proxy.h"
#include "nairoclip/core/model_manager.h"
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/thumbnail.h"
#include "nairoclip/core/waveform.h"
#include "nairoclip/core/scene_detect.h"
#include "nairoclip/core/beat_detect.h"
#include "nairoclip/core/speed_curve.h"
#include "nairoclip/core/commands.h"
#include "nairoclip/core/filters.h"
#include "nairoclip/core/ai_task.h"
#include "nairoclip/core/ai_engine.h"
#include "nairoclip/core/background_fill.h"
#include "nairoclip/core/depth_ai.h"
#include "nairoclip/core/matting_ai.h"
#include "nairoclip/core/speech_ai.h"
#include "nairoclip/core/super_res_ai.h"
#include "nairoclip/core/translate_ai.h"
#include "nairoclip/core/person_effects.h"
#include "nairoclip/core/template_apply.h"
#include "nairoclip/core/template_fill.h"

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/export_runner.h"
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#endif

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <new>
#include <vector>

#ifdef NAIROCLIP_HAS_FFMPEG
// POSIX 目录列举（自动扫描查找重链候选；仅 FFmpeg 构建下使用）
#include <dirent.h>
#endif
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
    /// 导出队列（文档第 6 部分「二、导出队列管理」；运行时状态）。
    ExportQueue exportQueue;
    /// 最近一次离线扫描报告（nc_media_apply_offline_flags 消费）。
    OfflineScanReport offlineReport;
    /// 隐形水印导出配置（nc_watermark_set_export 设置；nc_export_run 消费）。
    InvisibleWatermarkSpec invisibleWatermark;
    /// 会员心跳状态机（文档第 8 部分五；平台层驱动）。
    HeartbeatState heartbeat;
    /// AI 任务状态机（文档 Part 4 各 AI 功能的统一任务契约）。
    AiTaskRunner aiTask;
    /// AI 真实推理会话（深度图 / 人像抠像 / 语音识别；按需加载）。
    std::unique_ptr<DepthEstimator> depthEstimator;
    std::unique_ptr<MattingEstimator> mattingEstimator;
    std::unique_ptr<SpeechRecognizer> speechRecognizer;
    std::unique_ptr<SuperResEstimator> superResEstimator;
    std::unique_ptr<Translator> translator;
    /// 预览渲染器缓存（nc_preview_render；编辑操作后失效重建）。
    std::unique_ptr<PreviewRenderer> previewRenderer;
    /// 预览缓存有效性签名（编辑历史数 + 载入代次 + 直接修改代次 + 画布）。
    /// 说明：所有片段级编辑经编辑栈（undoCount 变化）；画布/轨道标志等
    /// 直接修改经 directEditCount 记账；nc_project_load 递增 loadCount。
    std::uint64_t previewSignature = 0;
    std::uint64_t loadCount = 0;        ///< 工程载入代次
    std::uint64_t directEditCount = 0;  ///< 绕过编辑栈的直接修改代次
    /// 缩略图缓存尺寸（nc_thumbnail_generate 后可用）。
    int thumbnailWidth = 0;
    int thumbnailHeight = 0;
    /// 波形缓存（nc_waveform_generate 生成；逐桶读取）。
    Waveform waveformCache;
    /// 预览渲染器专用读取器工厂（仅 FFmpeg 构建下可用）。
    std::unique_ptr<MediaReaderFactory> previewFactory;
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

// =============================================================================
// 失败原因记录（B0-3）
// =============================================================================
// 为什么不给 286 个 `int32_t` 函数都加 `char** out_error`：
//   1. 会改变全部函数签名 → 破坏已发布的 NC_ABI_VERSION 1，所有平台层与
//      测试都要改；而其中 259 个返回点只是「句柄/参数为空」，为这类错误
//      强制每个 getter 多一个出参，调用方负担远大于收益；
//   2. 头文件已约定「单个 editor 句柄不允许多线程并发调用，由平台层串行化」，
//      因此一个**线程局部**的失败原因槽位在契约内是安全的。
//
// 用法：调用返回非 NC_OK 后，调 `nc_last_error()` 取原因（指针在下次失败或
// 线程结束前有效，**无需释放**，与 nc_free_string 的语义不同）。
//
// 覆盖范围：所有走 `fail(...)` 或 `failNc(...)` 的返回路径。
// 仍有少数函数直接 `return NC_ERROR_X;`，它们会保留上一次的原因——因此
// 每次进入失败路径都会覆写，不会读到更早的陈旧信息（见 failNc 的清空逻辑）。
thread_local std::string g_lastError;

int32_t failNc(int32_t code, const std::string& message) {
    g_lastError = message;
    return code;
}

int32_t failNc(int32_t code, const char* message) {
    g_lastError = (message != nullptr) ? message : "";
    return code;
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

/// 转发 core::Result 并保留其 message（B0-3）。
/// 原 `toNcError(code)` 只取错误码，把 `Result::message` 丢了——上层只能看到
/// 一个整数，用户看到的是「操作失败」。
int32_t failResult(const Result& result) {
    return failNc(toNcError(result.code), result.message);
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
#ifdef NAIROCLIP_HAS_FFMPEG
    // 预览渲染器读取器工厂（FFmpeg 构建下启用真实解码预览）
    handle->previewFactory = std::make_unique<FFmpegReaderFactory>();
#endif
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "参数或状态非法");
    }
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(nclip_text, loaded);
    if (!result.result.isOk()) {
        setDetail(detail, result.result.message);
        return failResult(result.result);
    }
    // 替换工程后必须重建编辑器（引用失效）。
    editor->project = std::move(loaded);
    delete editor->editor;
    editor->editor = new (std::nothrow) TimelineEditor(editor->project);
    if (editor->editor == nullptr) {
        setDetail(detail, "内存分配失败");
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
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

// ---- 工程信息（文档 Part 5「草稿数据结构」）----

char* nc_project_name(const NcEditor* editor) {
    if (editor == nullptr) {
        return nullptr;
    }
    return dupString(editor->project.name);
}

int32_t nc_set_project_name(NcEditor* editor, const char* name) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (name == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "name 为空");
    }
    // 名称长度防御（避免超长标题破坏 UI 与文件）
    const std::string text(name);
    if (text.size() > 200) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    editor->project.name = text;
    return NC_OK;
}

int32_t nc_project_canvas(const NcEditor* editor, int32_t* out_width, int32_t* out_height) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (out_width != nullptr) *out_width = main->width;
    if (out_height != nullptr) *out_height = main->height;
    return NC_OK;
}

int32_t nc_set_project_canvas(NcEditor* editor, int32_t width, int32_t height) {
    // 文档 Part 3-C「比例」：项目级全局设置；边界校验防极端尺寸。
    // 下限 16（保证可编码），上限 16384（常规编码上限量级）。
    if (editor == nullptr || width < 16 || height < 16 || width > 16384 || height > 16384) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    main->width = width;
    main->height = height;
    return NC_OK;
}

int32_t nc_project_frame_rate(const NcEditor* editor, int64_t* out_num, int64_t* out_den) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (out_num != nullptr) *out_num = main->frameRate.value.num;
    if (out_den != nullptr) *out_den = main->frameRate.value.den;
    return NC_OK;
}

int32_t nc_set_project_frame_rate(NcEditor* editor, int64_t num, int64_t den) {
    if (editor == nullptr || num <= 0 || den <= 0 || num > 1000 || den > 1000) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    main->frameRate = FrameRate::of(num, den);
    return NC_OK;
}

// ---- 历史（撤销/重做；文档 Part 5(续) 第32节）----

int32_t nc_can_undo(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    return editor->editor->canUndo() ? 1 : 0;
}

int32_t nc_can_redo(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    return editor->editor->canRedo() ? 1 : 0;
}

int32_t nc_undo_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(editor->editor->undoCount());
}

int32_t nc_redo_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(editor->editor->redoCount());
}

char* nc_undo_history_name(const NcEditor* editor, int32_t index) {
    if (editor == nullptr || index < 0) {
        return nullptr;
    }
    const std::vector<std::string> names = editor->editor->undoHistoryNames();
    if (static_cast<std::size_t>(index) >= names.size()) {
        return nullptr;
    }
    return dupString(names[static_cast<std::size_t>(index)]);
}

int32_t nc_clear_undo_history(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    editor->editor->clearUndoHistory();
    return NC_OK;
}

int32_t nc_set_undo_depth_limit(NcEditor* editor, int32_t max_steps) {
    if (editor == nullptr || max_steps < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    editor->editor->setUndoDepthLimit(static_cast<std::size_t>(max_steps));
    return NC_OK;
}

// ---- 素材管理（文档 Part 5「轨道数据」/ Part 6 素材丢失）----

int32_t nc_asset_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(editor->project.assets.size());
}

int32_t nc_asset_get(const NcEditor* editor, int32_t index, uint64_t* out_asset_id,
                     char** out_name, char** out_path, int32_t* out_type,
                     int64_t* out_duration_us) {
    if (editor == nullptr || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (static_cast<std::size_t>(index) >= editor->project.assets.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "editor 为空");
    }
    const MediaAsset& asset = editor->project.assets[static_cast<std::size_t>(index)];
    if (out_asset_id != nullptr) *out_asset_id = asset.id.value;
    if (out_name != nullptr) *out_name = dupString(asset.name);
    if (out_path != nullptr) *out_path = dupString(asset.path);
    if (out_type != nullptr) {
        switch (asset.type) {
            case MediaType::Video: *out_type = 0; break;
            case MediaType::Audio: *out_type = 1; break;
            case MediaType::Image: *out_type = 2; break;
            default: *out_type = 3; break;
        }
    }
    if (out_duration_us != nullptr) *out_duration_us = toMicroseconds(asset.duration);
    return NC_OK;
}

int32_t nc_remove_asset(NcEditor* editor, uint64_t asset_id) {
    if (editor == nullptr || asset_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    return failResult(editor->editor->removeAsset(AssetId{asset_id}));
}

int32_t nc_asset_is_referenced(const NcEditor* editor, uint64_t asset_id) {
    if (editor == nullptr || asset_id == 0) {
        return -1;
    }
    return editor->editor->isAssetReferenced(AssetId{asset_id}) ? 1 : 0;
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
        case 3: type = TrackType::Adjustment; break;
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    return failResult(editor->editor->undo());
}

int32_t nc_redo(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    return failResult(editor->editor->redo());
}

// ---- 轨道管理（文档 Part 2-B 第8节「轨道控制列」+ 第28节「新增轨道」）----

int32_t nc_track_get(const NcEditor* editor, int32_t track_index, uint64_t* out_track_id,
                     char** out_name, int32_t* out_type, int32_t* out_locked,
                     int32_t* out_muted, int32_t* out_hidden, int32_t* out_solo,
                     double* out_gain_db) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (out_track_id != nullptr) *out_track_id = track.id.value;
    if (out_name != nullptr) *out_name = dupString(track.name);
    if (out_type != nullptr) {
        switch (track.type) {
            case TrackType::Video: *out_type = 0; break;
            case TrackType::Audio: *out_type = 1; break;
            case TrackType::Subtitle: *out_type = 2; break;
            case TrackType::Adjustment: *out_type = 3; break;
        }
    }
    if (out_locked != nullptr) *out_locked = track.locked ? 1 : 0;
    if (out_muted != nullptr) *out_muted = track.muted ? 1 : 0;
    if (out_hidden != nullptr) *out_hidden = track.hidden ? 1 : 0;
    if (out_solo != nullptr) *out_solo = track.solo ? 1 : 0;
    if (out_gain_db != nullptr) *out_gain_db = track.gainDb;
    return NC_OK;
}

int32_t nc_track_get_view(const NcEditor* editor, int32_t track_index,
                          int32_t* out_height_px, int32_t* out_collapsed) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空或索引为负");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "轨道索引超出范围");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (out_height_px != nullptr) *out_height_px = track.heightPx;
    if (out_collapsed != nullptr) *out_collapsed = track.collapsed ? 1 : 0;
    return NC_OK;
}

int32_t nc_set_track_view(NcEditor* editor, int32_t track_index, int32_t height_px,
                          int32_t collapsed) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空或索引为负");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "轨道索引超出范围");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(
        editor->editor->setTrackHeight(main->id, trackId, height_px, collapsed != 0));
}

int32_t nc_remove_track(NcEditor* editor, int32_t track_index) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->removeTrack(main->id, trackId));
}

int32_t nc_move_track(NcEditor* editor, int32_t track_index, int32_t new_index) {
    if (editor == nullptr || track_index < 0 || new_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空或索引为负");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "轨道索引超出范围");
    }
    if (static_cast<std::size_t>(new_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "目标位置超出范围");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->moveTrack(main->id, trackId, new_index));
}

int32_t nc_rename_track(NcEditor* editor, int32_t track_index, const char* new_name) {
    if (editor == nullptr || track_index < 0 || new_name == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、new_name 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->renameTrack(main->id, trackId, new_name));
}

int32_t nc_set_track_locked(NcEditor* editor, int32_t track_index, int32_t locked) {
    if (editor == nullptr || track_index < 0 || (locked != 0 && locked != 1)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setTrackLocked(main->id, trackId, locked != 0));
}

int32_t nc_set_track_muted(NcEditor* editor, int32_t track_index, int32_t muted) {
    if (editor == nullptr || track_index < 0 || (muted != 0 && muted != 1)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setTrackMuted(main->id, trackId, muted != 0));
}

int32_t nc_set_track_hidden(NcEditor* editor, int32_t track_index, int32_t hidden) {
    if (editor == nullptr || track_index < 0 || (hidden != 0 && hidden != 1)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setTrackHidden(main->id, trackId, hidden != 0));
}

// ---- 片段编辑（文档 Part 3-A 第12节「剪辑」基础操作）----

namespace {

/// 解析主序列与轨道（片段编辑类接口共用；失败返回 false 并置 code）。
bool resolveTrack(const NcEditor* editor, int32_t track_index, SequenceId* outSequenceId,
                  TrackId* outTrackId, int32_t* code) {
    if (editor == nullptr || track_index < 0) {
        *code = NC_ERROR_INVALID_ARGUMENT;
        return false;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        *code = NC_ERROR_NOT_FOUND;
        return false;
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        *code = NC_ERROR_OUT_OF_RANGE;
        return false;
    }
    *outSequenceId = main->id;
    *outTrackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return true;
}

}  // namespace

int32_t nc_remove_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    return failResult(editor->editor->removeClip(seqId, trackId, ClipId{clip_id}));
}

int32_t nc_split_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id, int64_t at_us) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || at_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor->splitClip(seqId, trackId, ClipId{clip_id}, fromMicroseconds(at_us)));
}

int32_t nc_move_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                     int64_t new_start_us) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || new_start_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor
                         ->moveClip(seqId, trackId, ClipId{clip_id},
                                    fromMicroseconds(new_start_us)));
}

int32_t nc_move_clip_to_track(NcEditor* editor, int32_t from_track, int32_t to_track,
                              uint64_t clip_id, int64_t new_start_us) {
    if (editor == nullptr || from_track < 0 || to_track < 0 || clip_id == 0 ||
        new_start_us < 0 || from_track == to_track) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(from_track) >= main->tracks.size() ||
        static_cast<std::size_t>(to_track) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
    }
    const TrackId fromId = main->tracks[static_cast<std::size_t>(from_track)].id;
    const TrackId toId = main->tracks[static_cast<std::size_t>(to_track)].id;
    return failResult(editor->editor
                         ->moveClipToTrack(main->id, fromId, toId, ClipId{clip_id},
                                           fromMicroseconds(new_start_us)));
}

int32_t nc_trim_clip_start(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int64_t new_start_us) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || new_start_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor
                         ->trimClipStart(seqId, trackId, ClipId{clip_id},
                                         fromMicroseconds(new_start_us)));
}

int32_t nc_trim_clip_end(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                         int64_t new_end_us) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || new_end_us <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor
                         ->trimClipEnd(seqId, trackId, ClipId{clip_id},
                                       fromMicroseconds(new_end_us)));
}

int32_t nc_set_clip_speed(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                          int64_t speed_num, int64_t speed_den) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || speed_num <= 0 || speed_den <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor
                         ->setClipSpeed(seqId, trackId, ClipId{clip_id},
                                        Rational::make(speed_num, speed_den)));
}

int32_t nc_ripple_delete(NcEditor* editor, int32_t track_index, const uint64_t* clip_ids,
                         int32_t clip_count) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_ids == nullptr || clip_count <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "clip_ids 为空");
    }
    std::vector<ClipId> ids;
    ids.reserve(static_cast<std::size_t>(clip_count));
    for (int32_t i = 0; i < clip_count; ++i) {
        ids.push_back(ClipId{clip_ids[i]});
    }
    return failResult(editor->editor->rippleDelete(seqId, trackId, std::move(ids)));
}

uint64_t nc_duplicate_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    // 文档「剪辑 → 复制」：复制当前片段并插入到原片段紧随其后的位置。
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return 0;
    }
    const Track* track = findTrack(editor->project, seqId, trackId);
    if (track == nullptr) {
        return 0;
    }
    const Clip* source = track->findClip(ClipId{clip_id});
    if (source == nullptr) {
        return 0;
    }
    if (track->locked) {
        return 0;
    }
    Clip copy = *source;
    copy.id = ClipId{};  // 由 insertClip 分配
    copy.start = source->end();
    // 相邻位置若已被占用则由 validateClipPlacement 拒绝（UI 应先腾出空间）
    ClipId outId;
    const Result r = editor->editor->insertClip(seqId, trackId, std::move(copy), &outId);
    if (!r.isOk()) {
        return 0;
    }
    return outId.value;
}

uint64_t nc_freeze_frame(NcEditor* editor, int32_t track_index, uint64_t clip_id, int64_t at_us,
                         int64_t duration_us) {
    // 文档「剪辑 → 定格」：在播放头位置生成一帧静止画面并插入时间轴（默认 2s）。
    //
    // 实现（B3-2 起）：Clip::freeze = true。
    // 旧实现用 speed = 1/1000000 骗取「几乎不动」——因为 Clip::valid() 要求
    // speed 为正，无法直接写 speed = 0。那个做法能跑，但语义是巧合：
    // 一旦速度下限放宽、或换成按帧取整的 reader，定格就会悄悄失效。
    // 现在语义显式（sourceTimeAt 直接返回 sourceIn），且导出/预览/C ABI
    // 三条路径共用同一判定。
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return 0;
    }
    if (duration_us <= 0) {
        duration_us = 2000000;  // 默认 2 秒（文档）
    }
    const Track* track = findTrack(editor->project, seqId, trackId);
    if (track == nullptr || track->locked) {
        return 0;
    }
    const Clip* source = track->findClip(ClipId{clip_id});
    if (source == nullptr) {
        return 0;
    }
    Clip frozen = *source;
    frozen.id = ClipId{};
    // 定格帧的源时间：以 at_us（时间轴微秒）折算到源时间
    const Time at = fromMicroseconds(at_us);
    const Time local = at - source->start;
    const Time sourceAt = source->sourceTimeAt(local.isNegative() ? Time{} : local);
    frozen.sourceIn = sourceAt;
    frozen.start = at;
    frozen.duration = fromMicroseconds(duration_us);
    // 定格：源时间恒为 sourceIn（显式语义，见 Clip::freeze 注释）
    frozen.freeze = true;
    frozen.speed = Rational::make(1);  // 保持 1：sourceOut 不会越过素材范围
    frozen.reversed = false;
    frozen.speedCurve.clear();
    ClipId outId;
    const Result r = editor->editor->insertClip(seqId, trackId, std::move(frozen), &outId);
    if (!r.isOk()) {
        return 0;
    }
    return outId.value;
}

int32_t nc_reverse_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    // 文档「剪辑 → 倒放」：画面播放顺序反转。
    // 实现：设置 Clip::reversed（源时间映射反转），时间轴时长与源区间不变。
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    return failResult(editor->editor->setClipReversed(seqId, trackId, ClipId{clip_id}, true));
}

int32_t nc_clip_info(const NcEditor* editor, int32_t track_index, uint32_t clip_index,
                     uint64_t* out_clip_id, int64_t* out_start_us, int64_t* out_duration_us,
                     uint64_t* out_asset_id, int64_t* out_speed_num, int64_t* out_speed_den,
                     int32_t* out_enabled) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (static_cast<std::size_t>(clip_index) >= track.clips.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
    }
    const Clip& clip = track.clips[static_cast<std::size_t>(clip_index)];
    if (out_clip_id != nullptr) *out_clip_id = clip.id.value;
    if (out_start_us != nullptr) *out_start_us = toMicroseconds(clip.start);
    if (out_duration_us != nullptr) *out_duration_us = toMicroseconds(clip.duration);
    if (out_asset_id != nullptr) *out_asset_id = clip.asset.value;
    if (out_speed_num != nullptr) *out_speed_num = clip.speed.num;
    if (out_speed_den != nullptr) *out_speed_den = clip.speed.den;
    if (out_enabled != nullptr) *out_enabled = clip.enabled ? 1 : 0;
    return NC_OK;
}

int32_t nc_set_clip_enabled(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t enabled) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || (enabled != 0 && enabled != 1)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    return failResult(editor->editor
                         ->setClipEnabled(seqId, trackId, ClipId{clip_id}, enabled != 0));
}

int32_t nc_get_clip_opacity(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            double* out_opacity) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "clip 为空");
    }
    if (out_opacity != nullptr) {
        *out_opacity = clip->animation.sample(ClipProperty::Opacity, Time{});
    }
    return NC_OK;
}

int32_t nc_set_clip_opacity(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            double opacity) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    if (clip_id == 0 || opacity < 0.0 || opacity > 1.0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor
                         ->setClipOpacity(seqId, trackId, ClipId{clip_id}, opacity));
}

// ---- 滤镜（文档 Part 4-B 第 7 节「滤镜」）----

int32_t nc_set_clip_filter(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t style, double intensity) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    FilterStyle filterStyle;
    if (clip_id == 0 || !filterStyleFromInt(style, filterStyle)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    if (intensity < 0.0 || intensity > 100.0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    return failResult(editor->editor
                         ->setClipFilter(seqId, trackId, ClipId{clip_id}, filterStyle, intensity));
}

int32_t nc_get_clip_filter(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t* out_style, double* out_intensity) {
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
    if (out_style != nullptr) *out_style = static_cast<int32_t>(clip->filterStyle);
    if (out_intensity != nullptr) *out_intensity = clip->filterIntensity;
    return clip->hasFilter() ? 1 : 0;
}

const char* nc_filter_display_name(int32_t style) {
    FilterStyle filterStyle;
    if (!filterStyleFromInt(style, filterStyle)) {
        return "无";
    }
    return filterStyleDisplayName(filterStyle);
}

// ---- 背景填充（文档 Part 4-C 第 11 节「背景」）----

int32_t nc_set_clip_background(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               int32_t mode, uint32_t rgba, double intensity) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    BackgroundMode backgroundMode;
    if (clip_id == 0 || !backgroundModeFromInt(mode, backgroundMode)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    return failResult(editor->editor
                         ->setClipBackground(seqId, trackId, ClipId{clip_id}, backgroundMode,
                                             rgba, intensity));
}

int32_t nc_get_clip_background(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               int32_t* out_mode, uint32_t* out_rgba, double* out_intensity) {
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
    if (out_mode != nullptr) *out_mode = static_cast<int32_t>(clip->backgroundMode);
    if (out_rgba != nullptr) *out_rgba = clip->backgroundColorRgba;
    if (out_intensity != nullptr) *out_intensity = clip->backgroundIntensity;
    return clip->hasBackground() ? 1 : 0;
}

// ---- 变速曲线预设（文档 Part 4-A 1.1）----

const char* nc_speed_curve_preset_name(int32_t preset) {
    SpeedCurvePreset value;
    if (!speedCurvePresetFromInt(preset, value)) {
        return "custom";
    }
    return speedCurvePresetName(value);
}

const char* nc_speed_curve_preset_display_name(int32_t preset) {
    SpeedCurvePreset value;
    if (!speedCurvePresetFromInt(preset, value)) {
        return "自定义";
    }
    return speedCurvePresetDisplayName(value);
}

int32_t nc_apply_speed_curve_preset(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                                    int32_t preset, int64_t source_duration_us) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    SpeedCurvePreset presetValue;
    if (clip_id == 0 || !speedCurvePresetFromInt(preset, presetValue)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    // 源区间：调用方指定或按片段实际（sourceIn..sourceOut）
    Time sourceDuration = fromMicroseconds(source_duration_us > 0 ? source_duration_us : 0);
    if (!sourceDuration.isPositive()) {
        const Track* track = findTrack(editor->project, seqId, trackId);
        const Clip* clip = track != nullptr ? track->findClip(ClipId{clip_id}) : nullptr;
        if (clip == nullptr) {
            return failNc(NC_ERROR_NOT_FOUND, "clip 为空");
        }
        sourceDuration = clip->sourceOut() - clip->sourceIn;
    }
    SpeedCurve curve = buildSpeedCurvePreset(presetValue, sourceDuration);
    return failResult(editor->editor->setClipSpeedCurve(seqId, trackId, ClipId{clip_id}, std::move(curve)));
}

// ---- 镜头追踪（文档 Part 4-A 3.3）----

namespace {

/// 查找字幕（按 ID；跨主序列）。
SubtitleCue* findSubtitleCue(NcEditor* editor, uint64_t subtitle_id) {
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return nullptr;
    }
    return main->subtitles.findCue(SubtitleId{subtitle_id});
}

const SubtitleCue* findSubtitleCueConst(const NcEditor* editor, uint64_t subtitle_id) {
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return nullptr;
    }
    return main->subtitles.findCue(SubtitleId{subtitle_id});
}

}  // namespace

int32_t nc_set_camera_tracking(NcEditor* editor, uint64_t subtitle_id, const double* times_us,
                               const double* center_x, const double* center_y,
                               int32_t point_count, int32_t apply_mode) {
    if (editor == nullptr || subtitle_id == 0 || point_count <= 0 || point_count > 4096 ||
        times_us == nullptr || center_x == nullptr || center_y == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、times_us、center_x、center_y 为空");
    }
    SubtitleCue* cue = findSubtitleCue(editor, subtitle_id);
    if (cue == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "cue 为空");
    }
    std::vector<SubtitleCue::TrackingPoint> points;
    points.reserve(static_cast<std::size_t>(point_count));
    for (int32_t i = 0; i < point_count; ++i) {
        if (times_us[i] < 0.0) {
            return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
        }
        SubtitleCue::TrackingPoint point;
        point.time = fromMicroseconds(static_cast<std::int64_t>(times_us[i] + 0.5));
        point.centerX = std::clamp(center_x[i], 0.0, 1.0);
        point.centerY = std::clamp(center_y[i], 0.0, 1.0);
        points.push_back(point);
    }
    // 时间升序（追踪采样天然有序；防御性排序）
    std::sort(points.begin(), points.end(),
              [](const SubtitleCue::TrackingPoint& a, const SubtitleCue::TrackingPoint& b) {
                  return a.time < b.time;
              });
    cue->tracking = std::move(points);
    cue->trackingMode = (apply_mode == 1) ? 1 : 0;
    return NC_OK;
}

int32_t nc_set_camera_tracking_scaled(NcEditor* editor, uint64_t subtitle_id,
                                      const double* times_us, const double* center_x,
                                      const double* center_y, const double* scales,
                                      int32_t point_count, int32_t apply_mode) {
    if (editor == nullptr || subtitle_id == 0 || point_count <= 0 || point_count > 4096 ||
        times_us == nullptr || center_x == nullptr || center_y == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、times_us、center_x、center_y 为空");
    }
    SubtitleCue* cue = findSubtitleCue(editor, subtitle_id);
    if (cue == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "cue 为空");
    }
    std::vector<SubtitleCue::TrackingPoint> points;
    points.reserve(static_cast<std::size_t>(point_count));
    for (int32_t i = 0; i < point_count; ++i) {
        if (times_us[i] < 0.0) {
            return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
        }
        SubtitleCue::TrackingPoint point;
        point.time = fromMicroseconds(static_cast<std::int64_t>(times_us[i] + 0.5));
        point.centerX = std::clamp(center_x[i], 0.0, 1.0);
        point.centerY = std::clamp(center_y[i], 0.0, 1.0);
        // 缩放范围防御（0.1x ~ 10x；文档未给定上限，按合理工程范围钳制）
        point.scale = scales != nullptr ? std::clamp(scales[i], 0.1, 10.0) : 1.0;
        points.push_back(point);
    }
    std::sort(points.begin(), points.end(),
              [](const SubtitleCue::TrackingPoint& a, const SubtitleCue::TrackingPoint& b) {
                  return a.time < b.time;
              });
    cue->tracking = std::move(points);
    cue->trackingMode = (apply_mode == 1) ? 1 : 0;
    return NC_OK;
}

int32_t nc_tracking_point_get_scaled(const NcEditor* editor, uint64_t subtitle_id, int32_t index,
                                     double* out_time_us, double* out_center_x,
                                     double* out_center_y, double* out_scale) {
    if (editor == nullptr || subtitle_id == 0 || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const SubtitleCue* cue = findSubtitleCueConst(editor, subtitle_id);
    if (cue == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "cue 为空");
    }
    if (static_cast<std::size_t>(index) >= cue->tracking.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "cue 为空");
    }
    const auto& point = cue->tracking[static_cast<std::size_t>(index)];
    if (out_time_us != nullptr) *out_time_us = static_cast<double>(toMicroseconds(point.time));
    if (out_center_x != nullptr) *out_center_x = point.centerX;
    if (out_center_y != nullptr) *out_center_y = point.centerY;
    if (out_scale != nullptr) *out_scale = point.scale;
    return NC_OK;
}

int32_t nc_tracking_point_count(const NcEditor* editor, uint64_t subtitle_id) {
    if (editor == nullptr || subtitle_id == 0) {
        return -1;
    }
    const SubtitleCue* cue = findSubtitleCueConst(editor, subtitle_id);
    if (cue == nullptr) {
        return -1;
    }
    return static_cast<int32_t>(cue->tracking.size());
}

int32_t nc_tracking_point_get(const NcEditor* editor, uint64_t subtitle_id, int32_t index,
                              double* out_time_us, double* out_center_x, double* out_center_y) {
    if (editor == nullptr || subtitle_id == 0 || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const SubtitleCue* cue = findSubtitleCueConst(editor, subtitle_id);
    if (cue == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "cue 为空");
    }
    if (static_cast<std::size_t>(index) >= cue->tracking.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "cue 为空");
    }
    const auto& point = cue->tracking[static_cast<std::size_t>(index)];
    if (out_time_us != nullptr) *out_time_us = static_cast<double>(toMicroseconds(point.time));
    if (out_center_x != nullptr) *out_center_x = point.centerX;
    if (out_center_y != nullptr) *out_center_y = point.centerY;
    return NC_OK;
}

int32_t nc_clear_camera_tracking(NcEditor* editor, uint64_t subtitle_id) {
    if (editor == nullptr || subtitle_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    SubtitleCue* cue = findSubtitleCue(editor, subtitle_id);
    if (cue == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "cue 为空");
    }
    cue->tracking.clear();
    cue->trackingMode = 0;
    return NC_OK;
}

// ---- 深度图效果（文档 Part 4-C 13.2）----

int32_t nc_set_depth_effect(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t mode, double focus_distance, double aperture,
                            double intensity, uint32_t color_rgba) {
    SequenceId seqId;
    TrackId trackId;
    int32_t code = NC_OK;
    if (!resolveTrack(editor, track_index, &seqId, &trackId, &code)) {
        return code;
    }
    DepthEffectMode depthMode;
    if (clip_id == 0 || !depthEffectModeFromInt(mode, depthMode)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    // 参数范围校验（文档 13.2）
    if (focus_distance < 0.0 || focus_distance > 1.0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    if (aperture < 1.0 || aperture > 22.0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    if (intensity < 0.0 || intensity > 100.0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    const Track* track = findTrack(editor->project, seqId, trackId);
    if (track == nullptr || track->locked) {
        return failNc(NC_ERROR_LOCKED, "track 为空");
    }
    Clip* clip = findClip(editor->project, seqId, trackId, ClipId{clip_id});
    if (clip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "clip 为空");
    }
    clip->depthEffect = depthMode;
    clip->depthFocusDistance = focus_distance;
    clip->depthAperture = aperture;
    clip->depthIntensity = intensity;
    clip->depthColorRgba = color_rgba;
    return NC_OK;
}

int32_t nc_get_depth_effect(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t* out_mode, double* out_focus_distance,
                            double* out_aperture, double* out_intensity,
                            uint32_t* out_color_rgba) {
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
    if (out_mode != nullptr) *out_mode = static_cast<int32_t>(clip->depthEffect);
    if (out_focus_distance != nullptr) *out_focus_distance = clip->depthFocusDistance;
    if (out_aperture != nullptr) *out_aperture = clip->depthAperture;
    if (out_intensity != nullptr) *out_intensity = clip->depthIntensity;
    if (out_color_rgba != nullptr) *out_color_rgba = clip->depthColorRgba;
    return clip->hasDepthEffect() ? 1 : 0;
}

// ---- 自定义关键帧转场（文档附录二）----

namespace {

/// 查找转场（跨主序列轨道）。
const Transition* findTransitionInTrack(const NcEditor* editor, int32_t track_index,
                                        uint64_t transition_id) {
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return nullptr;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    for (const auto& tr : track.transitions) {
        if (tr.id.value == transition_id) {
            return &tr;
        }
    }
    return nullptr;
}

}  // namespace

int64_t nc_transition_duration_us(const NcEditor* editor, int32_t track_index,
                                  uint64_t transition_id) {
    if (editor == nullptr || track_index < 0 || transition_id == 0) {
        return -1;
    }
    const Transition* tr = findTransitionInTrack(editor, track_index, transition_id);
    if (tr == nullptr) {
        return -1;
    }
    return toMicroseconds(tr->duration);
}

int32_t nc_add_transition_keyframe(NcEditor* editor, int32_t track_index,
                                   uint64_t transition_id, int32_t property, int64_t offset_us,
                                   double value, int32_t easing) {
    if (editor == nullptr || track_index < 0 || transition_id == 0 || property < 0 ||
        property > 5 || offset_us < 0 || easing < 0 || easing > 4) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Transition* tr = findTransitionInTrack(editor, track_index, transition_id);
    if (tr == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "tr 为空");
    }
    if (offset_us > toMicroseconds(tr->duration)) {
        return NC_ERROR_OUT_OF_RANGE;  // 超出转场区间（文档附录二：仅区间内）
    }
    // 位置 → from 片段的本地时间：转场起点在 from 片段尾部
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* fromClip = track.findClip(tr->fromClip);
    if (fromClip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "fromClip 为空");
    }
    // 转场区间起点 = from 片段终点（与导出计划编译一致：compiled.start = from->end()）
    const Time transitionStart = fromClip->end();
    const Time keyframeTime = transitionStart + fromMicroseconds(offset_us);
    const Time localTime = keyframeTime - fromClip->start;
    if (localTime.isNegative()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "调用失败");
    }
    Keyframe keyframe;
    keyframe.time = localTime;
    keyframe.value = value;
    // 缓动映射（0 线性 / 1 缓入 / 2 缓出 / 3 缓入缓出 / 4 阶梯）
    switch (easing) {
        case 1: keyframe.easing = Easing::easeIn(); break;
        case 2: keyframe.easing = Easing::easeOut(); break;
        case 3: keyframe.easing = Easing::easeInOut(); break;
        case 4: keyframe.easing = Easing::stepped(); break;
        default: keyframe.easing = Easing::linear(); break;
    }
    return failResult(editor->editor
                         ->insertKeyframe(main->id, track.id, fromClip->id,
                                          static_cast<ClipProperty>(property), keyframe));
}

int32_t nc_transition_keyframe_count(const NcEditor* editor, int32_t track_index,
                                     uint64_t transition_id, int32_t property) {
    if (editor == nullptr || track_index < 0 || transition_id == 0 || property < 0 ||
        property > 5) {
        return -1;
    }
    const Transition* tr = findTransitionInTrack(editor, track_index, transition_id);
    if (tr == nullptr) {
        return -1;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return -1;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* fromClip = track.findClip(tr->fromClip);
    if (fromClip == nullptr) {
        return -1;
    }
    const KeyframeTrack* curve =
        fromClip->animation.findCurve(static_cast<ClipProperty>(property));
    if (curve == nullptr) {
        return 0;
    }
    // 仅统计落在转场区间内的关键帧（相对 from 片段本地时间）
    const Time rangeStart = fromClip->end() - fromClip->start;  // = from 片段时长
    const Time rangeEnd = rangeStart + tr->duration;
    int32_t count = 0;
    for (const auto& keyframe : curve->keyframes()) {
        if (!(keyframe.time < rangeStart) && keyframe.time <= rangeEnd) {
            ++count;
        }
    }
    return count;
}

int32_t nc_transition_keyframe_get(const NcEditor* editor, int32_t track_index,
                                   uint64_t transition_id, int32_t property, int32_t index,
                                   double* out_offset_us, double* out_value,
                                   int32_t* out_easing) {
    if (editor == nullptr || track_index < 0 || transition_id == 0 || property < 0 ||
        property > 5 || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Transition* tr = findTransitionInTrack(editor, track_index, transition_id);
    if (tr == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "tr 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* fromClip = track.findClip(tr->fromClip);
    if (fromClip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "fromClip 为空");
    }
    const KeyframeTrack* curve =
        fromClip->animation.findCurve(static_cast<ClipProperty>(property));
    if (curve == nullptr) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "curve 为空");
    }
    const Time rangeStart = fromClip->end() - fromClip->start;  // = from 片段时长
    const Time rangeEnd = rangeStart + tr->duration;
    int32_t seen = 0;
    for (const auto& keyframe : curve->keyframes()) {
        if (!(keyframe.time < rangeStart) && keyframe.time <= rangeEnd) {
            if (seen == index) {
                if (out_offset_us != nullptr) {
                    *out_offset_us = static_cast<double>(
                        toMicroseconds(keyframe.time - rangeStart));
                }
                if (out_value != nullptr) *out_value = keyframe.value;
                if (out_easing != nullptr) {
                    switch (keyframe.easing.type) {
                        case EasingType::EaseIn: *out_easing = 1; break;
                        case EasingType::EaseOut: *out_easing = 2; break;
                        case EasingType::EaseInOut: *out_easing = 3; break;
                        case EasingType::Stepped: *out_easing = 4; break;
                        default: *out_easing = 0; break;
                    }
                }
                return NC_OK;
            }
            ++seen;
        }
    }
    return failNc(NC_ERROR_OUT_OF_RANGE, "调用失败");
}

int32_t nc_remove_transition_keyframe(NcEditor* editor, int32_t track_index,
                                      uint64_t transition_id, int32_t property,
                                      int64_t offset_us) {
    if (editor == nullptr || track_index < 0 || transition_id == 0 || property < 0 ||
        property > 5 || offset_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Transition* tr = findTransitionInTrack(editor, track_index, transition_id);
    if (tr == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "tr 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* fromClip = track.findClip(tr->fromClip);
    if (fromClip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "fromClip 为空");
    }
    const Time keyframeLocal =
        (fromClip->end() - fromClip->start) + fromMicroseconds(offset_us);
    return failResult(editor->editor
                         ->removeKeyframe(main->id, track.id, fromClip->id,
                                          static_cast<ClipProperty>(property), keyframeLocal));
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    return failResult(editor->editor->removeSubtitle(main->id, SubtitleId{subtitle_id}));
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(index) >= main->subtitles.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
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
    // 类型边界：按 TransitionType 枚举最大值（新增转场时无需改此处；
    // 修复——此前硬编码 7，文档新增的 26 种转场无法经 C ABI 添加）。
    if (transition_type < 0 ||
        transition_type > static_cast<int32_t>(TransitionType::OldFilmScratch)) {
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->removeTransition(main->id, trackId, TransitionId{transition_id}));
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (static_cast<std::size_t>(index) >= track.transitions.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
    }
    const Transition& tr = track.transitions[static_cast<std::size_t>(index)];
    if (out_type != nullptr) *out_type = static_cast<int32_t>(tr.type);
    if (out_duration_us != nullptr) *out_duration_us = toMicroseconds(tr.duration);
    if (out_from_clip != nullptr) *out_from_clip = tr.fromClip.value;
    if (out_to_clip != nullptr) *out_to_clip = tr.toClip.value;
    if (out_name != nullptr) *out_name = dupString(tr.name);
    return NC_OK;
}

int32_t nc_transition_get_id(const NcEditor* editor, int32_t track_index, int32_t index,
                             uint64_t* out_id) {
    if (editor == nullptr || track_index < 0 || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空或索引为负");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (static_cast<std::size_t>(index) >= track.transitions.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "转场序号超出范围");
    }
    if (out_id != nullptr) *out_id = track.transitions[static_cast<std::size_t>(index)].id.value;
    return NC_OK;
}

// ---- 转场库（文档 Part 4-B 第 6 节「转场」9 分类）----

int32_t nc_transition_type_count(void) {
    return static_cast<int32_t>(TransitionType::OldFilmScratch) + 1;
}

const char* nc_transition_type_name(int32_t type) {
    if (type < 0 || type > static_cast<int32_t>(TransitionType::OldFilmScratch)) {
        return "未知转场";
    }
    return transitionName(static_cast<TransitionType>(type));
}

const char* nc_transition_category(int32_t type) {
    if (type < 0 || type > static_cast<int32_t>(TransitionType::OldFilmScratch)) {
        return "";
    }
    return transitionCategory(static_cast<TransitionType>(type));
}

int32_t nc_transition_supports_direction(int32_t type) {
    if (type < 0 || type > static_cast<int32_t>(TransitionType::OldFilmScratch)) {
        return 0;
    }
    return transitionSupportsDirection(static_cast<TransitionType>(type)) ? 1 : 0;
}

int32_t nc_clip_get(const NcEditor* editor, int32_t track_index, int32_t clip_index,
                    int64_t* out_start_us, int64_t* out_duration_us, uint64_t* out_asset_id) {
    if (editor == nullptr || track_index < 0 || clip_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (static_cast<std::size_t>(clip_index) >= track.clips.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
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
    if (type <= 0 || type > 35) {
        return 0;  // 0 = None 无效；1..35 有效（基础 + 画面/氛围/动感/人物特效）
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->removeEffect(main->id, trackId, ClipId{clip_id}, EffectId{effect_id}));
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "clip 为空");
    }
    if (static_cast<std::size_t>(index) >= clip->effects.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "clip 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (property < 0 || property > 5) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    Keyframe k;
    k.time = fromMicroseconds(time_us);
    k.value = value;
    return failResult(editor->editor
                         ->insertKeyframe(main->id, trackId, ClipId{clip_id},
                                          static_cast<ClipProperty>(property), k));
}

int32_t nc_remove_keyframe(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t property, int64_t time_us) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || time_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (property < 0 || property > 5) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor
                         ->removeKeyframe(main->id, trackId, ClipId{clip_id},
                                          static_cast<ClipProperty>(property),
                                          fromMicroseconds(time_us)));
}

int32_t nc_set_clip_audio(NcEditor* editor, int32_t track_index, uint64_t clip_id, double gain_db,
                          int64_t fade_in_us, int64_t fade_out_us) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || fade_in_us < 0 ||
        fade_out_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor
                         ->setClipAudio(main->id, trackId, ClipId{clip_id}, gain_db,
                                        fromMicroseconds(fade_in_us),
                                        fromMicroseconds(fade_out_us)));
}

int32_t nc_set_clip_denoise(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t strength) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    DenoiseStrength denoiseStrength;
    if (!denoiseStrengthFromInt(strength, denoiseStrength)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setClipDenoise(main->id, trackId, ClipId{clip_id}, denoiseStrength));
}

int32_t nc_get_clip_denoise(const NcEditor* editor, int32_t track_index, uint64_t clip_id) {
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
    return static_cast<int32_t>(clip->denoise);
}

int32_t nc_set_frame_fit(NcEditor* editor, int32_t mode) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    FrameFit fit;
    if (!frameFitFromInt(mode, fit)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    main->backgroundRgba = rgba;
    return NC_OK;
}

int32_t nc_set_track_auto_duck(NcEditor* editor, int32_t track_index, int32_t enabled,
                               double sensitivity, double duck_amount_db, int64_t attack_us,
                               int64_t release_us, int64_t hold_us) {
    if (editor == nullptr || track_index < 0 || attack_us < 0 || release_us < 0 ||
        hold_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    AutoDuckSettings settings;
    settings.enabled = enabled != 0;
    settings.sensitivity = sensitivity;
    settings.duckAmountDb = duck_amount_db;
    settings.attackSeconds = static_cast<double>(attack_us) / 1000000.0;
    settings.releaseSeconds = static_cast<double>(release_us) / 1000000.0;
    settings.holdSeconds = static_cast<double>(hold_us) / 1000000.0;
    return failResult(editor->editor->setTrackAutoDuck(main->id, trackId, settings));
}

int32_t nc_get_track_auto_duck(const NcEditor* editor, int32_t track_index, int32_t* out_enabled,
                               double* out_sensitivity, double* out_duck_amount_db) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、clip_ids 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
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
        return failResult(r);
    }
    if (out_sequence_id != nullptr) *out_sequence_id = outSeq.value;
    if (out_clip_id != nullptr) *out_clip_id = outClip.value;
    return NC_OK;
}

int32_t nc_unwrap_compound_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->unwrapCompoundClip(main->id, trackId, ClipId{clip_id}));
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setTrackSolo(main->id, trackId, solo != 0));
}

int32_t nc_set_track_gain(NcEditor* editor, int32_t track_index, double gain_db) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setTrackGain(main->id, trackId, gain_db));
}

int32_t nc_get_track_mixer(const NcEditor* editor, int32_t track_index, int32_t* out_muted,
                           int32_t* out_solo, double* out_gain_db) {
    if (editor == nullptr || track_index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    if (out_muted != nullptr) *out_muted = track.muted ? 1 : 0;
    if (out_solo != nullptr) *out_solo = track.solo ? 1 : 0;
    if (out_gain_db != nullptr) *out_gain_db = track.gainDb;
    return NC_OK;
}

int32_t nc_set_master_gain(NcEditor* editor, double gain_db) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    return failResult(editor->editor->setMasterGain(main->id, gain_db));
}

int32_t nc_get_master_gain(const NcEditor* editor, double* out_gain_db) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (out_gain_db != nullptr) *out_gain_db = main->masterGainDb;
    return NC_OK;
}

int32_t nc_set_entitlement(NcEditor* editor, int32_t tier, int32_t lifetime,
                           int64_t expires_at_unix) {
    if (editor == nullptr || (tier != 0 && tier != 1)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    ProxyStatus proxyStatus;
    if (status < 0 || status > 3) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    proxyStatus = static_cast<ProxyStatus>(status);
    if (editor->project.findAsset(AssetId{asset_id}) == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "参数 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    PerformanceMode performanceMode;
    if (!performanceModeFromInt(mode, performanceMode)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    MaskShape maskShape;
    if (!maskShapeFromInt(shape, maskShape)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
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
    return failResult(editor->editor->setClipMask(main->id, trackId, ClipId{clip_id}, mask));
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

// ---- 智能抠像 / 色度抠像 ----

int32_t nc_matting_access(const NcEditor* editor, int32_t quality, int32_t* out_quality,
                          char** out_reason) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    MattingQuality requested;
    if (!mattingQualityFromInt(quality, requested)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    const bool proActive = editor->entitlement.proActiveAt(
        static_cast<std::int64_t>(std::time(nullptr)));
    const MattingAccess access = mattingAccessFor(proActive, requested);
    if (out_quality != nullptr) *out_quality = static_cast<int32_t>(access.quality);
    if (out_reason != nullptr) *out_reason = dupString(access.reason);
    return access.allowed ? 1 : 0;
}

int32_t nc_set_clip_chroma_key(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               const char* key_color, double inner_threshold,
                               double outer_threshold, double spill_suppression,
                               int32_t quality, int32_t enabled) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    MattingQuality requested;
    if (!mattingQualityFromInt(quality, requested)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    if (enabled == 0) {
        return failResult(editor->editor->setClipChromaKey(main->id, trackId, ClipId{clip_id}, ChromaKey{},
                                             false));
    }
    // Pro 门控（fail-closed：非 Pro 不允许色度抠像；文档权益矩阵 Pro 专属）
    const bool proActive = editor->entitlement.proActiveAt(
        static_cast<std::int64_t>(std::time(nullptr)));
    const MattingAccess access = mattingAccessFor(proActive, requested);
    if (!access.allowed) {
        return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
    }
    ChromaKey key;
    key.keyColorRgb = 0x00FF00;
    if (key_color != nullptr && key_color[0] != '\0') {
        const std::uint32_t parsed = chromaKeyColorFromString(key_color);
        if (parsed == 0xFFFFFFFFu) {
            return NC_ERROR_INVALID_ARGUMENT;  // 键控色非法
        }
        key.keyColorRgb = parsed;
    }
    key.innerThreshold = inner_threshold > 0.0 ? inner_threshold : key.innerThreshold;
    key.outerThreshold = outer_threshold > 0.0 ? outer_threshold : key.outerThreshold;
    key.spillSuppression =
        spill_suppression >= 0.0 ? spill_suppression : key.spillSuppression;
    if (!key.valid()) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "取值超出允许范围");
    }
    key = chromaKeyForQuality(key, access.quality);
    return failResult(editor->editor->setClipChromaKey(main->id, trackId, ClipId{clip_id}, key, true));
}

int32_t nc_get_clip_chroma_key(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               uint32_t* out_key_color_rgb, double* out_inner,
                               double* out_outer, double* out_spill) {
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
    if (!clip->hasChromaKey) {
        return 0;
    }
    if (out_key_color_rgb != nullptr) *out_key_color_rgb = clip->chromaKey.keyColorRgb;
    if (out_inner != nullptr) *out_inner = clip->chromaKey.innerThreshold;
    if (out_outer != nullptr) *out_outer = clip->chromaKey.outerThreshold;
    if (out_spill != nullptr) *out_spill = clip->chromaKey.spillSuppression;
    return 1;
}

double nc_chroma_key_estimate(const uint8_t* rgba, int32_t width, int32_t height,
                              const char* key_color, double inner_threshold,
                              double outer_threshold, double spill_suppression,
                              int32_t quality) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return -1.0;
    }
    MattingQuality requested;
    if (!mattingQualityFromInt(quality, requested)) {
        return -1.0;
    }
    ChromaKey key;
    if (key_color != nullptr && key_color[0] != '\0') {
        const std::uint32_t parsed = chromaKeyColorFromString(key_color);
        if (parsed == 0xFFFFFFFFu) {
            return -1.0;
        }
        key.keyColorRgb = parsed;
    }
    key.innerThreshold = inner_threshold > 0.0 ? inner_threshold : key.innerThreshold;
    key.outerThreshold = outer_threshold > 0.0 ? outer_threshold : key.outerThreshold;
    key.spillSuppression =
        spill_suppression >= 0.0 ? spill_suppression : key.spillSuppression;
    if (!key.valid()) {
        return -1.0;
    }
    key = chromaKeyForQuality(key, requested);
    return chromaKeyCoverageRatio(rgba, width, height, key, 2);
}

// ---- 画中画混合模式（文档第二段 5.9）----

int32_t nc_set_clip_blend_mode(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               int32_t mode) {
    if (editor == nullptr || track_index < 0 || clip_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    BlendMode blendMode;
    if (!blendModeFromInt(mode, blendMode)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    return failResult(editor->editor->setClipBlendMode(main->id, trackId, ClipId{clip_id}, blendMode));
}

int32_t nc_get_clip_blend_mode(const NcEditor* editor, int32_t track_index, uint64_t clip_id) {
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
    return static_cast<int32_t>(clip->blendMode);
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
    // 必须同步 executor 侧：ExportExecutorOptions::outputAudio 默认是 true，
    // 不跟着 include_audio 走的话，include_audio=0 时会出现
    // 「executor 照样解码并混合整条音轨 → 回调 writeAudioBlock →
    //    writer 因 audioCodec==nullptr 静默丢弃」。
    // 结果是对的（输出无音轨），但白做一遍全音频解码 + 重采样，
    // 导出耗时和内存都翻倍；也让「按需关音频」这个参数形同虚设。
    config.executor.outputAudio = include_audio != 0;
    config.executor.attachAiLabel = attach_ai_label != 0;
    if (ai_label_text != nullptr && ai_label_text[0] != '\0') {
        config.executor.aiLabelText = ai_label_text;
    }
    // AI 隐式标识元数据（《人工智能生成合成内容标识办法》要求）：
    // 附加 AI 标识的导出自动写入容器元数据（显式标识 + 隐式标识成对出现）。
    if (attach_ai_label != 0) {
        const std::string feature =
            ai_label_text != nullptr && ai_label_text[0] != '\0' ? ai_label_text
                                                                 : "AI 生成";
        const std::vector<AiMetadataEntry> entries = buildAiMetadata("NairoClip", feature);
        for (const auto& entry : entries) {
            config.writer.aiMetadata.emplace_back(entry.key, entry.value);
        }
    }
    // 隐形水印（Pro 内容溯源；由 nc_watermark_set_export 配置）
    config.executor.invisibleWatermark = editor->invisibleWatermark;
    // 会员门控：免费/过期会员 → 输出降级 1080p + 附加水印（文档 3.10）。
    // 使用系统时钟做到期判定；tier 由平台层经服务端心跳维护（文档第 8 部分）。
    config.policy = makeExportPolicy(editor->entitlement,
                                     static_cast<std::int64_t>(std::time(nullptr)));
    // ---------------------------------------------------------------------
    // 线程安全：导出使用 Project **快照**，不直接读 editor->project。
    // ---------------------------------------------------------------------
    // 背景：runExport 会对 project 做只读访问（subtitlesAt + AudioMixer::prepare），
    // 但它在调用线程中持续整个导出过程（可能数分钟）。期间若用户在 UI 线程
    // 继续编辑（移动/删除片段），会与导出线程的遍历并发 —— 迭代器失效、
    // 读到已释放内存，表现为随机崩溃。
    //
    // 方案：在**调用线程**（此处）做一次深拷贝，导出全程只读快照。
    // 语义清晰：用户点下「导出」那一刻的画面状态即最终成片，
    // 之后的编辑不影响本次导出（与专业软件一致）。
    //
    // 代价：一次 Project 深拷贝（含 assets/sequences/tracks/clips 的 vector），
    // 典型工程 < 1MB，耗时远小于导出本身。
    const Project exportSnapshot = editor->project;
    const ExportRunResult result = runExport(
        plan, readerFactory, writerFactory, config,
        [progress_cb](double p) {
            if (progress_cb == nullptr) return true;
            return progress_cb(p) != 0;
        },
        &exportSnapshot);
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
        return failNc(NC_ERROR_INTERNAL, "items 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "info 为空");
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
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    if (findBuiltinModel(model_id) == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "editor、model_id 为空");
    }
    return failResult(modelDownloads().begin(model_id, total_bytes, now_unix));
}

int32_t nc_model_download_progress(NcEditor* editor, const char* model_id,
                                   int64_t downloaded_bytes, int64_t now_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    return failResult(modelDownloads().markProgress(model_id, downloaded_bytes, now_unix));
}

int32_t nc_model_download_pause(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    return failResult(modelDownloads().pause(model_id, now_unix));
}

int32_t nc_model_download_resume(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    return failResult(modelDownloads().resume(model_id, now_unix));
}

int32_t nc_model_download_cancel(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    return failResult(modelDownloads().cancel(model_id, now_unix));
}

int32_t nc_model_download_finish(NcEditor* editor, const char* model_id, int64_t now_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    return failResult(modelDownloads().finishDownload(model_id, now_unix));
}

int32_t nc_model_download_fail(NcEditor* editor, const char* model_id, const char* reason,
                               int64_t now_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    const std::string text = reason != nullptr ? reason : "下载失败";
    return failResult(modelDownloads().fail(model_id, text, now_unix));
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
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "info 为空");
    }
    const DownloadResumePlan plan =
        planDownloadResume(info->id, info->version, downloaded_bytes);
    if (!plan.valid) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、model_id、ciphertext 为空");
    }
    const ModelInfo* info = findBuiltinModel(model_id);
    if (info == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "info 为空");
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
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_id == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_id、version、key、ciphertext 为空");
    }
    std::vector<std::uint8_t> payload;
    if (size > 0) {
        payload.assign(ciphertext, ciphertext + size);
    }
    std::vector<std::uint8_t> plain = decryptModelPayloadAt(
        key, model_id, version, static_cast<std::uint64_t>(offset), payload);
    if (plain.size() != payload.size()) {
        return failNc(NC_ERROR_INTERNAL, "调用失败");
    }
    if (out_size != nullptr) *out_size = static_cast<int64_t>(plain.size());
    if (out_plaintext != nullptr) {
        if (plain.empty()) {
            *out_plaintext = nullptr;
        } else {
            std::uint8_t* buffer = new (std::nothrow) std::uint8_t[plain.size()];
            if (buffer == nullptr) {
                return failNc(NC_ERROR_INTERNAL, "buffer 为空");
            }
            std::memcpy(buffer, plain.data(), plain.size());
            *out_plaintext = buffer;
        }
    }
    return NC_OK;
}

void nc_free_buffer(uint8_t* buffer) { delete[] buffer; }

// ===========================================================================
// 第 2 批：预览渲染 / 缩略图 / 波形 / 场景检测 / 节拍检测 / 速度曲线 /
//          关键帧查询 / 标记 / 音画分离
// ===========================================================================

namespace {

/// 预览缓存签名（编辑栈计数 + 载入 + 直接修改 + 画布尺寸）。
std::uint64_t previewSignatureOf(const NcEditor* editor) {
    const std::uint64_t edits = editor->editor->undoCount() + editor->editor->redoCount();
    const Sequence* seq = editor->project.mainSequence();
    std::uint64_t canvas = 0;
    if (seq != nullptr) {
        canvas = static_cast<std::uint64_t>(seq->width) * 1000003u +
                 static_cast<std::uint64_t>(seq->height);
    }
    return edits * 1000000007ull + editor->loadCount * 7919ull +
           editor->directEditCount * 104729ull + canvas;
}

/// 取（必要时重建）预览渲染器。
PreviewRenderer* ensurePreviewRenderer(NcEditor* editor) {
    if (editor == nullptr || editor->project.mainSequence() == nullptr) {
        return nullptr;
    }
    const std::uint64_t signature = previewSignatureOf(editor);
    if (editor->previewRenderer == nullptr || editor->previewSignature != signature) {
        // 重建（自带解码器缓存；旧缓存自然释放）
        editor->previewRenderer.reset(new (std::nothrow)
                                          PreviewRenderer(editor->project,
                                                          *editor->previewFactory));
        editor->previewSignature = signature;
        editor->directEditCount = 0;  // 已并入签名（后续直接修改重新计数）
        editor->loadCount = 0;
    }
    return editor->previewRenderer.get();
}

}  // namespace

int32_t nc_preview_render(NcEditor* editor, int64_t time_us, int32_t* out_width,
                          int32_t* out_height, uint8_t** out_rgba, uint64_t* out_clip_id,
                          double* out_opacity, double* out_scale, double* out_pos_x,
                          double* out_pos_y, double* out_rotation,
                          int32_t* out_transition_type, double* out_transition_progress) {
    if (editor == nullptr || time_us < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
#ifdef NAIROCLIP_HAS_FFMPEG
    PreviewRenderer* renderer = ensurePreviewRenderer(editor);
    if (renderer == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "renderer 为空");
    }
    const PreviewFrame frame = renderer->renderAt(fromMicroseconds(time_us));
    if (out_width != nullptr) *out_width = frame.width;
    if (out_height != nullptr) *out_height = frame.height;
    if (out_clip_id != nullptr) *out_clip_id = frame.clipId;
    if (out_opacity != nullptr) *out_opacity = frame.opacity;
    if (out_scale != nullptr) *out_scale = frame.scale;
    if (out_pos_x != nullptr) *out_pos_x = frame.posX;
    if (out_pos_y != nullptr) *out_pos_y = frame.posY;
    if (out_rotation != nullptr) *out_rotation = frame.rotation;
    if (out_transition_type != nullptr) *out_transition_type = frame.transitionType;
    if (out_transition_progress != nullptr) *out_transition_progress = frame.transitionProgress;
    if (out_rgba != nullptr) {
        const std::size_t bytes = frame.rgba.size();
        if (bytes == 0) {
            *out_rgba = nullptr;
        } else {
            // 无画面时刻：返回画布尺寸的纯黑帧（UI 显示黑场更直观）
            std::uint8_t* buffer = new (std::nothrow) std::uint8_t[bytes];
            if (buffer == nullptr) {
                return failNc(NC_ERROR_INTERNAL, "buffer 为空");
            }
            std::memcpy(buffer, frame.rgba.data(), bytes);
            *out_rgba = buffer;
        }
    }
    return NC_OK;
#else
    // 无 FFmpeg：预览无解码能力（明确失败，不伪造）
    (void)out_width; (void)out_height; (void)out_rgba; (void)out_clip_id;
    (void)out_opacity; (void)out_scale; (void)out_pos_x; (void)out_pos_y;
    (void)out_rotation; (void)out_transition_type; (void)out_transition_progress;
    return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
#endif
}

char* nc_preview_subtitles(const NcEditor* editor, int64_t time_us) {
    if (editor == nullptr || time_us < 0) {
        return nullptr;
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return nullptr;
    }
    std::vector<std::string> lines;
    for (const auto& cue : seq->subtitles.cues()) {
        if (cue.start <= fromMicroseconds(time_us) && fromMicroseconds(time_us) < cue.end()) {
            lines.push_back(cue.text);
        }
    }
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) joined.push_back('\n');
        joined += lines[i];
    }
    return dupString(joined);
}

void nc_preview_invalidate(NcEditor* editor) {
    if (editor == nullptr) {
        return;
    }
    editor->previewRenderer.reset();
    ++editor->directEditCount;
}

// ---- 缩略图 ----

int32_t nc_thumbnail_generate(NcEditor* editor, uint64_t asset_id, int32_t count,
                              int32_t max_width, int32_t max_height,
                              uint8_t** out_images, int64_t* out_times_us,
                              int32_t* out_count) {
    if (editor == nullptr || asset_id == 0 || count < 0 || count > 64) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
#ifdef NAIROCLIP_HAS_FFMPEG
    const MediaAsset* asset = editor->project.findAsset(AssetId{asset_id});
    if (asset == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "asset 为空");
    }
    ThumbnailConfig config;
    config.count = count > 0 ? count : 8;
    if (max_width > 0) config.maxWidth = max_width;
    if (max_height > 0) config.maxHeight = max_height;
    FFmpegMediaReader reader;
    const std::vector<Thumbnail> thumbs =
        generateThumbnails(reader, asset->path, asset->duration, config);
    if (out_count != nullptr) *out_count = static_cast<int32_t>(thumbs.size());
    if (!thumbs.empty()) {
        editor->thumbnailWidth = thumbs.front().width;
        editor->thumbnailHeight = thumbs.front().height;
    }
    if (out_images != nullptr) {
        if (thumbs.empty()) {
            *out_images = nullptr;
        } else {
            // 输出为「拼接缓冲」：所有缩略图依次排列（每张 width×height×4）。
            // UI 按 stride = width*height*4 逐张切分。
            const std::size_t perImage = static_cast<std::size_t>(thumbs.front().width) *
                                         static_cast<std::size_t>(thumbs.front().height) * 4;
            std::uint8_t* buffer = new (std::nothrow) std::uint8_t[perImage * thumbs.size()];
            if (buffer == nullptr) {
                return failNc(NC_ERROR_INTERNAL, "buffer 为空");
            }
            for (std::size_t i = 0; i < thumbs.size(); ++i) {
                std::memcpy(buffer + perImage * i, thumbs[i].rgba.data(),
                            std::min(perImage, thumbs[i].rgba.size()));
            }
            *out_images = buffer;
        }
    }
    if (out_times_us != nullptr) {
        for (std::size_t i = 0; i < thumbs.size(); ++i) {
            out_times_us[i] = toMicroseconds(thumbs[i].sourceTime);
        }
    }
    return NC_OK;
#else
    (void)max_width; (void)max_height; (void)out_images; (void)out_times_us; (void)out_count;
    return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
#endif
}

int32_t nc_thumbnail_size(const NcEditor* editor, int32_t* out_width, int32_t* out_height) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (out_width != nullptr) *out_width = editor->thumbnailWidth;
    if (out_height != nullptr) *out_height = editor->thumbnailHeight;
    return NC_OK;
}

// ---- 波形 ----

int32_t nc_waveform_generate(NcEditor* editor, uint64_t asset_id, int32_t bucket_count,
                             double* out_peak, int32_t* out_sample_rate,
                             int32_t* out_channels) {
    if (editor == nullptr || asset_id == 0 || bucket_count < 0 || bucket_count > 100000) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
#ifdef NAIROCLIP_HAS_FFMPEG
    const MediaAsset* asset = editor->project.findAsset(AssetId{asset_id});
    if (asset == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "asset 为空");
    }
    FFmpegMediaReader reader;
    if (!reader.open(asset->path).isOk()) {
        return failNc(NC_ERROR_NOT_FOUND, "目标不存在");
    }
    // 上限保护：只解码前 120s 音频（波形显示无需全片；避免长素材 OOM/耗时）
    constexpr double kMaxSeconds = 120.0;
    const double durationSeconds =
        std::min(asset->duration.toDouble(), kMaxSeconds);
    std::vector<AudioBlock> blocks;
    AudioBlock block;
    while (reader.readAudioBlock(block)) {
        if (block.start.toDouble() >= durationSeconds) {
            break;
        }
        blocks.push_back(std::move(block));
    }
    reader.close();
    editor->waveformCache = buildWaveform(blocks, bucket_count > 0 ? bucket_count : 512);
    if (out_peak != nullptr) *out_peak = editor->waveformCache.peak();
    if (out_sample_rate != nullptr) *out_sample_rate = editor->waveformCache.sampleRate;
    if (out_channels != nullptr) *out_channels = editor->waveformCache.channels;
    return NC_OK;
#else
    (void)bucket_count; (void)out_peak; (void)out_sample_rate; (void)out_channels;
    return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
#endif
}

int32_t nc_waveform_bucket_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(editor->waveformCache.buckets.size());
}

int32_t nc_waveform_bucket_get(const NcEditor* editor, int32_t index, double* out_min,
                               double* out_max, double* out_rms) {
    if (editor == nullptr || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const auto& buckets = editor->waveformCache.buckets;
    if (static_cast<std::size_t>(index) >= buckets.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
    }
    const WaveformBucket& bucket = buckets[static_cast<std::size_t>(index)];
    if (out_min != nullptr) *out_min = bucket.minValue;
    if (out_max != nullptr) *out_max = bucket.maxValue;
    if (out_rms != nullptr) *out_rms = bucket.rms;
    return NC_OK;
}

// ---- 场景检测 ----

int32_t nc_scene_detect(NcEditor* editor, double step_seconds, int32_t max_samples,
                        int32_t* out_boundary_count, double** out_boundaries) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
#ifdef NAIROCLIP_HAS_FFMPEG
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    // 采集帧特征（真实解码；复用章节检测的采样约定）
    std::vector<FrameFeature> features;
    int sampleCap = max_samples > 0 ? max_samples : 600;
    // 上一采样点状态（直方图差异 + 颜色差）
    std::vector<std::uint8_t> prevLuma;
    double prevR = 0.0;
    double prevG = 0.0;
    double prevB = 0.0;
    for (const auto& track : seq->tracks) {
        if (track.type != TrackType::Video || track.hidden) {
            continue;
        }
        for (const auto& clip : track.clips) {
            const MediaAsset* asset = editor->project.findAsset(clip.asset);
            if (asset == nullptr || asset->isImage()) {
                continue;
            }
            FFmpegMediaReader reader;
            if (!reader.open(asset->path).isOk()) {
                continue;
            }
            // 步长自适应：短片段细采样（0.25s），长片段粗采样（至 1s）
            const double clipSeconds = clip.duration.toDouble();
            double step = step_seconds > 0.0 ? step_seconds : clipSeconds / 24.0;
            if (step_seconds <= 0.0) {
                step = std::max(0.25, std::min(1.0, step));
            }
            if (step <= 0.0) step = 0.5;
            for (double local = 0.0; local < clipSeconds; local += step) {
                if (static_cast<int>(features.size()) >= sampleCap) {
                    break;
                }
                const Time localTime =
                    Rational::make(static_cast<std::int64_t>(local * 1000.0), 1000);
                const Time sourceTime =
                    clip.sourceTimeAt(localTime.isNegative() ? Time{} : localTime);
                if (!reader.seek(sourceTime).isOk()) {
                    break;
                }
                VideoFrame frame;
                if (!reader.readVideoFrame(frame)) {
                    break;
                }
                // 4x4 亮度网格 + 直方图差异（与章节检测采样约定一致：
                // 均值差对镜头切换敏感度不足，直方图差异是主信号）
                std::vector<std::uint8_t> lumaGrid;
                std::vector<int> hist(8, 0);
                const int stride = frame.stride > 0 ? frame.stride : frame.width * 4;
                for (int gy = 0; gy < 4; ++gy) {
                    for (int gx = 0; gx < 4; ++gx) {
                        const int sx = frame.width > 0 ? gx * frame.width / 4 : 0;
                        const int sy = frame.height > 0 ? gy * frame.height / 4 : 0;
                        const std::uint8_t* px =
                            frame.pixels.data() +
                            static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) +
                            static_cast<std::size_t>(sx) * 4;
                        const int lumaValue =
                            (px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000;
                        lumaGrid.push_back(static_cast<std::uint8_t>(std::min(255, lumaValue)));
                        hist[std::min(7, lumaValue / 32)]++;
                    }
                }
                // 整帧均值（供颜色差）
                double sumR2 = 0.0;
                double sumG2 = 0.0;
                double sumB2 = 0.0;
                std::size_t count2 = 0;
                for (int y = 0; y < frame.height; y += 4) {
                    const std::uint8_t* row =
                        frame.pixels.data() +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
                    for (int x = 0; x < frame.width; x += 4) {
                        const std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
                        sumR2 += px[0];
                        sumG2 += px[1];
                        sumB2 += px[2];
                        ++count2;
                    }
                }
                FrameFeature feature;
                feature.timestamp = clip.start + localTime;
                if (count2 > 0) {
                    feature.avgR = sumR2 / static_cast<double>(count2) / 255.0;
                    feature.avgG = sumG2 / static_cast<double>(count2) / 255.0;
                    feature.avgB = sumB2 / static_cast<double>(count2) / 255.0;
                }
                double diff = 0.0;
                if (!prevLuma.empty()) {
                    std::vector<int> prevHist(8, 0);
                    for (const auto v : prevLuma) {
                        prevHist[v / 32]++;
                    }
                    int sumAbs = 0;
                    int total = 0;
                    for (int i = 0; i < 8; ++i) {
                        sumAbs += std::abs(hist[static_cast<std::size_t>(i)] -
                                           prevHist[static_cast<std::size_t>(i)]);
                        total += std::max(hist[static_cast<std::size_t>(i)],
                                          prevHist[static_cast<std::size_t>(i)]);
                    }
                    diff = total > 0 ? static_cast<double>(sumAbs) / (2.0 * total) : 0.0;
                    const double colorDelta =
                        (std::abs(feature.avgR - prevR) + std::abs(feature.avgG - prevG) +
                         std::abs(feature.avgB - prevB)) /
                        3.0;
                    diff = std::max(diff, colorDelta * 3.0);  // 颜色差放大（与章节检测一致）
                }
                prevR = feature.avgR;
                prevG = feature.avgG;
                prevB = feature.avgB;
                prevLuma = std::move(lumaGrid);
                feature.histogramDiff = std::min(1.0, diff);
                features.push_back(feature);
            }
            reader.close();
        }
    }
    if (features.empty()) {
        return failNc(NC_ERROR_NOT_FOUND, "目标不存在");
    }
    const std::vector<ShotBoundary> shots =
        detectShots(features, seq->duration(), SceneDetectConfig{});
    if (out_boundary_count != nullptr) *out_boundary_count = static_cast<int32_t>(shots.size());
    if (out_boundaries != nullptr) {
        if (shots.empty()) {
            *out_boundaries = nullptr;
        } else {
            double* buffer = new (std::nothrow) double[shots.size() * 2];
            if (buffer == nullptr) {
                return failNc(NC_ERROR_INTERNAL, "buffer 为空");
            }
            for (std::size_t i = 0; i < shots.size(); ++i) {
                buffer[i * 2] = shots[i].start.toDouble();
                buffer[i * 2 + 1] = shots[i].end.toDouble();
            }
            *out_boundaries = buffer;
        }
    }
    return NC_OK;
#else
    (void)step_seconds; (void)max_samples; (void)out_boundary_count; (void)out_boundaries;
    return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
#endif
}

// ---- 节拍检测 ----

int32_t nc_beat_detect(NcEditor* editor, int32_t track_index, double* out_bpm,
                       double* out_confidence, int32_t* out_beat_count, double** out_beats) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
#ifdef NAIROCLIP_HAS_FFMPEG
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    // 选择音频轨（track_index < 0 = 首个音频轨）
    const Track* target = nullptr;
    if (track_index >= 0) {
        if (static_cast<std::size_t>(track_index) >= seq->tracks.size()) {
            return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
        }
        target = &seq->tracks[static_cast<std::size_t>(track_index)];
        if (target->type != TrackType::Audio) {
            return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
        }
    } else {
        for (const auto& track : seq->tracks) {
            if (track.type == TrackType::Audio) {
                target = &track;
                break;
            }
        }
    }
    if (target == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "target 为空");
    }
    // 采集音频块（映射到时间轴坐标）
    std::vector<AudioBlock> blocks;
    for (const auto& clip : target->clips) {
        const MediaAsset* asset = editor->project.findAsset(clip.asset);
        if (asset == nullptr) {
            continue;
        }
        FFmpegMediaReader reader;
        if (!reader.open(asset->path).isOk()) {
            continue;
        }
        if (!reader.seek(clip.sourceIn).isOk()) {
            reader.close();
            continue;
        }
        const Time sourceEnd = clip.sourceIn + clip.duration * clip.speed;
        AudioBlock block;
        std::size_t guard = 0;
        while (guard < 20000 && reader.readAudioBlock(block)) {
            ++guard;
            if (block.start >= sourceEnd) {
                break;
            }
            AudioBlock mapped = block;
            const Rational local =
                (block.start - clip.sourceIn) /
                (clip.speed.isPositive() ? clip.speed : Rational::make(1));
            mapped.start = clip.start + (local.isNegative() ? Time{} : local);
            blocks.push_back(std::move(mapped));
        }
        reader.close();
    }
    if (blocks.empty()) {
        return failNc(NC_ERROR_NOT_FOUND, "目标不存在");
    }
    const BeatAnalysis analysis = analyzeBeats(blocks, BeatDetectConfig{});
    if (out_bpm != nullptr) *out_bpm = analysis.bpm;
    if (out_confidence != nullptr) *out_confidence = analysis.confidence;
    if (out_beat_count != nullptr) *out_beat_count = static_cast<int32_t>(analysis.beats.size());
    if (out_beats != nullptr) {
        if (analysis.beats.empty()) {
            *out_beats = nullptr;
        } else {
            double* buffer = new (std::nothrow) double[analysis.beats.size()];
            if (buffer == nullptr) {
                return failNc(NC_ERROR_INTERNAL, "buffer 为空");
            }
            for (std::size_t i = 0; i < analysis.beats.size(); ++i) {
                buffer[i] = analysis.beats[i].time.toDouble();
            }
            *out_beats = buffer;
        }
    }
    return NC_OK;
#else
    (void)track_index; (void)out_bpm; (void)out_confidence; (void)out_beat_count; (void)out_beats;
    return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
#endif
}

// ---- 速度曲线 ----

namespace {

Easing easingFromInt(int value) {
    switch (value) {
        case 1: return Easing::easeIn();
        case 2: return Easing::easeOut();
        case 3: return Easing::easeInOut();
        case 4: return Easing::stepped();
        default: return Easing::linear();
    }
}

int easingToInt(const Easing& easing) {
    switch (easing.type) {
        case EasingType::EaseIn: return 1;
        case EasingType::EaseOut: return 2;
        case EasingType::EaseInOut: return 3;
        case EasingType::Stepped: return 4;
        default: return 0;
    }
}

}  // namespace

int32_t nc_set_clip_speed_curve(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                                const double* source_times, const double* speeds,
                                const int32_t* easings, int32_t point_count) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || point_count < 0 ||
        point_count > 256) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (point_count > 0 && (source_times == nullptr || speeds == nullptr)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "source_times、speeds 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const TrackId trackId = main->tracks[static_cast<std::size_t>(track_index)].id;
    // 构造曲线（点按源时间升序；同时刻后者替换）
    SpeedCurve curve;
    for (int32_t i = 0; i < point_count; ++i) {
        const Time sourceTime = Rational::make(
            static_cast<std::int64_t>(source_times[i] * 1000000.0 + 0.5), 1000000);
        const Rational speed = Rational::make(
            static_cast<std::int64_t>(speeds[i] * 1000.0 + 0.5), 1000);
        SpeedPoint point;
        point.sourceTime = sourceTime;
        point.speed = speed;
        point.easing = easings != nullptr ? easingFromInt(easings[i]) : Easing::linear();
        const Result added = curve.addPoint(point);
        if (!added.isOk()) {
            return failResult(added);
        }
    }
    return failResult(editor->editor->setClipSpeedCurve(main->id, trackId, ClipId{clip_id}, std::move(curve)));
}

int32_t nc_clip_speed_curve_count(const NcEditor* editor, int32_t track_index,
                                  uint64_t clip_id) {
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
    return static_cast<int32_t>(clip->speedCurve.size());
}

int32_t nc_clip_speed_curve_get(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                                int32_t index, double* out_source_seconds, double* out_speed,
                                int32_t* out_easing) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "clip 为空");
    }
    if (static_cast<std::size_t>(index) >= clip->speedCurve.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "clip 为空");
    }
    const SpeedPoint& point = clip->speedCurve.points()[static_cast<std::size_t>(index)];
    if (out_source_seconds != nullptr) *out_source_seconds = point.sourceTime.toDouble();
    if (out_speed != nullptr) *out_speed = point.speed.toDouble();
    if (out_easing != nullptr) *out_easing = easingToInt(point.easing);
    return NC_OK;
}

// ---- 关键帧查询 ----

namespace {

constexpr int kClipPropertyCount = 6;

ClipProperty clipPropertyFromInt(int value) {
    switch (value) {
        case 0: return ClipProperty::Opacity;
        case 1: return ClipProperty::Scale;
        case 2: return ClipProperty::PositionX;
        case 3: return ClipProperty::PositionY;
        case 4: return ClipProperty::Rotation;
        case 5: return ClipProperty::Volume;
        default: return ClipProperty::Opacity;
    }
}

int easingIndexOf(const Keyframe& keyframe) {
    switch (keyframe.easing.type) {
        case EasingType::EaseIn: return 1;
        case EasingType::EaseOut: return 2;
        case EasingType::EaseInOut: return 3;
        case EasingType::Stepped: return 4;
        default: return 0;
    }
}

}  // namespace

int32_t nc_keyframe_count(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                          int32_t property) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || property < 0 ||
        property >= kClipPropertyCount) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return 0;
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return 0;
    }
    const KeyframeTrack* curve = clip->animation.findCurve(clipPropertyFromInt(property));
    return curve != nullptr ? static_cast<int32_t>(curve->size()) : 0;
}

int32_t nc_keyframe_get(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                        int32_t property, int32_t index, int64_t* out_time_us,
                        double* out_value, int32_t* out_easing) {
    if (editor == nullptr || track_index < 0 || clip_id == 0 || property < 0 ||
        property >= kClipPropertyCount || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr || static_cast<std::size_t>(track_index) >= main->tracks.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Track& track = main->tracks[static_cast<std::size_t>(track_index)];
    const Clip* clip = track.findClip(ClipId{clip_id});
    if (clip == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "clip 为空");
    }
    const KeyframeTrack* curve = clip->animation.findCurve(clipPropertyFromInt(property));
    if (curve == nullptr || static_cast<std::size_t>(index) >= curve->size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "curve 为空");
    }
    const Keyframe& keyframe = curve->keyframes()[static_cast<std::size_t>(index)];
    if (out_time_us != nullptr) *out_time_us = toMicroseconds(keyframe.time);
    if (out_value != nullptr) *out_value = keyframe.value;
    if (out_easing != nullptr) *out_easing = easingIndexOf(keyframe);
    return NC_OK;
}

// ---- 标记 ----

uint64_t nc_add_marker(NcEditor* editor, int64_t time_us, const char* name, const char* color) {
    if (editor == nullptr || time_us < 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    Marker marker;
    marker.id = MarkerId{editor->project.ids.next()};
    marker.time = fromMicroseconds(time_us);
    marker.name = name != nullptr ? name : "标记";
    marker.color = color != nullptr ? color : "#2D8CFF";
    if (marker.name.size() > 200) {
        return 0;
    }
    const Result r = editor->editor->upsertMarker(main->id, marker);
    if (!r.isOk()) {
        return 0;
    }
    return marker.id.value;
}

int32_t nc_remove_marker(NcEditor* editor, uint64_t marker_id) {
    if (editor == nullptr || marker_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    return failResult(editor->editor->removeMarker(main->id, MarkerId{marker_id}));
}

int32_t nc_marker_count(const NcEditor* editor) {
    if (editor == nullptr) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(main->markers.size());
}

int32_t nc_marker_get(const NcEditor* editor, int32_t index, uint64_t* out_marker_id,
                      int64_t* out_time_us, char** out_name, char** out_color) {
    if (editor == nullptr || index < 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "main 为空");
    }
    if (static_cast<std::size_t>(index) >= main->markers.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "main 为空");
    }
    const Marker& marker = main->markers[static_cast<std::size_t>(index)];
    if (out_marker_id != nullptr) *out_marker_id = marker.id.value;
    if (out_time_us != nullptr) *out_time_us = toMicroseconds(marker.time);
    if (out_name != nullptr) *out_name = dupString(marker.name);
    if (out_color != nullptr) *out_color = dupString(marker.color);
    return NC_OK;
}

// ---- 音画分离 ----

uint64_t nc_detach_audio(NcEditor* editor, int32_t video_track_index, uint64_t video_clip_id,
                         int32_t audio_track_index) {
    if (editor == nullptr || video_track_index < 0 || audio_track_index < 0 ||
        video_clip_id == 0) {
        return 0;
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        return 0;
    }
    if (static_cast<std::size_t>(video_track_index) >= main->tracks.size() ||
        static_cast<std::size_t>(audio_track_index) >= main->tracks.size()) {
        return 0;
    }
    const TrackId videoTrackId =
        main->tracks[static_cast<std::size_t>(video_track_index)].id;
    const TrackId audioTrackId =
        main->tracks[static_cast<std::size_t>(audio_track_index)].id;
    // 记录分离前的音频片段集合，用于定位新片段 ID
    std::vector<ClipId> before;
    for (const auto& clip : main->tracks[static_cast<std::size_t>(audio_track_index)].clips) {
        before.push_back(clip.id);
    }
    const Result r = editor->editor->detachAudio(
        main->id, videoTrackId, ClipId{video_clip_id}, audioTrackId);
    if (!r.isOk()) {
        return 0;
    }
    const Sequence* after = editor->project.mainSequence();
    if (after == nullptr) {
        return 0;
    }
    for (const auto& clip : after->tracks[static_cast<std::size_t>(audio_track_index)].clips) {
        bool existed = false;
        for (const ClipId id : before) {
            if (id == clip.id) {
                existed = true;
                break;
            }
        }
        if (!existed) {
            return clip.id.value;
        }
    }
    return 0;
}

// ===========================================================================
// AI 任务（文档 Part 4 统一任务契约 + 第 30.3 节失败提示）
// ===========================================================================

int32_t nc_ai_feature_access(const NcEditor* editor, int32_t feature, int32_t logged_in,
                             int32_t* out_requires_network, int32_t* out_requires_pro,
                             char** out_reason) {
    if (editor == nullptr) {
        return -1;
    }
    AiFeatureType featureType;
    if (!aiFeatureFromInt(feature, featureType) || featureType == AiFeatureType::None) {
        return -1;
    }
    const bool requiresNetwork = aiFeatureRequiresNetwork(featureType);
    // Pro 门控：抠像/补帧/超分/人声分离/深度图属 Pro（文档权益矩阵；
    // 与既有 mattingAccessFor 等保持一致；此处供 UI 提前判断展示）。
    bool requiresPro = false;
    switch (featureType) {
        case AiFeatureType::Matting:
        case AiFeatureType::FrameInterpolation:
        case AiFeatureType::StemSeparation:
        case AiFeatureType::DepthMap:
        case AiFeatureType::SuperResolution:
            requiresPro = true;
            break;
        default:
            requiresPro = false;
            break;
    }
    if (out_requires_network != nullptr) *out_requires_network = requiresNetwork ? 1 : 0;
    if (out_requires_pro != nullptr) *out_requires_pro = requiresPro ? 1 : 0;
    // 可用性判定（文档 Part 5 第 26.1 节 + Part 6 第 41 节）
    if (requiresNetwork && logged_in == 0) {
        if (out_reason != nullptr) {
            *out_reason = dupString("该功能需要联网AI算力支持，登录账号后即可使用");
        }
        return 0;  // 不可用（弹登录拦截弹窗）
    }
    if (out_reason != nullptr) {
        *out_reason = dupString(std::string(aiFeatureDisplayName(featureType)) + "可用");
    }
    return 1;
}

int32_t nc_ai_task_status(const NcEditor* editor, int32_t* out_feature, int32_t* out_state,
                          double* out_progress, char** out_stage_note,
                          char** out_failure_reason, int32_t* out_retryable,
                          char** out_result_payload) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const AiTask& task = editor->aiTask.task();
    if (out_feature != nullptr) *out_feature = static_cast<int32_t>(task.feature);
    if (out_state != nullptr) *out_state = static_cast<int32_t>(task.state);
    if (out_progress != nullptr) *out_progress = task.progress;
    if (out_stage_note != nullptr) *out_stage_note = dupString(task.stageNote);
    if (out_failure_reason != nullptr) *out_failure_reason = dupString(task.failureReason);
    if (out_retryable != nullptr) *out_retryable = task.retryable ? 1 : 0;
    if (out_result_payload != nullptr) *out_result_payload = dupString(task.resultPayload);
    return NC_OK;
}

int32_t nc_ai_task_begin(NcEditor* editor, int32_t feature, uint64_t target_id,
                         char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    AiFeatureType featureType;
    if (!aiFeatureFromInt(feature, featureType) || featureType == AiFeatureType::None) {
        if (out_error != nullptr) *out_error = dupString("未指定 AI 功能类型");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    std::string failure;
    if (!editor->aiTask.begin(featureType, target_id, &failure)) {
        if (out_error != nullptr) *out_error = dupString(failure);
        return NC_ERROR_UNSUPPORTED;  // 已有任务运行中
    }
    return NC_OK;
}

int32_t nc_ai_task_progress(NcEditor* editor, double progress, const char* stage_note) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const std::string note = stage_note != nullptr ? stage_note : std::string();
    if (!editor->aiTask.setProgress(progress, note)) {
        return NC_ERROR_UNSUPPORTED;  // 无运行中任务
    }
    return NC_OK;
}

int32_t nc_ai_task_succeed(NcEditor* editor, const char* result_payload) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const std::string payload = result_payload != nullptr ? result_payload : std::string();
    if (!editor->aiTask.succeed(payload)) {
        return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
    }
    return NC_OK;
}

int32_t nc_ai_task_fail(NcEditor* editor, const char* reason, int32_t retryable) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const std::string text = reason != nullptr ? reason : std::string();
    if (!editor->aiTask.fail(text, retryable != 0)) {
        return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
    }
    return NC_OK;
}

int32_t nc_ai_task_cancel(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (!editor->aiTask.cancel()) {
        return failNc(NC_ERROR_UNSUPPORTED, "editor 为空");
    }
    return NC_OK;
}

int32_t nc_ai_task_reset(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    editor->aiTask.reset();
    return NC_OK;
}

char* nc_ai_task_failure_text(const NcEditor* editor) {
    if (editor == nullptr) {
        return nullptr;
    }
    return dupString(editor->aiTask.failureText());
}

int32_t nc_ai_engine_info(const NcEditor* editor, int32_t* out_available, char** out_version,
                          char** out_note) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const AiEngineInfo info = aiEngineInfo();
    if (out_available != nullptr) *out_available = info.available ? 1 : 0;
    if (out_version != nullptr) *out_version = dupString(info.version);
    if (out_note != nullptr) *out_note = dupString(info.note);
    return NC_OK;
}

// ===========================================================================
// AI 真实推理（深度图 / 人像抠像 / 语音转字幕）
// ===========================================================================

// =============================================================================
// 超分辨率（真实推理）
// =============================================================================

int32_t nc_superres_load(NcEditor* editor, const char* model_path, char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_path == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_path 为空");
    }
    std::string failure;
    if (editor->superResEstimator == nullptr) {
        editor->superResEstimator.reset(new (std::nothrow) SuperResEstimator());
    }
    if (editor->superResEstimator == nullptr) {
        setDetail(out_error, "内存分配失败");
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (!editor->superResEstimator->load(model_path, &failure)) {
        setDetail(out_error, failure);
        editor->superResEstimator.reset();
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    return NC_OK;
}

int32_t nc_superres_unload(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (editor->superResEstimator != nullptr) {
        editor->superResEstimator->unload();
        editor->superResEstimator.reset();
    }
    return NC_OK;
}

int32_t nc_superres_loaded(const NcEditor* editor) {
    if (editor == nullptr || editor->superResEstimator == nullptr) {
        return 0;
    }
    return editor->superResEstimator->loaded() ? 1 : 0;
}

int32_t nc_superres_upscale(NcEditor* editor, const uint8_t* rgba, int32_t width, int32_t height,
                            int32_t scale, uint8_t** out_rgba, int32_t* out_width,
                            int32_t* out_height, int32_t* out_tiles, int32_t* out_fallbacks,
                            char** out_error) {
    if (editor == nullptr || rgba == nullptr || width <= 0 || height <= 0 ||
        (scale != 2 && scale != 4)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、rgba 为空");
    }
    if (editor->superResEstimator == nullptr || !editor->superResEstimator->loaded()) {
        setDetail(out_error, "超分模型未加载（先调用 nc_superres_load）");
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    std::vector<std::uint8_t> out;
    SuperResStats stats;
    std::string failure;
    const SuperResScale sc = (scale == 2) ? SuperResScale::X2 : SuperResScale::X4;
    SuperResTiling tiling;  // 默认：自动分块 + 羽化拼接
    if (!editor->superResEstimator->upscale(rgba, width, height, sc, tiling, out, &stats,
                                            &failure)) {
        setDetail(out_error, failure);
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (out_rgba != nullptr) {
        std::uint8_t* buffer = new (std::nothrow) std::uint8_t[out.size()];
        if (buffer == nullptr) {
            return failNc(NC_ERROR_INTERNAL, "buffer 为空");
        }
        std::memcpy(buffer, out.data(), out.size());
        *out_rgba = buffer;
    }
    if (out_width != nullptr) *out_width = stats.outputWidth;
    if (out_height != nullptr) *out_height = stats.outputHeight;
    if (out_tiles != nullptr) *out_tiles = stats.tilesTotal;
    if (out_fallbacks != nullptr) *out_fallbacks = stats.tilesFallback;
    return NC_OK;
}

// =============================================================================
// 字幕翻译（真实推理）
// =============================================================================

int32_t nc_translate_load(NcEditor* editor, const char* encoder_path, const char* decoder_path,
                          const char* decoder_with_past_path, const char* tokenizer_json_path,
                          char** out_error) {
    if (editor == nullptr || encoder_path == nullptr || decoder_path == nullptr ||
        decoder_with_past_path == nullptr || tokenizer_json_path == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、encoder_path、decoder_path、decoder_with_past_path、tokenizer_json_path 为空");
    }
    std::string failure;
    if (editor->translator == nullptr) {
        editor->translator.reset(new (std::nothrow) Translator());
    }
    if (editor->translator == nullptr) {
        setDetail(out_error, "内存分配失败");
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (!editor->translator->load(encoder_path, decoder_path, decoder_with_past_path,
                                  tokenizer_json_path, &failure)) {
        setDetail(out_error, failure);
        editor->translator.reset();
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    return NC_OK;
}

int32_t nc_translate_unload(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (editor->translator != nullptr) {
        editor->translator->unload();
        editor->translator.reset();
    }
    return NC_OK;
}

int32_t nc_translate_loaded(const NcEditor* editor) {
    if (editor == nullptr || editor->translator == nullptr) {
        return 0;
    }
    return editor->translator->loaded() ? 1 : 0;
}

int32_t nc_translate_text(NcEditor* editor, const char* text, char** out_text, char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (text == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "text 为空");
    }
    if (editor->translator == nullptr || !editor->translator->loaded()) {
        setDetail(out_error, "翻译模型未加载（先调用 nc_translate_load）");
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    std::string translated;
    std::string failure;
    if (!editor->translator->translate(text, translated, 0, &failure)) {
        setDetail(out_error, failure);
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (out_text != nullptr) {
        *out_text = dupString(translated);
    }
    return NC_OK;
}

int32_t nc_translate_subtitles(NcEditor* editor, char** out_json, char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (editor->translator == nullptr || !editor->translator->loaded()) {
        setDetail(out_error, "翻译模型未加载（先调用 nc_translate_load）");
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    const auto& project = editor->project;
    if (project.sequences.empty()) {
        setDetail(out_error, "工程无序列");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "参数或状态非法");
    }
    const Sequence& seq = project.sequences.front();
    const auto& cues = seq.subtitles.cues();

    JsonValue root = JsonValue::object();
    root.set("ok", JsonValue::boolean(true));
    root.set("total", JsonValue::integer(static_cast<std::int64_t>(cues.size())));
    JsonValue lines = JsonValue::array();
    int translatedCount = 0;
    int failedCount = 0;
    for (const auto& cue : cues) {
        JsonValue item = JsonValue::object();
        item.set("src", JsonValue::string(cue.text));
        std::string out;
        std::string failure;
        if (editor->translator->translate(cue.text, out, 0, &failure) && !out.empty()) {
            item.set("dst", JsonValue::string(out));
            item.set("error", JsonValue::string(""));
            // 真正落盘：经编辑器命令改写字幕文本（可撤销；不改时间与样式）
            SubtitleCue updated = cue;
            updated.text = out;
            editor->editor->upsertSubtitle(seq.id, updated);
            ++translatedCount;
        } else {
            item.set("dst", JsonValue::string(""));
            item.set("error", JsonValue::string(failure.empty() ? "翻译失败" : failure));
            ++failedCount;
        }
        lines.push(item);
    }
    root.set("translated", JsonValue::integer(translatedCount));
    root.set("failed", JsonValue::integer(failedCount));
    root.set("lines", lines);
    root.set("ok", JsonValue::boolean(failedCount == 0));
    if (out_json != nullptr) {
        *out_json = dupString(toJsonString(root));
    }
    return (failedCount == 0) ? NC_OK : NC_ERROR_INTERNAL;
}

int32_t nc_depth_load(NcEditor* editor, const char* model_path, char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_path == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_path 为空");
    }
    std::string failure;
    // 复用 editor 上的推理器（会话复用；避免每帧重建）
    if (editor->depthEstimator == nullptr) {
        editor->depthEstimator.reset(new (std::nothrow) DepthEstimator());
    }
    if (editor->depthEstimator == nullptr) {
        setDetail(out_error, "内存分配失败");
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (!editor->depthEstimator->load(model_path, &failure)) {
        setDetail(out_error, failure);
        editor->depthEstimator.reset();
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    return NC_OK;
}

int32_t nc_depth_unload(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (editor->depthEstimator != nullptr) {
        editor->depthEstimator->unload();
        editor->depthEstimator.reset();
    }
    return NC_OK;
}

int32_t nc_depth_loaded(const NcEditor* editor) {
    if (editor == nullptr || editor->depthEstimator == nullptr) {
        return 0;
    }
    return editor->depthEstimator->loaded() ? 1 : 0;
}

int32_t nc_depth_estimate(NcEditor* editor, const uint8_t* rgba, int32_t width, int32_t height,
                          uint8_t** out_depth, int32_t* out_width, int32_t* out_height,
                          char** out_error) {
    if (editor == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、rgba 为空");
    }
    if (editor->depthEstimator == nullptr || !editor->depthEstimator->loaded()) {
        setDetail(out_error, "深度模型未加载（先调用 nc_depth_load）");
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    std::vector<std::uint8_t> depth;
    std::string failure;
    if (!editor->depthEstimator->estimate(rgba, width, height, depth, &failure)) {
        setDetail(out_error, failure);
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (out_depth != nullptr) {
        std::uint8_t* buffer = new (std::nothrow) std::uint8_t[depth.size()];
        if (buffer == nullptr) {
            return failNc(NC_ERROR_INTERNAL, "buffer 为空");
        }
        std::memcpy(buffer, depth.data(), depth.size());
        *out_depth = buffer;
    }
    if (out_width != nullptr) *out_width = width;
    if (out_height != nullptr) *out_height = height;
    return NC_OK;
}

int32_t nc_depth_apply_effect(uint8_t* rgba, const uint8_t* depth, int32_t width, int32_t height,
                              int32_t mode, double focus_distance, double aperture,
                              double intensity, uint32_t color_rgba, double frame_time) {
    if (rgba == nullptr || depth == nullptr || width <= 0 || height <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "rgba、depth 为空");
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> depthVec(depth, depth + count);
    bool ok = false;
    switch (mode) {
        case 1: {  // 景深虚化
            BokehParams params;
            params.focusDistance = focus_distance;
            params.aperture = aperture;
            params.intensity = intensity;
            ok = applyBokehEffect(rgba, width, height, depthVec, params);
            break;
        }
        case 2: {  // 雾化
            FogParams params;
            params.intensity = intensity;
            params.colorRgba = color_rgba;
            params.startDistance = focus_distance;
            ok = applyFogEffect(rgba, width, height, depthVec, params);
            break;
        }
        case 3: {  // 色彩分层
            ColorBandsParams params;
            params.nearRgba = color_rgba;
            params.smoothness = intensity;
            ok = applyColorBandsEffect(rgba, width, height, depthVec, params);
            break;
        }
        case 4: {  // 视差3D
            ParallaxParams params;
            params.intensity = intensity;
            params.trajectory = static_cast<int>(focus_distance);
            params.speed = static_cast<int>(aperture);
            ok = applyParallaxEffect(rgba, width, height, depthVec, params, frame_time);
            break;
        }
        default:
            return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    return ok ? NC_OK : NC_ERROR_INTERNAL;
}

int32_t nc_matting_load(NcEditor* editor, const char* model_path, char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (model_path == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "model_path 为空");
    }
    std::string failure;
    if (editor->mattingEstimator == nullptr) {
        editor->mattingEstimator.reset(new (std::nothrow) MattingEstimator());
    }
    if (editor->mattingEstimator == nullptr) {
        setDetail(out_error, "内存分配失败");
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (!editor->mattingEstimator->load(model_path, &failure)) {
        setDetail(out_error, failure);
        editor->mattingEstimator.reset();
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    return NC_OK;
}

int32_t nc_matting_unload(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (editor->mattingEstimator != nullptr) {
        editor->mattingEstimator->unload();
        editor->mattingEstimator.reset();
    }
    return NC_OK;
}

int32_t nc_matting_loaded(const NcEditor* editor) {
    if (editor == nullptr || editor->mattingEstimator == nullptr) {
        return 0;
    }
    return editor->mattingEstimator->loaded() ? 1 : 0;
}

int32_t nc_matting_estimate(NcEditor* editor, const uint8_t* rgba, int32_t width, int32_t height,
                            int32_t quality, double feather, uint8_t** out_alpha,
                            char** out_error) {
    if (editor == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、rgba 为空");
    }
    if (editor->mattingEstimator == nullptr || !editor->mattingEstimator->loaded()) {
        setDetail(out_error, "抠像模型未加载（先调用 nc_matting_load）");
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    MattingQuality mattingQuality;
    if (!mattingQualityFromInt(quality, mattingQuality)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    std::vector<std::uint8_t> alpha;
    std::string failure;
    if (!editor->mattingEstimator->estimate(rgba, width, height, mattingQuality, feather, alpha,
                                            &failure)) {
        setDetail(out_error, failure);
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (out_alpha != nullptr) {
        std::uint8_t* buffer = new (std::nothrow) std::uint8_t[alpha.size()];
        if (buffer == nullptr) {
            return failNc(NC_ERROR_INTERNAL, "buffer 为空");
        }
        std::memcpy(buffer, alpha.data(), alpha.size());
        *out_alpha = buffer;
    }
    return NC_OK;
}

int32_t nc_person_effect_apply(uint8_t* rgba, const uint8_t* alpha, int32_t width, int32_t height,
                               int32_t effect, double intensity) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "rgba 为空");
    }
    const bool ok = applyPersonEffectToRgba(rgba, width, height, effect, intensity, 0.0, alpha,
                                            RenderQuality::exportQuality());
    return ok ? NC_OK : NC_ERROR_INTERNAL;
}

int32_t nc_speech_load(NcEditor* editor, const char* encoder_path, const char* decoder_path,
                       const char* tokenizer_path, char** out_error) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (encoder_path == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "encoder_path 为空");
    }
    if (decoder_path == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "decoder_path 为空");
    }
    std::string failure;
    if (editor->speechRecognizer == nullptr) {
        editor->speechRecognizer.reset(new (std::nothrow) SpeechRecognizer());
    }
    if (editor->speechRecognizer == nullptr) {
        setDetail(out_error, "内存分配失败");
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    if (!editor->speechRecognizer->load(encoder_path, decoder_path, &failure)) {
        setDetail(out_error, failure);
        editor->speechRecognizer.reset();
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    // 词表（可选；失败不影响模型加载，但解码输出为空——明确告知）
    if (tokenizer_path != nullptr && tokenizer_path[0] != '\0') {
        std::string tokenizerFailure;
        if (!editor->speechRecognizer->loadTokenizer(tokenizer_path, &tokenizerFailure)) {
            setDetail(out_error, "词表加载失败（解码将返回空文本）：" + tokenizerFailure);
            // 模型仍可用（部分功能）；返回 OK 但给出提示
        }
    }
    return NC_OK;
}

int32_t nc_speech_unload(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (editor->speechRecognizer != nullptr) {
        editor->speechRecognizer->unload();
        editor->speechRecognizer.reset();
    }
    return NC_OK;
}

int32_t nc_speech_loaded(const NcEditor* editor) {
    if (editor == nullptr || editor->speechRecognizer == nullptr) {
        return 0;
    }
    return editor->speechRecognizer->loaded() ? 1 : 0;
}

int32_t nc_speech_transcribe(NcEditor* editor, const float* samples, int64_t frame_count,
                             int32_t sample_rate, int32_t language, char** out_segments_json,
                             char** out_error) {
    if (editor == nullptr || samples == nullptr || frame_count <= 0 || sample_rate <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、samples 为空");
    }
    if (editor->speechRecognizer == nullptr || !editor->speechRecognizer->loaded()) {
        setDetail(out_error, "语音识别模型未加载（先调用 nc_speech_load）");
        return failNc(NC_ERROR_UNSUPPORTED, "参数或状态非法");
    }
    SpeechLanguage speechLanguage;
    if (!speechLanguageFromInt(language, speechLanguage)) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    std::vector<SpeechSegment> segments;
    std::string failure;
    if (!editor->speechRecognizer->transcribe(samples, static_cast<std::size_t>(frame_count),
                                              sample_rate, speechLanguage, segments, nullptr,
                                              &failure)) {
        setDetail(out_error, failure);
        return failNc(NC_ERROR_INTERNAL, "参数或状态非法");
    }
    // 分段 JSON 序列化
    JsonValue array = JsonValue::array();
    for (const auto& segment : segments) {
        JsonValue item = JsonValue::object();
        item.set("start", JsonValue::number(segment.startSeconds));
        item.set("end", JsonValue::number(segment.endSeconds));
        item.set("text", JsonValue::string(segment.text));
        array.push(std::move(item));
    }
    if (out_segments_json != nullptr) {
        *out_segments_json = dupString(toJsonString(array));
    }
    return NC_OK;
}

int32_t nc_speech_mel(const float* samples, int64_t frame_count, int32_t sample_rate,
                      int32_t frames, float** out_mel) {
    if (samples == nullptr || frame_count <= 0 || sample_rate <= 0 || frames <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "samples 为空");
    }
    std::vector<float> mel;
    if (!SpeechRecognizer::computeMelSpectrogram(samples, static_cast<std::size_t>(frame_count),
                                                 sample_rate, frames, mel)) {
        return failNc(NC_ERROR_INTERNAL, "调用失败");
    }
    if (out_mel != nullptr) {
        float* buffer = new (std::nothrow) float[mel.size()];
        if (buffer == nullptr) {
            return failNc(NC_ERROR_INTERNAL, "buffer 为空");
        }
        std::memcpy(buffer, mel.data(), mel.size() * sizeof(float));
        *out_mel = buffer;
    }
    return NC_OK;
}

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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    const std::vector<Chapter> chapters = readChaptersFromSequence(*seq);
    if (static_cast<std::size_t>(index) >= chapters.size()) {
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
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
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、new_title 为空");
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    const std::vector<Chapter> chapters = readChaptersFromSequence(*seq);
    if (static_cast<std::size_t>(index) >= chapters.size()) {
        if (out_error != nullptr) *out_error = dupString("章节下标越界");
        return failNc(NC_ERROR_OUT_OF_RANGE, "取值超出允许范围");
    }
    std::string title = new_title;
    const std::size_t begin = title.find_first_not_of(" \t\r\n");
    const std::size_t end = title.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        if (out_error != nullptr) *out_error = dupString("标题不能为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
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
        return failResult(r);
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

// ---- 导出队列（文档第 6 部分「二、导出队列管理」+ 第 4 部分「后台导出」）----

int32_t nc_export_enqueue(NcEditor* editor, const char* name, const char* out_path,
                          int32_t preset_id, int32_t mode, int32_t include_audio,
                          int32_t attach_ai_label, const char* ai_label_text,
                          uint64_t* out_job_id, char** out_error) {
    if (editor == nullptr || name == nullptr || out_path == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、name、out_path 为空");
    }
    ExportPresetId preset = ExportPresetId::Custom;
    if (preset_id > 0) {
        preset = static_cast<ExportPresetId>(preset_id);
        // 越界防御：非内置预设回退 Custom
        const auto& presets = builtinExportPresets();
        bool found = false;
        for (const auto& p : presets) {
            if (p.id == preset) {
                found = true;
                break;
            }
        }
        if (!found) {
            preset = ExportPresetId::Custom;
        }
    }
    PerformanceMode performanceMode = PerformanceMode::Balanced;
    if (!performanceModeFromInt(mode, performanceMode)) {
        if (out_error != nullptr) *out_error = dupString("性能模式非法（0/1/2）");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const ExportQueueResult r = editor->exportQueue.enqueue(
        name, out_path, preset, performanceMode, editor->entitlement, now,
        include_audio != 0, attach_ai_label != 0,
        ai_label_text != nullptr ? ai_label_text : "AI 生成");
    if (out_job_id != nullptr) {
        *out_job_id = r.result.isOk() ? r.jobId : 0;
    }
    if (!r.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(r.result.message);
        return failResult(r.result);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_export_poll_next(NcEditor* editor, uint64_t* out_job_id, char** out_out_path,
                            int32_t* out_preset, int32_t* out_mode,
                            int32_t* out_include_audio, int32_t* out_attach_ai_label,
                            char** out_ai_label_text) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    // 调度策略按「队列中最早等待任务」的性能模式推导（平台层切换模式时重新拉取）
    ExportQueuePolicy policy = exportQueuePolicy(PerformanceMode::Balanced, 4);
    bool havePolicy = false;
    for (const auto& job : editor->exportQueue.jobs()) {
        if (job.state == ExportJobState::Queued) {
            policy = exportQueuePolicy(job.mode, 4);
            havePolicy = true;
            break;
        }
    }
    (void)havePolicy;
    const std::uint64_t jobId = editor->exportQueue.pollNext(policy);
    if (jobId == 0) {
        return 0;
    }
    const ExportJob* job = editor->exportQueue.find(jobId);
    if (job == nullptr) {
        return 0;
    }
    if (out_job_id != nullptr) *out_job_id = job->id;
    if (out_out_path != nullptr) *out_out_path = dupString(job->outputPath);
    if (out_preset != nullptr) *out_preset = static_cast<int32_t>(job->preset);
    if (out_mode != nullptr) *out_mode = static_cast<int32_t>(job->mode);
    if (out_include_audio != nullptr) *out_include_audio = job->includeAudio ? 1 : 0;
    if (out_attach_ai_label != nullptr) *out_attach_ai_label = job->attachAiLabel ? 1 : 0;
    if (out_ai_label_text != nullptr) *out_ai_label_text = dupString(job->aiLabelText);
    return 1;
}

int32_t nc_export_job_state(const NcEditor* editor, uint64_t job_id, int32_t* out_state,
                            double* out_progress, int64_t* out_frames_written, char** out_note) {
    if (editor == nullptr || job_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const ExportJob* job = editor->exportQueue.find(job_id);
    if (job == nullptr) {
        return 0;
    }
    if (out_state != nullptr) *out_state = static_cast<int32_t>(job->state);
    if (out_progress != nullptr) *out_progress = job->progress;
    if (out_frames_written != nullptr) *out_frames_written = job->framesWritten;
    if (out_note != nullptr) *out_note = dupString(job->note);
    return 1;
}

int32_t nc_export_job_progress(NcEditor* editor, uint64_t job_id, double progress) {
    if (editor == nullptr || job_id == 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const ExportQueueResult r = editor->exportQueue.updateProgress(job_id, progress);
    return failResult(r.result);
}

int32_t nc_export_job_update(NcEditor* editor, uint64_t job_id, int32_t action,
                             int64_t frames_written, const char* reason, int64_t now_unix,
                             char** out_error) {
    if (editor == nullptr || job_id == 0) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    ExportQueueResult r;
    switch (action) {
        case 0:  // 开始
            r = editor->exportQueue.markStarted(job_id, now_unix);
            break;
        case 1:  // 完成
            r = editor->exportQueue.markCompleted(job_id, frames_written, now_unix,
                                                  reason != nullptr ? reason : "");
            break;
        case 2:  // 失败
            r = editor->exportQueue.markFailed(job_id, reason != nullptr ? reason : "", now_unix);
            break;
        case 3:  // 暂停
            r = editor->exportQueue.pause(job_id, now_unix);
            break;
        case 4:  // 继续
            r = editor->exportQueue.resume(job_id, now_unix);
            break;
        case 5:  // 移除
            r = editor->exportQueue.remove(job_id, now_unix);
            break;
        default:
            if (out_error != nullptr) *out_error = dupString("未知动作（0-5）");
            return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    if (!r.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(r.result.message);
        return failResult(r.result);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_export_queue_summary(const NcEditor* editor, int32_t* out_queued,
                                int32_t* out_running, int32_t* out_active) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (out_queued != nullptr) {
        *out_queued = static_cast<int32_t>(editor->exportQueue.queuedCount());
    }
    if (out_running != nullptr) {
        *out_running = static_cast<int32_t>(editor->exportQueue.runningCount());
    }
    if (out_active != nullptr) {
        *out_active = static_cast<int32_t>(editor->exportQueue.activeCount());
    }
    return NC_OK;
}

void nc_export_queue_cancel_all(NcEditor* editor, int64_t now_unix) {
    if (editor == nullptr) {
        return;
    }
    editor->exportQueue.cancelAll(now_unix);
}

// ---- 媒体离线检测与重新链接（文档第 3 部分「四、媒体离线检测与重新链接」）----

namespace {

#ifdef NAIROCLIP_HAS_FFMPEG
/// FFmpeg 探测器：文件存在性用 avformat_open_input 探测；
/// 摘要用流式读文件 + SHA-256（crypto 模块）。
class FFmpegOfflineProbe : public OfflineProbe {
public:
    bool fileExists(const std::string& path) override {
        std::ifstream f(path, std::ios::binary);
        return f.good();
    }
    std::string fileDigest(const std::string& path) override {
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) {
            return std::string();
        }
        Sha256 hasher;
        std::vector<char> buffer(64 * 1024);
        while (f.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
               f.gcount() > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(f.gcount()));
            if (f.eof()) {
                break;
            }
        }
        std::uint8_t digest[kSha256DigestSize];
        hasher.finalize(digest);
        return sha256Hex(digest);
    }
    std::vector<std::string> listDirectory(const std::string& dir) override {
        std::vector<std::string> names;
        DIR* handle = opendir(dir.c_str());
        if (handle == nullptr) {
            return names;
        }
        while (struct dirent* entry = readdir(handle)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            names.push_back(name);
        }
        closedir(handle);
        return names;
    }
};
#endif

/// JSON 转义（与项目 JSON 约定一致的最小子集：引号/反斜杠/控制字符）。
std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

}  // namespace

int32_t nc_media_scan_offline(NcEditor* editor, int32_t probe_kind, char** out_report_json,
                              char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    OfflineProbe* probe = nullptr;
#ifdef NAIROCLIP_HAS_FFMPEG
    static FFmpegOfflineProbe ffmpegProbe;
    if (probe_kind == 1) {
        probe = &ffmpegProbe;
    }
#else
    if (probe_kind == 1) {
        if (out_error != nullptr) *out_error = dupString("此构建未包含 FFmpeg，无法使用探测器");
        return failNc(NC_ERROR_UNSUPPORTED, "调用失败");
    }
#endif
    (void)probe_kind;
    editor->offlineReport = scanOfflineAssets(editor->project, probe);
    if (out_report_json != nullptr) {
        std::string json = "{\"offline\":" + std::to_string(editor->offlineReport.offlineCount()) +
                           ",\"online\":" + std::to_string(editor->offlineReport.onlineCount()) +
                           ",\"items\":[";
        for (std::size_t i = 0; i < editor->offlineReport.checks.size(); ++i) {
            const AssetOfflineCheck& check = editor->offlineReport.checks[i];
            if (i > 0) json += ",";
            json += "{\"id\":" + std::to_string(check.asset.value) +
                    ",\"name\":\"" + jsonEscape(check.name) +
                    "\",\"path\":\"" + jsonEscape(check.path) +
                    "\",\"status\":\"" + offlineStatusName(check.status) +
                    "\",\"message\":\"" + jsonEscape(check.message) + "\"}";
        }
        json += "],\"notes\":[";
        for (std::size_t i = 0; i < editor->offlineReport.notes.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + jsonEscape(editor->offlineReport.notes[i]) + "\"";
        }
        json += "]}";
        *out_report_json = dupString(json);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_media_apply_offline_flags(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    return static_cast<int32_t>(applyOfflineFlags(editor->project, editor->offlineReport));
}

int32_t nc_media_relink(NcEditor* editor, uint64_t asset_id, const char* new_path,
                        int32_t verify_digest, char** out_warning, char** out_error) {
    if (editor == nullptr || new_path == nullptr || asset_id == 0) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、new_path 为空");
    }
    OfflineProbe* probe = nullptr;
#ifdef NAIROCLIP_HAS_FFMPEG
    static FFmpegOfflineProbe relinkProbe;
    probe = &relinkProbe;
#endif
    const MediaAsset* asset = editor->project.findAsset(AssetId{asset_id});
    if (asset == nullptr) {
        if (out_error != nullptr) *out_error = dupString("素材不存在");
        return failNc(NC_ERROR_NOT_FOUND, "asset 为空");
    }
    const std::string expectedDigest = verify_digest != 0 ? asset->contentSha256 : std::string();
    const RelinkResult r = relinkAsset(*editor->editor, editor->project, AssetId{asset_id},
                                       new_path, probe, expectedDigest);
    if (out_warning != nullptr) {
        *out_warning = r.digestWarnings.empty() ? nullptr
                                                : dupString(r.digestWarnings.front());
    }
    if (!r.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(r.result.message);
        return failResult(r.result);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_media_find_relink_candidates(NcEditor* editor, const char* search_dirs_json,
                                        char** out_candidates_json, char** out_error) {
    if (editor == nullptr || search_dirs_json == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、search_dirs_json 为空");
    }
    // 极简 JSON 数组解析（["a","b"]；容忍空白）
    std::vector<std::string> dirs;
    {
        const char* p = search_dirs_json;
        std::string current;
        bool inString = false;
        bool escape = false;
        for (; *p != '\0'; ++p) {
            const char c = *p;
            if (inString) {
                if (escape) {
                    current += c;
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    dirs.push_back(current);
                    current.clear();
                    inString = false;
                } else {
                    current += c;
                }
            } else if (c == '"') {
                inString = true;
            }
        }
    }
    if (dirs.empty()) {
        if (out_error != nullptr) *out_error = dupString("目录列表为空或格式非法");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    OfflineProbe* probe = nullptr;
#ifdef NAIROCLIP_HAS_FFMPEG
    static FFmpegOfflineProbe candidateProbe;
    probe = &candidateProbe;
#endif
    // 仅对离线素材查找（在线素材无需重链）
    std::vector<MediaAsset> offlineAssets;
    for (const auto& asset : editor->project.assets) {
        if (asset.offline || !asset.path.empty()) {
            offlineAssets.push_back(asset);
        }
    }
    const std::vector<RelinkCandidate> candidates =
        findRelinkCandidates(offlineAssets, dirs, probe);
    if (out_candidates_json != nullptr) {
        std::string json = "[";
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const RelinkCandidate& c = candidates[i];
            if (i > 0) json += ",";
            json += "{\"asset\":" + std::to_string(c.asset.value) +
                    ",\"fileName\":\"" + jsonEscape(c.fileName) +
                    "\",\"path\":\"" + jsonEscape(c.candidatePath) +
                    "\",\"digestMatches\":" + (c.digestMatches ? "true" : "false") + "}";
        }
        json += "]";
        *out_candidates_json = dupString(json);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_media_relink_batch(NcEditor* editor, const char* mappings_json,
                              int32_t* out_relinked, char** out_report_json, char** out_error) {
    if (editor == nullptr || mappings_json == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、mappings_json 为空");
    }
    // 极简解析：[{"asset":1,"path":"..."},...]（容忍键序与空白）
    std::vector<std::pair<AssetId, std::string>> mappings;
    {
        const char* p = mappings_json;
        while (*p != '\0') {
            const char* assetPos = std::strstr(p, "\"asset\"");
            if (assetPos == nullptr) {
                break;
            }
            const char* colon = std::strchr(assetPos, ':');
            if (colon == nullptr) {
                break;
            }
            const std::uint64_t id = std::strtoull(colon + 1, nullptr, 10);
            const char* pathPos = std::strstr(colon, "\"path\"");
            if (pathPos == nullptr) {
                break;
            }
            const char* pathColon = std::strchr(pathPos, ':');
            if (pathColon == nullptr) {
                break;
            }
            const char* open = std::strchr(pathColon, '"');
            if (open == nullptr) {
                break;
            }
            std::string path;
            bool escape = false;
            for (const char* q = open + 1; *q != '\0'; ++q) {
                if (escape) {
                    path += *q;
                    escape = false;
                } else if (*q == '\\') {
                    escape = true;
                } else if (*q == '"') {
                    break;
                } else {
                    path += *q;
                }
            }
            if (id != 0 && !path.empty()) {
                mappings.emplace_back(AssetId{id}, path);
            }
            p = pathColon + 1;
        }
    }
    if (mappings.empty()) {
        if (out_error != nullptr) *out_error = dupString("重链列表为空或格式非法");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    OfflineProbe* probe = nullptr;
#ifdef NAIROCLIP_HAS_FFMPEG
    static FFmpegOfflineProbe batchProbe;
    probe = &batchProbe;
#endif
    const RelinkResult r = relinkAssets(*editor->editor, editor->project, mappings, probe);
    if (out_relinked != nullptr) *out_relinked = static_cast<int32_t>(r.relinked);
    if (out_report_json != nullptr) {
        std::string json = "{\"relinked\":" + std::to_string(r.relinked) + ",\"messages\":[";
        for (std::size_t i = 0; i < r.digestWarnings.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + jsonEscape(r.digestWarnings[i]) + "\"";
        }
        json += "]}";
        *out_report_json = dupString(json);
    }
    if (!r.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(r.result.message);
        return failResult(r.result);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_media_set_digest(NcEditor* editor, uint64_t asset_id, const char* sha256_hex,
                            char** out_error) {
    if (editor == nullptr || asset_id == 0) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Result r = setAssetDigest(editor->project, AssetId{asset_id},
                                    sha256_hex != nullptr ? sha256_hex : "");
    if (!r.isOk()) {
        if (out_error != nullptr) *out_error = dupString(r.message);
        return failResult(r);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

// ---- 智能成片（多版本）+ 重新生成影响分析（文档第 7 部分 4.1 / 4.4 / 六 / 七）----
namespace {

/// 从扁平 JSON 恢复 StyleProfile（字段见 capi.h 注释；缺省字段使用中性值）。
StyleProfile styleProfileFromJson(const char* style_json, bool* outOk) {
    StyleProfile profile;
    if (outOk != nullptr) *outOk = false;
    if (style_json == nullptr) {
        return profile;
    }
    JsonValue root;
    JsonParseError error;
    if (!parseJson(style_json, root, error) || !root.isObject()) {
        return profile;
    }
    auto num = [&](const char* key, double fallback) {
        return root.has(key) ? root.get(key).asNumber(fallback) : fallback;
    };
    auto timeOf = [&](const char* key) {
        return Rational::make(static_cast<std::int64_t>(num(key, 0.0) * 1000.0), 1000);
    };
    profile.rhythm.shotCount =
        static_cast<std::size_t>(std::max<double>(0.0, num("shotCount", 0.0)));
    profile.rhythm.totalDuration = timeOf("totalSeconds");
    profile.rhythm.averageShotDuration = timeOf("average");
    profile.rhythm.median = timeOf("median");
    profile.rhythm.p25 = timeOf("p25");
    profile.rhythm.p75 = timeOf("p75");
    profile.rhythm.shortestShot = timeOf("shortest");
    profile.rhythm.longestShot = timeOf("longest");
    profile.color.avgR = num("avgR", 0.5);
    profile.color.avgG = num("avgG", 0.5);
    profile.color.avgB = num("avgB", 0.5);
    profile.color.avgSaturation = num("saturation", 0.5);
    profile.color.avgContrast = num("contrast", 0.5);
    if (outOk != nullptr) *outOk = profile.valid();
    return profile;
}

/// 缓存的成片计划（两段式：nc_smart_movie_plan → nc_smart_movie_apply）。
SmartMoviePlan& smartMovieCache() {
    static SmartMoviePlan plan;
    return plan;
}

SmartMovieConfig& smartMovieConfigCache() {
    static SmartMovieConfig config;
    return config;
}

/// 缓存的重新生成报告（nc_regeneration_clear 消费）。
RegenerationImpactReport& regenerationCache() {
    static RegenerationImpactReport report;
    return report;
}

}  // namespace

int32_t nc_reference_style_access(const NcEditor* editor, char** out_reason) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const ReferenceStyleAccess access = referenceStyleAccess(
        editor->entitlement, static_cast<std::int64_t>(std::time(nullptr)));
    if (out_reason != nullptr) *out_reason = dupString(access.reason);
    return access.allowed ? 1 : 0;
}

int32_t nc_movie_version_access(const NcEditor* editor, int32_t requested,
                                int32_t* out_allowed_count, char** out_reason) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const VersionCountAccess access = versionCountAccess(
        editor->entitlement, static_cast<std::int64_t>(std::time(nullptr)), requested);
    if (out_allowed_count != nullptr) *out_allowed_count = access.allowedCount;
    if (out_reason != nullptr) *out_reason = dupString(access.reason);
    return access.allowed ? 1 : 0;
}

int32_t nc_smart_movie_plan(NcEditor* editor, int32_t version_count, double pace_factor,
                            double target_seconds, const char* style_json,
                            const uint64_t* material_ids, const double* material_seconds,
                            int32_t material_count, char** out_summary) {
    if (editor == nullptr || material_count <= 0 || material_ids == nullptr ||
        material_seconds == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor、material_ids、material_seconds 为空");
    }
    bool styleOk = false;
    const StyleProfile profile = styleProfileFromJson(style_json, &styleOk);
    if (!styleOk) {
        return NC_ERROR_INVALID_ARGUMENT;  // 风格画像无效（无有效镜头）
    }
    // 版本数门控（fail-closed：超限自动降级到允许值）
    const VersionCountAccess access = versionCountAccess(
        editor->entitlement, static_cast<std::int64_t>(std::time(nullptr)), version_count);
    SmartMovieConfig config;
    config.versionCount = access.allowedCount > 0 ? access.allowedCount : 1;
    config.paceFactor = pace_factor < 0.0 ? 0.0 : (pace_factor > 1.0 ? 1.0 : pace_factor);
    config.targetSeconds = target_seconds < 0.0 ? 0.0 : target_seconds;
    config.applyColor = true;
    config.applyTransitions = true;
    config.color = profile.color;
    for (int32_t i = 0; i < material_count; ++i) {
        MaterialSegment segment;
        segment.asset = AssetId{material_ids[i]};
        segment.usableDuration =
            Rational::make(static_cast<std::int64_t>(material_seconds[i] * 1000.0), 1000);
        config.material.push_back(segment);
    }
    smartMovieCache() = planSmartMovie(profile, config);
    smartMovieConfigCache() = config;
    if (out_summary != nullptr) {
        std::string summary = smartMovieCache().summary;
        if (!access.allowed) {
            summary += "（注意：" + access.reason + "）";
        }
        *out_summary = dupString(summary);
    }
    return static_cast<int32_t>(smartMovieCache().okCount());
}

int32_t nc_smart_movie_apply(NcEditor* editor, const int32_t* track_indexes, int32_t track_count,
                             int32_t* out_applied, char** out_failed_json, char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const SmartMoviePlan& plan = smartMovieCache();
    if (plan.versions.empty()) {
        if (out_error != nullptr) {
            *out_error = dupString("没有已计划的版本，请先调用 nc_smart_movie_plan");
        }
        return failNc(NC_ERROR_INVALID_ARGUMENT, "调用失败");
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    std::vector<TrackId> trackIds;
    std::vector<std::string> preFailures;
    for (int32_t i = 0; i < track_count; ++i) {
        const int32_t index = track_indexes != nullptr ? track_indexes[i] : -1;
        if (index < 0 || static_cast<std::size_t>(index) >= seq->tracks.size()) {
            preFailures.push_back("轨道下标越界：" + std::to_string(index));
            trackIds.push_back(TrackId{});  // 占位（batch 落盘会因轨道不存在而记失败）
            continue;
        }
        trackIds.push_back(seq->tracks[static_cast<std::size_t>(index)].id);
    }
    const SmartMovieBatchResult batch = applySmartMovieVersions(
        *editor->editor, editor->project, seq->id, trackIds, plan, smartMovieConfigCache());
    if (out_applied != nullptr) *out_applied = static_cast<int32_t>(batch.versionsApplied);
    if (out_failed_json != nullptr) {
        std::string json = "{\"failed\":[";
        for (std::size_t i = 0; i < batch.failedVersionIndexes.size(); ++i) {
            if (i > 0) json += ",";
            json += std::to_string(batch.failedVersionIndexes[i]);
        }
        json += "],\"messages\":[";
        bool first = true;
        for (const auto& message : batch.failures) {
            if (!first) json += ",";
            first = false;
            json += "\"" + jsonEscape(message) + "\"";
        }
        for (const auto& message : preFailures) {
            if (!first) json += ",";
            first = false;
            json += "\"" + jsonEscape(message) + "\"";
        }
        json += "]}";
        *out_failed_json = dupString(json);
    }
    if (!batch.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(batch.result.message);
        return failResult(batch.result);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

int32_t nc_regeneration_impact(const NcEditor* editor, int32_t scope, char** out_report_json) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    const RegenerationScope regenScope =
        scope == 1 ? RegenerationScope::Sequence : RegenerationScope::WholeProject;
    regenerationCache() = analyzeRegenerationImpact(editor->project, seq->id, regenScope);
    if (out_report_json != nullptr) {
        const RegenerationImpactReport& report = regenerationCache();
        std::string json = "{\"scope\":" + std::to_string(scope) +
                           ",\"affectedTracks\":" + std::to_string(report.affectedTracks) +
                           ",\"protectedTracks\":" + std::to_string(report.protectedTracks) +
                           ",\"willLose\":[";
        for (std::size_t i = 0; i < report.willLose.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"code\":\"" + jsonEscape(report.willLose[i].code) + "\",\"label\":\"" +
                    jsonEscape(report.willLose[i].label) +
                    "\",\"count\":" + std::to_string(report.willLose[i].count) + "}";
        }
        json += "],\"willKeep\":[";
        for (std::size_t i = 0; i < report.willKeep.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"code\":\"" + jsonEscape(report.willKeep[i].code) + "\",\"label\":\"" +
                    jsonEscape(report.willKeep[i].label) +
                    "\",\"count\":" + std::to_string(report.willKeep[i].count) + "}";
        }
        json += "],\"summary\":\"" + jsonEscape(report.summary) + "\"}";
        *out_report_json = dupString(json);
    }
    return static_cast<int32_t>(regenerationCache().lossCount());
}

int32_t nc_regeneration_clear(NcEditor* editor, int32_t scope, char** out_cleared_json,
                              char** out_error) {
    if (editor == nullptr) {
        if (out_error != nullptr) *out_error = dupString("参数为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const Sequence* seq = editor->project.mainSequence();
    if (seq == nullptr) {
        if (out_error != nullptr) *out_error = dupString("工程没有主序列");
        return failNc(NC_ERROR_NOT_FOUND, "seq 为空");
    }
    const RegenerationScope regenScope =
        scope == 1 ? RegenerationScope::Sequence : RegenerationScope::WholeProject;
    if (regenerationCache().scope != regenScope) {
        regenerationCache() = analyzeRegenerationImpact(editor->project, seq->id, regenScope);
    }
    const RegenerationResult cleared =
        clearForRegeneration(*editor->editor, editor->project, seq->id, regenerationCache());
    if (!cleared.result.isOk()) {
        if (out_error != nullptr) *out_error = dupString(cleared.result.message);
        return failResult(cleared.result);
    }
    if (out_cleared_json != nullptr) {
        std::string json = "{\"tracksCleared\":" + std::to_string(cleared.tracksCleared) +
                           ",\"tracksProtected\":" + std::to_string(cleared.tracksProtected) +
                           ",\"markersCleared\":" + std::to_string(cleared.markersCleared) +
                           ",\"subtitlesCleared\":" + std::to_string(cleared.subtitlesCleared) +
                           ",\"summary\":\"" + jsonEscape(cleared.summary) + "\"}";
        *out_cleared_json = dupString(json);
    }
    if (out_error != nullptr) *out_error = nullptr;
    return NC_OK;
}

// ---- 隐形水印（文档第 8 部分 3.1「隐形水印（事后追溯）」）----

uint32_t nc_watermark_account_hash(const char* account_id) {
    return accountHashOf(account_id != nullptr ? account_id : "");
}

int32_t nc_watermark_set_export(NcEditor* editor, int32_t enabled, const char* account_id,
                                int32_t export_sequence) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    InvisibleWatermarkSpec spec;
    spec.enabled = enabled != 0;
    if (spec.enabled) {
        spec.payload.accountHash = accountHashOf(account_id != nullptr ? account_id : "");
        spec.payload.exportSequence =
            static_cast<std::uint16_t>(export_sequence < 0 ? 0 : export_sequence);
        spec.payload.finalize();
    }
    editor->invisibleWatermark = spec;
    return NC_OK;
}

int32_t nc_watermark_detect(const uint8_t* rgba, int32_t width, int32_t height,
                            uint32_t* out_account_hash, int32_t* out_export_sequence,
                            double* out_agreement, char** out_note) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "rgba 为空");
    }
    const InvisibleDetectResult detected = detectInvisibleWatermark(rgba, width, height);
    if (out_account_hash != nullptr) *out_account_hash = detected.payload.accountHash;
    if (out_export_sequence != nullptr) {
        *out_export_sequence = static_cast<int32_t>(detected.payload.exportSequence);
    }
    if (out_agreement != nullptr) *out_agreement = detected.bitAgreement;
    if (out_note != nullptr) *out_note = dupString(detected.note);
    return detected.detected ? 1 : 0;
}

// ---- 会员心跳校验（文档第 8 部分「五、会员心跳校验具体流程」）----

int32_t nc_heartbeat_handle(NcEditor* editor, int64_t server_time_unix, int32_t tier,
                            int32_t lifetime, int64_t expires_at_unix, int32_t active_devices,
                            int32_t device_limit, int32_t signature_valid, int32_t server_locked,
                            const char* message, int32_t* out_verdict, char** out_message) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    HeartbeatResponse response;
    response.transportOk = true;
    response.serverTimeUnix = server_time_unix;
    response.tier = tier == 1 ? MembershipTier::Pro : MembershipTier::Free;
    response.lifetime = lifetime != 0;
    response.expiresAtUnix = expires_at_unix;
    response.activeDevices = active_devices < 0 ? 0 : active_devices;
    response.deviceLimit = device_limit < 0 ? 0 : device_limit;
    response.signatureValid = signature_valid != 0;
    response.serverLocked = server_locked != 0;
    if (message != nullptr) {
        response.message = message;
    }
    std::string outText;
    const HeartbeatVerdict verdict = editor->heartbeat.handleResponse(
        response, HeartbeatConfig{}, editor->entitlement,
        static_cast<std::int64_t>(std::time(nullptr)), &outText);
    if (out_verdict != nullptr) *out_verdict = static_cast<int32_t>(verdict);
    if (out_message != nullptr) *out_message = dupString(outText);
    return NC_OK;
}

int32_t nc_heartbeat_network_error(NcEditor* editor, char** out_message) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    HeartbeatResponse response;
    response.transportOk = false;
    std::string outText;
    const HeartbeatVerdict verdict = editor->heartbeat.handleResponse(
        response, HeartbeatConfig{}, editor->entitlement,
        static_cast<std::int64_t>(std::time(nullptr)), &outText);
    if (out_message != nullptr) *out_message = dupString(outText);
    return static_cast<int32_t>(verdict);
}

int32_t nc_heartbeat_status(const NcEditor* editor, int32_t* out_locked,
                            int32_t* out_warning_count, int64_t* out_last_checked_unix) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (out_locked != nullptr) *out_locked = editor->heartbeat.locked() ? 1 : 0;
    if (out_warning_count != nullptr) *out_warning_count = editor->heartbeat.warningCount();
    if (out_last_checked_unix != nullptr) {
        *out_last_checked_unix = editor->heartbeat.lastCheckedUnix();
    }
    return NC_OK;
}

int32_t nc_heartbeat_periodic_due(const NcEditor* editor, int64_t now_unix,
                                  int64_t interval_seconds) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    const std::int64_t interval = interval_seconds > 0 ? interval_seconds : 1800;
    return editor->heartbeat.periodicDue(now_unix, interval) ? 1 : 0;
}

int32_t nc_heartbeat_clear_lock(NcEditor* editor) {
    if (editor == nullptr) {
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    editor->heartbeat.clearLock();
    return NC_OK;
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

const char* nc_last_error(void) { return g_lastError.c_str(); }

}  // extern "C"

// =============================================================================
// 剪辑模板（B2-1 ~ B2-4）
// =============================================================================

namespace {

/// 把单个模板序列化为 UI 列表项。
JsonValue templateSummaryJson(const EditTemplate& t) {
    JsonValue item = JsonValue::object();
    item.set("id", JsonValue::string(t.id));
    item.set("name", JsonValue::string(t.name));
    item.set("category", JsonValue::string(t.category));
    item.set("slotCount", JsonValue::integer(static_cast<std::int64_t>(t.slots.size())));
    item.set("totalDurationUs", JsonValue::integer(toMicroseconds(t.totalDuration)));
    if (!t.musicHint.empty()) item.set("musicHint", JsonValue::string(t.musicHint));
    if (!t.coverPath.empty()) item.set("coverPath", JsonValue::string(t.coverPath));
    return item;
}

/// 把候选素材数组转成 FillCandidate（无特征数据时的路径）。
std::vector<FillCandidate> candidatesFromIds(const Project& project, const uint64_t* ids,
                                            int32_t count) {
    std::vector<FillCandidate> out;
    for (int32_t i = 0; i < count; ++i) {
        const MediaAsset* asset = project.findAsset(AssetId{ids[i]});
        if (asset == nullptr) continue;  // 不存在的素材直接跳过（摘要里会体现）
        FillCandidate c;
        c.asset = *asset;
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace

int32_t nc_template_list(char** out_json, char** out_error) {
    JsonValue arr = JsonValue::array();
    for (const auto& t : builtinTemplates()) {
        arr.push(templateSummaryJson(t));
    }
    if (out_json != nullptr) {
        *out_json = dupString(toJsonString(arr));
    }
    return NC_OK;
}

int32_t nc_template_slot_count(const char* template_id) {
    if (template_id == nullptr) {
        return -1;
    }
    const EditTemplate* t = findBuiltinTemplate(template_id);
    if (t == nullptr) {
        return -1;
    }
    return static_cast<int32_t>(t->slots.size());
}

int32_t nc_template_apply(NcEditor* editor, const char* template_id,
                          const uint64_t* material_ids, int32_t count, char** out_summary,
                          char** out_error) {
    if (editor == nullptr) {
        setDetail(out_error, "editor 为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (template_id == nullptr) {
        setDetail(out_error, "template_id 为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "template_id 为空");
    }
    if (material_ids == nullptr || count <= 0) {
        setDetail(out_error, "material_ids 为空或 count <= 0");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "material_ids 为空或 count <= 0");
    }
    const EditTemplate* tmpl = findBuiltinTemplate(template_id);
    if (tmpl == nullptr) {
        setDetail(out_error, std::string("模板不存在: ") + template_id);
        return failNc(NC_ERROR_NOT_FOUND, std::string("模板不存在: ") + template_id);
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        setDetail(out_error, "工程没有主序列");
        return failNc(NC_ERROR_NOT_FOUND, "工程没有主序列");
    }

    const std::vector<FillCandidate> candidates = candidatesFromIds(editor->project, material_ids, count);
    if (candidates.empty()) {
        setDetail(out_error, "给定的素材 ID 都不在工程素材表中");
        return failNc(NC_ERROR_NOT_FOUND, "候选素材都不在工程素材表中");
    }

    // 自动填充
    const FillResult fill = fillTemplateSlots(*tmpl, candidates);
    std::vector<MaterialAssignment> assigns = assignmentsFromFill(fill, candidates);
    if (assigns.empty()) {
        setDetail(out_error, "没有素材能覆盖任何槽位（可能时长都不够）");
        return failNc(NC_ERROR_OUT_OF_RANGE, "没有素材能覆盖任何槽位（可能时长都不够）");
    }

    TemplateApplyResult result;
    const Result applied = applyTemplate(*editor->editor, main->id, *tmpl, assigns, result);
    if (!applied.isOk()) {
        setDetail(out_error, applied.message);
        return failNc(toNcError(applied.code), applied.message);
    }

    std::string summary = describeApplyResult(result);
    if (!result.warnings.empty()) {
        summary += "；提示：";
        for (std::size_t i = 0; i < result.warnings.size(); ++i) {
            if (i > 0) summary += "；";
            summary += result.warnings[i];
        }
    }
    if (out_summary != nullptr) {
        *out_summary = dupString(summary);
    }
    return NC_OK;
}

int32_t nc_template_apply_slots(NcEditor* editor, const char* template_id,
                                const int32_t* slot_indices, const uint64_t* material_ids,
                                int32_t count, char** out_summary, char** out_error) {
    if (editor == nullptr) {
        setDetail(out_error, "editor 为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "editor 为空");
    }
    if (template_id == nullptr) {
        setDetail(out_error, "template_id 为空");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "template_id 为空");
    }
    if (slot_indices == nullptr || material_ids == nullptr || count <= 0) {
        setDetail(out_error, "slot_indices/material_ids 为空或 count <= 0");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "slot_indices/material_ids 为空或 count <= 0");
    }
    const EditTemplate* tmpl = findBuiltinTemplate(template_id);
    if (tmpl == nullptr) {
        setDetail(out_error, std::string("模板不存在: ") + template_id);
        return failNc(NC_ERROR_NOT_FOUND, std::string("模板不存在: ") + template_id);
    }
    const Sequence* main = editor->project.mainSequence();
    if (main == nullptr) {
        setDetail(out_error, "工程没有主序列");
        return failNc(NC_ERROR_NOT_FOUND, "工程没有主序列");
    }

    std::vector<MaterialAssignment> assigns;
    for (int32_t i = 0; i < count; ++i) {
        if (slot_indices[i] < 0) {
            setDetail(out_error, "slot_index 不能为负");
            return failNc(NC_ERROR_INVALID_ARGUMENT, "slot_index 不能为负");
        }
        MaterialAssignment m;
        m.slotIndex = static_cast<std::size_t>(slot_indices[i]);
        m.asset = AssetId{material_ids[i]};
        assigns.push_back(m);
    }

    TemplateApplyResult result;
    const Result applied = applyTemplate(*editor->editor, main->id, *tmpl, assigns, result);
    if (!applied.isOk()) {
        setDetail(out_error, applied.message);
        return failNc(toNcError(applied.code), applied.message);
    }

    std::string summary = describeApplyResult(result);
    if (!result.warnings.empty()) {
        summary += "；提示：";
        for (std::size_t i = 0; i < result.warnings.size(); ++i) {
            if (i > 0) summary += "；";
            summary += result.warnings[i];
        }
    }
    if (out_summary != nullptr) {
        *out_summary = dupString(summary);
    }
    return NC_OK;
}

// =============================================================================
// 拖放相容性查询（B2-8）
// =============================================================================

namespace {

bool trackTypeFromInt(int32_t v, TrackType& out) {
    switch (v) {
        case 0: out = TrackType::Video; return true;
        case 1: out = TrackType::Audio; return true;
        case 2: out = TrackType::Subtitle; return true;
        case 3: out = TrackType::Adjustment; return true;
        default: return false;
    }
}

bool mediaTypeFromInt(int32_t v, MediaType& out) {
    switch (v) {
        case 0: out = MediaType::Video; return true;
        case 1: out = MediaType::Audio; return true;
        case 2: out = MediaType::Image; return true;
        case 3: out = MediaType::Unknown; return true;
        default: return false;
    }
}

/// 生成不相容原因（中文，供 UI tooltip）。
std::string placementRejectReason(TrackType trackType, MediaType mediaType) {
    switch (trackType) {
        case TrackType::Video:
            return std::string(mediaTypeLabel(mediaType)) + "不能放入视频轨（会显示为黑屏）";
        case TrackType::Audio:
            return std::string(mediaTypeLabel(mediaType)) +
                   "不能放入音频轨（读不出声音）";
        case TrackType::Subtitle:
            return "字幕轨不接受片段（字幕请用字幕编辑）";
        case TrackType::Adjustment:
            return "调整图层不接受片段";
    }
    return "不能放入该轨道";
}

}  // namespace

int32_t nc_track_accepts_media(int32_t track_type, int32_t media_type) {
    TrackType tt;
    MediaType mt;
    if (!trackTypeFromInt(track_type, tt) || !mediaTypeFromInt(media_type, mt)) {
        failNc(NC_ERROR_INVALID_ARGUMENT, "track_type 或 media_type 超出枚举范围");
        return -1;
    }
    return trackAcceptsClipType(tt, mt) == PlacementCompat::Rejected ? 0 : 1;
}

int32_t nc_track_placement_reason(int32_t track_type, int32_t media_type, char** out_reason,
                                  char** out_error) {
    TrackType tt;
    MediaType mt;
    if (!trackTypeFromInt(track_type, tt) || !mediaTypeFromInt(media_type, mt)) {
        setDetail(out_error, "track_type 或 media_type 超出枚举范围");
        return failNc(NC_ERROR_INVALID_ARGUMENT, "track_type 或 media_type 超出枚举范围");
    }
    const PlacementCompat compat = trackAcceptsClipType(tt, mt);
    if (compat == PlacementCompat::Rejected) {
        if (out_reason != nullptr) {
            *out_reason = dupString(placementRejectReason(tt, mt));
        }
        return 0;
    }
    if (compat == PlacementCompat::AllowedBecauseUnknown) {
        if (out_reason != nullptr) {
            *out_reason = dupString("素材类型未知（尚未探测），暂时允许");
        }
    }
    return 1;
}
