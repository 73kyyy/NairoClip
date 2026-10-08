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

// ---- 自动剪辑（参考风格生成）----

/// 在空轨道上执行自动剪辑。
/// - profile_shots：镜头边界数组（4 个 double 一组：[start_s, end_s, motion, brightness]）；
/// - shot_count：边界数量；
/// - avg_r/g/b/saturation/contrast：色调画像参数（0..1）；
/// - asset_ids：用户素材 ID 数组；usable_seconds：对应可用时长（double 秒）；
/// - material_count：素材数量；
/// - apply_color：是否应用参考色调。
/// 返回：成功返回生成的片段数量（>=0 且 *out_error 为 NULL），失败返回 -1 并
/// 通过 out_error（需 nc_free_string）给出原因。
int32_t nc_auto_edit(NcEditor* editor, int32_t track_index, const double* profile_shots,
                     int32_t shot_count, double avg_r, double avg_g, double avg_b,
                     double avg_saturation, double avg_contrast, const uint64_t* asset_ids,
                     const double* usable_seconds, int32_t material_count, int32_t apply_color,
                     char** out_error);

// ---- 导出（桌面 FFmpeg 路径；Android MediaCodec 走平台适配层）----

/// 导出主序列到文件。
/// 参数：
///  - out_path：输出路径（UTF-8）；
///  - crf：H.264 质量（0-51；23 默认）；
///  - include_audio：是否包含音频（0/1）；
///  - attach_ai_label：是否附加 AI 内容标识（合规；0/1）；
///  - ai_label_text：标识文案（可空 =「AI 生成」）；
///  - progress_cb：进度回调（0..1，返回 0 取消）；可为 NULL；
///  - out_error：错误信息（需 nc_free_string）；可为 NULL。
/// 返回：成功 = 输出帧数（>=0）；失败 = -1。
int64_t nc_export_run(NcEditor* editor, const char* out_path, int32_t crf, int32_t include_audio,
                      int32_t attach_ai_label, const char* ai_label_text,
                      int32_t (*progress_cb)(double), char** out_error);

// ---- 画幅适配（导出预设 9:16 / 16:9 / 1:1、智能重构图横转竖）----

/// 设置主序列的画幅适配模式（0=fit 等比内缩留黑边 1=fill 铺满居中裁切 2=crop 居中1:1裁切）。
/// 非法值返回 NC_ERROR_INVALID_ARGUMENT。影响后续导出与预览。
int32_t nc_set_frame_fit(NcEditor* editor, int32_t mode);

/// 读取主序列的画幅适配模式（返回 0/1/2；句柄为空返回 -1）。
int32_t nc_get_frame_fit(const NcEditor* editor);

/// 设置画幅适配的背景填充色（Fit 模式的信箱/邮筒边颜色；0xRRGGBBAA）。
int32_t nc_set_background_color(NcEditor* editor, uint32_t rgba);

// ---- 音频自动闪避（文档「🎵音频 → 自动闪避」）----

/// 设置轨道自动闪避（轨道下标；enabled 0/1；sensitivity 0..1；
/// duck_amount_db 0..24；attack/release/hold 单位微秒）。
/// 仅音频轨有效；参数非法或非音频轨返回错误码，失败不入撤销栈。
int32_t nc_set_track_auto_duck(NcEditor* editor, int32_t track_index, int32_t enabled,
                               double sensitivity, double duck_amount_db,
                               int64_t attack_us, int64_t release_us, int64_t hold_us);

/// 读取轨道自动闪避设置（出参可空；返回 NC_OK / 错误码）。
int32_t nc_get_track_auto_duck(const NcEditor* editor, int32_t track_index, int32_t* out_enabled,
                               double* out_sensitivity, double* out_duck_amount_db);

// ---- 复合片段 / 嵌套序列（文档第 3 部分「嵌套序列/复合片段」）----

/// 创建复合片段：把轨道上连续的片段打组为嵌套序列。
/// - track_index：目标轨道（主序列内）；
/// - clip_ids：片段 ID 数组（count 个；必须连续且同一轨道）；
/// - name：复合片段名称（UTF-8；可空 = 默认名）；
/// - out_sequence_id：出参，嵌套序列 ID（可空）；out_clip_id：出参，复合片段 ID（可空）。
/// 返回 NC_OK 或错误码（失败不入撤销栈、无副作用）。
int32_t nc_create_compound_clip(NcEditor* editor, int32_t track_index, const uint64_t* clip_ids,
                                int32_t count, const char* name, uint64_t* out_sequence_id,
                                uint64_t* out_clip_id);

/// 解除复合片段：把嵌套序列内容还原到原位置并删除嵌套序列。
int32_t nc_unwrap_compound_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id);

/// 查询片段是否为复合片段（返回 1/0；-1 = 参数错误）。
int32_t nc_clip_is_compound(const NcEditor* editor, int32_t track_index, uint64_t clip_id);

// ---- 混音台（文档第 3 部分「专业混音台视图」）----

/// 设置轨道独奏（任一轨道 solo 时只有 solo 的音频轨参与混音）。
int32_t nc_set_track_solo(NcEditor* editor, int32_t track_index, int32_t solo);

/// 设置轨道增益（dB；-60..+12；混音台推子/数值输入）。
int32_t nc_set_track_gain(NcEditor* editor, int32_t track_index, double gain_db);

/// 读取轨道混音参数（出参可空）。
int32_t nc_get_track_mixer(const NcEditor* editor, int32_t track_index, int32_t* out_muted,
                           int32_t* out_solo, double* out_gain_db);

/// 设置主输出增益（dB；-60..+12；混音台「主输出」）。
int32_t nc_set_master_gain(NcEditor* editor, double gain_db);

/// 读取主输出增益。
int32_t nc_get_master_gain(const NcEditor* editor, double* out_gain_db);

// ---- 会员权益（本地门控；tier 由平台层在服务端校验后更新）----

/// 设置会员权益（平台层在服务端心跳校验后调用；默认 Free = fail-closed）。
/// - tier：0 = Free，1 = Pro（其他值返回 NC_ERROR_INVALID_ARGUMENT）；
/// - lifetime：是否永久买断（0/1）；
/// - expires_at_unix：到期时间（Unix 秒；0 = 无到期信息）。
int32_t nc_set_entitlement(NcEditor* editor, int32_t tier, int32_t lifetime,
                           int64_t expires_at_unix);

/// 读取当前会员层级（0 = Free，1 = Pro；-1 = 句柄为空）。
int32_t nc_get_entitlement(const NcEditor* editor);

/// 查询能力：当前轨道数下是否还能新增轨道（UI 置灰依据；1/0；-1 = 句柄为空）。
/// 按当前会员层级判定（免费版上限 4 条）。
int32_t nc_can_add_track(const NcEditor* editor);

/// 查询能力：给定输出尺寸在当前权益下是否会被降级（1 = 会降级，0 = 不降级，-1 = 错误）。
/// 供导出页在用户选择分辨率时提前提示（文档 3.3 付费墙触发条件）。
int32_t nc_would_clamp_export(const NcEditor* editor, int32_t width, int32_t height);

// ---- AI 检查（文档第 7 部分 4.5：字幕重叠/音频峰值 → 跳转查看 + 全部自动修复）----

/// 执行 AI 检查（只读；不修改工程）。
/// - 返回问题数量（>= 0）；失败返回 -1 并写入 out_error（需 nc_free_string）；
/// - out_report_json：出参（可空）问题清单 JSON（供 UI 列表渲染；字段：
///   type / severity / time_us / entity_id / track_id / message / auto_fixable /
///   measured_value / summary）；需 nc_free_string 释放。
/// 说明：音频类检查需要真实解码器（此构建含 FFmpeg 时自动使用）。
int32_t nc_ai_check(NcEditor* editor, char** out_report_json, char** out_error);

/// 自动修复全部可修复问题（经编辑器命令，可撤销）。
/// 返回修复成功数量（>= 0）；失败返回 -1 并写入 out_error（需 nc_free_string）。
int32_t nc_ai_check_fix_all(NcEditor* editor, char** out_error);

// ---- 代理剪辑（文档第 3 部分「三、代理剪辑（Proxy Editing）」）----

/// 导入检查：判断素材是否需要生成代理（供导入弹窗与自动生成决策）。
/// - asset_id：素材 ID；
/// - out_proxy_width / out_proxy_height：出参（可空）建议代理尺寸；
/// - out_estimated_seconds / out_estimated_bytes：出参（可空）估算耗时/体积；
/// - out_reason：出参（可空）说明文案（需 nc_free_string）；
/// 返回：1 = 建议生成（自动模式下直接生成），0 = 无需生成，-1 = 错误（见 out_error）。
int32_t nc_proxy_check_import(NcEditor* editor, uint64_t asset_id, int32_t* out_proxy_width,
                              int32_t* out_proxy_height, double* out_estimated_seconds,
                              int64_t* out_estimated_bytes, char** out_reason,
                              char** out_error);

/// 写入/更新代理记录（平台层生成完成或失败后调用；线程约束同其他写接口）。
/// - path：代理文件路径（空 = 清除路径）；status：0=none 1=generating 2=ready 3=failed；
/// - width/height/size_bytes：代理实际参数（0 = 未知）；
/// - note：备注（可空）。
int32_t nc_proxy_set_record(NcEditor* editor, uint64_t asset_id, const char* path,
                            int32_t status, int32_t width, int32_t height,
                            int64_t size_bytes, const char* note);

/// 读取代理记录（多个出参可空）。
/// 返回：1 = 有记录（*out_status 有效），0 = 无记录，-1 = 错误。
int32_t nc_proxy_get_record(const NcEditor* editor, uint64_t asset_id, int32_t* out_status,
                            int32_t* out_width, int32_t* out_height, int64_t* out_size_bytes,
                            char** out_path, char** out_note);

/// 代理占用统计（设置页「代理文件占用: 1.2GB [清理已导出项目的代理]」）。
/// - out_ready_count / out_generating_count：出参（可空）就绪/生成中数量；
/// - out_total_bytes：出参（可空）就绪代理占用总量。
int32_t nc_proxy_usage(const NcEditor* editor, int32_t* out_ready_count,
                       int32_t* out_generating_count, int64_t* out_total_bytes);

/// 清除代理记录（供「清理代理」；返回被清除的记录数，-1 = 错误）。
/// 说明：core 不触碰文件系统，平台层据记录列表删除文件。
int32_t nc_proxy_clear_records(NcEditor* editor);

// ---- 导出预设 / 资源预估 / 性能模式（文档 6.1「导出页 & 快速导出预设库」）----

/// 列举内置预设（供 UI 快速预设列表）。
/// - out_ids：出参（可空）预设 ID 数组；capacity：数组容量；
/// - out_count：出参（可空）实际数量（可大于 capacity，用于查询总数）。
/// 返回 NC_OK。
int32_t nc_export_presets(int32_t* out_ids, int32_t capacity, int32_t* out_count);

/// 读取预设参数（供 UI 展示标签/尺寸）。
/// - preset_id：预设 ID（0..5；非法返回 NC_ERROR_INVALID_ARGUMENT）；
/// - 出参均可空：out_width/out_height/out_fps/out_requires_pro/out_label（需 nc_free_string）。
int32_t nc_export_preset_info(int32_t preset_id, int32_t* out_width, int32_t* out_height,
                              int32_t* out_fps, int32_t* out_requires_pro, char** out_label);

/// 资源预估（文档「📊 资源预估：生成文件大小 / 本地渲染耗时」）。
/// - width/height/fps/duration_us：输出规格与时长；
/// - video_bitrate_mbps：视频码率（<= 0 = 按分辨率推导）；
/// - include_audio：是否含音频（0/1）；
/// - 出参均可空：out_file_size_bytes / out_render_seconds / out_size_text / out_time_text
///   （两个文本出参需 nc_free_string）。
int32_t nc_export_estimate(int32_t width, int32_t height, int32_t fps, int64_t duration_us,
                           double video_bitrate_mbps, int32_t include_audio,
                           int64_t* out_file_size_bytes, double* out_render_seconds,
                           char** out_size_text, char** out_time_text);

/// 组装预设导出配置（含 4K 权益门控与性能模式）。
/// - preset_id：预设 ID；mode：性能模式（0=极速 1=平衡 2=省电）；
/// - cpu_cores：设备核心数（0 = 未知）；
/// - 出参均可空：out_width/out_height/out_fps/out_downgraded/out_crf/out_threads/
///   out_preset_name（编码预置名，需 nc_free_string）；
/// - out_warning：出参（可空）降级提示（需 nc_free_string；无降级时为空串）。
int32_t nc_export_setup_preset(NcEditor* editor, int32_t preset_id, int32_t mode,
                               int32_t cpu_cores, int32_t crf, int32_t* out_width,
                               int32_t* out_height, int32_t* out_fps, int32_t* out_downgraded,
                               char** out_preset_name, char** out_warning);

// ---- 内存 ----

/// 释放库返回的字符串。
void nc_free_string(char* text);

#ifdef __cplusplus
}  // extern "C"
#endif
