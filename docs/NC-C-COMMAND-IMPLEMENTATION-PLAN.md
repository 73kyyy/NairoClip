# NairoClip C 阶段第二阶段：统一 Command 契约与执行体系实施方案 V1.0

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-C-COMMAND-IMPLEMENTATION-PLAN |
| 阶段 | C 第二阶段（实施方案编制，**不含源码修改授权**） |
| 基线 | `main` @ `0cb302fbeee7f6176f249db0268d64c624b60fd2` |
| 依据 | `docs/NC-C-COMMAND-AUDIT.md`（76 项逐条审计） |
| 状态 | 待审阅 |

---

## 1. 现状与问题

### 1.1 审计结论（引用，不重复审计）

| 指标 | 数值 |
|---|---|
| Command 类 op | 76 |
| 去重后独立 C ABI | 67（9 组重复） |
| 经 `TimelineEditor` 门面（可撤销） | 71 |
| **直接写字段（绕过命令与撤销）** | **3** |
| 误归类只读查询 | 1（`nc_surge_preset_name`） |
| 非工程写操作 | 1（`nc_extract_audio`） |

### 1.2 必须解决的问题

| ID | 问题 | 现状证据 |
|---|---|---|
| P-1 | 3 个写操作绕过命令体系 | `capi.cpp` 中 `project.name` / `Sequence::width/height` / `Sequence::backgroundRgba` 直接赋值 |
| P-2 | 9 组重复 op 产生双重审计身份 | `nc_jni.cpp` 两套命名指向同一 ABI |
| P-3 | 命令信封未覆盖全部写路径 | 仅 `nc_apply_command` 走信封，其余 76 个 op 直连 C ABI |
| P-4 | 事件只在契约路径发布 | 旧 op 成功不产生任何事件 |
| P-5 | `nc_ref_apply` 事务语义未进契约 | 内部 `beginTransaction/endTransaction` 与 dispatcher 无关联 |
| P-6 | `nc_extract_audio` 归类错误 | 实为文件操作，却挂在 Command 下 |

### 1.3 现有可复用资产（不重复造）

- `contract::CommandRequest / CommandResult / ErrorDetail / EventBus / TaskRegistry`（P1 + T2.2）
- `TimelineEditor` 59 个门面方法 + `UndoStack`（事务、合并、`editRevision`）
- `CommandDispatcher`（当前仅 3 个命令类型）

---

## 2. 76 个 op 完整映射

> 列说明：**对外稳定名**为规范 `nc_*` 语义名；短名为兼容别名。
> **改工程/rev/事件/Undo** 均指接入后的目标行为（现状见 §1）。
> **兼容风险**：高=行为或语义会变；中=命名收敛或事务；低=纯包装。

| # | 原始 op | 对外稳定名 | commandType | C ABI | C++ 实现 | 入参 | 返回 | 改工程 | rev | 事件 | Undo | 风险 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `split` | `nc_split_clip` | `clip.split` | `nc_split_clip` | splitClip | time_us | int32_t | 是 | 是 | 是 | 是 | 低 |
| 2 | `delete` | `nc_remove_clip` | `clip.remove` | `nc_remove_clip` | removeClip | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 3 | `trim_start` | `nc_trim_clip_end` | `clip.trim.start` | `nc_trim_clip_end` | trimClipStart/End | time_us | int32_t | 是 | 是 | 是 | 是 | 低 |
| 4 | `move` | `nc_move_clip` | `clip.move` | `nc_move_clip` | moveClip | start_us | int32_t | 是 | 是 | 是 | 是 | 低 |
| 5 | `enabled` | `nc_set_clip_enabled` | `clip.enabled.set` | `nc_set_clip_enabled` | setClipEnabled | enabled | int32_t | 是 | 是 | 是 | 是 | 低 |
| 6 | `opacity` | `nc_set_clip_opacity` | `clip.opacity.set` | `nc_set_clip_opacity` | setClipOpacity | value | int32_t | 是 | 是 | 是 | 是 | 中 |
| 7 | `filter` | `nc_set_clip_filter` | `clip.filter.set` | `nc_set_clip_filter` | setClipFilter | intensity,style | int32_t | 是 | 是 | 是 | 是 | 低 |
| 8 | `rename_project` | `nc_set_project_name` | `project.rename` | `nc_set_project_name` | 直接写 Project::name | name | int32_t | 是 | 否 | 是（补齐） | 否 | 高 |
| 9 | `rename_track` | `nc_rename_track` | `track.rename` | `nc_rename_track` | renameTrack | name | int32_t | 是 | 是 | 是 | 是 | 低 |
| 10 | `nc_add_track` | `nc_add_track` | `track.add` | `nc_add_track` | addTrack | name,type | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 11 | `nc_add_text_clip` | `nc_add_text_clip` | `text.clip.add` | `nc_add_text_clip` | addTrack+insertClip | alignment,anchor_x,anchor_y,color,duration_us | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 12 | `nc_set_clip_text` | `nc_set_clip_text` | `text.clip.set` | `nc_set_clip_text` | setClipText | text | int32_t | 是 | 是 | 是 | 是 | 低 |
| 13 | `nc_set_text_style` | `nc_set_text_style` | `text.style.set` | `nc_set_text_style` | setClipText | alignment,anchor_x,anchor_y,color,font_size | int32_t | 是 | 是 | 是 | 是 | 低 |
| 14 | `nc_set_clip_blend_mode` | `nc_set_clip_blend_mode` | `clip.blend.set` | `nc_set_clip_blend_mode` | setClipBlendMode | mode | int32_t | 是 | 是 | 是 | 是 | 低 |
| 15 | `nc_add_effect` | `nc_add_effect` | `effect.add` | `nc_add_effect` | addEffect | type,value | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 16 | `nc_set_clip_opacity` | `nc_set_clip_opacity` | `clip.opacity.set` | `nc_set_clip_opacity` | setClipOpacity | opacity | int32_t | 是 | 是 | 是 | 是 | 低 |
| 17 | `nc_insert_keyframe` | `nc_insert_keyframe` | `keyframe.insert` | `nc_insert_keyframe` | insertKeyframe | property,time_us,value | int32_t | 是 | 是 | 是 | 是 | 低 |
| 18 | `add_track` | `nc_add_track` | `track.add` | `nc_add_track` | addTrack | name,type | uint64_t | 是 | 是 | 是 | 是 | 中 |
| 19 | `track_flag` | `nc_set_track_hidden` | `track.flag.set` | `nc_set_track_hidden` | setTrackHidden/Locked/Muted/Solo | flag,value | int32_t | 是 | 是 | 是 | 是 | 低 |
| 20 | `track_gain` | `nc_set_track_gain` | `track.gain.set` | `nc_set_track_gain` | setTrackGain | gain_db | int32_t | 是 | 是 | 是 | 是 | 低 |
| 21 | `duplicate` | `nc_duplicate_clip` | `clip.duplicate` | `nc_duplicate_clip` | insertClip | — | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 22 | `move_to_track` | `nc_move_clip_to_track` | `clip.move_to_track` | `nc_move_clip_to_track` | moveClipToTrack | start_us,to_track | int32_t | 是 | 是 | 是 | 是 | 低 |
| 23 | `ripple_delete` | `nc_ripple_delete` | `clip.ripple_delete` | `nc_ripple_delete` | rippleDelete | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 24 | `remove_keyframe` | `nc_remove_keyframe` | `keyframe.remove` | `nc_remove_keyframe` | removeKeyframe | at_us,prop,property,t | int32_t | 是 | 是 | 是 | 是 | 中 |
| 25 | `remove_transition` | `nc_remove_transition` | `transition.remove` | `nc_remove_transition` | removeTransition | transition | int32_t | 是 | 是 | 是 | 是 | 低 |
| 26 | `marker` | `nc_add_marker` | `marker.add` | `nc_add_marker` | upsertMarker | color,name,time_us | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 27 | `undo` | `nc_undo` | `history.undo` | `nc_undo` | undo | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 28 | `redo` | `nc_redo` | `history.redo` | `nc_redo` | redo | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 29 | `transform_set` | `nc_set_clip_transform` | `clip.transform.set` | `nc_set_clip_transform` | setClipTransform | anchor,enabled,pos_x,pos_y,rotation | int32_t | 是 | 是 | 是 | 是 | 低 |
| 30 | `transform_clear` | `nc_clear_clip_transform` | `clip.transform.clear` | `nc_clear_clip_transform` | → nc_set_clip_transform | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 31 | `blend` | `nc_set_clip_blend_mode` | `clip.blend.set` | `nc_set_clip_blend_mode` | setClipBlendMode | mode | int32_t | 是 | 是 | 是 | 是 | 中 |
| 32 | `speed` | `nc_set_clip_speed` | `clip.speed.set` | `nc_set_clip_speed` | setClipSpeed | den,num,speed | int32_t | 是 | 是 | 是 | 是 | 低 |
| 33 | `speed_curve` | `nc_set_clip_speed_curve` | `clip.speed_curve.set` | `nc_set_clip_speed_curve` | setClipSpeedCurve | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 34 | `effect_add` | `nc_add_effect` | `effect.add` | `nc_add_effect` | addEffect | type,value | uint64_t | 是 | 是 | 是 | 是 | 中 |
| 35 | `effect_update` | `nc_update_effect` | `effect.update` | `nc_update_effect` | addEffect | effect_id,type,value | int32_t | 是 | 是 | 是 | 是 | 低 |
| 36 | `effect_enabled` | `nc_set_effect_enabled` | `effect.enabled.set` | `nc_set_effect_enabled` | addEffect | effect_id,enabled | int32_t | 是 | 是 | 是 | 是 | 低 |
| 37 | `effect_move` | `nc_move_effect` | `effect.move` | `nc_move_effect` | moveEffect | direction,effect_id | int32_t | 是 | 是 | 是 | 是 | 低 |
| 38 | `effect_remove` | `nc_remove_effect` | `effect.remove` | `nc_remove_effect` | removeEffect | effect_id | int32_t | 是 | 是 | 是 | 是 | 低 |
| 39 | `mask` | `nc_set_clip_mask` | `clip.mask.set` | `nc_set_clip_mask` | setClipMask | feather,height,invert,radius,rotation | int32_t | 是 | 是 | 是 | 是 | 低 |
| 40 | `chroma` | `nc_set_clip_chroma_key` | `clip.chroma.set` | `nc_set_clip_chroma_key` | setClipChromaKey | enabled,inner,key,outer,quality | int32_t | 是 | 是 | 是 | 是 | 低 |
| 41 | `denoise` | `nc_set_clip_denoise` | `clip.denoise.set` | `nc_set_clip_denoise` | setClipDenoise | strength | int32_t | 是 | 是 | 是 | 是 | 低 |
| 42 | `audio` | `nc_set_clip_audio` | `clip.audio.set` | `nc_set_clip_audio` | setClipAudio | fade_in_us,fade_out_us,gain_db | int32_t | 是 | 是 | 是 | 是 | 低 |
| 43 | `keyframe` | `nc_insert_keyframe` | `keyframe.insert` | `nc_insert_keyframe` | insertKeyframe | at_us,prop,property,t,v | int32_t | 是 | 是 | 是 | 是 | 中 |
| 44 | `freeze` | `nc_freeze_frame` | `clip.freeze` | `nc_freeze_frame` | insertClip(freeze) | at_us,duration_us | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 45 | `reverse` | `nc_reverse_clip` | `clip.reverse` | `nc_reverse_clip` | setClipReversed | — | int32_t | 是 | 是 | 是 | 是 | 低 |
| 46 | `transition` | `nc_add_transition` | `transition.add` | `nc_add_transition` | addTransition | duration_us,from_clip,to_clip,type | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 47 | `canvas` | `nc_set_project_canvas` | `project.canvas.set` | `nc_set_project_canvas` | 直接写 Sequence::width/height | height,width | int32_t | 是 | 否 | 是（补齐） | 否 | 高 |
| 48 | `lut` | `nc_set_clip_lut` | `clip.lut.set` | `nc_set_clip_lut` | setClipLut | path,strength | int32_t | 是 | 是 | 是 | 是 | 低 |
| 49 | `nc_adjustment_add_effect` | `nc_adjustment_add_effect` | `track.adjustment.effect.add` | `nc_adjustment_add_effect` | setAdjustment | track_index,type,value | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 50 | `nc_adjustment_remove_effect` | `nc_adjustment_remove_effect` | `track.adjustment.effect.remove` | `nc_adjustment_remove_effect` | setAdjustment | effect_id,track_index | int32_t | 是 | 是 | 是 | 是 | 低 |
| 51 | `nc_adjustment_set_lut` | `nc_adjustment_set_lut` | `track.adjustment.lut.set` | `nc_adjustment_set_lut` | setAdjustment | path,strength,track_index | int32_t | 是 | 是 | 是 | 是 | 低 |
| 52 | `nc_remove_asset` | `nc_remove_asset` | `asset.remove` | `nc_remove_asset` | removeAsset | asset_id | int32_t | 是 | 是 | 是 | 是 | 低 |
| 53 | `nc_asset_reorder` | `nc_asset_reorder` | `asset.reorder` | `nc_asset_reorder` | reorderAssets | from_index,to_index | int32_t | 是 | 是 | 是 | 是 | 低 |
| 54 | `nc_set_track_view` | `nc_set_track_view` | `track.view.set` | `nc_set_track_view` | setTrackHeight | collapsed,height_px,track_index | int32_t | 是 | 是 | 是 | 是 | 低 |
| 55 | `adjust_effect_add` | `nc_adjustment_add_effect` | `track.adjustment.effect.add` | `nc_adjustment_add_effect` | setAdjustment | track_index,type,value | uint64_t | 是 | 是 | 是 | 是 | 中 |
| 56 | `adjust_effect_remove` | `nc_adjustment_remove_effect` | `track.adjustment.effect.remove` | `nc_adjustment_remove_effect` | setAdjustment | effect_id,track_index | int32_t | 是 | 是 | 是 | 是 | 中 |
| 57 | `adjust_lut` | `nc_adjustment_set_lut` | `track.adjustment.lut.set` | `nc_adjustment_set_lut` | setAdjustment | path,strength,track_index | int32_t | 是 | 是 | 是 | 是 | 中 |
| 58 | `group_create` | `nc_create_track_group` | `track_group.create` | `nc_create_track_group` | createTrackGroup | name,track_indices | int32_t | 是 | 是 | 是 | 是 | 低 |
| 59 | `group_remove` | `nc_remove_track_group` | `track_group.remove` | `nc_remove_track_group` | removeTrackGroup | group_id | int32_t | 是 | 是 | 是 | 是 | 低 |
| 60 | `group_flag` | `nc_set_group_hidden` | `track_group.flag.set` | `nc_set_group_hidden` | setGroupHidden/Locked/Muted | flag,group_id,value | int32_t | 是 | 是 | 是 | 是 | 低 |
| 61 | `nc_move_track` | `nc_move_track` | `track.move` | `nc_move_track` | moveTrack | new_index,track_index | int32_t | 是 | 是 | 是 | 是 | 低 |
| 62 | `nc_template_apply` | `nc_template_apply` | `template.apply` | `nc_template_apply` | applyTemplate(*) | count,material_ids,template_id | int32_t | 是 | 是 | 是 | 是 | 低 |
| 63 | `nc_surge_preset_name` | `nc_surge_preset_name` | `(Query) effect.surge.preset.list` | `nc_surge_preset_name` | 静态表查询（只读） | index | constchar* | 否 | 否 | 否 | 否 | 低 |
| 64 | `nc_surge_preset_apply` | `nc_surge_preset_apply` | `effect.surge.apply` | `nc_surge_preset_apply` | setClipEffects | clip_id,intensity,preset_index,track_index | int32_t | 是 | 是 | 是 | 是 | 低 |
| 65 | `nc_ref_apply` | `nc_ref_apply` | `reference.apply` | `nc_ref_apply` | 事务：insertClip+setClipSpeedCurve | asset_ids,contrast,durations,effect_amount,effect_kinds | int32_t | 是 | 是 | 是 | 是 | 中 |
| 66 | `nc_template_apply_slots` | `nc_template_apply_slots` | `template.apply_slots` | `nc_template_apply_slots` | applyTemplateSlots(*) | count,material_ids,slot_indices,template_id | int32_t | 是 | 是 | 是 | 是 | 低 |
| 67 | `nc_import_asset` | `nc_import_asset` | `asset.import` | `nc_import_asset` | importAsset | duration_us,name,path,type | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 68 | `nc_insert_clip` | `nc_insert_clip` | `clip.insert` | `nc_insert_clip` | insertClip | asset_id,duration_us,start_us,track_index | uint64_t | 是 | 是 | 是 | 是 | 低 |
| 69 | `shake_set` | `nc_set_clip_shake` | `clip.shake.set` | `nc_set_clip_shake` | setClipShake | amplitude,decay_seconds,frequency,phase,seed | int32_t | 是 | 是 | 是 | 是 | 低 |
| 70 | `xform_set` | `nc_set_clip_xform` | `clip.xform.set` | `nc_set_clip_xform` | setClipXform | args,kind | int32_t | 是 | 是 | 是 | 是 | 低 |
| 71 | `xform_clear` | `nc_clear_clip_xform` | `clip.xform.clear` | `nc_clear_clip_xform` | setClipXform | kind | int32_t | 是 | 是 | 是 | 是 | 低 |
| 72 | `beat_sync` | `nc_beat_sync_apply` | `edit.beat_sync.apply` | `nc_beat_sync_apply` | applyBeatSync(*) | asset_ids,audio_track,min_shot_seconds,prefer_transitions,target_track | int32_t | 是 | 是 | 是 | 是 | 低 |
| 73 | `nc_set_keyframe_easing` | `nc_set_keyframe_easing` | `keyframe.easing.set` | `nc_set_keyframe_easing` | insertKeyframe | c1x,c1y,c2x,c2y,easing | int32_t | 是 | 是 | 是 | 是 | 低 |
| 74 | `nc_extract_audio` | `nc_extract_audio` | `(Task) asset.audio.extract` | `nc_extract_audio` | 写文件（不改工程） | asset_id,out_path | int32_t | 否 | 否 | 否 | 否 | 低 |
| 75 | `nc_remove_keyframe` | `nc_remove_keyframe` | `keyframe.remove` | `nc_remove_keyframe` | removeKeyframe | property,time_us | int32_t | 是 | 是 | 是 | 是 | 低 |
| 76 | `nc_set_background_color` | `nc_set_background_color` | `project.background.set` | `nc_set_background_color` | 直接写 Sequence::backgroundRgba | rgba | int32_t | 是 | 否 | 是（补齐） | 否 | 高 |

**覆盖核对**：76 行，与审计清单一一对应，无遗漏。

---

## 3. 九组重复 op 收敛

### 3.1 映射表

| 组 | 旧短名 | 对外稳定名（规范） | commandType |
|---|---|---|---|
| 1 | `opacity` | `nc_set_clip_opacity` | `clip.opacity.set` |
| 2 | `add_track` | `nc_add_track` | `track.add` |
| 3 | `blend` | `nc_set_clip_blend_mode` | `clip.blend.set` |
| 4 | `effect_add` | `nc_add_effect` | `effect.add` |
| 5 | `keyframe` | `nc_insert_keyframe` | `keyframe.insert` |
| 6 | `remove_keyframe` | `nc_remove_keyframe` | `keyframe.remove` |
| 7 | `adjust_effect_add` | `nc_adjustment_add_effect` | `track.adjustment.effect.add` |
| 8 | `adjust_effect_remove` | `nc_adjustment_remove_effect` | `track.adjustment.effect.remove` |
| 9 | `adjust_lut` | `nc_adjustment_set_lut` | `track.adjustment.lut.set` |

### 3.2 四个概念严格分离

| 概念 | 示例 | 约束 |
|---|---|---|
| C ABI 函数名 | `nc_set_clip_opacity` | 稳定，不随 UI 改名 |
| 对外 op 名 | `opacity` / `nc_set_clip_opacity` | 两套并存，别名表收敛 |
| 内部 commandType | `clip.opacity.set` | **唯一业务身份**，进入审计/事件/日志 |
| 旧接口别名 | `opacity` | 仅路由，不产生独立 commandType |

规则：
1. 同一业务操作**只能有一个** commandType；
2. 别名只做参数搬运，不得有自己的校验/状态逻辑；
3. 删除任何旧名必须先完成调用方迁移 + 全量回归（本阶段**不删**）。

---

## 4. 统一 Command 数据结构

### 4.1 请求（在 P1 基础上做**加法**，不改既有字段语义）

| 字段 | 类型 | 说明 | P1 已有 |
|---|---|---|---|
| `schemaVersion` | int32 | 契约版本，当前 1 | ✅ |
| `commandId` | string | 唯一，1..128 可打印 ASCII | ✅ |
| `commandType` | string | 稳定业务名（如 `clip.opacity.set`） | ✅ |
| `projectId` | uint64 | 工程作用域 | ✅ |
| `sequenceId` | uint64 | 序列作用域（0 = 工程级） | **新增** |
| `expectedRevision` | uint64 | 乐观并发；0 = 不校验 | ✅ |
| `transactionId` | string | 事务分组（可空） | ✅ |
| `payload` | JSON object | 版本化参数 | ✅ |
| `source` | enum | user/undo/redo/ai/plugin/import/sync/system | ✅（原名 `origin`） |
| `timestampMs` | int64 | 调用方时间戳 | **新增** |

**兼容策略**：新增字段全部有默认值；`schemaVersion` 保持 1（加法兼容）。
反序列化遇到未知字段忽略，缺失新增字段取默认值。

### 4.2 响应

| 字段 | 来源 | 说明 |
|---|---|---|
| `commandId` | 请求 | 回显 |
| `success` | `status == Accepted` | **新增**（语义化别名） |
| `errorCode` | `ErrorDetail::code` | 稳定码 `NC-*`，成功为空 |
| `errorMessage` | `ErrorDetail::message` | 用户可读 |
| `resultingRevision` | `TimelineEditor::editRevision()` | 成功后的真实 revision |
| `affectedEntities` | 命令执行结果 | 如 `clip:42`、`track:7`（**新增**） |
| `eventIds` | 实际发布的事件 | 便于前端对账（**新增**） |
| `resultPayload` | 命令返回值 | 如新建 clipId（**新增**） |
| 兼容旧字段 | `status`/`newRevision`/`changeSetId`/`undoAvailable`/`error` | 保留 |

---

## 5. Revision 机制

### 5.1 唯一权威来源

`TimelineEditor::editRevision()` → `UndoStack::editRevision()`。
**禁止**任何模块自建计数器或手动赋值。

### 5.2 递增规则

| 场景 | 是否递增 | 说明 |
|---|---|---|
| 命令成功且**状态实际改变** | ✅ | `UndoStack::push` 内部递增 |
| 命令成功但 `isNoOp()` | ❌ | 已有机制拦截 |
| 命令失败 | ❌ | `apply` 失败不入栈 |
| `undo` / `redo` | ✅ | 栈版本随内容变化递增 |
| 事务（一组命令） | 每条子命令各递增一次，`endTransaction` 后 revision 为终值 | 由 `UndoStack` 统一驱动 |
| 只读查询 | ❌ | 不触碰栈 |

### 5.3 冲突处理

1. 检查时机：**执行前**，`expectedRevision != 0` 时比较当前 `editRevision()`；
2. 不匹配 → `NC-PROJECT-REVISION_CONFLICT`，携带 `expected/current` 诊断；
3. 冲突命令**不得**产生任何副作用、不得发布事件、不得递增 revision。

### 5.4 防重复递增

- 每条命令只在顶层 `apply()` 调用一次门面方法；子方法不得再包一层 dispatcher。
- 事务内子命令由 `UndoStack` 统一管理，dispatcher 不额外 bump。

---

## 6. 事件机制

### 6.1 产生时机

仅在**命令成功且 revision 发生变化**时发布；失败、冲突、no-op 一律不发布。

### 6.2 事件结构（沿用 P1，新增 3 字段）

| 字段 | 说明 |
|---|---|
| `schemaVersion` / `eventId` / `eventType` / `projectId` / `revision` / `occurredAtMs` / `correlationId` / `payload` | P1 已有 |
| `revisionBefore` | **新增** 变更前 revision |
| `revisionAfter` | **新增** 变更后 revision |
| `affectedEntities` | **新增** 受影响实体列表 |

### 6.3 标识与顺序

- `eventId` = `<commandId>#<序号>`，事务内序号递增，保证唯一；
- `eventType`：工程结构变化用 `timeline.changed`，工程元数据用 `project.changed`；
- 顺序：单编辑器 FIFO；事务内按子命令顺序；
- `correlationId` = `commandId`，跨层可追踪。

### 6.4 Undo / Redo 事件

撤销/重做同样发布事件（`revisionBefore/After` 反映真实变化），
前端据 `revision` 判断是否需要重拉快照。

---

## 7. Undo / Redo 方案

| 要求 | 实现 |
|---|---|
| 命令可撤销 | 新命令实现 `Command::apply/revert`，压入既有 `UndoStack` |
| 失败不污染栈 | `apply` 失败不入栈（既有机制） |
| 事务整体撤销 | `beginTransaction(name)` / `endTransaction()`，事务内命令合并为一条历史 |
| 事务回滚 | 任一步失败 → 逆序 `revert` 已应用子命令，再退出事务 |
| 旧路径兼容 | 旧 `nc_*` 适配器内部仍走同一 `TimelineEditor`，历史记录形态不变 |

---

## 8. `nc_ref_apply` 事务语义

现状：`beginTransaction → insertClip → setClipSpeedCurve → endTransaction`。

方案：
1. `CommandRequest.transactionId` 透传到 `CommandDispatcher`；
2. dispatcher 遇到新 `transactionId` 时 `beginTransaction`，同 id 的后续命令复用；
3. 事务结束（显式 `commit` 或最后一条）→ `endTransaction`，整体成为一条撤销记录；
4. 任一步失败 → 逆序 `revert` 已应用命令 + `dropLast` 清理，**不留部分状态**；
5. 重复提交同一 `transactionId` → 幂等拒绝（`NC-PROJECT-DUPLICATE_TRANSACTION`）。

测试：成功、中途失败回滚、重复提交、撤销后重做。

---

## 9. `nc_extract_audio` 接入 Task

### 9.1 现状（诚实评估）

- 该函数**同步**执行：读素材 → 写音频文件 → 返回。
- **没有异步执行器**；若包装成 Task 却无进度来源，就是虚构。

### 9.2 方案

| 阶段 | 做法 |
|---|---|
| C 阶段 | 保留同步实现；新增 Task 记录时**只标记真实状态**：创建即运行、结束时 `complete/fail`，progress 只有 0→1 两个真实点，**不伪造中间进度** |
| 后续 | 引入后台执行器（独立线程/线程池）后，改为真正的异步任务，进度由编码器回调驱动 |

明确标注：**异步执行器未实现**，当前为「同步任务记录」。

### 9.3 任务契约

- `taskId` 统一由 `TaskRegistry` 生成；
- 结果类型 `asset.audio.extract`，载荷含输出路径、字节数、时长；
- 失败：`NC-MEDIA-*` / `NC-EXPORT-*` 稳定码 + 临时文件清理；
- 取消：沿用 T2.2 两阶段（`cancelRequested` → 执行器 `ack`）；同步路径下取消窗口仅存在于编码过程中；
- 资源：输出文件所有权归调用方（路径由调用方提供）；失败时删除 `.part` 临时文件。

---

## 10. 旧接口兼容迁移

| 层 | 迁移方式 |
|---|---|
| JS | 旧 `op` 名保留；`nc-api.js` 增加别名表；新代码用 `Engine.applyCommand` |
| Kotlin | 旧 `call(fn,args)` 保留；新增 `applyCommand` 走契约 |
| JNI | `DispatchEdit` 保留；内部对写 op 改为构造 `CommandRequest` 调 dispatcher |
| C ABI | 76 个 `nc_*` 全部保留；实现体改为 dispatcher 适配器 |
| 兼容层 | **只做参数搬运**，不得有独立业务逻辑 |

不删除任何旧接口，直到调用方全部迁移并完成回归。

---

## 11. 全平台架构

```
JS / Swift / Kotlin / Qt  (UI)
        │  统一 CommandRequest(JSON)
        ▼
   Editor API（契约层，与平台无关）
        │
   CommandDispatcher（校验/revision/事件/事务）
        │
   Project Core（C++，唯一事实源）
```

- 契约层**不依赖 Android/JNI**，只用 C++17 + JSON；
- 各平台通过各自桥接（JNI / Obj-C++ / C ABI）传入同一 `CommandRequest`；
- 错误码、事件、revision 语义跨平台一致。

---

## 12. 分批实施计划

| 批次 | 内容 | 产出 | 预估规模 |
|---|---|---|---|
| **B1** | 3 个绕过项：新增 `RenameProjectCommand` / `SetCanvasCommand` / `SetBackgroundColorCommand`，实现 apply/revert + 校验 | 3 个命令类 + 9 个用例 | M |
| **B2** | 9 组重复 op 收敛：别名表 + 单一 commandType，行为不变 | 别名映射 + 回归 | S |
| **B3** | 71 个门面 op 接入信封：`DispatchEdit` 构造请求 → dispatcher | 覆盖率 100% + 逐 op 用例 | L |
| **B4** | 事务与 Task：`nc_ref_apply` 事务化、`nc_extract_audio` Task 记录 | 事务/任务用例 | M |
| **B5** | 全链路与兼容：JS→Kotlin→JNI→C ABI→C++、旧接口回归、事件顺序 | 端到端用例 + 验收报告 | M |

每批次独立 PR、独立 CI、独立回滚。

---

## 13. 文件级修改清单

| 文件 | 批次 | 改动 |
|---|---|---|
| `core/include/nairoclip/core/contract/command.h` | B1 | 新增 `sequenceId`/`timestampMs`/`success`/`affectedEntities`/`eventIds`/`resultPayload` |
| `core/src/contract/command.cpp` | B1 | 序列化/校验适配 |
| `core/include/nairoclip/core/contract/dispatch.h` | B3 | 命令处理器注册表 |
| `core/src/contract/dispatch.cpp` | B3 | 76 个 commandType 的路由 |
| `core/include/nairoclip/core/commands.h` | B1 | 3 个新命令类声明 |
| `core/src/commands.cpp` | B1 | 3 个新命令实现 |
| `core/src/capi.cpp` | B2/B3 | 写 op 改为 dispatcher 适配器 |
| `core/include/nairoclip/core/capi.h` | B3 | 新增 `nc_command_catalog`（只读能力查询） |
| `mobile/native/android/nc_jni.cpp` | B2 | 别名收敛 |
| `mobile/assets/nc-api.js` | B2 | 别名表 |
| `tests/test_command_bypass_fix.cpp` | B1 | 3 个绕过项用例 |
| `tests/test_command_alias.cpp` | B2 | 别名收敛用例 |
| `tests/test_command_coverage.cpp` | B3 | 76 op 覆盖率 |
| `tests/test_command_transaction.cpp` | B4 | 事务原子性 |
| `tests/test_command_task_bridge.cpp` | B4 | 任务接入 |
| `docs/NC-C-COMMAND-IMPLEMENTATION-PLAN.md` | — | 本文档 |

---

## 14. 测试方案

| # | 测试 | 批次 | 判定 |
|---|---|---|---|
| 1 | 76 个 op 映射完整性 | B3 | 覆盖率 76/76，缺失即失败 |
| 2 | 9 组重复 op 兼容映射 | B2 | 同一 commandType，行为一致 |
| 3 | commandId 唯一性 | B1 | 重复提交被拒 |
| 4 | schemaVersion 兼容 | B1 | 旧版本可读，未来版本拒绝 |
| 5 | expectedRevision 冲突 | B1 | 返回 `NC-PROJECT-REVISION_CONFLICT` 且无副作用 |
| 6 | revision 正确递增 | B1 | 成功 +1 语义、失败不变 |
| 7 | 事件发布与顺序 | B3 | 成功才发、失败不发、顺序稳定 |
| 8 | Undo/Redo | B1 | 撤销重做后数据与 revision 一致 |
| 9 | 三个绕过项整改 | B1 | 改后撤销有效、事件已发 |
| 10 | `nc_ref_apply` 事务原子性 | B4 | 中途失败无残留 |
| 11 | `nc_extract_audio` Task 生命周期 | B4 | 状态/结果/清理正确 |
| 12 | 旧接口兼容 | B5 | 旧 JS/Kotlin 调用结果不变 |
| 13 | 参数校验 | B1/B3 | 非法输入返回稳定错误码 |
| 14 | 错误处理 | B3 | 错误码 → 用户文案链路 |
| 15 | 全链路 | B5 | JS→Kotlin→JNI→C ABI→C++ 一次闭环 |

另有：ASAN、UBSAN、Android APK 构建 + 冒烟、main 合并后 CI。

---

## 15. 风险分析与回滚

| ID | 风险 | 等级 | 缓解 | 回滚 |
|---|---|---|---|---|
| RK-1 | 76 op 一次性接入回归面过大 | 高 | 分 5 批，逐批 CI | 按批 revert |
| RK-2 | 事务回滚不彻底 | 高 | 预校验 + 逆序 revert + 专项测试 | 关闭事务开关 |
| RK-3 | 事件风暴影响 UI 性能 | 中 | 高频事件合并（P1 已有） | 事件开关 |
| RK-4 | 旧接口行为变化 | 中 | 适配器只搬运参数；对照测试 | 保留旧路径开关 |
| RK-5 | revision 双源 | 高 | 只认 `editRevision()`，禁止覆盖 | 删除自定义计数 |
| RK-6 | 画布变更影响预览/导出 | 中 | 专项测试 + 一致性断言 | 恢复旧画布命令 |

回滚单元 = 批次；每批一个 PR，可独立 revert。

---

## 16. 最终验收标准

1. **工程写操作绕过统一 Command 入口数量 = 0**（含 3 个直接写字段项）。
2. 76/76 op 有 commandType 且进入统一入口。
3. 9 组重复 op 收敛为 9 个唯一 commandType。
4. 所有成功写操作：revision 递增 + 事件发布 + 可撤销。
5. 失败/冲突操作：无副作用、无事件、revision 不变。
6. `nc_ref_apply` 事务原子性通过。
7. `nc_extract_audio` 任务记录真实（无伪造进度）。
8. 旧接口回归全部通过。
9. ASAN / UBSAN / APK 冒烟 / main CI 全绿。

---

## 17. 待确认事项

1. 画布的"比例约束"具体规则（是否锁定 16:9/9:16 等预设）？
2. 背景色是否需要支持透明度（RGBA 的 A 通道语义）？
3. `nc_extract_audio` 的 Task 记录是否接受"同步任务（progress 0→1）"这一过渡形态？
