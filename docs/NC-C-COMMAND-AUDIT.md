# C 阶段：76 个 Command 统一契约接入审计报告

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-C-COMMAND-AUDIT |
| 阶段 | C 第一阶段（只读审计） |
| 基线 | `main` @ `f021606c8466f95e6c97ce205bb628e3b028584c` |
| 审计对象 | `mobile/native/android/nc_jni.cpp` 中归类为 Command 的 76 个 op |
| 方法 | 静态解析 op 分支 → JNI 调用 → C ABI 实现体 → `TimelineEditor` 门面/直接字段写入 |
| 源码证据 | `nc_jni.cpp`、`core/src/capi.cpp`、`core/include/nairoclip/core/editor.h` |

> **性质**：只读审计，未修改任何源码。分类均由**实现体**判断
> （是否调用 `editor->editor->…` 门面 / 是否直接写 `editor->project.*` 字段），
> 不依据函数名猜测。

---

## 1. 总览

| 指标 | 数值 |
|---|---|
| Command 类 op 总数 | **76** |
| 去重后的独立 C ABI | **67**（9 组重复 op 指向同一函数） |
| 经 `TimelineEditor` 门面执行（可撤销） | **71** |
| **直接写 project 字段（绕过命令与撤销）** | **3** |
| 误归类（实为只读查询） | **1** |
| 非工程写操作（写文件） | **1** |

### 1.1 三种调用链形态

**A. 门面模式（71 个，占 93%）**

```
JS op → JNI DispatchEdit → nc_xxx → editor->editor->method(...) → Command → UndoStack
```
特点：进入撤销栈、`editRevision()` 递增、失败返回 `Result`。

**B. 直接写入模式（3 个，占 4%）**

```
JS op → JNI → nc_xxx → editor->project.<field> = ... → return NC_OK
```
特点：**不入撤销栈、不递增 revision、无事件**。这是 C 阶段的主要整改对象。

**C. 非工程副作用（1 个）**

`nc_extract_audio`：读取素材并写出音频文件，不修改工程数据。
应归入 Task/文件任务，不属于 Command。

---

## 2. 逐项清单（76 项）

| # | 原始 op | 真实 C++ 入口 | commandType 建议 | 改工程 | 可撤销 | 备注 |
|---|---|---|---|---|---|---|
| 1 | `split` | TimelineEditor::splitClip | `clip.split` | 是 | 是 |  |
| 2 | `delete` | TimelineEditor::removeClip | `clip.remove` | 是 | 是 |  |
| 3 | `trim_start` | TimelineEditor::trimClipStart/End | `clip.trim.start` | 是 | 是 |  |
| 4 | `move` | TimelineEditor::moveClip | `clip.move` | 是 | 是 |  |
| 5 | `enabled` | TimelineEditor::setClipEnabled | `clip.enabled.set` | 是 | 是 |  |
| 6 | `opacity` | TimelineEditor::setClipOpacity | `clip.opacity.set` | 是 | 是 | 与 #16 重复 |
| 7 | `filter` | TimelineEditor::setClipFilter | `clip.filter.set` | 是 | 是 |  |
| 8 | `rename_project` | 直接写 Project::name | `project.rename` | 是（**否**） | 否 | **绕过命令/撤销** |
| 9 | `rename_track` | TimelineEditor::renameTrack | `track.rename` | 是 | 是 |  |
| 10 | `nc_add_track` | TimelineEditor::addTrack | `track.add` | 是 | 是 | 与 #18 重复 |
| 11 | `nc_add_text_clip` | TimelineEditor::addTrack+insertClip | `text.clip.add` | 是 | 是 |  |
| 12 | `nc_set_clip_text` | TimelineEditor::setClipText | `text.clip.set` | 是 | 是 |  |
| 13 | `nc_set_text_style` | TimelineEditor::setClipText | `text.style.set` | 是 | 是 |  |
| 14 | `nc_set_clip_blend_mode` | TimelineEditor::setClipBlendMode | `clip.blend.set` | 是 | 是 | 与 #31 重复 |
| 15 | `nc_add_effect` | TimelineEditor::addEffect | `effect.add` | 是 | 是 | 与 #34 重复 |
| 16 | `nc_set_clip_opacity` | TimelineEditor::setClipOpacity | `clip.opacity.set` | 是 | 是 | 与 #6 重复 |
| 17 | `nc_insert_keyframe` | TimelineEditor::insertKeyframe | `keyframe.insert` | 是 | 是 | 与 #43 重复 |
| 18 | `add_track` | TimelineEditor::addTrack | `track.add` | 是 | 是 | 与 #10 重复 |
| 19 | `track_flag` | setTrackHidden/Locked/Muted/Solo | `track.flag.set` | 是 | 是 | 四态合并 |
| 20 | `track_gain` | TimelineEditor::setTrackGain | `track.gain.set` | 是 | 是 |  |
| 21 | `duplicate` | TimelineEditor::insertClip | `clip.duplicate` | 是 | 是 |  |
| 22 | `move_to_track` | TimelineEditor::moveClipToTrack | `clip.move_to_track` | 是 | 是 |  |
| 23 | `ripple_delete` | TimelineEditor::rippleDelete | `clip.ripple_delete` | 是 | 是 |  |
| 24 | `remove_keyframe` | TimelineEditor::removeKeyframe | `keyframe.remove` | 是 | 是 | 与 #75 重复 |
| 25 | `remove_transition` | TimelineEditor::removeTransition | `transition.remove` | 是 | 是 |  |
| 26 | `marker` | TimelineEditor::upsertMarker | `marker.add` | 是 | 是 |  |
| 27 | `undo` | TimelineEditor::undo | `history.undo` | 是 | 是 |  |
| 28 | `redo` | TimelineEditor::redo | `history.redo` | 是 | 是 |  |
| 29 | `transform_set` | TimelineEditor::setClipTransform | `clip.transform.set` | 是 | 是 |  |
| 30 | `transform_clear` | → nc_set_clip_transform | `clip.transform.clear` | 是 | 是 | 委派实现 |
| 31 | `blend` | TimelineEditor::setClipBlendMode | `clip.blend.set` | 是 | 是 | 与 #14 重复 |
| 32 | `speed` | TimelineEditor::setClipSpeed | `clip.speed.set` | 是 | 是 |  |
| 33 | `speed_curve` | TimelineEditor::setClipSpeedCurve | `clip.speed_curve.set` | 是 | 是 |  |
| 34 | `effect_add` | TimelineEditor::addEffect | `effect.add` | 是 | 是 | 与 #15 重复 |
| 35 | `effect_update` | TimelineEditor::addEffect | `effect.update` | 是 | 是 |  |
| 36 | `effect_enabled` | TimelineEditor::addEffect | `effect.enabled.set` | 是 | 是 |  |
| 37 | `effect_move` | TimelineEditor::moveEffect | `effect.move` | 是 | 是 |  |
| 38 | `effect_remove` | TimelineEditor::removeEffect | `effect.remove` | 是 | 是 |  |
| 39 | `mask` | TimelineEditor::setClipMask | `clip.mask.set` | 是 | 是 |  |
| 40 | `chroma` | TimelineEditor::setClipChromaKey | `clip.chroma.set` | 是 | 是 |  |
| 41 | `denoise` | TimelineEditor::setClipDenoise | `clip.denoise.set` | 是 | 是 |  |
| 42 | `audio` | TimelineEditor::setClipAudio | `clip.audio.set` | 是 | 是 |  |
| 43 | `keyframe` | TimelineEditor::insertKeyframe | `keyframe.insert` | 是 | 是 | 与 #17 重复 |
| 44 | `freeze` | TimelineEditor::insertClip（Clip::freeze） | `clip.freeze` | 是 | 是 |  |
| 45 | `reverse` | TimelineEditor::setClipReversed | `clip.reverse` | 是 | 是 |  |
| 46 | `transition` | TimelineEditor::addTransition | `transition.add` | 是 | 是 |  |
| 47 | `canvas` | 直接写 Sequence::width/height | `project.canvas.set` | 是（**否**） | 否 | **绕过命令/撤销** |
| 48 | `lut` | TimelineEditor::setClipLut | `clip.lut.set` | 是 | 是 |  |
| 49 | `nc_adjustment_add_effect` | TimelineEditor::setAdjustment | `track.adjustment.effect.add` | 是 | 是 | 与 #55 重复 |
| 50 | `nc_adjustment_remove_effect` | TimelineEditor::setAdjustment | `track.adjustment.effect.remove` | 是 | 是 | 与 #56 重复 |
| 51 | `nc_adjustment_set_lut` | TimelineEditor::setAdjustment | `track.adjustment.lut.set` | 是 | 是 | 与 #57 重复 |
| 52 | `nc_remove_asset` | TimelineEditor::removeAsset | `asset.remove` | 是 | 是 |  |
| 53 | `nc_asset_reorder` | TimelineEditor::reorderAssets | `asset.reorder` | 是 | 是 |  |
| 54 | `nc_set_track_view` | TimelineEditor::setTrackHeight | `track.view.set` | 是 | 是 |  |
| 55 | `adjust_effect_add` | TimelineEditor::setAdjustment | `track.adjustment.effect.add` | 是 | 是 | 与 #49 重复 |
| 56 | `adjust_effect_remove` | TimelineEditor::setAdjustment | `track.adjustment.effect.remove` | 是 | 是 | 与 #50 重复 |
| 57 | `adjust_lut` | TimelineEditor::setAdjustment | `track.adjustment.lut.set` | 是 | 是 | 与 #51 重复 |
| 58 | `group_create` | TimelineEditor::createTrackGroup | `track_group.create` | 是 | 是 |  |
| 59 | `group_remove` | TimelineEditor::removeTrackGroup | `track_group.remove` | 是 | 是 |  |
| 60 | `group_flag` | setGroupHidden/Locked/Muted | `track_group.flag.set` | 是 | 是 |  |
| 61 | `nc_move_track` | TimelineEditor::moveTrack | `track.move` | 是 | 是 |  |
| 62 | `nc_template_apply` | applyTemplate(*editor) | `template.apply` | 是 | 是 |  |
| 63 | `nc_surge_preset_name` | 纯查询（静态表） | `—` | — | 否 | **误归类：应为 Query** |
| 64 | `nc_surge_preset_apply` | TimelineEditor::setClipEffects | `effect.surge.apply` | 是 | 是 |  |
| 65 | `nc_ref_apply` | beginTransaction+insertClip+setClipSpeedCurve | `reference.apply` | 是 | 是 | 多命令事务 |
| 66 | `nc_template_apply_slots` | applyTemplateSlots(*) | `template.apply_slots` | 是 | 是 |  |
| 67 | `nc_import_asset` | TimelineEditor::importAsset | `asset.import` | 是 | 是 |  |
| 68 | `nc_insert_clip` | TimelineEditor::insertClip | `clip.insert` | 是 | 是 |  |
| 69 | `shake_set` | TimelineEditor::setClipShake | `clip.shake.set` | 是 | 是 |  |
| 70 | `xform_set` | TimelineEditor::setClipXform | `clip.xform.set` | 是 | 是 |  |
| 71 | `xform_clear` | TimelineEditor::setClipXform | `clip.xform.clear` | 是 | 是 |  |
| 72 | `beat_sync` | applyBeatSync(*editor) | `edit.beat_sync.apply` | 是 | 是 |  |
| 73 | `nc_set_keyframe_easing` | TimelineEditor::insertKeyframe | `keyframe.easing.set` | 是 | 是 |  |
| 74 | `nc_extract_audio` | 写文件，不改工程 | `asset.audio.extract` | 是 | 否 | 非工程写操作 |
| 75 | `nc_remove_keyframe` | TimelineEditor::removeKeyframe | `keyframe.remove` | 是 | 是 | 与 #24 重复 |
| 76 | `nc_set_background_color` | 直接写 Sequence::backgroundRgba | `project.background.set` | 是（**否**） | 否 | **绕过命令/撤销** |

---

## 3. 关键发现

### D-C1 三个写操作完全绕过命令体系（P1）

| op | C ABI | 直接写入 | 证据 |
|---|---|---|---|
| `rename_project` | `nc_set_project_name` | `editor->project.name = text` | `capi.cpp:447-470` |
| `canvas` | `nc_set_project_canvas` | `main->width/height = ...` | `capi.cpp` 对应实现 |
| `nc_set_background_color` | 同名 | `main->backgroundRgba = rgba` | `capi.cpp` 对应实现 |

后果：改项目名/画布/背景色后，**撤销无效、revision 不变、无事件**，
与 T2.2 之前"编辑无审计轨迹"属同一类问题。

### D-C2 九组重复 op（P2）

| 组 | 两个 op | 同一 C ABI |
|---|---|---|
| 1 | `opacity` / `nc_set_clip_opacity` | `nc_set_clip_opacity` |
| 2 | `nc_add_track` / `add_track` | `nc_add_track` |
| 3 | `nc_set_clip_blend_mode` / `blend` | `nc_set_clip_blend_mode` |
| 4 | `nc_add_effect` / `effect_add` | `nc_add_effect` |
| 5 | `nc_insert_keyframe` / `keyframe` | `nc_insert_keyframe` |
| 6 | `remove_keyframe` / `nc_remove_keyframe` | `nc_remove_keyframe` |
| 7 | `nc_adjustment_add_effect` / `adjust_effect_add` | `nc_adjustment_add_effect` |
| 8 | `nc_adjustment_remove_effect` / `adjust_effect_remove` | `nc_adjustment_remove_effect` |
| 9 | `nc_adjustment_set_lut` / `adjust_lut` | `nc_adjustment_set_lut` |

两套命名并存（历史 `nc_*` 透传 vs 短名面板调用）。统一契约时必须收敛为**一个 commandType**，
否则同一操作会有两个审计身份。

### D-C3 `nc_surge_preset_name` 被误归类为 Command（P3）

实现是纯静态表查询（`return kSurgePresets[index].name;`），**不修改任何状态**。
应归入 Query。这是 T2.1 启发式分类的已知误差，本次按源码证据更正。

### D-C4 `transform_clear` 通过委派实现（澄清）

`nc_clear_clip_transform` 内部转调 `nc_set_clip_transform(enabled=0)`，
因此**可撤销**，不是绕过路径。T2.1 的"未调用 C ABI"是解析误判。

### D-C5 `freeze` 的真实实现（澄清）

op 分支调用 `nc_freeze_frame` → 构造 `Clip{freeze=true}` → `insertClip`，
属门面模式。T2.1 中"该分支未调用 C ABI"是静态解析截断造成的误判，
本次以 `capi.cpp` 实现体为准更正。

### D-C6 事务型命令（P2）

`nc_ref_apply` 内部使用 `beginTransaction + insertClip + setClipSpeedCurve + endTransaction`，
一次调用产生**多条子命令**。接入统一契约时必须保留事务语义
（`transactionId`），否则撤销会拆成多步。

### D-C7 直接写字段缺少错误码

`rename_project` 只做长度校验；`canvas` 只做范围校验；`set_background_color` 无校验。
统一接入后必须给出 `NC-PROJECT-*` 稳定错误码。

---

## 4. 风险清单

| ID | 风险 | 等级 | 说明 |
|---|---|---|---|
| RC-1 | 3 个写操作绕过撤销/revision | P1 | 用户改画布后撤销无效 |
| RC-2 | 9 组重复 op | P2 | 同一操作两个身份，审计与权限判断会分叉 |
| RC-3 | 事务命令接入后可能被拆解 | P2 | 撤销粒度退化 |
| RC-4 | 71 个 op 逐个接入，回归面大 | P1 | 必须分批 + 旧路径保留 |
| RC-5 | `nc_extract_audio` 归类错误 | P3 | 不属 Command，应走 Task |
| RC-6 | 直接写字段无稳定错误码 | P2 | UI 无法区分失败原因 |

---

## 5. commandType 命名规范（建议）

```
<域>.<对象>.<动作>[.<子动作>]
```

- 域：`project` / `track` / `clip` / `text` / `effect` / `keyframe` / `transition` / `marker` / `asset` / `track_group` / `template` / `reference` / `history` / `edit`
- 动作统一小写，复合动作用 `.` 分段（如 `clip.transform.clear`）
- 稳定 ID，**不复用**；废弃项保留映射但标记 `deprecated`

---

## 6. 第二阶段实施方案要点（预告，待审计验收后细化）

1. **统一入口**：所有写 op 在 JNI/C ABI 层改为调用 `CommandDispatcher`，不再直连 `nc_*`。
2. **分批**：
   - 批次 1：3 个绕过项（rename_project / canvas / background_color）——补齐 Command 类。
   - 批次 2：9 组重复 op 的收敛（保留双入口但映射到同一 commandType）。
   - 批次 3：其余 64 个门面 op 的 envelope 包装。
3. **revision**：沿用 `TimelineEditor::editRevision()`，禁止另建计数器。
4. **事件**：仅在命令**真实成功且状态发生变化**时发布 `timeline.changed`。
5. **旧接口**：`nc_*` 全部保留；新路径通过 `nc_apply_command` 进入。
6. **测试**：每个 commandType 至少一条"信封 → 执行 → revision → 事件 → 撤销"闭环用例。

---

## 7. 待确认

1. `rename_project` / `canvas` / `background_color` 是否需要**可撤销**？（建议需要）
2. 9 组重复 op 中，哪一套命名作为对外稳定 op 名？
3. `nc_extract_audio` 是否改归 Task API（影响 T2.2 范围外，需单独授权）？
