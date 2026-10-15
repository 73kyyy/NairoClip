# NairoClip 平台绑定层（C ABI）说明

## 1. 分层关系

```
┌────────────────────────────────────────────────┐
│ Android (Kotlin/JNI) │ iOS (Swift/C) │ 桌面 (Qt/C++) │
├────────────────────────────────────────────────┤
│            NairoClip C ABI（capi.h）             │
│   纯 C 接口、不透明句柄、UTF-8、稳定错误码          │
├────────────────────────────────────────────────┤
│              core/（C++17 静态库）               │
└────────────────────────────────────────────────┘
```

## 2. 为什么用 C ABI 而不是直接暴露 C++

- C++ ABI 在不同编译器/标准库版本间**不稳定**，JNI/Swift 直接调用 C++ 会踩版本坑；
- C ABI 是事实标准（JNI、Swift 互操作、Dart FFI、Python ctypes 均可无胶水调用）；
- 不透明句柄（NcEditor*）把实现细节完全封在库内，升级库不会破坏调用方。

## 3. 内存契约（必须遵守）

| 情形 | 规则 |
|---|---|
| 库返回的 `char*`（如 nc_project_save） | 调用方必须用 `nc_free_string` 释放 |
| 传入的 `const char*` 参数 | 调用返回后即可释放，库不长期持有 |
| 句柄 | `nc_editor_create` / `nc_editor_destroy` 严格配对 |
| 错误详情出参 | 同样用 `nc_free_string` 释放 |

## 4. 线程模型

- 单个 `NcEditor*` 句柄**不支持**多线程并发调用；
- 平台层把编辑操作串行化到单一工作线程（Android 建议 HandlerThread）；
- 只读查询（duration/track_count）可与写操作交错，但同一句柄上仍建议同一线程。

## 5. 版本策略

- `NC_ABI_VERSION` 只在不兼容变更时 +1；
- 平台层启动时用 `nc_abi_version()` 校验与预期一致，不一致时降级提示更新 App；
- 追加新函数不递增版本（向后兼容）。

## 6. 错误码映射

| C 错误码 | core::ErrorCode | 场景示例 |
|---|---|---|
| NC_ERROR_INVALID_ARGUMENT | InvalidArgument | 负起点、非法轨道下标 |
| NC_ERROR_NOT_FOUND | NotFound | 引用的实体不存在 |
| NC_ERROR_OVERLAP | Overlap | 片段重叠 |
| NC_ERROR_OUT_OF_RANGE | OutOfRange | 超出素材范围 |
| NC_ERROR_LOCKED | Locked | 轨道已锁定 |
| NC_ERROR_UNSUPPORTED | Unsupported | .nclip 版本过高 |

## 7. 导出接口（nc_export_run）
```c
int64_t nc_export_run(nc_editor* h, const char* out_path, int crf, int include_audio,
                      int attach_ai_label, const char* ai_label_text,
                      int (*progress_cb)(double), char** out_error);
```
- **返回**：成功 = 输出视频帧数（>=0）；失败 = -1（错误信息经 `out_error`，需 `nc_free_string`）；
- **attach_ai_label**：合规开关——App 在「本次编辑使用了 AI 成片 / TTS 配音」时必须传 1；
  标识渲染规格由 UI 层按算法备案要求绘制（core 只产出标准描述）；
- **progress_cb**：进度回调（0..1），返回 0 取消导出；可在工作线程调用，但需自行 post 到 UI 线程；
- **线程**：导出为长任务，必须在工作线程执行，禁止占用 UI 线程；
- 注意：回调禁止回调 `nc_editor_*` 写操作（避免同句柄重入）。

## 8. Android JNI 接入示例（规划）

```kotlin
// Kotlin 侧（示意，M2.x 实现）
object NairoClipNative {
    init { System.loadLibrary("nairoclip") }
    external fun abiVersion(): Int
    external fun editorCreate(): Long
    external fun editorDestroy(handle: Long)
    external fun projectSave(handle: Long): String
    // ...
}
```

JNI 层（C++）只做两件事：句柄转换（long ↔ NcEditor*）、字符串转换（jstring ↔ UTF-8 char*）。
业务逻辑全部留在 core/ 中，JNI 层代码量越小越安全。

---

## 9. 完整接口索引（181 个函数；按功能分组）

> **给 UI/UX 窗口的对接说明**：下表为 C ABI 全量清单（`capi.h` 为权威定义）。
> 所有 `char*` 返回值需 `nc_free_string`；所有 `uint8_t*`/`double*` 数组需
> `nc_free_buffer`；`NcEditor*` 不支持多线程并发。错误码见第 6 节。
> 每次 UI 操作后建议刷新：`nc_undo_count`/`nc_can_undo`/`nc_can_redo` 驱动工具栏状态。

### 9.1 生命周期与工程（10）
```
nc_abi_version  nc_editor_create  nc_editor_destroy
nc_project_save  nc_project_load
nc_project_duration_us  nc_project_name  nc_set_project_name
nc_project_canvas  nc_set_project_canvas  nc_project_frame_rate  nc_set_project_frame_rate
```

### 9.2 历史/撤销（8）
```
nc_undo  nc_redo  nc_can_undo  nc_can_redo  nc_undo_count  nc_redo_count
nc_undo_history_name  nc_clear_undo_history  nc_set_undo_depth_limit
```
> 文档 Part 5(续) 第32节：退出剪辑页时调 `nc_clear_undo_history`；
> 进页时调 `nc_set_undo_depth_limit(editor, 20)`。

### 9.3 素材（7）
```
nc_import_asset  nc_asset_count  nc_asset_get  nc_remove_asset  nc_asset_is_referenced
nc_media_scan_offline  nc_media_find_relink_candidates  nc_media_relink  nc_media_relink_batch
nc_media_apply_offline_flags  nc_media_set_digest
nc_proxy_check_import  nc_proxy_set_record  nc_proxy_get_record  nc_proxy_clear_records  nc_proxy_usage
```

### 9.4 轨道（12）
```
nc_add_track  nc_track_count  nc_remove_track  nc_rename_track  nc_track_get
nc_set_track_locked  nc_set_track_muted  nc_set_track_hidden
nc_get_track_mixer  nc_set_track_gain  nc_set_track_solo  nc_get_master_gain  nc_set_master_gain
nc_get_track_auto_duck  nc_set_track_auto_duck
```
> `nc_track_get` 一次取齐：ID/名称/类型/锁定/静音/隐藏/独奏/增益（轨道控制列与混音台直接消费）。

### 9.5 片段基础编辑（16）
```
nc_insert_clip  nc_clip_count  nc_clip_get  nc_clip_info
nc_remove_clip  nc_split_clip  nc_move_clip  nc_move_clip_to_track
nc_trim_clip_start  nc_trim_clip_end  nc_set_clip_speed  nc_ripple_delete
nc_duplicate_clip  nc_freeze_frame  nc_reverse_clip  nc_set_clip_enabled
```

### 9.6 片段属性（12）
```
nc_set_clip_audio  nc_set_clip_denoise  nc_get_clip_denoise
nc_get_clip_opacity  nc_set_clip_opacity
nc_set_clip_mask  nc_get_clip_mask
nc_set_clip_chroma_key  nc_get_clip_chroma_key  nc_chroma_key_estimate  nc_matting_access
nc_set_clip_blend_mode  nc_get_clip_blend_mode
nc_set_clip_speed_curve  nc_clip_speed_curve_count  nc_clip_speed_curve_get
```

### 9.7 特效/关键帧/转场/字幕/标记（20）
```
nc_add_effect  nc_remove_effect  nc_effect_count  nc_effect_get
nc_insert_keyframe  nc_remove_keyframe  nc_keyframe_count  nc_keyframe_get
nc_add_transition  nc_remove_transition  nc_transition_count  nc_transition_get
nc_upsert_subtitle  nc_remove_subtitle  nc_subtitle_count  nc_subtitle_get
nc_add_marker  nc_remove_marker  nc_marker_count  nc_marker_get
nc_detach_audio  nc_create_compound_clip  nc_unwrap_compound_clip  nc_clip_is_compound
```

### 9.8 预览/缩略图/波形（9）
```
nc_preview_render   // 单帧 RGBA8 + 活动片段 + 动画快照 + 转场信息
nc_preview_subtitles  nc_preview_invalidate
nc_thumbnail_generate  nc_thumbnail_size
nc_waveform_generate  nc_waveform_bucket_count  nc_waveform_bucket_get
nc_free_buffer  // 释放上述返回的数组/像素缓冲
```
> **预览用法（UI 每帧调用）**：
> 1. 用户拖动播放头 → `nc_preview_render(editor, time_us, ...)` 取帧；
> 2. 返回 `out_clip_id` 供属性面板高亮；`out_opacity/scale/pos/rotation` 供浮层绘制；
> 3. 无素材时刻返回纯背景色帧（不失败），UI 正常显示黑场。

### 9.9 分析与智能（14）
```
nc_scene_detect  nc_beat_detect  nc_speech_analyze  nc_speech_cut_apply
nc_ai_check  nc_ai_check_fix_all  nc_auto_edit
nc_chapter_detect  nc_chapter_apply  nc_chapter_clear  nc_chapter_count  nc_chapter_get  nc_chapter_rename
nc_smart_movie_plan  nc_smart_movie_apply  nc_regeneration_impact  nc_regeneration_clear
nc_reference_style_access  nc_movie_version_access
```

### 9.10 导出（16）
```
nc_export_run  nc_export_estimate  nc_set_frame_fit  nc_get_frame_fit
nc_export_presets  nc_export_preset_info  nc_export_setup_preset  nc_would_clamp_export
nc_export_enqueue  nc_export_poll_next  nc_export_job_state  nc_export_job_progress
nc_export_job_update  nc_export_queue_summary  nc_export_queue_cancel_all
nc_set_background_color
```

### 9.11 会员/合规/水印/模型（24）
```
nc_get_entitlement  nc_set_entitlement  nc_would_clamp_export  nc_matting_access
nc_reference_style_access  nc_movie_version_access
nc_watermark_set_export  nc_watermark_detect  nc_watermark_account_hash
nc_heartbeat_handle  nc_heartbeat_status  nc_heartbeat_periodic_due
nc_heartbeat_network_error  nc_heartbeat_clear_lock
nc_model_catalog  nc_model_info  nc_model_status  nc_model_install  nc_model_remove
nc_model_download_begin  nc_model_download_pause  nc_model_download_resume
nc_model_download_cancel  nc_model_download_finish  nc_model_download_fail
nc_model_download_progress  nc_model_download_state  nc_model_resume_plan
nc_model_storage_stats  nc_model_access_allowed  nc_model_decrypt
nc_free_string  nc_free_string_array
```

### 9.12 UI 典型调用序列（参考）

```c
// ① 启动：创建 + 载入
NcEditor* ed = nc_editor_create();
nc_project_load(ed, nclip_text, &detail);

// ② 绘制时间轴：逐轨逐片段读取
int tracks = nc_track_count(ed);
for (int t = 0; t < tracks; ++t) {
    char* name; int type, locked, muted, hidden, solo; double gain;
    nc_track_get(ed, t, NULL, &name, &type, &locked, &muted, &hidden, &solo, &gain);
    nc_free_string(name);
    int clips = nc_clip_count(ed, t);
    for (int c = 0; c < clips; ++c) {
        uint64_t id, asset; int64_t start, dur, sn, sd; int enabled;
        nc_clip_info(ed, t, c, &id, &start, &dur, &asset, &sn, &sd, &enabled);
    }
}

// ③ 拖动播放头：渲染预览
uint8_t* rgba; int w, h; uint64_t clipId;
nc_preview_render(ed, time_us, &w, &h, &rgba, &clipId, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
// ... 显示 rgba ...
nc_free_buffer(rgba);

// ④ 编辑操作（每次后刷新撤销状态）
nc_split_clip(ed, 0, clipId, at_us);
int canUndo = nc_can_undo(ed);  // 驱动撤销按钮可用态

// ⑤ 退出剪辑页
nc_project_save(ed);                 // 取 .nclip 文本自行持久化
nc_clear_undo_history(ed);           // 文档：不跨会话保留撤销记录
```

