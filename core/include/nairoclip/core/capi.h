#pragma once
// =============================================================================
// NairoClip C ABI —— 平台绑定层（JNI / Swift / Qt 均经此外接）
// =============================================================================
// 设计原则：
//  1. 纯 C 接口（extern "C"），无 C++ 类型穿越边界，ABI 稳定；
//  2. 不透明句柄（nc_editor* 等），调用方不能触碰内部结构；
//  3. 所有字符串参数 UTF-8；返回的错误码为 int32（0 = 成功）；
//  4. 线程约束：单个 nc_editor 句柄不允许多线程并发调用，
//     由平台层保证串行化（UI 线程或专用工作线程）；
//  5. 内存约束：nc_* 返回的字符串由库分配，调用方必须用 nc_free_string
//     释放；传入指针在调用返回后即可失效（库内部不长期持有）。
//
// 版本策略：
//  - NC_ABI_VERSION 随不兼容变更递增；
//  - nc_abi_version() 供平台层启动时校验。

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NC_ABI_VERSION 1

/// 运行库 ABI 版本。
int32_t nc_abi_version(void);

/// 错误码（与 core::ErrorCode 对齐）。
enum NcErrorCode {
    NC_OK = 0,
    NC_ERROR_INVALID_ARGUMENT = 1,
    NC_ERROR_NOT_FOUND = 2,
    NC_ERROR_OVERLAP = 3,
    NC_ERROR_OUT_OF_RANGE = 4,
    NC_ERROR_LOCKED = 5,
    NC_ERROR_UNSUPPORTED = 6,
    NC_ERROR_INTERNAL = 7,
};

/// 不透明编辑器句柄。
typedef struct NcEditor NcEditor;

// ---- 生命周期 ----

/// 创建编辑器（内含空工程）。
NcEditor* nc_editor_create(void);
/// 销毁编辑器；传入 NULL 安全。
void nc_editor_destroy(NcEditor* editor);

// ---- 工程序列化 ----

/// 保存工程为 .nclip 文本；返回字符串需 nc_free_string 释放。
char* nc_project_save(const NcEditor* editor);
/// 从 .nclip 文本载入工程。失败返回错误码，detail 出参（可 NULL）接收错误信息。
int32_t nc_project_load(NcEditor* editor, const char* nclip_text, char** detail);

// ---- 查询 ----

/// 工程当前时长（微秒）。
int64_t nc_project_duration_us(const NcEditor* editor);
/// 轨道数量（主序列）。
int32_t nc_track_count(const NcEditor* editor);
/// 主序列主轨道上的片段数量。
int32_t nc_clip_count(const NcEditor* editor, int32_t track_index);

// ---- 编辑操作（最小集；逐步扩展） ----

/// 添加素材，返回素材 ID（0 = 失败）。
/// type: 0=video 1=audio 2=image。
uint64_t nc_import_asset(NcEditor* editor, const char* name, const char* path, int32_t type,
                         int64_t duration_us);

/// 添加轨道，返回轨道 ID（0 = 失败）。
uint64_t nc_add_track(NcEditor* editor, const char* name, int32_t track_type);

/// 在轨道上插入片段。
/// track_index 为轨道下标，返回片段 ID（0 = 失败）。
uint64_t nc_insert_clip(NcEditor* editor, int32_t track_index, uint64_t asset_id,
                        int64_t start_us, int64_t duration_us);

/// 撤销 / 重做。
int32_t nc_undo(NcEditor* editor);
int32_t nc_redo(NcEditor* editor);

// ---- 字幕 ----

/// 添加/更新字幕。返回 >0 成功（返回 SubtitleId），0 失败。
/// start_us/duration_us 为微秒。
uint64_t nc_upsert_subtitle(NcEditor* editor, int64_t start_us, int64_t duration_us,
                            const char* text);

/// 删除字幕，返回错误码。
int32_t nc_remove_subtitle(NcEditor* editor, uint64_t subtitle_id);

/// 字幕数量。
int32_t nc_subtitle_count(const NcEditor* editor);

/// 读取第 index 条字幕的时间范围（微秒）与文本。
/// out_text 需 nc_free_string。下标越界返回错误码。
int32_t nc_subtitle_get(const NcEditor* editor, int32_t index, int64_t* start_us,
                        int64_t* duration_us, char** out_text);

// ---- 转场 ----

/// 在轨道上添加转场（type 枚举值同 core::TransitionType）。
/// from/to 为片段 ID。返回 >0（TransitionId）或 0。
uint64_t nc_add_transition(NcEditor* editor, int32_t track_index, int32_t transition_type,
                           int64_t duration_us, uint64_t from_clip_id, uint64_t to_clip_id);

/// 删除转场。
int32_t nc_remove_transition(NcEditor* editor, int32_t track_index, uint64_t transition_id);

/// 轨道上的转场数量。
int32_t nc_transition_count(const NcEditor* editor, int32_t track_index);

/// 读取第 index 个转场描述：类型/时长/两端片段。
/// out_name 需 nc_free_string（可为 NULL）。
int32_t nc_transition_get(const NcEditor* editor, int32_t track_index, int32_t index,
                          int32_t* out_type, int64_t* out_duration_us, uint64_t* out_from_clip,
                          uint64_t* out_to_clip, char** out_name);

// ---- 片段查询扩展 ----

/// 读取轨道上第 index 个片段的时间信息（微秒）。
int32_t nc_clip_get(const NcEditor* editor, int32_t track_index, int32_t clip_index,
                    int64_t* out_start_us, int64_t* out_duration_us, uint64_t* out_asset_id);

// ---- 特效 ----

/// 给片段添加特效（type/value 见 core::EffectType / effectSpec 范围）。
/// 返回 >0（EffectId）或 0。
uint64_t nc_add_effect(NcEditor* editor, int32_t track_index, uint64_t clip_id, int32_t type,
                       double value);

/// 删除片段特效。
int32_t nc_remove_effect(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                         uint64_t effect_id);

/// 读取片段特效数量。
int32_t nc_effect_count(const NcEditor* editor, int32_t track_index, uint64_t clip_id);

/// 读取第 index 个特效（类型/数值/启用状态）。
int32_t nc_effect_get(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                      int32_t index, int32_t* out_type, double* out_value,
                      int32_t* out_enabled);

// ---- 关键帧 ----

/// 插入关键帧（property 见 ClipProperty 枚举值：0=Opacity..5=Volume）。
int32_t nc_insert_keyframe(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t property, int64_t time_us, double value);

/// 删除关键帧。
int32_t nc_remove_keyframe(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t property, int64_t time_us);

// ---- 音频属性 ----

/// 设置片段音频（增益 dB / 淡入微秒 / 淡出微秒）。
int32_t nc_set_clip_audio(NcEditor* editor, int32_t track_index, uint64_t clip_id, double gain_db,
                          int64_t fade_in_us, int64_t fade_out_us);

// ---- 内存 ----

/// 释放库返回的字符串。
void nc_free_string(char* text);

#ifdef __cplusplus
}  // extern "C"
#endif
