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
    NC_ERROR_CANCELLED = 8,
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

// ---- 工程信息（文档 Part 5「草稿数据结构」）----

/// 读取工程名称（需 nc_free_string；NULL = 失败）。
char* nc_project_name(const NcEditor* editor);
/// 设置工程名称（草稿重命名；返回错误码）。
int32_t nc_set_project_name(NcEditor* editor, const char* name);

/// 工程画布尺寸（像素；主序列）。
int32_t nc_project_canvas(const NcEditor* editor, int32_t* out_width, int32_t* out_height);
/// 设置工程画布尺寸（文档 Part 3-C「比例」；宽高必须为正且 <= 16384）。
int32_t nc_set_project_canvas(NcEditor* editor, int32_t width, int32_t height);
/// 读取工程帧率（分子/分母；如 30/1）。
int32_t nc_project_frame_rate(const NcEditor* editor, int64_t* out_num, int64_t* out_den);
/// 设置工程帧率（分子/分母均须为正且 <= 1000）。
int32_t nc_set_project_frame_rate(NcEditor* editor, int64_t num, int64_t den);

// ---- 历史（撤销/重做；文档 Part 5(续) 第32节）----

/// 是否可撤销 / 可重做（1/0）。
int32_t nc_can_undo(const NcEditor* editor);
int32_t nc_can_redo(const NcEditor* editor);
/// 可撤销步数 / 可重做步数。
int32_t nc_undo_count(const NcEditor* editor);
int32_t nc_redo_count(const NcEditor* editor);
/// 读取第 index 条历史记录名称（按时间顺序；需 nc_free_string；越界 = NULL）。
/// 供 UI 历史面板展示（如「分割片段」「删除片段」）。
char* nc_undo_history_name(const NcEditor* editor, int32_t index);
/// 清空撤销历史（退出剪辑页重新进入时调用；文档第 32 节「栈清空时机」）。
int32_t nc_clear_undo_history(NcEditor* editor);
/// 设置撤销栈最大深度（文档第 32 节要求 20 步；0 = 不限制；默认 0 不限制）。
/// 说明：超出深度时最早记录被移除（不再可撤销）。
int32_t nc_set_undo_depth_limit(NcEditor* editor, int32_t max_steps);

// ---- 素材管理（文档 Part 5「轨道数据」/ Part 6 素材丢失）----

/// 素材数量。
int32_t nc_asset_count(const NcEditor* editor);
/// 读取第 index 个素材：ID / 名称 / 路径 / 类型 / 时长。
/// - out_name/out_path 需 nc_free_string（可 NULL）；
/// - out_type：0=video 1=audio 2=image 3=unknown；
/// 返回 NC_OK 或错误码。
int32_t nc_asset_get(const NcEditor* editor, int32_t index, uint64_t* out_asset_id,
                     char** out_name, char** out_path, int32_t* out_type,
                     int64_t* out_duration_us);
/// 删除素材（若被片段引用则失败，错误码 NC_ERROR_LOCKED）。
int32_t nc_remove_asset(NcEditor* editor, uint64_t asset_id);
/// 查询素材是否被任意片段引用（1 = 已引用；0 = 未引用；-1 = 参数错误）。
int32_t nc_asset_is_referenced(const NcEditor* editor, uint64_t asset_id);

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

// ---- 轨道管理（文档 Part 2-B 第8节「轨道控制列」+ 第28节「新增轨道」）----

/// 读取轨道信息：ID / 名称 / 类型 / 标志位 / 轨道增益。
/// - out_type：0=video 1=audio 2=subtitle 3=effect；
/// - out_locked/out_muted/out_hidden/out_solo：1/0；
/// - out_name 需 nc_free_string（可 NULL）。
int32_t nc_track_get(const NcEditor* editor, int32_t track_index, uint64_t* out_track_id,
                     char** out_name, int32_t* out_type, int32_t* out_locked,
                     int32_t* out_muted, int32_t* out_hidden, int32_t* out_solo,
                     double* out_gain_db);

/// 删除轨道（含其中片段；可撤销）。
int32_t nc_remove_track(NcEditor* editor, int32_t track_index);
/// 重命名轨道。
int32_t nc_rename_track(NcEditor* editor, int32_t track_index, const char* new_name);
/// 设置轨道锁定 / 静音 / 隐藏（1/0）。
int32_t nc_set_track_locked(NcEditor* editor, int32_t track_index, int32_t locked);
int32_t nc_set_track_muted(NcEditor* editor, int32_t track_index, int32_t muted);
int32_t nc_set_track_hidden(NcEditor* editor, int32_t track_index, int32_t hidden);

// ---- 片段编辑（文档 Part 3-A 第12节「剪辑」基础操作）----

/// 删除片段（可撤销；文档「剪辑 → 删除」）。
int32_t nc_remove_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id);
/// 分割片段（以 at_us 为界切割为前后两段；文档「剪辑 → 分割」）。
int32_t nc_split_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id, int64_t at_us);
/// 移动片段到新起点（同轨道；文档「剪辑」拖动）。
int32_t nc_move_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                     int64_t new_start_us);
/// 跨轨移动片段（文档「剪辑」跨轨拖动）。
int32_t nc_move_clip_to_track(NcEditor* editor, int32_t from_track, int32_t to_track,
                              uint64_t clip_id, int64_t new_start_us);
/// 裁剪片段起点 / 终点（文档「剪辑」拖动片段边缘）。
int32_t nc_trim_clip_start(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int64_t new_start_us);
int32_t nc_trim_clip_end(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                         int64_t new_end_us);
/// 设置片段速度（分子/分母；文档「剪辑 → 变速 → 常规」0.1x~100x）。
int32_t nc_set_clip_speed(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                          int64_t speed_num, int64_t speed_den);
/// 波纹删除（删除片段并把后续片段前移；文档「剪辑」）。
int32_t nc_ripple_delete(NcEditor* editor, int32_t track_index, const uint64_t* clip_ids,
                         int32_t clip_count);

/// 复制片段（在原片段紧随其后插入副本；返回新片段 ID，0 = 失败）。
/// 文档「剪辑 → 复制」。
uint64_t nc_duplicate_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id);
/// 在播放头位置插入定格帧片段（默认时长 default_duration_us；文档「剪辑 → 定格」）。
/// 说明：从源素材 at_us 处取一帧作为静止画面（duration 由调用方指定，默认 2s）。
uint64_t nc_freeze_frame(NcEditor* editor, int32_t track_index, uint64_t clip_id, int64_t at_us,
                         int64_t duration_us);
/// 倒放片段（画面顺序反转；文档「剪辑 → 倒放」）。
/// 说明：等价于 speed = -1 语义的时长等价实现（保持时间轴时长不变）。
int32_t nc_reverse_clip(NcEditor* editor, int32_t track_index, uint64_t clip_id);

/// 读取片段完整信息（时间/素材/速度/启用状态）。
/// - out_speed_num/out_speed_den：播放速度（如 2/1）；
/// 返回 NC_OK 或错误码。
int32_t nc_clip_info(const NcEditor* editor, int32_t track_index, uint32_t clip_index,
                     uint64_t* out_clip_id, int64_t* out_start_us, int64_t* out_duration_us,
                     uint64_t* out_asset_id, int64_t* out_speed_num, int64_t* out_speed_den,
                     int32_t* out_enabled);
/// 设置片段启用/禁用（1/0；文档「剪辑」片段开关）。
int32_t nc_set_clip_enabled(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t enabled);
/// 读取片段透明度（0~1；由动画通道 Opacity 的常量值决定；无关键帧 = 静态值）。
int32_t nc_get_clip_opacity(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            double* out_opacity);
/// 设置片段透明度（0~1；文档「剪辑 → 不透明度」）。
int32_t nc_set_clip_opacity(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            double opacity);

// ---- 滤镜（文档 Part 4-B 第 7 节「滤镜」；14 种风格）----

/// 设置片段滤镜（style 见 FilterStyle：0=none 1=movie 2=retro 3=fresh 4=black_white
/// 5=cyber 6=japanese 7=film 8=warm_sun 9=cool_tone 10=hong_kong 11=high_gray
/// 12=cream 13=negative；intensity 0..100）。
/// 返回 NC_OK 或错误码（非法 style/强度）。
int32_t nc_set_clip_filter(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t style, double intensity);
/// 读取片段滤镜（out_style/out_intensity 可空）。
/// 返回：1 = 已启用，0 = 未启用，-1 = 参数错误。
int32_t nc_get_clip_filter(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                           int32_t* out_style, double* out_intensity);
/// 滤镜展示名（style 0..13；返回静态字符串，无需释放；非法返回 "无"）。
const char* nc_filter_display_name(int32_t style);

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

// ---- 转场库（文档 Part 4-B 第 6 节「转场」9 分类）----

/// 转场类型总数（34 个：既有 8 + 文档新增 26）。
int32_t nc_transition_type_count(void);
/// 转场类型名称（type 0..33；静态字符串；非法返回 "未知转场"）。
const char* nc_transition_type_name(int32_t type);
/// 转场分类名（文档 6.1 九分类："基础"/"叠化"/"运镜"/"扭曲"/"故障"/
/// "光效"/"几何"/"粒子"/"复古"；静态字符串；非法返回空串）。
const char* nc_transition_category(int32_t type);
/// 是否支持方向选择器（文档 6.3：仅推拉/擦除/百叶窗类显示；1/0）。
int32_t nc_transition_supports_direction(int32_t type);

// ---- 片段查询扩展 ----

/// 读取轨道上第 index 个片段的时间信息（微秒）。
int32_t nc_clip_get(const NcEditor* editor, int32_t track_index, int32_t clip_index,
                    int64_t* out_start_us, int64_t* out_duration_us, uint64_t* out_asset_id);

// ---- 预览渲染（文档 Part 2-A「预览区」；UI 逐帧取画面）----
// 说明：预览为「按需单帧」路径（与导出顺序全帧不同）；渲染器内部缓存解码器，
//       反复调用只解码目标帧。缓存随工程结构变化自动失效（载入/编辑后重建）。

/// 渲染指定时间轴时刻的一帧（RGBA8）。
/// - time_us：时间轴时刻（微秒）；
/// - out_width/out_height：画布尺寸（可空）；
/// - out_rgba：出参（库分配；需 nc_free_buffer 释放；容量 = 宽 × 高 × 4）；
/// - out_clip_id：出参（可空）该时刻活动片段 ID（0 = 无片段）；
/// - out_opacity/out_scale/out_pos_x/out_pos_y/out_rotation：出参（可空）动画快照；
/// - out_transition_type/out_transition_progress：出参（可空）转场信息（-1 = 无）；
/// 返回 NC_OK（画面可能为纯背景色 = 无素材时刻）。
int32_t nc_preview_render(NcEditor* editor, int64_t time_us, int32_t* out_width,
                          int32_t* out_height, uint8_t** out_rgba, uint64_t* out_clip_id,
                          double* out_opacity, double* out_scale, double* out_pos_x,
                          double* out_pos_y, double* out_rotation,
                          int32_t* out_transition_type, double* out_transition_progress);

/// 渲染指定时刻的字幕文本列表（扁平序列化：以 '\n' 分隔；需 nc_free_string；
/// 无字幕返回空字符串；NULL 表示参数错误）。
char* nc_preview_subtitles(const NcEditor* editor, int64_t time_us);

/// 使预览缓存失效（工程结构被直接修改后调用；编辑操作/载入会自动失效，
/// 一般不必手动调用）。
void nc_preview_invalidate(NcEditor* editor);

// ---- 缩略图（文档 Part 2-B「时间轴胶片条」/ 素材库预览）----

/// 为素材生成缩略图序列（真实解码）。
/// - asset_id：素材 ID；
/// - count：目标数量（1..64；<=0 用 8）；
/// - max_width/max_height：单张最大尺寸（<=0 用 160x90）；
/// - out_images：出参（库分配）RGBA8 图像数组指针（每个元素需 nc_free_buffer）；
///   容量 = count；
/// - out_times_us：出参（可空）每张缩略图对应的源时间（微秒；数组长度 = 返回数）；
/// - out_count：出参（可空）实际生成数量；
/// 返回 NC_OK 或错误码（素材不存在/无法解码）。
int32_t nc_thumbnail_generate(NcEditor* editor, uint64_t asset_id, int32_t count,
                              int32_t max_width, int32_t max_height,
                              uint8_t** out_images, int64_t* out_times_us,
                              int32_t* out_count);

/// 查询缩略图尺寸（生成后每张输出尺寸一致；未生成返回 0）。
/// 返回 NC_OK 或错误码。
int32_t nc_thumbnail_size(const NcEditor* editor, int32_t* out_width, int32_t* out_height);

// ---- 音频波形（文档 Part 2-B「音频轨波形」）----

/// 为素材生成音频波形（真实解码 + 桶化 min/max/RMS）。
/// - asset_id：素材 ID；
/// - bucket_count：目标桶数（1..100000；<=0 用 512）；
/// - out_peak：出参（可空）全波形峰值（0..1）；
/// - out_sample_rate/out_channels：出参（可空）源格式；
/// 返回 NC_OK 或错误码；波形数据经 nc_waveform_bucket_get 逐桶读取。
int32_t nc_waveform_generate(NcEditor* editor, uint64_t asset_id, int32_t bucket_count,
                             double* out_peak, int32_t* out_sample_rate,
                             int32_t* out_channels);
/// 读取上次生成的波形桶数（未生成返回 0）。
int32_t nc_waveform_bucket_count(const NcEditor* editor);
/// 读取单个桶数据（min/max/rms；越界返回 NC_ERROR_OUT_OF_RANGE）。
int32_t nc_waveform_bucket_get(const NcEditor* editor, int32_t index, double* out_min,
                               double* out_max, double* out_rms);

// ---- 场景检测（文档「剪辑节奏」/ 章节自动生成的前置分析）----

/// 对工程主序列执行镜头边界检测（真实解码采样）。
/// - step_seconds：采样步长（<=0 = 自适应 0.25~1s）；
/// - max_samples：采样上限（<=0 = 默认 600）；
/// - out_boundary_count：出参（可空）检测到的镜头数；
/// - out_boundaries：出参（可空）边界时间数组（库分配；需 nc_free_buffer；
///   每 2 个 double 一组：[start_s, end_s]）；
/// 返回 NC_OK 或错误码。
int32_t nc_scene_detect(NcEditor* editor, double step_seconds, int32_t max_samples,
                        int32_t* out_boundary_count, double** out_boundaries);

// ---- 节拍检测（文档「音乐卡点」/ 动感特效节拍数据）----

/// 对指定音频轨执行节拍检测（真实解码 + 精细周期扫描）。
/// - track_index：音频轨下标（<0 = 首个音频轨）；
/// - out_bpm：出参（可空）检测到的 BPM；
/// - out_confidence：出参（可空）置信度 0..1；
/// - out_beat_count：出参（可空）节拍数量；
/// - out_beats：出参（可空）节拍时间数组（秒；库分配；需 nc_free_buffer）；
/// 返回 NC_OK 或错误码。
int32_t nc_beat_detect(NcEditor* editor, int32_t track_index, double* out_bpm,
                       double* out_confidence, int32_t* out_beat_count, double** out_beats);

// ---- 速度曲线（文档「剪辑 → 变速 → 曲线」）----

/// 设置片段速度曲线的控制点（整组替换；sourceTimes 为片段本地时间秒）。
/// - speeds：对应速度（0.1..10）；
/// - easings：缓动类型（0 线性 / 1 缓入 / 2 缓出 / 3 缓入缓出 / 4 阶梯）；
/// - point_count：控制点数量（0 = 清空曲线恢复恒速）；
/// 返回 NC_OK 或错误码。
int32_t nc_set_clip_speed_curve(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                                const double* source_times, const double* speeds,
                                const int32_t* easings, int32_t point_count);
/// 读取片段速度曲线控制点数量（无曲线返回 0；错误返回 -1）。
int32_t nc_clip_speed_curve_count(const NcEditor* editor, int32_t track_index,
                                  uint64_t clip_id);
/// 读取第 index 个控制点（源时间秒/速度/缓动；越界返回错误码）。
int32_t nc_clip_speed_curve_get(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                                int32_t index, double* out_source_seconds, double* out_speed,
                                int32_t* out_easing);

// ---- 变速曲线预设（文档 Part 4-A 1.1「曲线预设」）----

/// 预设名称（preset 0..6；静态字符串；非法返回 "custom"）。
const char* nc_speed_curve_preset_name(int32_t preset);
/// 预设中文展示名（文档逐字："蒙太奇"/"英雄时刻"/...；非法返回 "自定义"）。
const char* nc_speed_curve_preset_display_name(int32_t preset);

/// 应用速度曲线预设到片段（source_duration_us = 片段源区间长度；<=0 用片段实际）。
/// - preset：0=自定义(清空曲线恢复恒速) 1=蒙太奇 2=英雄时刻 3=子弹时间
///           4=跳跃 5=闪进 6=闪出；
/// 返回 NC_OK 或错误码。
int32_t nc_apply_speed_curve_preset(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                                    int32_t preset, int64_t source_duration_us);

// ---- 镜头追踪（文档 Part 4-A 3.3「镜头追踪」）----
// 语义（文档 3.3 边界）：追踪结果生成**关键帧**并绑定到「文字对象」；
// 本工程文字对象 = 字幕条目（SubtitleCue），其位置由锚点 (anchorX, anchorY)
// 表达。追踪即：把每帧的追踪位置写入目标字幕的位置关键帧序列。
// 实现分层：追踪源数据（帧显著性/轨迹）由 reframe 模块产出（平台层解码后
// 传入采样点）；本组接口负责「采样点 → 字幕位置」的落盘与回读。

/// 设置镜头追踪结果（按时间点序列写入字幕位置变更）。
/// - subtitle_id：目标文字（字幕）ID；
/// - times_us/center_x/center_y：追踪采样点（时间微秒 + 归一化坐标 0..1）；
/// - point_count：采样点数（1..4096）；
/// - apply_mode：0=位置 1=位置+缩放（缩放写为锚点附近的固定值，语义占位）；
/// 实现：把采样点按线性插值写入字幕的 anchorX/anchorY 序列（等价于关键帧），
///       数据经工程序列化持久化；UI 可逐帧读取（nc_tracking_point_get）。
/// 返回 NC_OK 或错误码。
int32_t nc_set_camera_tracking(NcEditor* editor, uint64_t subtitle_id, const double* times_us,
                               const double* center_x, const double* center_y,
                               int32_t point_count, int32_t apply_mode);

/// 设置镜头追踪结果（含缩放；文档 3.3「追踪类型：位置+缩放」）。
/// - scales：每点的缩放倍数（1.0 = 原始；可空 = 全部 1.0）；
/// 其余参数同 nc_set_camera_tracking。apply_mode 为 1 时 scale 才参与渲染。
int32_t nc_set_camera_tracking_scaled(NcEditor* editor, uint64_t subtitle_id,
                                      const double* times_us, const double* center_x,
                                      const double* center_y, const double* scales,
                                      int32_t point_count, int32_t apply_mode);
/// 读取追踪采样点数（未设置返回 0；错误返回 -1）。
int32_t nc_tracking_point_count(const NcEditor* editor, uint64_t subtitle_id);
/// 读取第 index 个追踪采样点（时间微秒/归一化坐标）。
int32_t nc_tracking_point_get(const NcEditor* editor, uint64_t subtitle_id, int32_t index,
                              double* out_time_us, double* out_center_x, double* out_center_y);

/// 读取第 index 个追踪采样点（含缩放）。
int32_t nc_tracking_point_get_scaled(const NcEditor* editor, uint64_t subtitle_id, int32_t index,
                                     double* out_time_us, double* out_center_x,
                                     double* out_center_y, double* out_scale);
/// 清除追踪结果（恢复为无追踪状态；可撤销）。
int32_t nc_clear_camera_tracking(NcEditor* editor, uint64_t subtitle_id);

// ---- 深度图效果（文档 Part 4-C 13.2「深度图」4 种应用效果）----

/// 深度图应用效果模式（文档 13.2）。
enum NcDepthEffect {
    NC_DEPTH_EFFECT_NONE = 0,        ///< 未设置
    NC_DEPTH_EFFECT_BOKEH = 1,       ///< 景深虚化
    NC_DEPTH_EFFECT_FOG = 2,         ///< 雾化
    NC_DEPTH_EFFECT_COLOR_BANDS = 3, ///< 色彩分层
    NC_DEPTH_EFFECT_PARALLAX = 4,    ///< 视差3D
};

/// 设置片段深度图效果（文档 13.2 四 Tab；互斥）。
/// - mode：0=none 1=bokeh 2=fog 3=color_bands 4=parallax；
/// - focus_distance：景深对焦距离 0..1（点击画布赋值；仅 bokeh）；
/// - aperture：光圈 1.0..22.0（仅 bokeh）；
/// - intensity：主强度 0..100（虚化/雾浓度/过渡柔和/视差强度）；
/// - color_rgba：雾颜色/分层近景色（0xRRGGBBAA；仅 fog/color_bands）；
/// 返回 NC_OK 或错误码（深度图生成完成前由平台层拦截——AI 任务状态机）。
int32_t nc_set_depth_effect(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t mode, double focus_distance, double aperture,
                            double intensity, uint32_t color_rgba);
/// 读取片段深度图效果（出参均可空）。
/// 返回：1 = 已设置，0 = 未设置，-1 = 参数错误。
int32_t nc_get_depth_effect(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t* out_mode, double* out_focus_distance,
                            double* out_aperture, double* out_intensity,
                            uint32_t* out_color_rgba);

// ---- 自定义关键帧转场（文档附录二）----
// 语义：文档附录二要求用户可在转场区间内对 5 个属性（位置X/位置Y/缩放/旋转/
// 不透明度）打关键帧。本工程的关键帧系统（ClipProperty 0..5）已完整支持，
// 本组接口为「转场区间内关键帧」提供便捷读写（把区间相对时间换算为
// 片段本地时间：转场起点在 from 片段尾部）。

/// 在转场区间内添加关键帧（时间相对转场起点；文档附录二交互）。
/// - transition_id：转场 ID；property：0..5（文档附录二的 5 属性映射：
///   位置X=2 位置Y=3 缩放=1 旋转=4 不透明度=0）；
/// - offset_us：相对转场起点的时间偏移（0..转场时长）；
/// - value：数值；easing：0 线性 / 1 缓入 / 2 缓出 / 3 缓入缓出 / 4 阶梯；
/// 实现：换算为 from 片段的本地时间后写入其关键帧通道（可撤销）。
/// 返回 NC_OK 或错误码。
int32_t nc_add_transition_keyframe(NcEditor* editor, int32_t track_index,
                                   uint64_t transition_id, int32_t property, int64_t offset_us,
                                   double value, int32_t easing);
/// 读取转场区间内的关键帧数量（某属性）。
int32_t nc_transition_keyframe_count(const NcEditor* editor, int32_t track_index,
                                     uint64_t transition_id, int32_t property);
/// 读取第 index 个转场区间关键帧（偏移微秒/数值/缓动）。
int32_t nc_transition_keyframe_get(const NcEditor* editor, int32_t track_index,
                                   uint64_t transition_id, int32_t property, int32_t index,
                                   double* out_offset_us, double* out_value,
                                   int32_t* out_easing);
/// 删除转场区间关键帧（偏移微秒）。
int32_t nc_remove_transition_keyframe(NcEditor* editor, int32_t track_index,
                                      uint64_t transition_id, int32_t property,
                                      int64_t offset_us);
/// 转场总时长（微秒；供 UI 绘制迷你时间轴；越界返回 -1）。
int64_t nc_transition_duration_us(const NcEditor* editor, int32_t track_index,
                                  uint64_t transition_id);

// ---- 关键帧查询（文档「自定义关键帧转场」/ 参数动画面板）----

/// 读取片段某通道的关键帧数量（property 0..5）。
int32_t nc_keyframe_count(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                          int32_t property);
/// 读取第 index 个关键帧（时刻微秒/数值/缓动类型 0..4）。
int32_t nc_keyframe_get(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                        int32_t property, int32_t index, int64_t* out_time_us,
                        double* out_value, int32_t* out_easing);

// ---- 背景填充（文档 Part 4-C 第 11 节「背景」）----

/// 背景填充模式（文档 11.1 三 Tab：颜色/模糊/画布样式；三 Tab 互斥）。
enum NcBackgroundMode {
    NC_BACKGROUND_NONE = 0,   ///< 无（默认；留白为序列背景色）
    NC_BACKGROUND_COLOR = 1,  ///< 颜色填充（预设色板 + 自定义取色）
    NC_BACKGROUND_BLUR = 2,   ///< 模糊（素材放大模糊铺满空白）
    NC_BACKGROUND_STYLE = 3,  ///< 画布样式（预制纹理）
};

/// 设置片段背景填充（文档 Part 4-C 11.1：单片段级别；三 Tab 互斥）。
/// - mode：0=none 1=color 2=blur 3=style；
/// - rgba：主参数（color 模式 = 填充色 0xRRGGBBAA；其余保留）；
/// - intensity：mode=blur 时为模糊度 0..100；mode=style 时为样式编号（0..15）；
/// 返回 NC_OK 或错误码。
int32_t nc_set_clip_background(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               int32_t mode, uint32_t rgba, double intensity);
/// 读取片段背景填充（出参均可空）。
/// 返回：1 = 已设置（非 none），0 = 未设置，-1 = 参数错误。
int32_t nc_get_clip_background(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               int32_t* out_mode, uint32_t* out_rgba, double* out_intensity);

// ---- 标记（文档 Part 3 时间轴标记/批注）----
/// 添加/更新标记（同 ID 替换；返回 MarkerId，0 = 失败）。
/// - time_us：标记时间；name：名称（可空 = 默认）；color：颜色字符串（可空 = 默认）。
uint64_t nc_add_marker(NcEditor* editor, int64_t time_us, const char* name, const char* color);
/// 删除标记。
int32_t nc_remove_marker(NcEditor* editor, uint64_t marker_id);
/// 标记数量。
int32_t nc_marker_count(const NcEditor* editor);
/// 读取第 index 个标记（时间/名称/颜色；字符串需 nc_free_string）。
int32_t nc_marker_get(const NcEditor* editor, int32_t index, uint64_t* out_marker_id,
                      int64_t* out_time_us, char** out_name, char** out_color);

// ---- 音画分离（文档「剪辑」音画分离）----

/// 将视频片段的音频分离到目标音频轨（同素材同时间；返回新音频片段 ID，0 = 失败）。
uint64_t nc_detach_audio(NcEditor* editor, int32_t video_track_index, uint64_t video_clip_id,
                         int32_t audio_track_index);

// ---- AI 任务（文档 Part 4 各 AI 功能的统一任务契约 + 第 30.3 节失败提示）----
// 设计：core 提供**任务簿记状态机**（进度/失败原因/重试标记/结果载荷），
//       真实推理由平台层执行并回填进度；core 不伪造任何进度。
// UI 契约：
//   ① 点击联网 AI 功能图标 → nc_ai_feature_access 检查（登录态由平台层传入）；
//   ② 展开面板 → nc_ai_task_begin 开始 → 平台层执行推理并周期调用
//      nc_ai_task_progress 回填进度（UI 显示进度条 + 置灰其他参数）；
//   ③ 完成 → nc_ai_task_succeed（结果载荷 JSON）；失败 → nc_ai_task_fail
//      （原因非空；UI 弹「✕ 处理失败」+ 重试/取消）；
//   ④ 关闭提示 → nc_ai_task_reset。

/// 查询 AI 功能可用性（登录态门控；文档 Part 6 第 41.3 节"点击图标时检测"）。
/// - feature：功能类型（0..10；见 core::AiFeatureType）；
/// - logged_in：平台层传入的登录态（0/1）；
/// - out_requires_network：出参（可空）该功能是否需联网/登录；
/// - out_requires_pro：出参（可空）该功能是否需 Pro（未登录时仍返回网络要求）；
/// - out_reason：出参（可空）提示文案（需 nc_free_string）；
/// 返回：1 = 可用；0 = 不可用（弹登录/付费引导）；-1 = 参数错误。
int32_t nc_ai_feature_access(const NcEditor* editor, int32_t feature, int32_t logged_in,
                             int32_t* out_requires_network, int32_t* out_requires_pro,
                             char** out_reason);

/// 读取当前 AI 任务状态（供 UI 每帧轮询驱动进度条）。
/// - out_feature/out_state：出参（可空）功能与状态（见 AiFeatureType/AiTaskState）；
/// - out_progress：出参（可空）进度 0..1；
/// - out_stage_note/out_failure_reason/out_result_payload：出参（可空；需
///   nc_free_string；失败原因在 Failed 时非空）；
/// - out_retryable：出参（可空）是否可重试（1/0）；
/// 返回 NC_OK（无任务时 state = 0 = idle）。
int32_t nc_ai_task_status(const NcEditor* editor, int32_t* out_feature, int32_t* out_state,
                          double* out_progress, char** out_stage_note,
                          char** out_failure_reason, int32_t* out_retryable,
                          char** out_result_payload);

/// 开始 AI 任务（要求当前无运行中任务；返回 NC_OK 或错误码）。
/// - feature：功能类型；target_id：关联对象 ID（片段/素材；可 0）。
int32_t nc_ai_task_begin(NcEditor* editor, int32_t feature, uint64_t target_id,
                         char** out_error);
/// 回填进度（平台层在推理过程中调用；单调不减；stage_note 可空）。
int32_t nc_ai_task_progress(NcEditor* editor, double progress, const char* stage_note);
/// 标记成功（result_payload 为 JSON 字符串；可空）。
int32_t nc_ai_task_succeed(NcEditor* editor, const char* result_payload);
/// 标记失败（reason **必须**非空——空原因回退为通用文案；retryable：0/1）。
int32_t nc_ai_task_fail(NcEditor* editor, const char* reason, int32_t retryable);
/// 用户取消（UI 静默返回）。
int32_t nc_ai_task_cancel(NcEditor* editor);
/// 复位到空闲（UI 关闭提示后调用）。
int32_t nc_ai_task_reset(NcEditor* editor);
/// 失败文案（统一结构；供 UI 直接展示；需 nc_free_string；无失败返回空串）。
char* nc_ai_task_failure_text(const NcEditor* editor);

/// 查询 AI 引擎与任务框架信息（供「关于」页/诊断）。
/// - out_available：出参（可空）推理引擎是否可用（有 ONNX Runtime；1/0）；
/// - out_version：出参（可空）引擎版本（需 nc_free_string）；
/// - out_note：出参（可空）说明（需 nc_free_string）。
int32_t nc_ai_engine_info(const NcEditor* editor, int32_t* out_available, char** out_version,
                          char** out_note);

// ---- AI 真实推理（深度图 / 人像抠像 / 语音转字幕；文档 Part 4）----
// 设计：core 提供真实推理会话（模型加载/推理/卸载）+ 效果渲染；
//       模型文件路径由平台层提供（经 model_manager 分发）；
//       无模型/无 ORT 时明确失败（不伪造结果）。
// 耗时说明：单帧推理 100ms~3s（CPU）；平台层应在工作线程执行，
//           并经 AI 任务状态机（nc_ai_task_*）驱动进度与取消。

/// 加载深度图模型（Depth-Anything ONNX）。
/// - model_path：模型文件路径；
/// - out_error：出参（可空）失败原因（需 nc_free_string）。
/// 返回 NC_OK 或错误码（引擎不可用/文件缺失/模型解析失败）。
int32_t nc_depth_load(NcEditor* editor, const char* model_path, char** out_error);
/// 卸载深度图模型。
int32_t nc_depth_unload(NcEditor* editor);
/// 深度图模型是否已加载（1/0）。
int32_t nc_depth_loaded(const NcEditor* editor);

/// 生成单帧深度图（真实推理）。
/// - rgba/width/height：输入帧（RGBA8）；
/// - out_depth：出参（库分配；尺寸 = width × height；需 nc_free_buffer；
///   灰度：255 = 最近 / 0 = 最远）；
/// - out_width/out_height：出参（可空）输出尺寸；
/// - out_error：出参（可空）失败原因（需 nc_free_string）。
/// 返回 NC_OK 或错误码（未加载/尺寸非法/推理失败）。
int32_t nc_depth_estimate(NcEditor* editor, const uint8_t* rgba, int32_t width, int32_t height,
                          uint8_t** out_depth, int32_t* out_width, int32_t* out_height,
                          char** out_error);

/// 应用深度效果到帧（真实像素运算；深度图驱动）。
/// - mode：1=景深虚化 2=雾化 3=色彩分层 4=视差3D；
/// - focus_distance：景深对焦距离 0..1（仅 bokeh）；
/// - aperture：光圈 1.0~22.0（仅 bokeh）；
/// - intensity：主强度 0..100；
/// - color_rgba：雾色/近景色调（0xRRGGBBAA）；
/// - rgba/depth：输入帧与深度图（同尺寸）；frame_time：时间秒（视差轨迹动画）；
/// 返回 NC_OK 或错误码。
int32_t nc_depth_apply_effect(uint8_t* rgba, const uint8_t* depth, int32_t width, int32_t height,
                              int32_t mode, double focus_distance, double aperture,
                              double intensity, uint32_t color_rgba, double frame_time);

// =============================================================================
// 超分辨率（真实推理；模型见 docs/COMPLIANCE.md §2.1 锁定清单）
// =============================================================================

/// 加载超分模型（.onnx）。
/// 返回 NC_OK 或错误码（引擎不可用/文件缺失/模型解析失败）。
int32_t nc_superres_load(NcEditor* editor, const char* model_path, char** out_error);
/// 卸载超分模型。
int32_t nc_superres_unload(NcEditor* editor);
/// 超分模型是否已加载（1/0）。
int32_t nc_superres_loaded(const NcEditor* editor);

/// 放大单帧（真实推理；分块重叠 + 羽化拼接；块内失败回退双线性）。
/// - scale：2 或 4；
/// - out_rgba：出参（库分配；尺寸 = (width*scale) × (height*scale) × 4；
///   需 nc_free_buffer）；
/// - out_width/out_height：出参（可空）输出尺寸；
/// - out_tiles/out_fallbacks：出参（可空）分块总数 / 回退块数（诊断）；
/// - out_error：出参（可空）失败原因（需 nc_free_string）。
int32_t nc_superres_upscale(NcEditor* editor, const uint8_t* rgba, int32_t width, int32_t height,
                            int32_t scale, uint8_t** out_rgba, int32_t* out_width,
                            int32_t* out_height, int32_t* out_tiles, int32_t* out_fallbacks,
                            char** out_error);

// =============================================================================
// 字幕翻译（真实推理）
// =============================================================================

/// 加载翻译模型（分体导出四件产物）。
/// - encoder_path / decoder_path / decoder_with_past_path：三个 .onnx；
/// - tokenizer_json_path：tokenizer.json；
/// 说明：必须使用**分体导出**（decoder_model + decoder_with_past_model）。
///       merged 导出在 cache 分支下 encoder KV 会收缩，导致译文退化。
int32_t nc_translate_load(NcEditor* editor, const char* encoder_path, const char* decoder_path,
                          const char* decoder_with_past_path, const char* tokenizer_json_path,
                          char** out_error);
/// 卸载翻译模型。
int32_t nc_translate_unload(NcEditor* editor);
/// 翻译模型是否已加载（1/0）。
int32_t nc_translate_loaded(const NcEditor* editor);

/// 单句翻译（中→英）。
/// - text：UTF-8 原文；
/// - out_text：出参（库分配；需 nc_free_string）；
/// - out_error：出参（可空）失败原因（需 nc_free_string）。
int32_t nc_translate_text(NcEditor* editor, const char* text, char** out_text, char** out_error);

/// 批量翻译当前工程字幕（整条轨道）。
/// - out_json：出参（库分配；JSON：{ok,total,translated,failed,lines:[{src,dst,error}]}；
///   需 nc_free_string）；
/// 说明：仅改写字幕文本，不改时间与样式；单句失败保留原文并在 error 中说明。
int32_t nc_translate_subtitles(NcEditor* editor, char** out_json, char** out_error);

/// 加载人像抠像模型（MODNet ONNX）。
int32_t nc_matting_load(NcEditor* editor, const char* model_path, char** out_error);
/// 卸载人像抠像模型。
int32_t nc_matting_unload(NcEditor* editor);
/// 人像抠像模型是否已加载（1/0）。
int32_t nc_matting_loaded(const NcEditor* editor);

/// 生成单帧人像 Alpha 蒙版（真实推理；文档 1.6）。
/// - quality：0=快速 1=标准 2=高精度（推理分辨率 256/512/768）；
/// - feather：边缘羽化 0..100（文档默认 35）；
/// - out_alpha：出参（库分配；尺寸 = width × height；需 nc_free_buffer；
///   255 = 人像 / 0 = 背景）；
/// 返回 NC_OK 或错误码。
int32_t nc_matting_estimate(NcEditor* editor, const uint8_t* rgba, int32_t width, int32_t height,
                            int32_t quality, double feather, uint8_t** out_alpha,
                            char** out_error);

/// 应用人像特效（消费 Alpha；文档 Part 4-B 5.2「瘦身/发光轮廓」）。
/// - effect：34=瘦身 35=发光轮廓（其余走 nc_add_effect）；
/// - intensity：强度 0..1；alpha：抠像蒙版（尺寸 = width × height；可空——
///   为空时零改动，不伪造人像区域）。
int32_t nc_person_effect_apply(uint8_t* rgba, const uint8_t* alpha, int32_t width, int32_t height,
                               int32_t effect, double intensity);

/// 加载语音识别模型（Whisper encoder + decoder + BPE 词表）。
/// - tokenizer_path：词表 JSON 路径（可空 = 不加载词表，解码返回 token 占位）。
int32_t nc_speech_load(NcEditor* editor, const char* encoder_path, const char* decoder_path,
                       const char* tokenizer_path, char** out_error);
/// 卸载语音识别模型。
int32_t nc_speech_unload(NcEditor* editor);
/// 语音识别模型是否已加载（1/0）。
int32_t nc_speech_loaded(const NcEditor* editor);

/// 识别单声道 PCM（真实推理；文档 3.2）。
/// - samples：单声道 float PCM（[-1,1]）；frame_count：帧数；sample_rate：采样率；
/// - language：0=自动 1=中文 2=英语 3=日语 4=韩语；
/// - out_segments_json：出参（库分配；需 nc_free_string）分段 JSON：
///   [{"start":秒,"end":秒,"text":"..."}...]；
/// 返回 NC_OK 或错误码（未加载/音频为空/推理失败）。
int32_t nc_speech_transcribe(NcEditor* editor, const float* samples, int64_t frame_count,
                             int32_t sample_rate, int32_t language, char** out_segments_json,
                             char** out_error);

/// 计算 Whisper Mel 频谱（供平台层/诊断；80 × frames）。
/// - out_mel：出参（库分配；需 nc_free_buffer；浮点数组，长度 = 80 × frames）。
int32_t nc_speech_mel(const float* samples, int64_t frame_count, int32_t sample_rate,
                      int32_t frames, float** out_mel);

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

/// 设置片段降噪强度（文档「🎵音频 → 降噪 → 开关/强度(弱中强)」）。
/// - strength：0=off 1=weak 2=medium 3=strong（非法返回 NC_ERROR_INVALID_ARGUMENT）；
/// - 启用后导出混音对该片段做真实谱减降噪（Pro 门控由平台层执行）。
int32_t nc_set_clip_denoise(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                            int32_t strength);

/// 读取片段降噪强度（0..3；未设置返回 0 = off；参数错误返回 -1）。
int32_t nc_get_clip_denoise(const NcEditor* editor, int32_t track_index, uint64_t clip_id);

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

// ---- 口播精剪（文档 7.2「AI成片 → 口播精剪」）----

/// 分析素材语音活动（真实解码 + VAD；供精剪预览与统计展示）。
/// - asset_id：音频/视频素材 ID；
/// - out_speech_us / out_silence_us：出参（可空）有效语音/静音总时长；
/// - out_keep_ratio：出参（可空）保留比（0..1）；
/// - out_segment_count：出参（可空）保留区间数量；
/// - out_note：出参（可空）诊断说明（需 nc_free_string；失败原因为空时给出提示）。
/// 返回：>=0 = 保留区间数量；-1 = 错误（见 out_error）。
int32_t nc_speech_analyze(NcEditor* editor, uint64_t asset_id, int64_t* out_speech_us,
                          int64_t* out_silence_us, double* out_keep_ratio,
                          int32_t* out_segment_count, char** out_note, char** out_error);

/// 落盘口播精剪（真实删静音；一次调用整体可撤销）。
/// - track_index：目标轨道（主序列内）；track_index < 0 时使用首个音频轨；
/// - 返回：>=0 = 插入的片段数；-1 = 错误（见 out_error）。
int32_t nc_speech_cut_apply(NcEditor* editor, int32_t track_index, char** out_error);

// ---- 蒙版（文档「✂️剪辑 → 蒙版：线性/镜面/圆形/矩形（羽化/反转）」）----

/// 设置片段蒙版。
/// - shape：0=none 1=linear 2=mirror 3=circle 4=rectangle（非法返回错误码）；
/// - center_x/center_y：归一化中心（0..1）；
/// - rotation_degrees：旋转角；width/height/feather/corner_radius：归一化尺寸参数；
/// - invert：是否反转（0/1）。
int32_t nc_set_clip_mask(NcEditor* editor, int32_t track_index, uint64_t clip_id, int32_t shape,
                         double center_x, double center_y, double rotation_degrees,
                         double width, double height, double feather, double corner_radius,
                         int32_t invert);

/// 读取片段蒙版（多个出参可空）。
/// 返回：1 = 已启用蒙版（出参有效），0 = 未启用，-1 = 参数错误。
int32_t nc_get_clip_mask(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                         int32_t* out_shape, double* out_center_x, double* out_center_y,
                         double* out_rotation_degrees, double* out_width, double* out_height,
                         double* out_feather, double* out_corner_radius, int32_t* out_invert);

// ---- 智能抠像 / 色度抠像（文档第 2 部分「🤖AI实验室 → 智能抠像 → 色度抠像(绿幕)」
//      + 权益矩阵「AI智能抠像：基础版 / 高精度版+物品/色度抠像（Pro）」）----

/// 查询色度抠像功能可用性（Pro 门控；fail-closed）。
/// - quality：0 = 基础版 / 1 = 高精度版；
/// - out_quality：出参（可空）生效档位；
/// - out_reason：出参（可空）展示文案（需 nc_free_string；含 🔒 语义）。
/// 返回：1 = 可用；0 = 不可用（弹付费墙）；-1 = 参数错误。
int32_t nc_matting_access(const NcEditor* editor, int32_t quality, int32_t* out_quality,
                          char** out_reason);

/// 设置片段色度抠像（绿幕）。
/// - key_color：键控色字符串（"green"/"blue"/"magenta"/"#RRGGBB"，可空 = 纯绿）；
/// - inner_threshold/outer_threshold：双阈值（0..1；outer > inner；<=0 用默认）；
/// - spill_suppression：溢色抑制强度 0..1（< 0 用默认）；
/// - quality：0 = 基础版 / 1 = 高精度版（档位参数会调整处理强度）；
/// - enabled：0 = 清除抠像，1 = 启用（启用前会做 Pro 门控，不可用返回 NC_ERROR_UNSUPPORTED）。
/// 返回 NC_OK 或错误码。
int32_t nc_set_clip_chroma_key(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               const char* key_color, double inner_threshold,
                               double outer_threshold, double spill_suppression,
                               int32_t quality, int32_t enabled);

/// 读取片段色度抠像（多个出参可空）。
/// - out_key_color_rgb：出参（可空）键控色；
/// - out_inner/out_outer/out_spill：出参（可空）参数；
/// 返回：1 = 已启用抠像（出参有效），0 = 未启用，-1 = 参数错误。
int32_t nc_get_clip_chroma_key(const NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               uint32_t* out_key_color_rgb, double* out_inner,
                               double* out_outer, double* out_spill);

/// 估计片段某帧的抠像前景覆盖率（采样；供 UI 预览「前景约 X%」）。
/// - rgba/width/height：帧数据（平台层从预览管线取）；
/// - key 参数同 nc_set_clip_chroma_key（quality 参与）；
/// 返回覆盖率 0..1；参数错误返回 -1。
double nc_chroma_key_estimate(const uint8_t* rgba, int32_t width, int32_t height,
                              const char* key_color, double inner_threshold,
                              double outer_threshold, double spill_suppression,
                              int32_t quality);

// ---- 画中画混合模式（文档第二段 5.9）----

/// 设置片段的画中画混合模式（多层合成时与下层的混合公式）。
/// - mode：0=normal 1=screen 2=overlay 3=darken 4=hard_light；
/// - 返回 NC_OK 或错误码（非法 mode → NC_ERROR_INVALID_ARGUMENT）。
/// 说明：混合模式属于工程数据（随 .nclip 持久化；默认 normal 不写文件）。
int32_t nc_set_clip_blend_mode(NcEditor* editor, int32_t track_index, uint64_t clip_id,
                               int32_t mode);

/// 读取片段的画中画混合模式（0..4；未设置返回 0 = normal）。
/// 返回：0..4 = 模式；-1 = 参数错误。
int32_t nc_get_clip_blend_mode(const NcEditor* editor, int32_t track_index, uint64_t clip_id);

// ---- AI 模型管理（文档第 5 部分「二、AI模型管理」+ 第 8 部分「模型分发架构」）----

/// 列举内置模型（供 UI「AI 模型管理」页列表）。
/// - out_ids：出参（可空）模型 ID 字符串数组（需逐个 nc_free_string 并释放数组本身）；
///   capacity：数组容量；out_count：出参（可空）实际数量（可大于 capacity 用于查询总数）。
/// - out_ids 为 char** 数组，由库分配；调用方用 nc_free_string_array 释放。
/// 返回 NC_OK。
int32_t nc_model_catalog(char*** out_ids, int32_t capacity, int32_t* out_count);

/// 读取模型目录信息（名称/体积/版本/能力/是否 Pro）。
/// - model_id：模型标识（如 "matting_hq"）；
/// - 出参均可空：out_name/out_description/out_version（需 nc_free_string）、
///   out_size_bytes/out_requires_pro/out_capability（枚举值）。
/// 返回 NC_OK 或 NC_ERROR_NOT_FOUND。
int32_t nc_model_info(const char* model_id, char** out_name, char** out_description,
                      int32_t* out_size_bytes, char** out_version, int32_t* out_requires_pro,
                      int32_t* out_capability);

/// 查询模型状态（0=未下载 1=下载中 2=已安装 3=损坏 4=可更新 5=校验中；-1=参数错误）。
int32_t nc_model_status(const NcEditor* editor, const char* model_id);

/// 查询模型在当前权益下是否可用（1/0；-1 = 参数错误）。
/// 高精度抠像/光流补帧为 Pro 专属；其余能力本地放行（配额由服务端裁决）。
int32_t nc_model_access_allowed(const NcEditor* editor, const char* model_id);

/// 开始下载模型（平台层获取签名 URL 后调用；total_bytes 来自 Content-Length）。
/// 返回 NC_OK 或错误码（状态非法 / 参数非法时不产生副作用）。
int32_t nc_model_download_begin(NcEditor* editor, const char* model_id, int64_t total_bytes,
                                int64_t now_unix);

/// 更新下载进度（单调不减；超出总量钳制）。
int32_t nc_model_download_progress(NcEditor* editor, const char* model_id,
                                   int64_t downloaded_bytes, int64_t now_unix);

/// 暂停下载（断点保留）。
int32_t nc_model_download_pause(NcEditor* editor, const char* model_id, int64_t now_unix);

/// 继续下载（断点保留；平台层用 nc_model_resume_plan 取续传坐标）。
int32_t nc_model_download_resume(NcEditor* editor, const char* model_id, int64_t now_unix);

/// 取消下载（断点保留，支持「[继续下载]」）。
int32_t nc_model_download_cancel(NcEditor* editor, const char* model_id, int64_t now_unix);

/// 下载完成 → 进入校验阶段。
int32_t nc_model_download_finish(NcEditor* editor, const char* model_id, int64_t now_unix);

/// 下载失败（记录原因；断点保留可重试）。
int32_t nc_model_download_fail(NcEditor* editor, const char* model_id, const char* reason,
                               int64_t now_unix);

/// 读取下载状态（供 UI 进度条）。
/// - 出参均可空：out_phase（0..6 枚举）、out_total_bytes、out_downloaded_bytes、
///   out_message（需 nc_free_string）。
/// 返回：1 = 有任务记录，0 = 无记录，-1 = 参数错误。
int32_t nc_model_download_state(const NcEditor* editor, const char* model_id, int32_t* out_phase,
                                int64_t* out_total_bytes, int64_t* out_downloaded_bytes,
                                char** out_message);

/// 获取断点续传坐标（HTTP Range 起点 + AES-CTR 计数器 + 块内丢弃字节数）。
/// - 出参均可空：out_offset_bytes、out_counter（16 字节缓冲）、out_intra_block_skip。
/// 返回 NC_OK；参数非法返回错误码。
int32_t nc_model_resume_plan(const NcEditor* editor, const char* model_id, int64_t downloaded_bytes,
                             int64_t* out_offset_bytes, uint8_t* out_counter,
                             int32_t* out_intra_block_skip);

/// 校验并安装模型（平台层下载完成后调用；真实 SHA-256 校验，失败记入损坏状态）。
/// - ciphertext/size：下载到的密文载荷副本（平台层从文件读入）；
/// - expected_sha256_hex：服务器清单摘要；
/// - out_installed：出参（可空）1 = 校验通过并记录安装，0 = 校验失败；
/// - out_message：出参（可空）结论文案（需 nc_free_string）。
/// 返回：NC_OK = 校验流程正常完成（无论通过与否，见 out_installed）；
///       错误码 = 参数/状态非法。
int32_t nc_model_install(NcEditor* editor, const char* model_id, const uint8_t* ciphertext,
                         int64_t size, const char* path, const char* expected_sha256_hex,
                         int64_t now_unix, int32_t* out_installed, char** out_message);

/// 删除模型记录（供「🗑️」；返回 NC_OK；core 不触碰文件系统，
/// 平台层据 nc_model_info 的路径删除文件）。
int32_t nc_model_remove(NcEditor* editor, const char* model_id);

/// 模型存储统计（文档 5.2「存储：可用45GB/总计128GB」）。
/// - device_total_bytes / device_free_bytes：平台层探测的设备容量；
/// - 出参均可空：out_model_bytes/out_installed_count/out_corrupt_count、
///   out_usage_text/out_model_text（需 nc_free_string）。
int32_t nc_model_storage_stats(const NcEditor* editor, int64_t device_total_bytes,
                               int64_t device_free_bytes, int64_t* out_model_bytes,
                               int32_t* out_installed_count, int32_t* out_corrupt_count,
                               char** out_usage_text, char** out_model_text);

/// 从偏移解密模型载荷（推理前内存解密；支持续传后的部分数据）。
/// - key：32 字节密钥（服务器下发；由平台层内存持有，禁止落盘）；
/// - out_plaintext/out_size：出参（可空）明文缓冲（需 nc_free_buffer 释放）。
/// 返回 NC_OK 或错误码。
int32_t nc_model_decrypt(const char* model_id, const char* version, const uint8_t* key,
                         const uint8_t* ciphertext, int64_t size, int64_t offset,
                         uint8_t** out_plaintext, int64_t* out_size);

/// 释放库分配的内存缓冲（模型解密输出等）。
void nc_free_buffer(uint8_t* buffer);

// ---- 章节自动生成（文档第 7 部分「🧪AI工具 → 章节自动生成：自动检测/手动编辑标题」）----

/// 自动检测章节（真实解码采样 → 场景/静音边界 → 时长约束）。
/// - 参数为 0 表示使用默认值（最短 20s / 最长 300s / 静音 1.5s）；
///   use_scene_detect / use_silence_detect 传 -1 表示默认（都启用）；
///   max_chapters 传 0 表示不限制；
/// - 检测结果缓存在 editor 内，供 nc_chapter_apply 落盘（可先预览再决定）；
/// - out_summary：出参（可空）结论（如「检测到 5 个章节（场景 3 / 静音 1 / 硬切 1…）」）；
/// - out_note：出参（可空）无结果时的原因说明；
/// - out_error：出参（可空）错误说明。
/// 返回：检测到的章节数（>= 0；0 = 未检测到）；-1 = 参数错误。
int32_t nc_chapter_detect(NcEditor* editor, double min_chapter_seconds,
                          double max_chapter_seconds, double silence_boundary_seconds,
                          int32_t use_scene_detect, int32_t use_silence_detect,
                          int32_t max_chapters, char** out_summary, char** out_note,
                          char** out_error);

/// 落盘缓存的检测结果（一条撤销事务；重复调用按幂等语义更新既有标记）。
/// 返回：写入的标记数（新增 + 更新）；-1 = 无缓存结果/失败。
int32_t nc_chapter_apply(NcEditor* editor, char** out_error);

/// 已落盘章节数（由序列章节标记重建）。
int32_t nc_chapter_count(const NcEditor* editor);

/// 读取已落盘章节（out_title 需 nc_free_string）。
int32_t nc_chapter_get(const NcEditor* editor, int32_t index, int64_t* out_start_us,
                       int64_t* out_end_us, char** out_title);

/// 手动编辑章节标题（更新既有章节标记；可撤销）。
int32_t nc_chapter_rename(NcEditor* editor, int32_t index, const char* new_title,
                          char** out_error);

// ---- 智能成片（多版本）+ 重新生成影响分析（文档第 7 部分 4.1 / 4.4 / 六 / 七）----

/// 参考风格权益门控（Free 不可用 → 弹付费墙；fail-closed）。
/// - out_reason：出参（可空）提示文案（需 nc_free_string）。
/// 返回：1 = 可用；0 = 不可用（付费墙）；-1 = 参数错误。
int32_t nc_reference_style_access(const NcEditor* editor, char** out_reason);

/// 生成版本数门控（Free 固定 1；Pro 1-5）。
/// - requested：用户请求数（<= 0 按 1）；
/// - out_allowed_count：出参（可空）允许的版本数；
/// - out_reason：出参（可空）提示文案（需 nc_free_string）。
/// 返回：1 = 请求可用；0 = 被降级/拒绝（见 out_allowed_count 与 note）；-1 = 参数错误。
int32_t nc_movie_version_access(const NcEditor* editor, int32_t requested,
                                int32_t* out_allowed_count, char** out_reason);

/// 生成多版本成片计划（真实素材剪辑建议；缓存供 apply 消费）。
/// - version_count：生成版本数（1-5；由 nc_movie_version_access 门控）；
/// - pace_factor：剪辑节奏滑块（0..1；0.5 = 忠实参考）；
/// - target_seconds：目标成片时长（秒；<= 0 = 按参考节奏自然长度）；
/// - style_json：参考风格 JSON（{"rhythm":{...},"color":{...}}；见下）；
/// - material_ids/material_seconds：素材 ID 数组与对应可用秒数（真实素材）；
/// - material_count：素材数量；
/// - out_summary：出参（可空）计划摘要（需 nc_free_string）。
/// 返回：可落盘版本数（>= 0）；-1 = 参数错误。
/// 说明：style_json 为简单扁平 JSON（避免引入复杂嵌套解析），字段：
///   {"shotCount":3,"totalSeconds":6,"median":2,"p25":1.5,"p75":3,
///    "average":2,"avgR":0.5,"avgG":0.5,"avgB":0.5,"saturation":0.5,"contrast":0.5}
int32_t nc_smart_movie_plan(NcEditor* editor, int32_t version_count, double pace_factor,
                            double target_seconds, const char* style_json,
                            const uint64_t* material_ids, const double* material_seconds,
                            int32_t material_count, char** out_summary);

/// 落盘已计划的版本（逐版本；某版本失败不影响其它；文档 4.1 失败态）。
/// - track_indexes：目标轨道下标数组（每版本一条空轨道）；
/// - track_count：轨道数量；
/// - out_applied：出参（可空）成功落盘的版本数；
/// - out_failed_json：出参（可空）失败明细 JSON（需 nc_free_string）。
int32_t nc_smart_movie_apply(NcEditor* editor, const int32_t* track_indexes, int32_t track_count,
                             int32_t* out_applied, char** out_failed_json, char** out_error);

/// 重新生成影响分析（文档 4.4「将会丢失 / 将会保留」）。
/// - scope：0 = 整个项目；1 = 当前序列；
/// - out_report_json：出参（可空）报告 JSON（需 nc_free_string）。
int32_t nc_regeneration_impact(const NcEditor* editor, int32_t scope, char** out_report_json);

/// 执行重新生成的清理阶段（保留锁定轨道；一条事务可回退）。
/// - out_cleared_json：出参（可空）结果 JSON（需 nc_free_string）。
int32_t nc_regeneration_clear(NcEditor* editor, int32_t scope, char** out_cleared_json,
                              char** out_error);

/// 清除全部章节标记（一条撤销事务）。
int32_t nc_chapter_clear(NcEditor* editor, char** out_error);

// ---- 导出队列（文档第 6 部分「二、导出队列管理」+ 第 4 部分「后台导出」）----

/// 入队一个导出任务。参数同 nc_export_run，另加名称/预设/性能模式。
/// - name：任务展示名（如「春游Vlog」）；
/// - preset_id：导出预设（0 = 自定义，其余见 nc_export_presets）；
/// - mode：性能模式（0 极速 / 1 平衡 / 2 省电）；
/// - include_audio / attach_ai_label / ai_label_text：同 nc_export_run；
/// - out_job_id：出参（可空）任务 ID；
/// - out_error：出参（可空）失败原因（如队列已满，需 nc_free_string）。
/// 返回：NC_OK 或错误码（Unsupported = 队列已满，弹付费墙）。
int32_t nc_export_enqueue(NcEditor* editor, const char* name, const char* out_path,
                          int32_t preset_id, int32_t mode, int32_t include_audio,
                          int32_t attach_ai_label, const char* ai_label_text,
                          uint64_t* out_job_id, char** out_error);

/// 查询下一个应执行的任务（调度决策；无 = 返回 0）。
/// - out_job_id：出参（可空）任务 ID；
/// - out_out_path：出参（可空）输出路径（需 nc_free_string）；
/// - out_preset/out_mode：出参（可空）预设与性能模式；
/// - out_include_audio/out_attach_ai_label：出参（可空）；
/// - out_ai_label_text：出参（可空）标识文案（需 nc_free_string）。
/// 返回：1 = 有任务；0 = 无；负数 = 参数错误。
int32_t nc_export_poll_next(NcEditor* editor, uint64_t* out_job_id, char** out_out_path,
                            int32_t* out_preset, int32_t* out_mode,
                            int32_t* out_include_audio, int32_t* out_attach_ai_label,
                            char** out_ai_label_text);

/// 查询任务状态（out_state 见 ExportJobState 枚举；out_note 需 nc_free_string）。
/// 返回：1 = 找到；0 = 未找到；负数 = 参数错误。
int32_t nc_export_job_state(const NcEditor* editor, uint64_t job_id, int32_t* out_state,
                            double* out_progress, int64_t* out_frames_written, char** out_note);

/// 回填任务进度（仅运行中任务；单调不减）。
int32_t nc_export_job_progress(NcEditor* editor, uint64_t job_id, double progress);

/// 任务状态操作：action 0=开始 / 1=完成 / 2=失败 / 3=暂停 / 4=继续 / 5=移除。
/// - frames_written：action=1 时的输出帧数；
/// - reason：action=2 时的失败原因（可空）；
/// - now_unix：当前时间（Unix 秒）；
/// - out_error：出参（可空）失败原因（需 nc_free_string）。
int32_t nc_export_job_update(NcEditor* editor, uint64_t job_id, int32_t action,
                             int64_t frames_written, const char* reason, int64_t now_unix,
                             char** out_error);

/// 队列摘要（out_queued = 等待中，out_running = 运行中，out_active = 活动总数）。
int32_t nc_export_queue_summary(const NcEditor* editor, int32_t* out_queued,
                                int32_t* out_running, int32_t* out_active);

/// 停止所有活动任务（应用退出时调用）。
void nc_export_queue_cancel_all(NcEditor* editor, int64_t now_unix);

// ---- 媒体离线检测与重新链接（文档第 3 部分「四、媒体离线检测与重新链接」）----

/// 扫描工程素材在线状态（路径存在性 + 摘要校验）。
/// - probe_kind：探测器类型（0 = 无探测器，全部未验证；1 = 用 FFmpeg 探测器，
///   需构建包含 FFmpeg；未来可扩展系统 API 实现）；
/// - out_report_json：出参（可空）报告 JSON（需 nc_free_string）。
///   格式：{"offline":N,"online":M,"items":[{"id":..,"name":..,"path":..,
///          "status":"missing|digest_mismatch|online|unverified","message":..}]}
/// 返回：NC_OK 或错误码。
int32_t nc_media_scan_offline(NcEditor* editor, int32_t probe_kind, char** out_report_json,
                              char** out_error);

/// 把最近一次扫描结果写回工程（设置素材离线标记；不阻塞项目打开）。
/// 返回：被更新的素材数；负数 = 错误。
int32_t nc_media_apply_offline_flags(NcEditor* editor);

/// 重新链接单个素材（手动选择路径）。
/// - new_path：新文件路径；verify_digest：1 = 校验内容摘要（要求素材已记录摘要）；
/// - out_warning：出参（可空）提示（需 nc_free_string）。
int32_t nc_media_relink(NcEditor* editor, uint64_t asset_id, const char* new_path,
                        int32_t verify_digest, char** out_warning, char** out_error);

/// 自动扫描查找离线素材的新位置（按文件名匹配候选目录）。
/// - search_dirs_json：候选目录 JSON 数组（如 ["/a","/b"]）；
/// - out_candidates_json：出参（可空）候选列表 JSON（需 nc_free_string）。
///   格式：[{"asset":..,"fileName":..,"path":..,"digestMatches":true/false}]
int32_t nc_media_find_relink_candidates(NcEditor* editor, const char* search_dirs_json,
                                        char** out_candidates_json, char** out_error);

/// 批量重新链接（mappings_json：[{"asset":..,"path":".."},...]）。
/// - out_relinked：出参（可空）成功数；
/// - out_report_json：出参（可空）明细（需 nc_free_string）。
int32_t nc_media_relink_batch(NcEditor* editor, const char* mappings_json,
                              int32_t* out_relinked, char** out_report_json, char** out_error);

/// 记录素材内容摘要（平台层在导入后计算写入；hex 为 64 位十六进制或空）。
int32_t nc_media_set_digest(NcEditor* editor, uint64_t asset_id, const char* sha256_hex,
                            char** out_error);

// ---- 隐形水印（文档第 8 部分 3.1「隐形水印（事后追溯）」）----

/// 从账号 ID 计算含水印载荷的账号哈希（SHA-256 前 32 位）。
/// 返回哈希值（0 = 空账号）。
uint32_t nc_watermark_account_hash(const char* account_id);

/// 在导出管线启用隐形水印（Pro 内容溯源；对后续 nc_export_run / 队列任务生效）。
/// - enabled：0 = 关闭；1 = 启用；
/// - account_id：账号标识（可为空 = 匿名）；核心只存哈希，不存原文；
/// - export_sequence：导出序号（每次导出唯一，用于定位批次）；
/// - 说明：容量不足时导出会记警告并跳过（不中断）。
/// 返回 NC_OK 或错误码。
int32_t nc_watermark_set_export(NcEditor* editor, int32_t enabled, const char* account_id,
                                int32_t export_sequence);

/// 检测画面中的隐形水印（平台层把解码帧传入；用于溯源验证）。
/// - rgba/width/height：交错 RGBA8 帧；
/// - out_account_hash/out_export_sequence：出参（可空）恢复的载荷；
/// - out_agreement：出参（可空）比特一致率（0..1）；
/// - out_note：出参（可空）结论（需 nc_free_string）。
/// 返回：1 = 检出有效水印；0 = 未检出；-1 = 参数错误。
int32_t nc_watermark_detect(const uint8_t* rgba, int32_t width, int32_t height,
                            uint32_t* out_account_hash, int32_t* out_export_sequence,
                            double* out_agreement, char** out_note);

// ---- 会员心跳校验（文档第 8 部分「五、会员心跳校验具体流程」）----

/// 心跳处理：把服务端校验结果映射为本地权益状态与分级提示。
/// - server_time_unix / tier / lifetime / expires_at_unix：服务端权威值；
/// - active_devices / device_limit：设备数校验（limit <= 0 = 不判定）；
/// - signature_valid：签名校验（加固 SDK / App Attest 判定结果）；
/// - server_locked：服务端是否下发功能锁定；
/// - message：服务端说明（可空）；
/// - out_verdict：出参（可空）判定（0 正常 / 1 警告 / 2 锁定 / 3 网络错误）；
/// - out_message：出参（可空）展示文案（需 nc_free_string）。
/// 返回 NC_OK 或错误码。
int32_t nc_heartbeat_handle(NcEditor* editor, int64_t server_time_unix, int32_t tier,
                            int32_t lifetime, int64_t expires_at_unix, int32_t active_devices,
                            int32_t device_limit, int32_t signature_valid, int32_t server_locked,
                            const char* message, int32_t* out_verdict, char** out_message);

/// 网络失败时的心跳处理（保持权益不变、不累计异常）。
int32_t nc_heartbeat_network_error(NcEditor* editor, char** out_message);

/// 查询心跳状态：锁定标志 / 连续异常计数 / 上次校验时间。
int32_t nc_heartbeat_status(const NcEditor* editor, int32_t* out_locked,
                            int32_t* out_warning_count, int64_t* out_last_checked_unix);

/// 周期触发是否到期（文档「每 30 分钟」）。
/// - now_unix：当前时间；interval_seconds：间隔（<= 0 用默认 1800）。
/// 返回：1 = 到期；0 = 未到期；-1 = 参数错误。
int32_t nc_heartbeat_periodic_due(const NcEditor* editor, int64_t now_unix,
                                  int64_t interval_seconds);

/// 解除功能锁定（用户完成账号验证后由平台层调用）。
int32_t nc_heartbeat_clear_lock(NcEditor* editor);

/// 释放字符串数组（nc_model_catalog 的输出）。
void nc_free_string_array(char** items, int32_t count);

// ---- 内存 ----

/// 释放库返回的字符串。
void nc_free_string(char* text);

#ifdef __cplusplus
}  // extern "C"
#endif
