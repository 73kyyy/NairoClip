# NairoClip JNI op 清单与归类审计（P2 · T2.1）

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-P2-OP-INVENTORY |
| 审计对象 | `mobile/native/android/nc_jni.cpp` 的 `DispatchEdit()` |
| 基线 | main @ `cba18dcb` |
| 审计方式 | 静态解析 `if (op == "...")` 分支 + 分支体内 `nc_*` 调用点 |
| op 总数 | **112** |

> **性质说明**：本文是只读审计结果，不改任何代码。
> 归类依据是「分支体内实际调用的 C ABI 函数名 + 是否修改工程状态」，
> **属于启发式判定**，T2.2 实施时必须逐条人工确认。
> 标 `待审计` 的条目证据不足，不作推断。

## 1. 归类统计

| 类别 | 数量 | 含义 |
|---|---|---|
| Command | 76 | 修改工程/时间轴状态，应走命令信封 |
| Query | 25 | 只读查询，应走 Query 契约并带 revision |
| Task | 8 | 异步任务生命周期，应走 Task 契约（taskId/进度/取消） |
| 系统 | 2 | 缓存/模型等进程级状态，不属于工程数据 |
| 待审计 | 1 | 证据不足 |
| **合计** | **112** | — |

## 2. 关键发现

1. **单函数 112 个字符串分支**集中在 `DispatchEdit`，与 03 §6「禁止仅凭函数名字符串判断业务权限」直接冲突。
2. 其中 **76 个是写操作**，全部绕过契约层（无 commandId / expectedRevision / 事件）。
3. **8 个 AI 任务 op**（`nc_ai_task_*`）已自带任务语义，却没有 taskId 契约——应优先并入 P1 的 `TaskRegistry`。
4. 存在 **同名透传 op**（如 `op == "nc_add_track"` 直接转发 `nc_add_track`），说明前端与 ABI 已耦合到函数名。
5. `frame_fit` 分支**未调用任何 C ABI**，需确认是纯前端状态还是死分支。

## 3. Command（76）

| op | 调用点 | 入参键 |
|---|---|---|
| `add_track` | `nc_add_track` | name, type |
| `adjust_effect_add` | `nc_adjustment_add_effect` | track_index, type, value |
| `adjust_effect_remove` | `nc_adjustment_remove_effect` | effect_id, track_index |
| `adjust_lut` | `nc_adjustment_set_lut` | path, strength, track_index |
| `audio` | `nc_set_clip_audio` | fade_in_us, fade_out_us, gain_db |
| `beat_sync` | `nc_beat_sync_apply`, `nc_free_string` | asset_ids, audio_track, min_shot_seconds, prefer_transitions, target_track, usable_seconds |
| `blend` | `nc_set_clip_blend_mode` | mode |
| `canvas` | `nc_set_project_canvas` | height, width |
| `chroma` | `nc_set_clip_chroma_key` | enabled, inner, key, outer, quality, spill |
| `delete` | `nc_remove_clip` | — |
| `denoise` | `nc_set_clip_denoise` | strength |
| `duplicate` | `nc_duplicate_clip` | — |
| `effect_add` | `nc_add_effect` | type, value |
| `effect_enabled` | `nc_set_effect_enabled` | effect_id, enabled |
| `effect_move` | `nc_move_effect` | direction, effect_id |
| `effect_remove` | `nc_remove_effect` | effect_id |
| `effect_update` | `nc_update_effect` | effect_id, type, value |
| `enabled` | `nc_set_clip_enabled` | enabled |
| `filter` | `nc_set_clip_filter` | intensity, style |
| `freeze` | `nc_freeze_frame` | at_us, duration_us |
| `group_create` | `nc_create_track_group` | name, track_indices |
| `group_flag` | `nc_set_group_hidden`, `nc_set_group_locked`, `nc_set_group_muted` | flag, group_id, value |
| `group_remove` | `nc_remove_track_group` | group_id |
| `keyframe` | `nc_insert_keyframe` | at_us, prop, property, t, v |
| `lut` | `nc_set_clip_lut` | path, strength |
| `marker` | `nc_add_marker` | color, name, time_us |
| `mask` | `nc_set_clip_mask` | feather, height, invert, radius, rotation, shape, width, x |
| `move` | `nc_move_clip` | start_us |
| `move_to_track` | `nc_move_clip_to_track` | start_us, to_track |
| `nc_add_effect` | `nc_add_effect` | type, value |
| `nc_add_text_clip` | `nc_add_text_clip` | alignment, anchor_x, anchor_y, color, duration_us, font_size, start_us, text |
| `nc_add_track` | `nc_add_track` | name, type |
| `nc_adjustment_add_effect` | `nc_adjustment_add_effect` | track_index, type, value |
| `nc_adjustment_remove_effect` | `nc_adjustment_remove_effect` | effect_id, track_index |
| `nc_adjustment_set_lut` | `nc_adjustment_set_lut` | path, strength, track_index |
| `nc_asset_reorder` | `nc_asset_reorder` | from_index, to_index |
| `nc_extract_audio` | `nc_extract_audio`, `nc_free_string` | asset_id, out_path |
| `nc_import_asset` | `nc_import_asset` | duration_us, name, path, type |
| `nc_insert_clip` | `nc_insert_clip` | asset_id, duration_us, start_us, track_index |
| `nc_insert_keyframe` | `nc_insert_keyframe` | property, time_us, value |
| `nc_move_track` | `nc_move_track` | new_index, track_index |
| `nc_ref_apply` | `nc_free_string`, `nc_ref_apply` | asset_ids, contrast, durations, effect_amount, effect_kinds, lut_preset, lut_strength, saturation |
| `nc_remove_asset` | `nc_remove_asset` | asset_id |
| `nc_remove_keyframe` | `nc_remove_keyframe` | property, time_us |
| `nc_set_background_color` | `nc_set_background_color` | rgba |
| `nc_set_clip_blend_mode` | `nc_set_clip_blend_mode` | mode |
| `nc_set_clip_opacity` | `nc_set_clip_opacity` | opacity |
| `nc_set_clip_text` | `nc_set_clip_text` | text |
| `nc_set_keyframe_easing` | `nc_set_keyframe_easing` | c1x, c1y, c2x, c2y, easing, property, time_us |
| `nc_set_text_style` | `nc_set_text_style` | alignment, anchor_x, anchor_y, color, font_size, outline_color, outline_width |
| `nc_set_track_view` | `nc_set_track_view` | collapsed, height_px, track_index |
| `nc_surge_preset_apply` | `nc_free_string`, `nc_surge_preset_apply` | clip_id, intensity, preset_index, track_index |
| `nc_surge_preset_name` | `nc_surge_preset_name` | index |
| `nc_template_apply` | `nc_free_string`, `nc_template_apply` | count, material_ids, template_id |
| `nc_template_apply_slots` | `nc_free_string`, `nc_template_apply_slots` | count, material_ids, slot_indices, template_id |
| `opacity` | `nc_set_clip_opacity` | value |
| `redo` | `nc_redo` | — |
| `remove_keyframe` | `nc_remove_keyframe` | at_us, prop, property, t |
| `remove_transition` | `nc_remove_transition` | transition |
| `rename_project` | `nc_set_project_name` | name |
| `rename_track` | `nc_rename_track` | name |
| `reverse` | `nc_reverse_clip` | — |
| `ripple_delete` | `nc_ripple_delete` | — |
| `shake_set` | `nc_set_clip_shake` | amplitude, decay_seconds, frequency, phase, seed, trajectory |
| `speed` | `nc_set_clip_speed` | den, num, speed |
| `speed_curve` | `nc_set_clip_speed_curve` | — |
| `split` | `nc_split_clip` | time_us |
| `track_flag` | `nc_set_track_hidden`, `nc_set_track_locked`, `nc_set_track_muted`, `nc_set_track_solo` | flag, value |
| `track_gain` | `nc_set_track_gain` | gain_db |
| `transform_clear` | `nc_clear_clip_transform` | — |
| `transform_set` | `nc_set_clip_transform` | anchor, enabled, pos_x, pos_y, rotation, scale |
| `transition` | `nc_add_transition` | duration_us, from_clip, to_clip, type |
| `trim_start` | `nc_trim_clip_end`, `nc_trim_clip_start` | time_us |
| `undo` | `nc_undo` | — |
| `xform_clear` | `nc_clear_clip_xform` | kind |
| `xform_set` | `nc_set_clip_xform` | args, kind |

## 4. Query（25）

| op | 调用点 | 入参键 |
|---|---|---|
| `adjust_effect_get` | `nc_adjustment_effect_get` | index, track_index |
| `nc_adjustment_get` | `nc_adjustment_get`, `nc_free_string` | track_index |
| `nc_ai_feature_access` | `nc_ai_feature_access`, `nc_free_string` | feature, logged_in |
| `nc_asset_is_referenced` | `nc_asset_is_referenced` | asset_id |
| `nc_beat_detect` | `nc_beat_detect`, `nc_free_buffer` | track_index |
| `nc_blend_mode_count` | `nc_blend_mode_count` | — |
| `nc_blend_mode_name` | `nc_blend_mode_name` | mode |
| `nc_effect_spec` | `nc_effect_spec`, `nc_free_string` | type |
| `nc_keyframe_count` | `nc_keyframe_count` | property |
| `nc_keyframe_get` | `nc_keyframe_get` | index, property |
| `nc_keyframe_get_ex` | `nc_keyframe_get_ex` | index, property |
| `nc_lut_validate_file` | `nc_free_string`, `nc_lut_validate_file` | path |
| `nc_preview_cache_stats` | `nc_preview_cache_stats` | — |
| `nc_scene_detect` | `nc_free_buffer`, `nc_scene_detect` | max_samples, step_seconds |
| `nc_surge_preset_count` | `nc_surge_preset_count` | — |
| `nc_surge_preset_describe` | `nc_free_string`, `nc_surge_preset_describe` | index |
| `nc_template_list` | `nc_free_string`, `nc_template_list` | — |
| `nc_track_accepts_media` | `nc_track_accepts_media` | media_type, track_type |
| `nc_track_placement_reason` | `nc_free_string`, `nc_track_placement_reason` | media_type, track_type |
| `nc_transition_category` | `nc_transition_category` | type |
| `nc_transition_type_count` | `nc_transition_type_count` | — |
| `nc_transition_type_name` | `nc_transition_type_name` | type |
| `shake_get` | `nc_get_clip_shake` | — |
| `transform_get` | `nc_get_clip_transform` | — |
| `xform_get` | `nc_get_clip_xform` | kind |

## 5. Task（8）

| op | 调用点 | 入参键 |
|---|---|---|
| `nc_ai_task_begin` | `nc_ai_task_begin`, `nc_free_string` | feature, target_id |
| `nc_ai_task_cancel` | `nc_ai_task_cancel` | — |
| `nc_ai_task_fail` | `nc_ai_task_fail` | reason, retryable |
| `nc_ai_task_progress` | `nc_ai_task_progress` | progress, stage_note |
| `nc_ai_task_reset` | `nc_ai_task_reset` | — |
| `nc_ai_task_status` | `nc_ai_task_status`, `nc_free_string` | — |
| `nc_ai_task_succeed` | `nc_ai_task_succeed` | result_payload |
| `nc_ref_analyze` | `nc_free_string`, `nc_ref_analyze` | asset_id |

## 6. 系统（2）

| op | 调用点 | 入参键 |
|---|---|---|
| `nc_model_remove` | `nc_model_remove` | model_id |
| `nc_preview_invalidate` | `nc_free_buffer`, `nc_preview_invalidate` | — |

## 7. 待审计（1）

| op | 调用点 | 入参键 |
|---|---|---|
| `frame_fit` | — | — |

## 8. T2.2 下一步（实施前置）

1. 逐条确认归类，特别是 `待审计` 与 `系统` 两类。
2. 为 76 个 Command 补充 `commandType` 映射（如 `split` → `clip.split`）。
3. 为 25 个 Query 定义返回结构（含 revision）。
4. 把 8 个 `nc_ai_task_*` 迁到 `TaskRegistry`。
5. 确认 `frame_fit` 是否是死分支。

*本清单由静态解析生成，行号未固定（文件后续会变动）；以 op 名为准。*