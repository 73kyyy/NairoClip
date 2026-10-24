# NairoClip UI 对接整改文档

> **目标读者**：UI/UX 窗口 + 平台层实现者
> **文档基准**：仓库 `73kyyy/NairoClip` @ `13a1b4e`（2026-10-24）
> **权威代码**：`core/include/nairoclip/core/capi.h`（232 个 C ABI）
> **本文作用**：把「设计桥只暴露 31 op / C ABI 有 232 函数」的缺口，拆成可验收的批次

---

## 0. 现状实测（不是估算）

| 项目 | 实测值 | 验证方式 |
|---|---|---|
| C ABI 函数数 | **232** | `grep` 统计 `capi.h` 声明（去重） |
| 设计桥（harness）HTTP 端点 | **17** | `grep '"/api/'` in `nc_design_harness.cpp` |
| 设计桥 `/api/edit` 支持的 op | **31** | `grep 'op == "'` 统计 |
| 设计桥直接调用的 `nc_*` | **87** | `grep -oE 'nc_[a-z0-9_]+\('` 去重 |
| **缺口**（C ABI 有、桥未接） | **145** | 232 − 87 |
| 设计桥编译 | 通过 | `bash design/tools/build_harness.sh build` |
| 设计桥运行 | 通过 | 实测 `工程就绪：3 条轨道，时长 8.00s` |
| 桥引用的 87 个 `nc_*` 全存在 | 87/87 | 与 `capi.h` 逐一比对 |

### 现有 17 个 HTTP 端点

```
GET  /api/state          真实工程状态（轨道/片段/属性）
GET  /api/presets        导出预设列表
GET  /api/frame          预览帧（PNG；真实解码 + 特效）
GET  /api/preset_frame   预设对比帧
GET  /api/prefetch       素材预热
GET  /api/thumb          时间轴缩略图（真实解码）
GET  /api/waveform       音频波形（真实解码 + 桶化）
GET  /api/export_status  导出进度
GET  /api/estimate       导出资源预估
GET  /api/ai_features    AI 功能可用性矩阵
GET  /api/models         AI 模型目录
GET  /api/projects       草稿列表
POST /api/save           保存 .nclip
POST /api/load           载入 .nclip
POST /api/edit           编辑操作（31 op）
POST /api/export         开始导出
POST /api/export_cancel  取消导出
```

### 现有 31 个 op（`/api/edit`）

```
undo  redo  split  move  move_to_track  trim_start  trim_end  delete
ripple_delete  duplicate  filter  opacity  speed  enabled
track_flag  track_gain  rename_track  marker_color  add_track
transition  marker  effect_add  denoise  mask  chroma  audio
speed_curve  keyframe  frame_fit  canvas  rename_project
```

---

## 1. 缺口分类（232 个 C ABI 按批次）

| 批次 | 领域 | 函数数 | 说明 |
|---|---|---|---|
| **P0** | 剪辑核心 | **90** | 原型/工程/历史/素材/轨道/片段编辑/属性/特效/字幕/标记 |
| **P1** | 时间轴 + 预览 | **37** | 预览渲染/缩略图/波形/场景/节拍/追踪/转场库/关键帧转场/画幅 |
| **P2** | 音频混音 | **7** | 混音台（增益/独奏/主输出）/自动闪避 |
| **P3** | AI | **32** | 任务状态机/深度图/抠像/语音/人物特效/AI 检查/自动剪辑 |
| **P4** | 导出 + 会员 + 运维 | **66** | 导出/队列/预设/权益/水印/模型管理/代理/离线/章节/成片/心跳 |
| **合计** | | **232** | 与 `capi.h` 完全一致，无重复无遗漏（已用脚本校验） |

---

## 2. 推进方案（建议：方案 C+）

| 方案 | 范围 | 优点 | 缺点 |
|---|---|---|---|
| A. 分 5 批全做 | 232 函数 / 约 200 op | 完整 | 周期长，中间态需反复验收 |
| B. 一次全做完 | 232 函数 / 约 200 op | 一次交付 | 中途无法验收，方向错则全废 |
| **C. P0+P1 优先** | **127 函数（90+37）** | **UI 立刻可用** | P2-P4 需后续补 |
| **C+（建议）** | P0+P1 UI 入口 + P2-P4 API 直通 | 兼顾可用性与完整性 | 略增工作量 |

### 推荐 C+ 的产出

```
阶段 A（本批）：P0 + P1 = 126 op
  ├─ 桥扩展：31 → 126 op
  ├─ 前端 window.NC_API：232 函数全量包装
  ├─ 联调验证清单（UI 操作 → 期望 → 实测）
  └─ 完成后可真实操作：打开工程 → 拖片段 → 看预览 → 撤销 → 保存

阶段 B（后续）：P2-P4 = 106 op
  ├─ 桥扩展至 232 op
  ├─ 未配 UI 的功能标注「接口就绪，UI 待接」
  └─ 网络/模型依赖项按 fail-closed 返回明确错误

阶段 C：交付文档
  ├─ 本文档定稿
  ├─ 验证清单（可勾选）
  └─ .gitignore（避免 99MB 设计产物入库）
```

---

## 3. 「连接上」的达标定义（建议：方案三）

| 方案 | 内容 | 评价 |
|---|---|---|
| A. 仅 UI 入口 + 真实调用 | 能点、能看到变化 | 未暴露接口需返工 |
| B. 仅 API 直通 | `window.NC_API.xxx()` 可调 | 无法证明「真能用」 |
| **C. API 全量直通 + 核心 UI 入口（建议）** | 232 全包装；核心流程有入口 | 兼顾验证与扩展 |

### 达标清单

```
□ 打开 UI → /api/state 返回真实工程（名称/画布/轨道/片段）
□ 拖片段改起点 → op=move → 时间轴位置变化 + 预览帧更新
□ 分割片段 → op=split → 两段 + 片段数 +1
□ 改不透明度 → op=opacity → 预览画面透明度变化（像素级可见）
□ 撤销 → op=undo → 状态回滚 + 撤销按钮态更新
□ 保存 → /api/save → 生成 .nclip（可重新载入）
□ 所有失败路径返回明确错误码（不静默）
```

---

## 4. 依赖外部资源项的处理（建议：方案一）

### 分级原则（与既有代码一致：fail-closed + 明确说明）

| 类别 | 定义 | 处理 |
|---|---|---|
| **A. 本地可完全真跑** | 无外部依赖 | 真实实现 + 真实测试 |
| **B. 本地状态机真跑，对端待接** | 需网络/文件 | 状态机真实 + 明确返回「对端未配置」 |
| **C. 需模型文件** | 需 .onnx | 模型存在则真跑；缺失则明确失败 |

### 逐项判定（实测）

| 接口组 | 类别 | 说明 |
|---|---|---|
| `nc_model_*`（18） | B | 状态机/断点续传/校验真实；真实下载需签名 URL |
| `nc_heartbeat_*`（5） | B | 分级/锁定/偏差判定真实；HTTP 由平台层发出 |
| `nc_watermark_*`（3） | A | QIM 嵌入/提取真实（CRF18 检出率 0.988） |
| `nc_proxy_*`（5） | A | 导入判定真实 |
| `nc_media_*`（6） | A | 离线扫描/重链候选真实 |
| `nc_chapter_*`（6） | A | scene_detect + VAD，真实解码 |
| `nc_depth_*`/`nc_matting_*`/`nc_speech_*`（15） | C（可跑） | 模型已下载（见 §6） |
| `nc_export_*`（12） | A | 真实 FFmpeg 编解码 |
| `nc_smart_movie_*`/`nc_regeneration_*`（4） | A | 真实素材剪辑建议 |

**结论**：没有「完全不可用」的接口。B 类只缺「对端配置」，链路真实。

---

## 5. 232 个接口逐项清单

### P0 剪辑核心（90）

**原型与工程（14）**
```
nc_abi_version  nc_editor_create  nc_editor_destroy
nc_project_save  nc_project_load  nc_project_duration_us
nc_project_name  nc_set_project_name  nc_project_canvas
nc_set_project_canvas  nc_project_frame_rate  nc_set_project_frame_rate
nc_track_count  nc_clip_count
```

**历史/撤销（9）**
```
nc_can_undo  nc_can_redo  nc_undo_count  nc_redo_count
nc_undo_history_name  nc_clear_undo_history  nc_set_undo_depth_limit
nc_undo  nc_redo
```

**素材（5）**
```
nc_asset_count  nc_asset_get  nc_remove_asset  nc_asset_is_referenced
nc_import_asset
```

**轨道（7）**
```
nc_add_track  nc_track_get  nc_remove_track  nc_rename_track
nc_set_track_locked  nc_set_track_muted  nc_set_track_hidden
```

**片段编辑（15）**
```
nc_insert_clip  nc_remove_clip  nc_split_clip  nc_move_clip
nc_move_clip_to_track  nc_trim_clip_start  nc_trim_clip_end
nc_set_clip_speed  nc_ripple_delete  nc_duplicate_clip
nc_freeze_frame  nc_reverse_clip  nc_clip_info  nc_clip_get
nc_set_clip_enabled
```

**片段属性（14）**
```
nc_get_clip_opacity  nc_set_clip_opacity  nc_set_clip_audio
nc_set_clip_denoise  nc_get_clip_denoise  nc_set_clip_filter
nc_get_clip_filter  nc_filter_display_name  nc_set_clip_mask
nc_get_clip_mask  nc_set_clip_blend_mode  nc_get_clip_blend_mode
nc_set_clip_background  nc_get_clip_background
```

**变速曲线（6）**
```
nc_set_clip_speed_curve  nc_clip_speed_curve_count  nc_clip_speed_curve_get
nc_speed_curve_preset_name  nc_speed_curve_preset_display_name
nc_apply_speed_curve_preset
```

**关键帧（4）**
```
nc_insert_keyframe  nc_remove_keyframe  nc_keyframe_count  nc_keyframe_get
```

**特效（4）**
```
nc_add_effect  nc_remove_effect  nc_effect_count  nc_effect_get
```

**字幕（4）**
```
nc_upsert_subtitle  nc_remove_subtitle  nc_subtitle_count  nc_subtitle_get
```

**标记（4）**
```
nc_add_marker  nc_remove_marker  nc_marker_count  nc_marker_get
```

**音画分离 / 复合片段（4）**
```
nc_detach_audio  nc_create_compound_clip  nc_unwrap_compound_clip
nc_clip_is_compound
```

### P1 时间轴 + 预览（37）

**预览（3）**
```
nc_preview_render  nc_preview_subtitles  nc_preview_invalidate
```

**时间轴可视化（5）**
```
nc_thumbnail_generate  nc_thumbnail_size
nc_waveform_generate  nc_waveform_bucket_count  nc_waveform_bucket_get
```

**转场（8）**
```
nc_add_transition  nc_remove_transition  nc_transition_count  nc_transition_get
nc_transition_type_count  nc_transition_type_name  nc_transition_category
nc_transition_supports_direction
```

**自定义关键帧转场（5）**
```
nc_add_transition_keyframe  nc_transition_keyframe_count
nc_transition_keyframe_get  nc_remove_transition_keyframe
nc_transition_duration_us
```

**分析（2）**
```
nc_scene_detect  nc_beat_detect
```

**镜头追踪（6）**
```
nc_set_camera_tracking  nc_set_camera_tracking_scaled  nc_tracking_point_count
nc_tracking_point_get  nc_tracking_point_get_scaled  nc_clear_camera_tracking
```

**画幅 / 背景 / 深度（6）**
```
nc_set_frame_fit  nc_get_frame_fit  nc_set_background_color
nc_set_depth_effect  nc_get_depth_effect  nc_free_buffer
```

**内存（2）**
```
nc_free_string  nc_free_string_array
```

### P2 音频混音（7）
```
nc_set_track_solo  nc_set_track_gain  nc_get_track_mixer
nc_set_master_gain  nc_get_master_gain
nc_set_track_auto_duck  nc_get_track_auto_duck
```

### P3 AI（32）

**任务契约（10）**
```
nc_ai_feature_access  nc_ai_task_status  nc_ai_task_begin
nc_ai_task_progress  nc_ai_task_succeed  nc_ai_task_fail
nc_ai_task_cancel  nc_ai_task_reset  nc_ai_task_failure_text
nc_ai_engine_info
```

**深度图（5）**
```
nc_depth_load  nc_depth_unload  nc_depth_loaded
nc_depth_estimate  nc_depth_apply_effect
```

**抠像（4）**
```
nc_matting_load  nc_matting_unload  nc_matting_loaded  nc_matting_estimate
```

**人物特效（1）**
```
nc_person_effect_apply
```

**语音（5）**
```
nc_speech_load  nc_speech_unload  nc_speech_loaded
nc_speech_transcribe  nc_speech_mel
```

**检查 / 自动剪辑 / 色度抠像（7）**
```
nc_ai_check  nc_ai_check_fix_all  nc_auto_edit
nc_set_clip_chroma_key  nc_get_clip_chroma_key  nc_chroma_key_estimate  nc_matting_access
```

### P4 导出 + 会员 + 运维（66）

**导出（6）**
```
nc_export_run  nc_export_presets  nc_export_preset_info
nc_export_estimate  nc_export_setup_preset  nc_would_clamp_export
```

**导出队列（7）**
```
nc_export_enqueue  nc_export_poll_next  nc_export_job_state
nc_export_job_progress  nc_export_job_update  nc_export_queue_summary
nc_export_queue_cancel_all
```

**权益（5）**
```
nc_set_entitlement  nc_get_entitlement  nc_can_add_track
nc_reference_style_access  nc_movie_version_access
```

**水印（3）**
```
nc_watermark_account_hash  nc_watermark_set_export  nc_watermark_detect
```

**模型管理（17）**
```
nc_model_catalog  nc_model_info  nc_model_status  nc_model_access_allowed
nc_model_download_begin  nc_model_download_progress  nc_model_download_pause
nc_model_download_resume  nc_model_download_cancel  nc_model_download_finish
nc_model_download_fail  nc_model_download_state  nc_model_resume_plan
nc_model_install  nc_model_remove  nc_model_storage_stats  nc_model_decrypt
```

**代理（5）**
```
nc_proxy_check_import  nc_proxy_set_record  nc_proxy_get_record
nc_proxy_usage  nc_proxy_clear_records
```

**离线重链（6）**
```
nc_media_scan_offline  nc_media_apply_offline_flags  nc_media_relink
nc_media_find_relink_candidates  nc_media_relink_batch  nc_media_set_digest
```

**章节（6）**
```
nc_chapter_detect  nc_chapter_apply  nc_chapter_count
nc_chapter_get  nc_chapter_rename  nc_chapter_clear
```

**智能成片（4）**
```
nc_smart_movie_plan  nc_smart_movie_apply
nc_regeneration_impact  nc_regeneration_clear
```

**口播精剪（2）**
```
nc_speech_analyze  nc_speech_cut_apply
```

**心跳（5）**
```
nc_heartbeat_handle  nc_heartbeat_network_error  nc_heartbeat_status
nc_heartbeat_periodic_due  nc_heartbeat_clear_lock
```

---

## 6. 沙箱环境与模型资产（实测）

| 模型 | 大小 | IR | 用途 | 实测 |
|---|---|---|---|---|
| `depth_anything_v2_small.onnx` | 94.5MB | v9 | 深度图 | 推理成功（0.68~5.00） |
| `modnet.onnx` | 24.7MB | v6 | 人像抠像 | 成功（人像171/背景0.001） |
| `whisper_tiny_encoder.onnx` | 31.4MB | v7 | 语音识别 | 成功 |
| `whisper_tiny_decoder.onnx` | 113MB | v9 | 语音解码 | 自回归成功 |
| `whisper_tiny_tokenizer.json` | 2.4MB | - | BPE | 中文正确（"我"=1654） |
| `rife47.onnx` | 20.5MB | v9 | 光流补帧 | 成功 |
| `uvr_main_340.onnx` | 63.7MB | v6 | 人声分离 | 成功 |
| `silero_vad.onnx` | 2.2MB | v8 | VAD | 加载成功 |
| Real-ESRGAN x4 | 63.8MB | - | 超分 | 需转 ONNX |
| NLLB-600M | 1.5GB | v6 | 翻译 | 需量化 |

ONNX Runtime：`/workspace/deps/onnxruntime-linux-aarch64-1.23.0/`（1.23.0，IR ≤ 10+）

### 构建命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DNAIROCLIP_ORT_ROOT=/workspace/deps/onnxruntime-linux-aarch64-1.23.0
cmake --build build --parallel 4
cd build && ./tests/nairoclip_tests      # 1132 cases, 0 failed

# 无 FFmpeg（降级路径）
cmake -S . -B build_noffmpeg -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
      -DNAIROCLIP_ORT_ROOT=OFF
cmake --build build_noffmpeg --parallel 4
./build_noffmpeg/tests/nairoclip_tests   # 1087 cases, 0 failed

# 设计桥
bash design/tools/build_harness.sh build
./design/tools/nc_design_harness 8777 design
```

---

## 7. 平台层对接（不要用 HTTP）

| 平台 | 方式 | 说明 |
|---|---|---|
| **Android** | Kotlin + JNI 直调 `nc_*` | NDK 交叉编译（已有 CI 产出 3 ABI） |
| **iOS** | Swift + C interop | 需 macOS + Xcode |
| **桌面 Qt** | C++ 直调 | 无中间层 |
| **Web/WASM** | 需 WASM 化 core | **当前不支持** |

### Android 交叉编译产物（已产出）

```
Release tag: android-builds
  core-android-arm64-v8a.tar.gz     9.29MB
  core-android-armeabi-v7a.tar.gz   8.46MB
  core-android-x86_64.tar.gz        8.85MB
```

### 调用契约

```
· 单个 NcEditor* 不支持多线程并发（平台层串行化）
· char* 返回值用 nc_free_string 释放
· uint8_t*/double* 数组用 nc_free_buffer 释放
· 长任务（导出/AI 推理）必须在工作线程执行
· AI 推理耗时：单帧 100ms~3s（CPU）
```

---

## 8. 不会冲突的保证

| 关注点 | 结论 | 依据 |
|---|---|---|
| C ABI 版本 | 稳定 | `NC_ABI_VERSION = 1`；新增函数不递增版本 |
| 新增接口破坏 UI？ | 不会 | 全部为追加（如 `_scaled` 变体） |
| 枚举扩展破坏序列化？ | 不会 | 只追加不重排（旧 .nclip 值不变） |
| 桥与 core 同步 | 已同步 | 87 个引用全存在（实测） |

---

## 9. 验收清单（阶段 A 完成后勾选）

### P0 剪辑核心

```
□ 打开工程 → /api/state 返回真实数据
□ 插入片段 → clip_count +1
□ 分割 → 两段 + 预览变化
□ 移动 → 起点变化
□ 跨轨移动 → 换轨成功
□ 裁剪起/终点 → 时长变化
□ 删除 → 片段消失
□ 波纹删除 → 后续片段前移
□ 复制 → 新增副本
□ 定格 → 静止片段
□ 倒放 → reversed=true（序列化往返）
□ 变速 → 时长按比例
□ 变速曲线 → 曲线点可读写
□ 变速预设 → 6 预设可用
□ 不透明度 → 预览像素可见变化
□ 音量/淡化 → 混音输出变化
□ 降噪 → 噪声段 RMS 下降
□ 滤镜 → 色调变化
□ 蒙版 → 区域裁剪生效
□ 混合模式 → 公式生效
□ 背景填充 → 留白区域填充
□ 特效增删 → 列表与画面同步
□ 关键帧 → 参数随时间变化
□ 字幕 → 时间轴 + 预览文本
□ 标记 → 时间轴标记点
□ 轨道锁定/静音/隐藏 → 状态生效
□ 轨道增益 → 混音变化
□ 撤销/重做 → 状态与按钮态同步
□ 撤销深度 20 步 → 达上限后不可撤销
□ 保存/载入 → 往返一致
```

### P1 时间轴 + 预览

```
□ 预览帧 → /api/frame → 真实解码（非纯色）
□ 缩略图 → /api/thumb → 真实序列
□ 波形 → /api/waveform → 真实桶数据
□ 场景检测 → 边界时间点
□ 节拍检测 → BPM + 节拍数组
□ 转场添加 → 转场图标出现
□ 转场库 → 34 种 + 9 分类
□ 转场关键帧 → 区间内可读写
□ 镜头追踪 → 锚点随时间插值
□ 画幅切换 → 预览比例变化
□ 画布比例 → 预览尺寸变化
```

---

## 10. 未决事项（需确认）

1. **推进批次**：P0+P1 优先（建议）还是全量 232？
2. **达标定义**：API 全量直通 + 核心 UI 入口（建议）还是仅其一？
3. **依赖项处理**：如实标注 + 链路打通（建议）还是 mock？
4. **设计产物入库**：`design/`+`mobile/`+`ui/` 共 99MB，是否 `.gitignore`？
5. **目标平台**：Android / iOS / 桌面 / Web？

---

**文档版本**：v1.0（2026-10-24）
**对应提交**：`13a1b4e`
**维护约定**：每次新增 C ABI 后同步更新 §5 与 §9
