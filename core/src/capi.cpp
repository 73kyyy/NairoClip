#include "nairoclip/core/capi.h"

#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

#include <cstring>
#include <new>
#include <string>

using namespace nairoclip::core;

// 不透明句柄的真实定义。
struct NcEditor {
    Project project;
    TimelineEditor* editor;  // 堆分配（依赖 project 引用，必须在 project 之后构造）
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

void nc_free_string(char* text) { delete[] text; }

}  // extern "C"
