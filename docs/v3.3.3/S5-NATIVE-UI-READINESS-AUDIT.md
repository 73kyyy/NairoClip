# S5 原生 UI 迁移就绪审计

- 审计分支：`work/v3.3.3-s5-native-editor-foundation`
- 审计 HEAD：`1fa969a5b1eeb8216b935976043040e3cf83ed57`
- 审计日期：2026-11-20
- 性质：基于当前分支源码与任务文档的只读核验；不是编译、运行或真机验收。

## 1. 结论

**S5 原生 UI 迁移尚未完成，当前处于原生编辑器原型/部分能力适配阶段。**

当前代码已经具备若干可复用的 Jetpack Compose / Android 原生组件，但尚未形成可作为正式产品入口的完整原生编辑器运行链路。特别是宿主导航、预览 Surface 生命周期接入、播放/音频时钟、导入与工程恢复、外围页面以及旧 WebView 退出验收均未闭环。

本审计不把“存在 Kotlin/Compose 源码”解释为“原生 UI 已接通”，也不把静态源码检查解释为编译或真机通过。

## 2. 已存在的原生实现

| 区域 | 当前源码证据 | 审计判断 |
|---|---|---|
| 编辑器外壳 | `mobile/native/android/NativeEditorScreen.kt` | 已有 Compose 屏幕骨架、顶部栏、预览区、快捷操作、时间轴、片段选择和工具栏 |
| 时间轴 | `NativeTimelineView.kt`、`NativeTimelineSnapshot.kt` | 已有 Android Canvas 时间轴、轨道/片段显示、播放头手势、缩放与滚动、轨道显隐/静音交互 |
| 变换面板 | `NativeEditorScreen.kt`、`NativeEditorCommandAdapter.kt` | X/Y/缩放/旋转及部分关键帧命令适配已存在 |
| 引擎桥 | `NativeEngineBridge.kt` | 存在 JNI 引擎调用、Surface attach/detach、异步单帧渲染等能力 |
| Surface 组件 | `PreviewSurfaceView.kt`、`NativePreviewSurfaceController.kt` | 有可复用原生 Surface 与控制器实现，但控制器尚未进入正式 Activity/宿主运行链 |
| 交互降级 | `NativeEditorScreen.kt` | 未迁移工具通过旧编辑器回退；运输控制区已明确标注播放、分割、删除仍由完整编辑器处理 |

## 3. 尚未闭环的迁移工作

### S5-1 宿主导航
- 当前 `android/app/src/main/java/com/nairoclip/app/MainActivity.kt` 仍创建 WebView，并加载 `pages/01-home.html`。
- 尚未建立 Compose 原生宿主导航和原生页面栈。
- 不得在依赖/审批门未解除前擅自替换 Activity 入口或 Manifest。

### S5-2 外围页面
- 首页、项目列表、媒体导入/选择、设置及其他外围页面尚未形成完整原生导航闭环。
- 导入链路依赖 S1-4A；不能以空白页面或静态假数据代替真实项目/媒体数据。

### S5-3 编辑器外壳
- Compose 编辑器原型已存在，但尚未成为正式运行入口。
- 需要将工程状态、项目切换、生命周期、错误/加载状态与正式宿主连接。
- 需保持既有编辑器布局、工具栏横向二级展开、时间轴和面板结构，不以原生迁移为由重新设计。

### S5-4 预览
- `NativePreviewSurfaceController` 目前未安装到 MainActivity/正式宿主。
- 当前旧宿主仍自行管理 PreviewSurfaceView；这不等于新原生编辑器已使用该控制器。
- 单帧异步渲染不等于连续播放；播放时钟、音频同步、暂停/恢复与后台生命周期尚未形成完整能力。
- 需要设备验证 Surface 创建/尺寸变化/销毁、旋转/分屏/后台恢复及连续渲染。

### S5-5 时间轴
- 原生 Canvas 时间轴组件已有较多交互，但必须与已验收的 S3-1 Rational 时间模型对齐。
- 时间轴拖动回调本身不等于真实引擎 Seek；必须闭合 Seek 命令、预览帧请求、播放头状态和工程快照之间的数据链。

### S5-6 工具栏/面板
- 变换及关键帧面板已有部分原生实现。
- 未迁移工具仍会回退旧编辑器；需按 S1-2 已验收的命令路由契约逐项接通。
- 默认空回调不能被误认为功能可用；正式宿主必须提供真实命令处理、错误反馈和状态回读。

### S5-7 其他面板
- 其他编辑面板尚未形成完整原生覆盖，须按实际功能清单逐项迁移，不得用空壳按钮冒充。

### S5-8 旧代码清理
- 需等 S5-2 至 S5-7 完成并验收后再清理重复旧 UI。
- 兼容回退路径在迁移验收前必须保留。

### S5-9 WebView 退出验收
- 这是最后的验收任务，不是当前可提前执行的清理动作。
- A-6 删除 WebView 审批仍待决策；未授权前不得删除 WebView 兼容路径。

## 4. 任务依赖与审批状态

当前任务注册表/依赖矩阵给出的约束：

- `S1-3 → S5-0B`：工程格式、保存与恢复先验收。
- `S3-1 → S5-0B`：Rational 时间模型先验收。
- `S5-0B → S5-1 → {S5-2, S5-3}`。
- `S5-3 → S5-4`。
- `S5-3 + S3-1 → S5-5`。
- `S5-3 + S1-2 → S5-6 → S5-7`。
- `S5-2..S5-7 → S5-8 → S5-9`。
- `S1-8D → S5-1`：Manifest 共享文件任务须先串行完成。
- A-5（原生 UI 迁移）在任务审批表中仍为待决策；A-6（删除 WebView）也仍为待迁移完成/待决策。

`NC-V3.3.3-ACCEPTANCE-ROLLBACK.md` 当前将 S5-* 的编译/设备/真机验证列为待执行或不可验证，没有可据此标记为通过的证据。

## 5. 下一阶段允许执行的边界

在 S1-3、S3-1、S1-8D 的真实验收状态核实完成且 A-5 门解除之前：
1. 可继续只读核对原生组件、命令契约、交互缺陷与测试缺口。
2. 不得将原生编辑器接入 MainActivity 作为正式入口。
3. 不得伪造播放/Seek/导出 JNI 接口或以 no-op 回调冒充功能。
4. 不得删除 WebView、旧页面或兼容回退。
5. 不得宣称 Android 编译、CI、自动化或真机验收通过。

解除门槛后，按依赖顺序推进 S5-0B 至 S5-9；S5-4/S5-5/S5-6 仅在各自前置条件满足后并行实施，并为每一批保留可回退路径。

## 6. 本次审计限制

- 本次使用 GitHub 当前分支源码与仓库文档进行静态核验。
- 未执行 Android Gradle 编译、C++/JNI 集成测试、UI 自动化、截图视觉回归或真机测试。
- 本审计报告不替代 S1-3/S3-1 验收，不构成 A-5/A-6 审批，也不改变任务注册表中的依赖关系。


## 7. Follow-up source delta (2026-11-20)

本节补充核对原审计 HEAD 之后的 S5 原生组件变化；不改变第 4、5 节记录的依赖与审批状态。

- 关键帧适配器的插入、删除、缓动接口统一使用 `clipLocalTimeUs` 命名，并明确要求宿主按片段起点与播放速度换算；这只是时间语义契约澄清，尚无 Kotlin runtime host 调用点，因此不代表工程播放头到片段本地时间的转换已接通。
- 变换参数 UI 已按 C ABI 对齐 X/Y 归一化范围 `-10..10`、缩放范围 `5%..1000%`；本次进一步将旋转滑块及数值输入范围对齐到 C ABI 的 `-3600°..3600°`。
- Canvas 时间轴增加标准 Android 无障碍 SeekBar 语义：读出播放位置/总时长/轨道数，支持标准 progress 调整及一秒步进。轨道、片段仍绘制在单一 Canvas 中，尚未提供每个轨道/片段独立的虚拟无障碍节点。
- 上述变更仅完成源码静态核对；未运行 Android 编译、TalkBack/Accessibility Service 验证、自动化测试或真机手势测试。
- 本节不解除 S1-3、S3-1、S1-8D 前置，不构成 A-5/A-6 审批，也不授权 Activity/Manifest 入口切换或删除 WebView。

- Native transform bridge validation now rejects out-of-contract values before JNI instead of relying on the C ABI's silent clamping: scale `5%..1000%`, normalized X/Y `-10..10`, rotation `-3600°..3600°`, and anchor `0..8`. The error message reports the supported ranges. Commit: `e3ca75b9a4a1c65cba8895a3bb8c5ac9830bc1eb`. Static source assertions passed; no Android unit test/build/device execution is available in this turn.

### Transform numeric-range policy aligned with After Effects (2026-11-20)
- Reviewed Adobe's documented layer transform semantics: Position is expressed in composition/parent coordinates; Scale is percentage-based; Rotation preserves whole turns plus the within-turn angle; Anchor Point is a layer-space coordinate and is the pivot for scale/rotation. AE's nine-point anchor shortcuts are convenience presets, not the full mathematical definition of an anchor. References: Adobe Help, “Adjust layer properties” and “Expression language reference”; Adobe Learn, “Create composition animation”.
- NairoClip's current C ABI is intentionally not identical to AE's coordinate model: X/Y are normalized canvas offsets (1.0 = one canvas width/height), Scale is a positive multiplier (0.05..10.0 = 5%..1000%), Rotation is unwrapped degrees (-3600..3600), and Anchor is currently a discrete 3×3 preset (0..8, center 4). These are engine hard bounds, not claims about AE's limits.
- Established one shared Kotlin policy for the current engine contract. Mobile sliders now use comfortable common travel windows (X/Y -2..2, Scale 5%..400%, Rotation -360..360°) while direct numeric entry retains the full engine-safe bounds (X/Y -10..10, Scale 5%..1000%, Rotation -3600..3600°). If a loaded value is outside the common slider window, the slider expands to include that current value rather than silently clipping it. The UI explains units and distinguishes drag range from typed-entry range.
- Static transform bridge validation and transform-keyframe adapter validation now reference the same shared hard-bound constants. This closes the prior inconsistency where keyframe insertion only required positive scale and did not enforce the static transform X/Y/scale/rotation domains.
- Anchor remains a 9-position preset in the existing C ABI; arbitrary pixel/normalized XY anchor coordinates are not implemented and must not be simulated as if the nine presets were free-form AE anchor coordinates. A future free-anchor implementation requires a versioned core model/serialization/C ABI change and corresponding preview/export transform-pivot validation.
- Static source review only. No Android Gradle build, unit tests, CI workflow, Compose runtime validation, or physical-device interaction test has been run. Existing S1/S3/S5 integration and Activity/Manifest approval gates remain unchanged.


### Native nine-point anchor control and pivot contract (2026-11-20)
- The normative cross-platform ClipTransform unit/range/anchor contract is recorded in `docs/v3.3.3/TRANSFORM-PARAMETER-STANDARD.md`; it distinguishes common slider travel from engine hard limits and defines non-finite fallback behavior.
- NativeEditorScreen now accepts the selected anchor as explicit host state and requires an `onAnchorChanged(trackIndex, clipId, anchor)` callback. The transform panel exposes all nine positions with labels, selected-state feedback, and a clear description that the anchor is the scale/rotation pivot. The callback validates `0..8` before forwarding; it does not silently substitute the center.
- The Android app's current MainActivity still launches the WebView editor and does not instantiate NativeEditorScreen. This UI change therefore establishes the native screen contract and interaction only; it does not claim that anchor edits are already wired to the running app. S5 runtime-host and entrypoint approval gates remain in force.
- C++ `clipTransformMatrix` maps indices as row-major 3×3: 0 top-left, 1 top-center, 2 top-right, 3 middle-left, 4 center, 5 middle-right, 6 bottom-left, 7 bottom-center, 8 bottom-right. The pivot should remain fixed under combined scale and rotation. Added a native unit test covering all nine pivot positions; test execution is pending.
- No C ABI or project serialization change was needed for the existing nine-point preset model. Free-coordinate anchors remain a separate future versioned core/ABI migration.


### Transform keyframe boundary consistency (2026-11-20)
- Closed a bypass in the generic keyframe adapter: transform-property values are now validated at the shared generic insertion boundary as well as in the transform-specific helper. Engine scale keyframes are checked in multiplier units (0.05..10.0), while X/Y and rotation use their shared normalized/degrees domains.
- Keyframe reads now reject out-of-domain transform values instead of accepting any finite number into the Compose model. This prevents corrupted or stale engine keyframes from being rendered as valid UI state.
- Non-transform channels retain their existing property-specific semantics; this change does not impose transform bounds on opacity/audio or other channels.
- Static source verification only; no Android build, Kotlin unit tests, CI, or device test was run.

- Follow-up interaction fix: when a selected clip loads a transform value outside the common slider window, the expanded slider domain is now captured per selected clip/parameter and remains stable for the entire drag. It is recalculated only when the selected clip or control domain changes, preventing a moving-boundary/thumb-chasing effect during pointer input.


### Native anchor selector navigation fix (2026-11-20)
- Connected the existing nine-point anchor selector to the native `画面 → 锚点` subtool route. The selector is now shown only in the anchor panel, and transform keyframe controls are hidden there because anchor is not yet a keyframe channel in the current engine contract.
- The native screen exposes the selected anchor value and a required `onAnchorChanged(trackIndex, clipId, anchor)` callback; persistence remains the host's responsibility through `NativeEditorCommandAdapter.setTransform` and must retain the current transform/enabled state when updating the anchor.
- Touch targets remain 48 dp and use radio-button semantics. Static source inspection only; no Compose build, accessibility service, or device test was run.


### Core transform/keyframe domain hardening (2026-11-20)
- Found a cross-platform contract mismatch: static clip scale was clamped to multiplier 0.05..10.0, but the generic animation property spec allowed 0.01..100.0. This let keyframed scale exceed the static transform limit even though the Android adapter rejected it. Core animation Scale now shares the static transform range.
- Added canonical C++ transform limit constants for scale, normalized X/Y, rotation, and anchor. Static clip transform clamping and animation property specifications consume the shared constants.
- Non-finite keyframe values and Bézier controls are rejected by KeyframeTrack; command insertion rejects values outside the selected property's declared range. Animation sampling falls back to the property's neutral default for legacy non-finite values.
- Added native unit-test cases for non-finite keyframe rejection and scale-keyframe lower/upper bounds. Existing native transform tests already cover NaN/±Inf recovery for static transform fields. Tests were added/updated but not executed in this turn.
- These checks apply to core/C ABI callers across platforms, not only Android. The C ABI still clamps finite out-of-range static transform values per its existing contract; Kotlin rejects them before crossing JNI.

- Anchor grid accessibility follow-up: the nine native presets are now grouped with Compose `selectableGroup()` and each cell retains `Role.RadioButton` selected semantics. This makes the nine choices one coherent accessibility group without changing the C ABI index mapping. Source-level verification only; Compose compilation and TalkBack testing remain pending.

- Core animation property specifications now source X/Y and rotation limits from `transform_limits.h`, matching `ClipTransform::clamp()` rather than duplicating literals. `tests/test_animation.cpp` now asserts Scale, PositionX, PositionY and Rotation property specs against those shared constants. Changes are committed; native test execution remains pending.


## 7. 变换控件范围统一策略（2026-11-20）

根据产品交互要求，变换面板的滑块范围与数值输入范围统一，不再采用“滑块常用范围较窄、数值输入范围较宽”的双范围设计：

| 参数 | 滑块范围 | 数值输入范围 | 引擎硬范围 |
|---|---:|---:|---:|
| X / Y | -10..10 | -10..10 | -10..10 |
| 缩放 | 5%..1000% | 5%..1000% | 0.05..10.0 |
| 旋转 | -3600°..3600° | -3600°..3600° | -3600°..3600° |
| 锚点 | 九宫格 0..8 | 九宫格 0..8 | 0..8 |

滑块用于粗略定位，数值输入用于相同范围内精细定位。滑块边界固定，不因当前值或拖动过程动态改变。该修改仅调整控件交互域，不改变 C++ 核心参数范围、序列化格式或 C ABI。

## 8. Transform keyframe evaluation and preview-cache follow-up (2026-11-20)

- Verified core keyframe evaluation: rotation is interpolated as an unwrapped numeric degree value. It is not normalized modulo 360, so a 0° → 720° curve passes through 360° and preserves two full turns.
- ClipAnimation::sample() clamps evaluated values after easing to the per-property hard domain. This also constrains Bézier Y overshoot; the endpoint keyframes remain individually range-validated by InsertKeyframeCommand.
- Added native regression coverage for multi-turn rotation, rotation keyframe values outside ±3600°, and scale/position Bézier overshoot at the domain boundaries. Tests were added but have not been executed in this environment.
- Found and fixed a real preview-cache invalidation defect: contentFingerprint() previously hashed keyframe time/value but omitted easing type and Bézier control points. Editing only a curve could therefore reuse stale preview pixels. Clip animation and adjustment-layer animation fingerprints now include easing type plus all four control coordinates; adjustment animation also includes curve size.
- Added a preview regression test that changes easing type and then changes Bézier Y controls without changing keyframe time/value, asserting that the same timestamp is invalidated and re-rendered.
- This verifies the existing core/export sampling route and cache identity only. No Android runtime host integration, native editor entrypoint switch, build, native test execution, CI run, or device test is claimed. PR #32 remains Draft and unmerged.
