# S5 原生 UI 迁移就绪审计

- 审计分支：`work/v3.3.3-s5-native-editor-foundation`
- 审计代码基准 HEAD：`6ee842855615ccdad3a79e896a26258c5fa8efa9`
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

## 9. Per-channel static transform fallback (2026-11-20)

- Found a preview/export consistency defect in the export transform resolver: it used the clip-wide hasAnimation flag to decide whether to sample every transform property. If only opacity had keyframes, unkeyed scale/position/rotation were sampled as neutral defaults and their static ClipTransform values were ignored.
- Export now checks animation.hasCurve(property) independently for each transform channel. A keyframe on one property no longer overrides another property's static value.
- PreviewFrame transform metadata now uses the same per-property priority as the actual preview pixels: that property's keyframe > that property's static transform > neutral default. This removes a mismatch where preview pixels could be transformed correctly while the PreviewFrame scale/position/rotation metadata reported defaults.
- Added preview and export regression tests using opacity-only keyframes with a static 50% scale. The preview test checks resolved transform metadata; the export test checks that pixels outside the centered scaled image remain background.
- The normative transform contract now explicitly requires per-channel keyframe/static/default resolution and preview/export parity. Tests are committed but have not been executed; no native build, CI, or device validation is claimed.


## 10. 全部产品页面原生化范围清点（2026-11-20）

本节按当前分支 `mobile/pages/` 与 `mobile/native/android/` 的实际文件清点，不把原生引擎桥、预览 Surface 或编辑器组件误计为已经完成的原生页面。

### 10.1 页面清单与目标承载方式

| 页面文件 | 当前页面 | 目标承载方式 | S5 归属 | 当前状态 |
|---|---|---|---|---|
| `01-home.html` | 首页 / 我的草稿 | Compose 原生页面；真实草稿列表、空态、创建项目入口 | S5-1/S5-2 | `NativeHomeScreen` 已实现；未接宿主数据/导航 |
| `02-me.html` | 我的 | Compose 原生页面；账户资料、入口和状态 | S5-2 | `NativeMeScreen` 已实现；未接账号服务/导航 |
| `03-auth.html` | 登录 | Compose 原生表单；认证仍走既有安全认证服务 | S5-2 | `NativeAuthScreen` 已实现；未接认证服务/导航 |
| `03a-phone.html` | 手机号登录 | Compose 原生表单；验证码/风控调用现有认证契约 | S5-2 | `NativePhoneLoginScreen` 已实现；未接短信/风控服务 |
| `04-settings.html` | 设置 | Compose 原生列表、开关、选择器和子页面导航 | S5-2 | `NativeSettingsScreen` 已实现；未接偏好/平台动作 |
| `05-legal.html` | 条款与说明 | 原生 Compose 文本阅读页；长文分段/目录/滚动，不需要 WebView | S5-2 | `NativeLegalScreen` 已实现；未接法律文本数据源 |
| `06-feedback.html` | 意见反馈 | Compose 原生表单；提交走既有反馈接口 | S5-2 | `NativeFeedbackScreen` 已实现；未接反馈服务 |
| `07-faq.html` | 常见问题 | Compose 原生折叠列表/搜索 | S5-2 | `NativeFaqScreen` 已实现；未接 FAQ 内容 |
| `08-crashlog.html` | 崩溃日志 | Compose 原生列表/详情/复制；日志读取权限保持最小化 | S5-2 | `NativeCrashLogScreen` 已实现；未接日志仓库 |
| `10-editor.html` | 剪辑器 | Compose 外壳 + 自定义 Canvas 时间轴 + Surface 预览 + 原生参数面板 | S5-3..S5-7 | 原生组件已存在；尚未接入正式宿主 |
| `20-template.html` | AI 模板剪辑 | Compose 原生模板列表/筛选/详情 | S5-7 | `NativeTemplateScreen` 已实现；未接模板服务/导航 |
| `21-template-fill.html` | 填充素材 | Compose 原生素材槽位与系统 Photo Picker/文件选择器 | S5-7 | `NativeTemplateFillScreen` 已实现；未接素材仓库/选择器 |
| `22-template-progress.html` | 生成进度 | Compose 原生任务状态/进度/取消；只显示真实任务状态 | S5-7 | `NativeTemplateProgressScreen` 已实现；未接 TaskRegistry 事件 |
| `23-ai-edit.html` | AI 参考视频剪辑 | Compose 原生分步流程、素材选择和分析结果 | S5-7 | `NativeAIEditWizardScreen` 已实现；未接 AI 编排命令 |

`_probe.html` 是调试探针，不属于产品页面，必须继续排除在正式导航与发布页面清单之外。

### 10.2 创建项目弹窗与全局交互

创建项目流程不应被当作一个独立静态页面遗漏。它应作为首页的原生 Compose Dialog/BottomSheet 流程实现，并覆盖：
- 新建工程：名称、画幅/分辨率、帧率、背景等字段；选项必须来自核心层支持范围，不得只在 UI 伪造。
- 打开工程：最近工程、文件选择、缺失素材/版本兼容提示。
- 创建/载入中：可取消、错误可见、避免重复提交；完成后导航到原生编辑器。
- 全局导航：返回栈、登录态、深链/恢复入口和进程重建后的路由恢复。

### 10.3 原生技术分工

- **Compose**：首页、我的、登录/手机号登录、设置、条款、反馈、FAQ、崩溃日志、模板/AI 流程、创建项目弹窗，以及编辑器外框和参数面板。
- **Android 自定义 View + Canvas**：多轨时间轴绘制与高频手势；保持单一 Canvas/视口虚拟化，不为每个片段创建 View/Compose 节点。
- **SurfaceView/原生 Surface**：预览画面显示；渲染请求在专用工作线程串行化，UI 线程不得执行同步 JNI 解码/渲染。
- **C++ 共享核心 + 稳定 C ABI**：工程、时间、轨道/片段、效果、关键帧、预览/导出等领域逻辑；Kotlin 只维护视图状态与平台生命周期。
- **系统组件**：媒体/文档选择使用 Android Photo Picker / SAF；认证、支付、模型下载等外部能力继续使用已定义的安全服务契约，不复制业务规则到 UI。

### 10.4 完成判定与迁移顺序

1. 页面逐项迁移并逐项对照现有 HTML/JS 的真实功能、状态、错误与空态；只有视觉壳相似不算完成。
2. 所有页面与弹窗共享同一导航/主题/状态策略；工程数据只能来自 Editor API/C ABI，不得维护第二份工程事实源。
3. 编辑器原生工具栏遵循已确认的交互：二级工具在一级工具同一水平行向右展开；再次点击收起；切换一级工具自动关闭旧二级工具。
4. 每个迁移页面必须有状态/交互回归；关键流程需覆盖进程重建、旋转/窗口变化、无数据、失败、重复点击和权限拒绝。
5. 旧 WebView 只能在 S5-2 至 S5-7 全部迁移并验收后进入 S5-8/S5-9；在 A-6 未批准前保留可回退路径。

**本节状态：页面范围清点完成；Compose 页面实现、宿主导航接入、功能等价验证、WebView 退出均未完成。** 本节不解除 S1-3、S3-1、S1-8D 等前置，也不代替 A-5/A-6 审批或 Android/真机验收。


## 11. 本轮仓库状态复核（2026-11-20）

以下状态以本轮 GitHub API 读取结果为准，覆盖旧审计快照中的 PR/分支状态描述：

| 事项 | 当前证据 | 结论 |
|---|---|---|
| S3-1 Rational 序列化校验 | PR #29，`work/v3.3.3-s3-1-time-validation`，open / draft / unmerged；提交状态无 CI status context | 代码切片已提交 PR，但未验收 |
| S3-1 C ABI 浮点时间输入校验 | PR #31，`work/v3.3.3-s3-1-capi-finite-time`，open / draft / unmerged；提交状态无 CI status context | 代码切片已提交 PR，但未验收 |
| S1-3 工程格式/保存/恢复 | 当前任务注册表仍要求 S1-1、S1-2 等前置；本轮未找到可证明 S1-3 完整验收的合并 PR/验收记录 | 不能标记完成 |
| S1-8D Android 备份策略 | 验收回退表仍为待执行/不可验证；本轮未找到完成证据 | S5-1 的 Manifest 入口改动仍须等待该项串行完成 |
| S5 原生编辑器基础 | PR #32，open / draft / unmerged（head 随本分支提交更新；以 GitHub PR 页面实时状态为准） | 原生组件与本节页面清点已提交；未进入正式运行入口 |
| PR 自动验证 | #29、#31、#32 的当前提交状态均为 pending，且 status context 数为 0 | 不得声称 CI/构建通过 |

因此，当前可继续的是不改变宿主入口/Manifest/持久化契约的隔离式原生页面与组件准备；S5-0B 的正式接口冻结、S5-1 的宿主导航切换，以及 S5-4/5/6 的真实引擎运行接线，必须在各自前置任务与验收门解除后执行。旧 WebView 回退路径继续保留。


## 12. 首页 / 设置原生页面实现切片（2026-11-20）

新增首页/设置原生 Compose 页面组件（当前源码位于 `mobile/native/android/NativeHomeSettingsScreens.kt`），将原 HTML 页面中的主要信息架构拆成宿主驱动的 Compose 页面组件：

- `NativeHomeScreen`：原生首页头部、创建项目入口、四个快捷入口、草稿搜索、空态/加载/错误态、草稿列表和管理选择。
- `NativeCreateProjectDialog`：原生创建项目对话框；工程规格只接受宿主从引擎/正式项目配置契约提供的 `NativeProjectPreset` 列表。没有规格时禁用创建，不在 UI 内私自硬编码分辨率或帧率。
- `NativeSettingsScreen`：原生设置分组折叠、开关、选项、只读状态和动作入口。设置项由宿主注入，变更通过回调提交，不在 Composable 内另建一套偏好存储。
- 草稿删除需要二次确认；删除动作由宿主执行，选择状态在宿主刷新项目列表后按真实文件集合收敛，不把 UI 列表移除误报成磁盘删除。

### 本切片明确未完成的工作

- 尚未将这两个 Composable 接入 MainActivity / 正式导航，也未更改 Manifest。
- 尚未实现平台项目仓库、草稿扫描/打开/删除、项目规格读取和创建命令的端到端接线。
- 设置页当前是类型化、回调驱动的渲染层；登录态、存储、模型状态、权限、缓存、清理本机数据、导出数据、更新检查等必须由后续宿主/服务层提供真实状态和动作。未接入时不应伪造“已连接”“已完成”。
- 现有 HTML/JS 仍是完整运行回退；未执行 Gradle 构建、Compose 编译、UI 测试、TalkBack 测试或设备验证。

本切片属于隔离式 S5-2 页面实现准备，不代表 S5-2 已验收完成，也不解除 S1-3、S1-8D、S3-1 和 S5 接口冻结等前置门。


## 13. 账号 / 帮助 / 法律页面原生组件切片（2026-11-20）

在独立源码文件 `mobile/native/android/NativeAccountSupportScreens.kt` 中增加宿主驱动的页面组件：

- `NativeMeScreen`：账号摘要、手机号绑定入口、会员状态入口、可配置分组、账号退出动作；开关/选项沿用统一设置行组件。
- `NativeAuthScreen`：手机号登录入口和平台可用的第三方登录入口；Android UI 不显示当前 NativeAuthBridge 明确拒绝的 Apple 登录。
- `NativePhoneLoginScreen`：手机号/验证码表单、请求中/登录中/冷却状态由宿主提供；输入端限制字符和长度，服务端仍必须执行号码、验证码、频率和风控校验。
- `NativeFeedbackScreen`：反馈类型、描述、联系方式、提交中状态、复制和提交回调；不在 UI 内伪造反馈已送达。
- `NativeFaqScreen`：问题搜索与折叠答案。
- `NativeLegalScreen`：条款/隐私/版权/AI 说明的原生长文阅读承载。
- `NativeCrashLogScreen`：宿主提供日志列表、单条复制和导出回调；页面不自行扫描无关文件或扩大存储权限。

这些组件统一采用回调注入，认证、短信、反馈提交、法律文本、FAQ 内容、日志读取/导出均由宿主服务提供权威状态。未接服务时，页面不能显示成功状态。

**本节状态：原生页面组件已提交；尚未接入导航/Activity，尚未完成数据源、真实命令、权限与生命周期集成。** 仍未执行 Android Gradle/Compose 编译、单元/UI/无障碍测试或真机验证。它们是 S5-2 页面实现切片，不等同于 S5-2 通过验收。


## 14. 模板 / AI 创作页面原生承载切片（2026-11-20）

新增独立源码文件 `mobile/native/android/NativeCreationFlowScreens.kt`，其中宿主驱动 Compose 页面组件覆盖现有产品页面 `20-template.html`、`21-template-fill.html`、`22-template-progress.html`、`23-ai-edit.html` 的主要信息架构：

- `NativeTemplateScreen`：模板搜索、分类筛选、单选和下一步；模板项由既有 `nc_template_list` 结果映射，不在 UI 内生成虚构模板。
- `NativeTemplateFillScreen`：按模板槽位选择素材；模板、槽位和素材列表均由宿主提供，缺少素材时不允许提交生成。
- `NativeTemplateProgressScreen`：展示真实状态文本、有限进度值、失败/完成状态和可取消状态；无可信进度时显示不确定进度，不将 NaN/Infinity 渲染为百分比。
- `NativeAIEditWizardScreen`：四步流程的原生交互承载（参考素材、镜头分析结果、配乐、镜头素材分配/强度/叠加素材）；分析结果与任务执行仍由既有 AI/引擎命令提供。

**边界：** 以上是 UI 层与回调契约，不代表模板创建、参考视频分析、音乐节拍检测、叠加轨道写入、素材导入、任务取消或进度事件已完成原生端到端接线。特别是 AI 流程中的镜头分析、自动分配、配乐处理、叠加模式和生成命令必须复用现有引擎/C ABI 业务能力，不能在 Compose 中复制一套算法。当前尚未接入正式导航/Activity，未执行 Gradle 构建、测试、无障碍或设备验证。


## 15. 原生页面源码拆分与 AI 参数对照修正（2026-11-20）

为避免把全部 Compose 页面堆在一个近 60 KB 的 Kotlin 文件中，已按职责拆分为：
- `NativeHomeSettingsScreens.kt`：首页、创建项目弹窗、设置、通用设置行和时长格式化。
- `NativeAccountSupportScreens.kt`：我的、登录、手机号登录、反馈、FAQ、法律阅读、崩溃日志。
- `NativeCreationFlowScreens.kt`：模板列表、模板素材填充、生成进度、AI 参考创作向导。
- 原 `NativeAppShell.kt` 仅保留包声明和迁移说明，避免重复定义。

对照 `23-ai-edit.html` 与 `ai-edit.js` 后，补回原流程中容易遗漏的交互：创作风格（smooth/effect/curve）、强度 20%–100%、镜头智能/原有顺序、叠层密度 few/mid/all，以及现有混合模式枚举。强度控件已放回原产品流程对应的“创作方向”步骤；选项横向滚动，避免窄屏裁切。

**仍需宿主接线的回调**：风格/排序/叠层密度/混合模式变更必须映射回现有 AI 编排状态和引擎命令；本次只补 UI 表达与类型化回调，不在 Kotlin UI 层重写 AI 编排算法。此项仍未通过编译、UI 自动化或真机验收。


## 16. 页面源码路径与 PR 状态校正（2026-11-20）

本节校正前文早期切片记录与当前仓库状态之间的差异：

- 首页/创建项目弹窗/设置页面当前实现文件为 `mobile/native/android/NativeHomeSettingsScreens.kt`。
- 我的/认证/手机号登录/反馈/FAQ/法律/崩溃日志页面当前实现文件为 `mobile/native/android/NativeAccountSupportScreens.kt`。
- 模板/素材填充/生成进度/AI 创作向导当前实现文件为 `mobile/native/android/NativeCreationFlowScreens.kt`。
- `mobile/native/android/NativeAppShell.kt` 已收缩为迁移说明空壳，不再承载上述页面实现。第 12–14 节中提到该旧文件的描述应按以上拆分后的实际路径理解。
- 本次代码核对基准为 `6ee842855615ccdad3a79e896a26258c5fa8efa9`；PR #32 仍为 open / draft / unmerged。代码核对提交没有任何 status context（statuses 为空）。本节后续文档提交会推进 PR head，因此不把文档内记录的旧 SHA 当作永久当前 head。
- PR #32 仍未修改 `MainActivity`、Manifest、持久化和 JNI/C ABI；WebView 仍是实际运行入口和回退路径。原生页面组件存在不等于原生页面已接入或已通过端到端验收。

本次仅校正文档路径与状态，不改变 S1-3、S3-1、S1-8D、S5 接口冻结、A-5/A-6 审批及正式入口切换的先决条件。


## 17. S5 前置 PR 的当前阻塞复核（2026-11-20）

本轮重新读取 GitHub PR 与 commit status：

- PR #29（S3-1 Rational 项目时间数据校验）：open、draft、unmerged；head `267dd4670a705ac15313ad3a68d498a9f6bfd91e`，combined status 无 status context。
- PR #31（S3-1 C ABI 浮点时间输入校验）：open、draft、unmerged；head `a7e0aa647d1a8b72140d5ddaa997d8a06ca3a124`，combined status 无 status context。
- PR #30（为 `development/v3.3.3` 启用 PR CI）：open、draft、unmerged；head `ea4768690cedbb06221ad314e97e85c1d4526050`。因此不能假定该工作流触发修复已进入目标分支。
- PR #32（S5 原生 UI 基础）：open、draft、unmerged；本轮代码核对基准 `6ee842855615ccdad3a79e896a26258c5fa8efa9` 的 combined status 无 status context。

这些 PR 的代码切片不能视为依赖项已验收。当前可继续的范围仍限于隔离式原生组件、静态契约核对、文档和不改变运行入口的安全修复；不得把 S5-0B/S5-1 或引擎运行接线提前标为可执行/已完成。


## 18. 首页原生交互对齐修正（2026-11-20）

对照 `mobile/pages/01-home.html` 与 `mobile/assets/home.js`，本轮修正 `NativeHomeSettingsScreens.kt`：

- 搜索改为顶部显式开启/取消的就地搜索行，不再默认占用草稿列表区域；取消搜索会清空筛选词。
- 四个快捷入口恢复为两行两列，并补齐与 Web 原页面一致的名称和用途说明：AI 模板剪辑、参考仿剪、音乐卡点、智能抠像。动作仍通过 `onQuickAction` 交由宿主处理；其中 `auto` 应进入现有模板流程，其他入口按现有产品逻辑给出上下文引导，不得在首页伪装成已执行剪辑。
- 草稿元信息恢复展示文件大小，并保留时长、修改时间；新增 `formatNativeSize`，单位/精度与原页面一致。
- 草稿数量显示与空列表时隐藏“管理”操作；N 品牌标识不再与“我的”入口重复触发同一动作。

本轮代码提交：`18d0a60be624f8eaacd733dbe3160465ebb8dac2`（首页搜索/快捷入口/文件大小）；`66fd46bf30c39a5d7601557e8c0d6432e4a71f5f`（草稿头部细节）。

**验收边界：** 本次是原生 UI 组件层对照修正。宿主仍须提供真实项目列表、规格 presets、删除实现和快捷入口路由；特别是列表移除不能冒充磁盘工程删除。未执行 Android Gradle/Compose 构建、UI 自动化或真机测试。


## 19. 设置页原生宿主映射契约（2026-11-20）

对照 `mobile/pages/04-settings.html` 与 `mobile/assets/settings.js`，`NativeSettingsScreen` 的 `groups/items` 应按以下稳定 ID 由宿主构造；页面只呈现状态并回调，不自行假定服务成功：

| 分组 | 稳定 item ID | 类型 | 宿主责任 |
|---|---|---|---|
| 账号 | `row-login`、`row-logout` | ACTION | 登录导航；退出前确认并清理权威登录态 |
| 通用 | `sel-lang`、`sel-theme`、`sel-start`、`sw-guide` | CHOICE / TOGGLE | 持久化语言、外观、启动页与新手引导偏好 |
| 通知 | `sw-draft`、`sw-export`、`sw-promo` | TOGGLE | 持久化通知偏好；系统通知权限另行处理 |
| 存储 | `row-clear-cache` | ACTION | 读取真实缓存/工程/模型状态；清理前说明范围并确认 |
| 隐私与安全 | `sw-network`、`row-perm`、`row-export-data`、`row-wipe`、法律文档 ID | TOGGLE / ACTION | 权限设置跳转、数据导出、清除本地数据前二次确认、法律阅读导航 |
| 帮助与支持 | `row-faq`、`row-feedback`、`row-update`、`row-logs` | ACTION | 打开真实 FAQ/反馈/更新检查/诊断日志流程 |
| 关于 | `about-build`、`about-core`、`about-ai` 等 | VALUE | 仅显示宿主实际读取的版本、引擎与模型状态 |

`NativeSettingKind` 当前支持 ACTION、TOGGLE、CHOICE、VALUE，足以表达上述基础控件；`NativeSettingItem.destructive` 只负责危险操作的视觉提示，不是确认机制。所有危险操作必须由宿主在调用真实服务前弹出确认，并处理失败/取消。法律条款应继续复用 `assets/legal/legal-text.js` 的单一事实来源或其正式跨平台数据层，不复制一份可能过期的文案。

本节是接口映射规范，不表示以上回调、偏好持久化、系统权限、缓存管理、更新服务或日志导出已在 Compose 宿主接通。


## 20. 编辑器首个原生命令纵向切片：片段分割（2026-11-20）

对照 `mobile/assets/editor.js` 的 `runFunction('clip', {id:'split'})` 与 `mobile/native/android/nc_jni.cpp` 的 `DispatchEdit`：

- 现有 JNI 分发器明确接受 `nc_split_clip` 别名，参数为 `track_index`、`clip_id`、`at_us`；该路径最终调用现有 `nc_split_clip` C ABI，不新增或猜测引擎操作名。
- `NativeEditorCommandAdapter.splitClipAndReadEditorState()` 已增加输入校验、串行执行以及编辑后 timeline + undo/redo 状态快照读取。时间参数沿用现有编辑器播放头的绝对工程时间微秒，不把工程时间误当片段局部时间。
- `NativeEditorScreen` 新增可选宿主回调 `onSplitClip(trackIndex, clipId, playheadUs)`。仅当选中有效片段且宿主提供该回调时，“剪辑 → 分割”才走原生命令；未接线时仍走旧编辑器回退，不会显示成功假象。

**尚未完成的宿主工作：** Activity/ViewModel 尚未将回调连接到 `NativeEditorCommandAdapter.splitClipAndReadEditorState()` 并把返回快照发布回 Compose 状态。因而这是已实现的原生 UI → Adapter API 纵向切片，不是已经在正式运行界面可用的功能。

**下一批原生命令候选必须先核对路由能力：** 删除、复制、波纹删除目前只在旧 `DispatchEdit` 短操作名分支可见；`NativeEngineBridge.call()` 只接受 `nc_*` 前缀，而结构化 `applyCommand` 目前只支持 `clip.move`、`clip.split`、`clip.remove`，没有复制/波纹删除。不得直接把短操作名塞进 `call()`，也不得伪造 typed command 类型。下一步先确认稳定 track/sequence/project ID 与 revision 的可读契约，再决定删除接入路径。

**验收边界：** 本次没有修改 JNI/C ABI、`MainActivity` 或正式导航入口；没有执行 Android Gradle/Compose 编译、自动化测试或真机验证。

## 21. 原生片段分割的目标与时间边界校验（2026-11-20）

在第 20 节的分割 API 切片上补充了执行前校验：

- splitClipAndReadEditorState() 现在先在 NativeEngineBridge 同一状态锁内读取权威时间轴快照。
- 必须同时匹配当前 trackIndex 与 clipId；目标轨道或片段已失效时直接拒绝，不向 JNI 发出编辑命令。
- 分割时间必须严格位于所选片段的 [startUs, startUs + durationUs] 内部，不允许在片段起点/终点分割；计算片段终点前检查 Long 溢出。
- 校验、原生分割和编辑后 timeline + undo/redo 快照读取保持在同一可重入状态锁中，避免 UI 选择或并发编辑在校验与提交之间改变目标。

**边界：** 这项修正只加固现有 nc_split_clip 路径，不修改 C ABI/JNI、Activity、Manifest 或正式入口。UI callback 仍待宿主接线；没有执行 Gradle 构建、自动化测试或真机验证。

## 22. 原生片段删除、复制与波纹删除命令切片（2026-11-20）

本轮继续扩展 S5 编辑器命令纵向切片，复用现有 C ABI，不伪造结构化命令类型：

- JNI DispatchEdit 为现有 nc_remove_clip、nc_duplicate_clip、nc_ripple_delete 增加 nc_* 全名 alias，兼容 NativeEngineBridge.call() 的 nc_ 前缀约束；底层仍分别调用既有 C ABI。
- NativeTimelineTrack 增加 locked 状态，NativeTimelineSnapshot 严格解析 BuildState 已提供的 locked 字段。
- NativeEditorCommandAdapter 新增 removeClipAndReadEditorState、duplicateClipAndReadEditorState、rippleDeleteClipAndReadEditorState；在 bridge 状态锁内回读权威时间轴、校验轨道/片段仍存在及轨道未锁定，再执行命令并回读 timeline + undo/redo 状态。
- 复制命令必须返回正数新 clip_id，否则按失败处理；删除/波纹删除不虚构返回片段 ID。
- NativeEditorScreen 新增可选 onDeleteClip/onDuplicateClip/onRippleDeleteClip 回调；仅当宿主提供回调时走原生命令，否则保留旧编辑器回退，不显示假成功。

**契约边界：** 这些旧 C ABI 入口使用当前主序列的轨道索引 + clip ID，并由 C ABI 内部 resolveTrack 映射真实 SequenceId/TrackId；本切片不需要臆造 projectId/sequenceId，也不把 track array index 冒充稳定 TrackId。该路径通过 bridge stateLock 串行化，但不是带 expectedRevision 的结构化 CommandRequest。若未来迁移到跨平台 typed command，仍需补齐主序列 ID/项目身份与 revision 快照字段。

**未完成：** Activity/ViewModel 尚未接线这些新回调；正式入口仍受 S1-3、S3-1 与 S5 interface freeze 前置条件约束。未执行 Gradle 编译、自动化测试或真机验证。

## 23. 原生常规变速参数与 UI 切片（2026-11-20）

- NativeTimelineClip 现在保留引擎快照中的 speed_num/speed_den 有理数，不把分数速度先压成浮点数再回写；快照解析拒绝缺失、非正分子/分母及超出 C ABI 0.1x–100x 合同范围的数据。
- JNI DispatchEdit 为现有 nc_set_clip_speed 增加 nc_* 全名 alias，仍调用原有整数分子/分母 C ABI。
- NativeEditorCommandAdapter.setClipSpeedAndReadEditorState() 验证正分子/分母、0.1x–100x 范围、目标轨道/片段存在和轨道未锁定，在状态锁内执行并回读时间轴及撤销/重做状态。
- NativeEditorScreen 增加“剪辑 → 变速”原生面板；滑块和数字输入均为 0.1x–100x，数字输入支持三位小数。宿主回调未接入时仍走旧编辑器，不展示假成功。

**范围说明：** 0.1x–100x 直接来自 core/include/nairoclip/core/capi.h 的现有引擎合同，本轮没有自行扩张引擎范围。UI 以千分之一为输入精度并通过分子/分母保存；速度曲线仍未接入原生 UI，本轮不将其标记为完成。

**未完成与验证边界：** Activity/ViewModel 尚未接入 onClipSpeedChanged；未执行 Gradle 编译、自动化测试或真机验证。速度曲线的读回分支与曲线点编辑器需作为后续独立切片处理。

## 24. 原生变速曲线预设命令切片（2026-11-20）

- 对照 `core/include/nairoclip/core/capi.h`，复用既有 `nc_apply_speed_curve_preset`，不在 UI 层重新计算曲线。引擎已定义 0–6 共七种预设：自定义/恒速、蒙太奇、英雄时刻、子弹时间、跳跃、闪进、闪出。
- `nc_jni.cpp` 为该 C ABI 增加 `nc_apply_speed_curve_preset` 全名分发入口；校验预设编号 0–6 与非负 source_duration_us，再调用原有 C ABI。source_duration_us 传 0，按接口合同由引擎解析片段实际源区间。
- `NativeEditorCommandAdapter.applySpeedCurvePresetAndReadEditorState()` 在同一 editor state lock 内读取权威时间轴，检查轨道/片段存在及轨道未锁定，调用引擎预设接口并回读编辑器状态。
- `NativeEditorScreen` 增加“剪辑 → 变速曲线”预设面板。所有选项通过可选宿主回调提交；未提供回调时不展示可操作面板，也不假装执行成功。选择预设会替换当前片段曲线，这是 C ABI 的既定语义。

**尚未完成：** 当前切片只覆盖引擎内置预设应用，不是自由曲线编辑器；曲线控制点的原生读回、拖动/插入/删除、每点缓动编辑，以及预设名称从引擎动态读取仍待实现。Activity/ViewModel 尚未接入回调。没有执行 Gradle/NDK 构建、自动化测试或真机验证。

## 25. 原生速度曲线控制点读写与编辑控件（2026-11-20）

- JNI 新增 `nc_clip_speed_curve_count` / `nc_clip_speed_curve_get` 只读分发，按引擎 C ABI 读取控制点数量、源时间秒数、速度和缓动类型；响应 JSON 作为结构化结果返回。
- `nc_set_clip_speed_curve_points` 复用既有 `nc_set_clip_speed_curve` C ABI。数组分别传入 `times[]`、`speeds[]`、`easings[]`；JNI 校验数组长度一致、缓动为 0–4 整数，兼容旧版单一 `easing` 字段。
- `NativeEditorCommandAdapter.readSpeedCurve()` 限制最多 256 个点，校验源时间严格递增、速度在引擎合同 0.1–10 范围、缓动 0–4；`setSpeedCurveAndReadEditorState()` 在 editor state lock 下验证轨道/片段及锁定状态，调用引擎并回读状态。
- `NativeEditorScreen` 增加原生控制点列表：显示源时间、速度、缓动；可添加中间点、删除控制点、调节速度、循环切换缓动类型。时间仍按引擎源时间坐标排序，当前控件尚未提供时间拖拽/曲线画布。
- 预设面板继续复用引擎预设 API。预设应用与手动点编辑是两条不同路径，均通过可选宿主回调执行；没有 Activity/ViewModel 接线，不宣称正式入口已启用。

**剩余缺口：** 曲线画布、控制点二维拖动、时间数值精调、撤销/重做 UI 状态刷新，以及宿主回调接线仍待完成。JNI/Compose 修改尚未经过 Android/NDK 编译或测试；没有真机验证。

## 26. 片段音量与淡入淡出原生读写切片（2026-11-20）

- 现有 C ABI 只有 `nc_set_clip_audio`，缺少参数读回；本轮补充 `nc_get_clip_audio`，从当前主序列的目标片段读取增益 dB、淡入微秒和淡出微秒。该 getter 只读取真实 Clip 状态，不在 UI 端以默认值覆盖既有工程参数。
- JNI 新增 `nc_get_clip_audio`，并为设置命令增加 `nc_set_clip_audio` 全名 alias；两者均调用 C ABI。
- `NativeEditorCommandAdapter` 新增音频状态读取与写入：增益使用核心既有 `-60..+12 dB` 约束；淡入/淡出必须非负，且两者之和不能超过目标片段时长。写入前在同一状态锁内验证目标片段、轨道锁定状态，成功后回读编辑器快照。
- `NativeEditorScreen` 增加“音频 → 音量”和“音频 → 淡入淡出”控件。淡入/淡出滑块动态限制另一侧剩余时长，避免 UI 产生超出片段总时长的组合。
- **边界：** 目前是可选宿主回调切片，MainActivity/ViewModel 尚未加载并回传音频状态，正式入口未接线；没有编译/测试/真机验证。

## 27. 片段降噪原生控件与状态读回切片（2026-11-20）

- JNI 为现有 `nc_get_clip_denoise` 增加只读入口，并为 `nc_set_clip_denoise` 增加全名 alias；没有复制降噪算法，仍由现有核心导出混音路径处理。
- `NativeEditorCommandAdapter` 增加降噪强度读取与设置：0=关闭、1=弱、2=中、3=强；写入前验证片段存在、轨道未锁定，成功后回读编辑器状态。
- `NativeEditorScreen` 增加“音频 → 降噪”四档原生选项。宿主回调缺失时仍回退旧编辑器，不显示假成功。
- **边界：** 降噪模型/算法未在 Compose 中实现；Pro 权益门控、真实导出效果与 UI 状态需继续由现有平台服务/引擎统一管理。宿主接线、编译、测试和真机验收均未完成。

## 28. 原生片段视频特效栈切片（2026-11-20）

本轮继续推进编辑器工具栏的原生化，选择旧编辑器已有明确 JNI/C ABI 命令的“特效 → 视频特效”作为下一条纵向切片：

- NativeTimelineClip 新增效果栈模型；NativeTimelineSnapshot 严格读取 BuildState() 已输出的 effects[]（ID、类型、参数值、启用状态），拒绝缺字段、重复 ID、非有限参数或非法标志，避免面板展示伪造的空效果栈。
- 将片段速度分子/分母归属修正为 NativeTimelineClip，与快照解析和编辑器所读取的选中片段速度一致；不再错误地挂在轨道模型上。
- NativeEditorCommandAdapter 增加引擎特效规格读取（nc_effect_spec）、添加、更新、启停、排序和删除命令适配。修改前统一读取当前时间轴并校验目标片段存在、轨道未锁定；参数值必须处于引擎返回的 min/max 之内。
- NativeEditorScreen 增加原生“视频特效·效果栈”面板源码：显示已有特效、启停、上下移动、删除、按引擎规格调整参数，以及添加旧编辑器目录中 13–35 号效果类型。只在宿主提供完整回调时进入该面板；未接回调时不把 UI 假装成可用功能。
- 这一切片仍未接入 Activity/ViewModel：正式宿主尚未提供效果规格缓存、添加/更新/启停/排序/删除回调，因此目前属于原生组件 + 适配器 API + 快照数据契约，不等于用户运行时已经能使用。
- 本轮没有修改 MainActivity、Manifest、JNI/C ABI 或 WebView 入口；不越过 S1-3、S3-1、S1-8D 和 A-5/A-6 约束。
- 静态源码回读与 Kotlin/Compose 编译仍需继续验证；本轮未运行 Gradle、NDK、单元/仪器测试或真机操作。
- 后续静态检查补充：效果参数规格的 min/max/default 必须可安全转换为 Compose Float；快照拒绝超出 Float 表示域的特效值；效果参数滑块先更新本地 draft，仅在拖动结束时发出一次引擎写命令，避免每个触摸采样都生成一次编辑命令/撤销记录。

## 29. 原生片段滤镜面板切片（2026-11-20）

本轮继续完成“特效 → 滤镜”的原生纵向切片：

- 复用现有 C ABI `nc_get_clip_filter` / `nc_set_clip_filter`，未新增滤镜算法或另造参数范围。
- Android JNI 新增 `filter_get` 只读分发，返回 style、intensity、enabled；原有 `filter` 写入分发保持原样。
- `NativeEditorCommandAdapter` 新增滤镜状态读取和写入后回读，校验风格 0..13、强度 0..100，并在同一 editor state lock 内验证目标片段存在且轨道未锁定。
- `NativeEditorScreen` 新增原生滤镜选择条与强度滑块，按旧规格表的 14 个样式（原片 + 13 种滤镜）呈现；强度滑块在拖动结束后才发出写命令。
- 强度同时提供 0–100 的数字精调输入，与滑块使用同一允许范围；原片模式禁用强度输入与滑块。
- 该面板仍未接 Activity/ViewModel：需要正式宿主提供 `readClipFilter` / `setClipFilterAndRead` 状态发布和错误反馈回调，当前不代表运行时已经可用。
- 同步发现并修正上一轮特效目录遗漏引擎现有 36–38 号类型（震动、泛光、镜头光晕）；此前 13–35 列表不完整。C ABI `EffectType::LensFlare` 是当前上限来源。
- 本轮未修改 MainActivity、Manifest 或正式入口；未运行 Gradle/NDK 编译、自动化测试或真机验证。

## 30. 原生片段蒙版状态读取切片（2026-11-20）

本轮从旧编辑器“蒙版”功能切入，先补齐原生参数面板所需的权威读取链路：

- Android JNI 新增只读 `mask_get` 分发，直接调用现有 C ABI `nc_get_clip_mask`，返回形状、中心、旋转、宽高、羽化、圆角、反转和启用状态；目标片段/轨道缺失时明确失败。
- `NativeEditorCommandAdapter` 新增 `ClipMaskState`、`readClipMask` 与 `setClipMaskAndRead`：在 editor state lock 内验证目标片段存在且轨道可编辑；写入整组蒙版参数后立即回读引擎状态。适配器拒绝缺失、非数值、非有限或超出核心已声明域的状态。
- 参数校验严格遵循 `core/include/nairoclip/core/mask.h`：形状枚举 0..6；羽化与圆角 0..0.5。宽高属于形状相关归一化尺寸，不擅自限制在 0..1；中心/旋转/尺寸只要求有限数值，不在 UI 适配层发明核心未声明的硬范围。
- `NativeEditorScreen` 新增原生蒙版参数面板：形状（关闭/线性/镜面/圆形/矩形/星形/爱心）、反转、归一化中心 X/Y、羽化、圆角、旋转和宽高数值输入；滑块在拖动结束时提交，数值字段在完成/失焦时校验提交。工具栏“特效 → 蒙版”仅在宿主提供 `clipMask` 与 `onClipMaskChanged` 时进入原生面板，否则保持旧编辑器回退路径。
- 蒙版面板、JNI 和 Kotlin 适配器尚未接入 Activity/ViewModel；目前属于可接线的原生 UI/API 切片，不代表运行时已启用。
- 尚未运行 Android Gradle/Compose/NDK 编译、单元/仪器测试或真机验证；正式宿主接线仍受 S5 前置门约束。

## 31. 原生转场读写与面板切片（2026-11-20）

- Android JNI 新增 `transition_get`：使用现有 C ABI `nc_transition_count`、`nc_transition_get`、`nc_transition_get_id` 读取轨道转场 ID、类型、时长、两端片段和名称；逐项检查返回码并释放引擎分配的名称字符串。
- `NativeEditorCommandAdapter` 新增转场状态模型、轨道转场读取、引擎转场类型数量/名称读取、添加与删除接口。添加前校验轨道存在且未锁定、两端片段均存在于同一轨道且时间轴首尾严格相接、片段 ID 不相同、转场类型属于引擎枚举、时长为正；删除前校验轨道存在且未锁定。
- `NativeEditorScreen` 新增“特效 → 转场”原生面板：从引擎类型库展示类型，允许输入微秒时长，显示当前片段关联转场并提供删除；只有时间轴中严格首尾相接的后续片段可作为新转场终点，避免跨空隙错误创建。宿主未提供回调时仍走旧编辑器。
- 转场面板、类型库与适配器尚未接入 Activity/ViewModel；目前是可接线的原生 UI/API 切片，不代表运行时已启用。
- 本轮未执行 Android Gradle/Compose/NDK 编译、自动化测试或真机测试。


## 32. 原生片段 LUT 调色面板切片（2026-11-20）

- Android JNI 新增 `lut_get` / `lut` 命令：读取片段 LUT 路径与强度，写入时调用现有 C ABI `nc_set_clip_lut`；读取后释放引擎分配的路径字符串，并对 JSON 路径执行转义。
- `NativeEditorCommandAdapter` 新增 `ClipLutState`、`readClipLut` 与 `setClipLutAndRead`；写入前校验目标轨道/片段存在、轨道未锁定、强度为有限数且位于核心声明的 0..1 域，写入后回读真实状态。
- `NativeEditorScreen` 新增“调节 → LUT”原生参数面板：编辑 .cube 3D LUT 文件路径、强度滑块与数值输入、应用和清除操作。滑块和数值输入统一使用核心 0..1 范围；滑块在拖动结束时提交，数值在失焦时校验提交。
- 不创建虚假的内置 LUT 预设。核心接口说明当前内置 LUT 预设数量为 0；本切片仅支持用户输入设备可访问的 .cube 路径。原生文件选择器、URI 到应用可读路径的授权/复制流程尚未实现，因此不能宣称已完成导入体验。
- 面板、JNI 和适配器尚未接入 Activity/ViewModel；只有宿主提供 `clipLut` 与 `onClipLutChanged` 时才进入原生面板，正式运行时仍未启用。
- 本轮未执行 Android Gradle/Compose/NDK 编译、单元/仪器测试或真机验证。


## 33. 原生转场与 LUT 切片的边界校验补强（2026-11-20）

- 修正转场类型列表异步加载时选中类型仍停留在初始占位值的问题：类型列表更新后，默认选择同步回到当前引擎类型列表的首项。
- 转场读取适配器现在会验证每条转场的起止片段 ID 不同，且两个端点都仍存在于该轨道当前快照；不再只校验字段格式与正数。
- LUT 路径校验改用 NUL 字符检测，避免 Kotlin 字符字面量转义歧义。
- 这些属于源码级静态修正；未执行 Android/NDK 构建或运行时验证。


## 34. 原生 LUT 文件格式校验入口（2026-11-20）

- JNI 新增 `lut_validate`，调用核心 `nc_lut_validate_file`，返回 .cube 边长、TITLE 与诊断信息；无论成功或失败均释放核心分配的字符串，失败时将核心给出的具体原因传回 UI。
- `NativeEditorCommandAdapter.validateLutFile` 增加只读校验入口，不修改工程；拒绝空路径和 NUL 字符，并验证核心返回的边长位于 2..64。
- LUT 原生面板新增“校验 .cube”操作，可展示校验结果及 LUT 标题。该操作依赖宿主提供 `onValidateClipLut` 回调；正式 Activity/ViewModel 尚未接线，因此目前是可复用的原生 UI/API 切片。
- 文件选择器、Android URI 持久授权、导入文件复制/生命周期管理仍未实现；当前输入完整路径不是最终用户导入体验。
- 本轮未执行 Android Gradle/Compose/NDK 编译、自动化测试或真机验证。


## 35. LUT 应用前置文件校验（2026-11-20）

- LUT 强度滑块拖动结束、数值输入失焦和“应用路径与强度”操作统一经过核心 .cube 文件校验；校验失败或宿主尚未提供校验回调时，阻止将非空路径写入片段。
- 清除 LUT 仍允许直接提交空路径，不要求文件校验。
- 校验成功后才调用宿主写入回调，并显示边长与文件标题；该交互仍需宿主接入 `validateLutFile` 与 `setClipLutAndRead` 才能形成完整运行时闭环。
- 未执行构建或运行时测试。


## 36. 原生变速曲线二维预览（2026-11-20）

- 在现有原生速度曲线控制点列表上方增加二维曲线预览：横轴按控制点源素材时间归一化，纵轴按引擎允许的 0.1×–10× 速度域映射；网格、折线和控制点均由当前真实控制点数据绘制。
- 预览不生成或写入新的曲线数据，不改变引擎插值/缓动语义；曲线编辑仍由现有控制点列表、速度滑块和缓动操作提交。
- 仅在至少存在两个真实控制点时显示曲线图，空曲线仍明确提示当前没有自定义曲线，避免展示虚构默认曲线。
- 这是 Compose 源码切片；尚未接入 Activity/ViewModel 的速度曲线状态加载与回调，不能视为正式运行时启用。
- 未执行 Android Gradle/Compose 编译、自动化测试或真机 Canvas 渲染验证。

## 37. 原生变速曲线直接操控与时间精调（2026-11-20）

- 二维曲线预览升级为可交互控制区：用户可直接拖动控制点调节速度，内部控制点还可沿时间轴移动；首尾控制点的源素材时间固定，避免改变曲线定义区间。
- 拖动速度严格限制在核心已有的 0.1×–10×；内部时间点限制在相邻控制点之间，并保留至少 2ms 的严格递增间隔。
- 曲线拖动期间只更新 Compose 本地草稿，手势结束后统一发出一次变更回调，避免每个触摸采样都写引擎或生成多条撤销记录。
- 增加内部控制点源素材时间数值输入；输入值必须为有限数，且落在相邻控制点允许区间内。无效输入恢复显示当前有效值，不写入引擎。
- 控制点绘制触控半径扩大，改善手机端直接操作命中率；现有列表、速度滑块和缓动选择继续保留。
- 该修改仍属于 Compose 组件源码切片；宿主状态读取/写入接线、Gradle 编译、自动化测试和真机拖动验证尚未完成。首尾时间点固定是 UI 编辑约束，不更改引擎曲线数据结构。

## 38. 原生基础画面调节效果栈面板切片（2026-11-20）

- 将旧版 `l3-spec.js` 的 8 项基础调节映射到内核既有通用片段效果接口：亮度(1)、对比度(2)、饱和度(3)、色温(4)、色调(5)、暗角(9)、锐化(10)、颗粒(11)。
- 原生面板复用 `nc_effect_spec` 返回的权威名称、单位、提示和数值上下限；不把旧规格表的范围硬编码为运行时权威值。
- 新增/更新/启停/删除仍复用既有 JNI 与 Adapter 的真实效果栈命令；参数范围由引擎规格校验，拖动结束后提交。
- 该面板只显示上述八类基础调节效果。由于底层效果可能与其他效果共用同一片段效果栈，基础调节视图隐藏其他类别时禁用上下移动按钮，避免在不完整列表上重排完整效果栈造成隐藏项错位。
- 旧版规格中的曲线、色轮仍未迁移；本节不代表专业调色曲线/色轮 UI 已完成。
- 该切片仍需宿主注入真实效果规格与读写回调；未接入 Activity/ViewModel，未启用正式运行时入口。尚未执行 Android/Compose 编译、自动化测试或真机验证。


## 39. 原生 LUT SAF 文件导入与工程私有副本（2026-11-20）

- “调节 → LUT”面板增加 Android Storage Access Framework 文档选择入口（OpenDocument），用户主动选择文件；不申请广泛存储权限、不扫描共享存储。
- 导入器只接受扩展名为 .cube 的文件，将内容复制到应用私有持久目录 `filesDir/nairoclip/luts`，先写临时文件再重命名；工程写入私有副本的绝对路径，不依赖 Content URI 的临时授权或原文件后续移动。
- 导入完成后仍调用现有引擎 `nc_lut_validate_file` 校验格式/边长，再走现有 LUT 写入与回读链路；导入本身不代表 LUT 已通过核心校验或已成功应用。
- 不自动删除历史 LUT 副本，避免删除仍被已保存工程引用的文件；后续需在项目资源管理/引用计数层统一做安全回收。该文件存储实现仅属于 Android 平台适配，跨平台层仍应抽象为“导入到应用管理资源并返回稳定资源引用”，iOS/桌面不能复用 Android URI 或绝对路径语义。
- 本切片新增 Activity Compose 与 Android Coroutines 显式依赖；未修改 MainActivity、Manifest、正式入口或 S5 宿主接线。
- 尚未执行 Gradle/NDK 编译、自动化测试、SAF 真机选择、应用重启后工程 LUT 回读验证；不能将此源码切片视为完整运行时闭环。


## 40. 专业调色曲线与色轮的核心能力门禁（2026-11-20）

本轮对旧版调节面板、核心效果实现与 C ABI 做交叉检查，确认“曲线/色轮”不能直接作为现有基础调节面板的另一个 UI 分页接入：

- 当前 `core::EffectType` / `EffectSpec` 是单一标量效果模型；亮度、对比度、饱和度、色温、色调等均为单值参数。现有 `nc_add_effect` / `nc_update_effect` / `nc_effect_get` 等片段接口只承载 effect type + scalar value。
- `ColorTransform` 当前聚合标量亮度、对比度、饱和度、色温、色调及少量风格效果，没有 RGB/Log/HSL 多通道曲线控制点模型，也没有 Lift/Gamma/Gain、色轮向量或其序列化/读回接口。
- 当前 `nc_adjustment_*` 接口能对 Adjustment 轨道添加/移除标量效果并读取效果栈/LUT，但没有完整的效果参数更新、启停与重排闭环；因此也不能把它误当作专业调色层已经具备的底层能力。
- 旧版 `l3-panel.js` 的通用“曲线”画布主要生成 `{t,v}` 控制点；对非速度参数它并未提供与核心持久化、预览和导出闭环相匹配的专用曲线接口。仅复刻画布会产生无法可靠保存/渲染的假面板。
- **实施门禁：** 在实现专业曲线/色轮 UI 前，先定义跨平台颜色分级数据模型（通道/色彩空间/控制点或色轮参数、版本与默认值）、C ABI 读写与校验、工程序列化兼容、预览/导出一致的渲染语义及撤销重做命令；之后再做 Compose/iOS/桌面共用的参数契约与各平台原生控件。不能只加 UI 或把曲线伪装成一组普通标量效果。
- 本轮仅完成能力审计与门禁记录，未声称曲线或色轮功能已迁移；需要后续作为核心引擎能力切片推进。


## 41. 片段级 RGB 主曲线核心模型与预览/导出接入（2026-11-20）

- 新增跨平台核心模型 `ColorGrade`：Master、Red、Green、Blue 四条归一化 1D 曲线；默认均为恒等曲线，输入/输出在 [0,1]，每条曲线 2–32 个控制点，输入严格递增且两端固定为 (0,0)/(1,1)。
- 曲线按线性分段插值；应用顺序为 Master 后各自 RGB 通道。当前切片没有添加 HSL 曲线、Hue-vs-Sat、色轮、Lift/Gamma/Gain，也没有擅自给这些尚未定义的控件定范围。
- `.nclip` v1 的片段新增可选 `colorGrade` 对象，带独立 schema version=1；旧工程缺字段时加载恒等曲线，当前读写器严格拒绝版本未知、控制点越界/乱序/非有限或端点不合规的数据。因字段可选且旧版工程仍可加载，未提升整体文件版本。
- 片段模型校验加入曲线有效性检查。预览和导出都调用同一 RGBA 核心曲线函数；预览视觉缓存指纹包含四条曲线的全部控制点，避免调曲线后复用旧帧。导出单片段、转场两侧和多层下方片段路径均接入。
- 像素处理保留 Alpha；曲线有效性在整帧入口校验一次，避免每像素重复扫描控制点，减少 CPU 负担。
- 新增曲线插值、非法控制点、主曲线与 RGB 顺序、RGBA Alpha 保留、工程 round-trip 与非法序列化输入测试。
- **仍未完成**：编辑命令/撤销重做与 C ABI/JNI 接口、Compose 曲线控件、宿主状态回读写入、iOS/桌面 UI；色轮模型和调色 UI 未实现。当前是核心模型、持久化和预览/导出接入切片，不是完整的专业调色面板。
- 本轮未在本环境执行 CMake/Android Gradle 编译、测试运行或真机预览导出；静态回读检查不能替代构建和测试。


## 42. RGB 曲线撤销命令与跨平台 ABI/JNI 对接（2026-11-20）

- `TimelineEditor::setClipColorGrade` 将四条曲线作为完整片段状态，通过既有 `SetClipCommand` 一次提交；一次修改只生成一条“调节颜色曲线”撤销记录，Undo/Redo 恢复完整曲线组。
- 新增 C ABI `nc_set_clip_color_grade` / `nc_get_clip_color_grade`。读写采用每通道扁平 XY 数组，明确 2–32 点、[0,1]、输入严格递增、固定端点校验；读取缓冲容量不足返回错误码。
- Android JNI 新增 `color_grade_set` / `color_grade_get` 分发，Kotlin Adapter 增加曲线模型、严格响应校验、轨道锁定/片段存在检查，以及修改后权威状态回读。该 Adapter 尚未接入 Compose 面板或 Activity/ViewModel。
- 新增 C ABI 测试源码覆盖设置/读取、Undo/Redo 和非法控制点拒绝。
- iOS/桌面可复用相同 C ABI 数据契约；当前尚未完成 Swift/Qt 绑定。
- 本轮没有执行 CMake/NDK/Gradle 编译、测试运行或真机验证。不能据此声称 JNI 运行时已验证，也不能声称曲线控件 UI 已完成。


## 43. 原生 RGB 曲线编辑面板（2026-11-20）

- `NativeEditorScreen` 的“调节 → 曲线”新增 Compose 原生面板入口，支持主曲线/R/G/B 通道切换、二维曲线绘制、控制点拖动、添加与删除内部点，以及 X/Y 数值精调。
- 首尾端点锁定；内部点拖动/数值输入都限制在相邻点之间，保留严格递增的 X 轴；输出 Y 限制在 [0,1]；控制点总数 2–32。UI 校验与核心模型契约一致。
- 面板支持当前通道重置为恒等曲线；只通过 `clipColorGrade` 状态和 `onClipColorGradeChanged` 回调工作。缺少宿主注入时明确显示等待状态，不伪造本地成功或绕过核心命令。
- 面板当前未接入 Activity/ViewModel 的状态加载与回调，也未启用正式原生入口；因此源码组件完成不等于 Android 运行时已切换。
- 色轮仍未实现；不把 RGB 曲线误称为完整调色套件。本轮未执行 Compose/Gradle 编译、测试或真机交互验证。


## 44. RGB 曲线 C ABI 边界测试补强（2026-11-20）

- 补充重复提交同一组曲线的 no-op 断言，确保不会生成“撤销后看似无变化”的空历史记录。
- 补充空指针写入拒绝与输出缓冲区容量不足错误码断言。
- 原生曲线图拖动手势改用 `rememberUpdatedState` 读取最新控制点，不再把每帧变化的 points 作为 `pointerInput` key，避免拖动过程中手势协程因状态更新而被取消。
- 仍未替代真实 Android 编译与仪器测试；需在 Compose/NDK 构建环境验证手势、键盘焦点和 JNI 数据传递。


## 45. RGB 曲线工具栏路由修复（2026-11-20）

- 修复“调节 → 曲线”二级工具点击时遗漏原生路由的问题：此前工具栏分支没有匹配曲线项，会落入 `onOpenLegacy()`，导致已经编写的 Compose 曲线面板无法通过正常工具栏打开。
- 新增原生曲线面板路由；只要求存在选中片段和曲线修改回调。允许状态仍在异步加载时先进入面板并显示等待提示，与其他参数面板的宿主加载行为一致。
- 该修复只完成 UI 路由条件，不代表 Activity/ViewModel 已提供曲线初始状态和写入回调；缺少宿主注入时仍不能实际读写工程。


## 46. RGB 曲线提交后的撤销状态一致性（2026-11-20）

- 新增 `setClipColorGradeAndReadEditorState`，在同一 bridge state lock 内执行目标片段/轨道锁定检查、曲线命令提交、权威曲线回读，以及时间轴与 Undo/Redo 状态回读。
- 返回 `ClipColorGradeMutationResult`，同时包含引擎确认后的四通道曲线和编辑器历史快照，宿主可用同一结果刷新曲线面板与顶部撤销/重做状态，避免曲线更新成功但历史按钮滞后的状态分裂。
- 原 `setClipColorGradeAndRead` 保留兼容签名，内部复用新事务接口并只返回曲线值。
- 该接口仍是 Adapter 层能力；Activity/ViewModel 尚未接入，当前没有运行时宿主调用。未运行 Kotlin/Gradle 编译或测试。


## 47. 原生编辑器宿主状态归约契约（2026-11-20）

- 新增 `NativeEditorHostState.kt`，将原生编辑器宿主需要发布的不可变状态集中为时间轴快照、Undo/Redo 历史、选中片段、播放头、刷新状态和错误状态。
- 新增纯函数 `NativeEditorHostReducer` 与事件类型：刷新开始/成功/失败、选择/清除片段、播放头变化、命令提交/失败和错误清除。宿主负责执行 Adapter I/O，再将成功回读结果作为一个事件分发。
- 时间轴刷新失败时保留最后一次有效快照；刷新/命令成功时，时间轴与 Undo/Redo 从同一 `EditorStateSnapshot` 一起更新，避免 UI 只更新一半。
- 选择状态会在快照更新后重新校验：轨道必须仍存在且可见，片段必须存在且启用；播放头被限制在当前工程时长内。重复/分割等命令可通过引擎返回的新片段 ID 指定后续选择。
- 该状态归约器不持有 Activity、View、JNI 或引擎生命周期，不改 MainActivity/正式入口，供后续 S5 宿主接线使用。它是 Android 宿主层状态契约，不替代未来 iOS/桌面各自的 UI 状态实现；跨平台编辑语义仍由共享核心和 C ABI 定义。
- 本轮仅完成源码新增和审计记录；未执行 Kotlin/Gradle/Compose 编译、单元测试或真机验证，也未将该 reducer 接入当前 WebView 运行入口。


## 48. 原生编辑器宿主归约器单元测试基础（2026-11-20）

- Android app 模块新增 JUnit 4 测试依赖，并新增 `NativeEditorHostReducerTest`。
- 覆盖刷新失败保留最后有效时间轴与选中状态、轨道隐藏后清除选中片段、工程时长变化后播放头夹紧、命令提交时历史与时间轴同批更新、重复/分割指定的新片段必须存在，以及非法片段选择拒绝。
- 测试源码已加入标准 `android/app/src/test/java` 源集；尚未执行 Gradle 测试任务，不能声称测试通过。Android/NDK 构建和设备验证仍未运行。


## 49. 剪辑工具栏原生动画关键帧入口（2026-11-21）

- 将原生工具栏“剪辑 → 动画”从默认旧编辑器回退分支接入现有原生参数/关键帧面板。
- 动画模式复用已存在的 X、Y、缩放、旋转四个关键帧通道、关键帧列表、缓动类型切换/曲线预览和删除能力；新增当前通道“添加关键帧”入口，沿用宿主提供的 `onInsertKeyframe` 回调与当前播放头时间语义，不复制另一套关键帧数据模型。
- 该入口需要已选中的有效片段。关键帧初始状态、当前播放头到片段本地时间的换算以及 JNI 写入仍由宿主注入；本轮没有伪造成功状态，也没有接入 Activity/ViewModel。
- 代码已静态回读确认路由、面板渲染条件、标题及新增入口均存在。未运行 Compose/Gradle 编译、单元测试或真机手势验证。

## 50. 原生关键帧适配器六通道值域校验（2026-11-21）

- 对照共享核心 `ClipProperty` 与 C ABI，确认动画属性编号为 0..5：透明度、缩放、位置 X、位置 Y、旋转、音量。
- 修复 Android Adapter 的通用关键帧写入/读取校验缺口：此前缩放、位置、旋转已有值域校验，但透明度与音量只检查有限数，越界值会继续下发 JNI，最终依赖核心层拒绝。
- 新增纯函数规则对象 `NativeKeyframeValueRules`，Adapter 在关键帧写入前和读取回包时统一调用：透明度 0..1、音量 0..4；其余四个变换通道继续使用既有统一硬范围，缩放仍按引擎倍率 0.05..10.0 校验。
- 新增 `NativeKeyframeValueRulesTest`，覆盖透明度/音量边界、四个变换通道的代表性边界及 NaN/Infinity 拒绝。测试源码已加入 JUnit 源集，但尚未执行。
- 该修正补齐 Adapter 边界防御，不代表原生 UI 已开放透明度/音量动画通道，也不替代核心 C ABI 校验。Android/Kotlin 编译与单元测试尚未执行。

## 51. 原生关键帧通道时间顺序校验（2026-11-21）

- Adapter 读取每个动画通道后，除逐帧检查非负时间外，新增整组时间戳校验：必须严格递增，不允许重复时间戳。
- 该校验防止异常/损坏的引擎回包把重复关键帧交给 Compose 面板，避免列表选择、删除和曲线显示对同一时间点产生歧义。
- 规则集中在纯函数 `NativeKeyframeValueRules.validateTimes`，并补充单元测试覆盖空通道、正常递增、负时间、重复时间和逆序时间。
- 本项仅强化读取边界，不修改核心关键帧排序策略，也不改变项目时间到片段本地时间的换算契约。Gradle 测试与真机验证尚未执行。

## 52. 原生文本片段创建面板与事务适配器（2026-11-21）

- 将“文本 → 添加文本”接入原生 Compose 面板；支持多行正文输入、UTF-8 16 KiB 输入上限、播放头位置提示与创建操作。默认样式采用核心 C ABI 文档默认值：48 画布点、白色、居中、归一化锚点 (0.5, 0.5)，片段默认时长 3 秒。
- `NativeEditorCommandAdapter.addTextClipAndReadEditorState` 先检查文本非空/长度、时间溢出与工程时长，再在同一编辑器状态锁内调用已有 `nc_add_text_clip` JNI 分发，并回读权威时间轴及 Undo/Redo 快照；返回新文本片段 ID 和同批编辑器状态。
- JNI 与核心 C ABI 的 `nc_add_text_clip` 已存在，本切片不新增底层协议、不修改 MainActivity/Manifest/正式入口。
- Compose 面板通过可选 `onCreateTextClip` 回调交给宿主执行；当前仍未接入 Activity/ViewModel，因此没有声称正式运行时已启用。宿主需在成功后用返回的 `TextClipMutationResult.editorState` 更新时间轴/历史，并可按 `clipId` 定位新片段。
- 未执行 Android Gradle/Compose/NDK 编译、JUnit 或真机验证；当前仅可做源码回读与静态检查。

## 53. 原生文字样式局部编辑与 JNI 哨兵修复（2026-11-21）

- “文本 → 样式”新增 Compose 原生样式面板：字号、描边宽度、填充色预设、左/中/右对齐。只在存在选中片段且宿主回调已注入时进入该面板。
- Adapter 新增 `TextStyleUpdate` 与 `setTextStyleAndReadEditorState`：只提交用户明确修改的字段；字号 8–256 画布点、描边宽度 0–32 画布点、颜色严格接受 `#RRGGBB` / `#RRGGBBAA`、对齐 0–2。空修改拒绝；在状态锁内校验轨道/片段/锁定状态，提交后回读时间轴和撤销/重做状态。
- 修复 JNI `nc_set_text_style` 的空描边颜色处理：此前 JNI 将空字符串强制替换为黑色，破坏 C ABI“空值表示保持原值”的哨兵契约；现在原样传递空字符串，且缺省描边宽度改为 -1 保持不变。这样局部修改字号/颜色/对齐不会意外重置描边颜色或宽度。
- 核心 `SubtitleStyle` 使用画布点字号、归一化锚点与 0/1/2 对齐；本面板不擅自加入未定义的字体族、背景框或锚点精细拖动。C ABI 没有文本样式 getter，因此面板不伪造当前选中片段的样式回显，控件提交为明确的新值。
- 回调仍需 Activity/ViewModel 注入；未接正式运行时入口。未执行 Kotlin/Compose/NDK 编译、JUnit 或真机验证。

## 54. 原生字幕轨 CRUD 与字幕编辑入口（2026-11-21）

- 为既有字幕 C ABI 增加 JNI DispatchEdit 路由：`subtitle_upsert`、`subtitle_remove`、`subtitle_list`。列表读取逐条调用 `nc_subtitle_get`，并对每个 `char*` 使用 `nc_free_string`，避免字幕文本内存泄漏；文本通过 JSON 转义后返回。
- Adapter 新增字幕列表模型、读取、添加和删除操作。添加时校验 UTF-8 16 KiB 上限、非负起始时间、正时长及工程时长，并将字幕时长裁到工程剩余时长；写入后回读权威时间轴/历史状态。
- 编辑器“文本”工具新增独立“字幕”入口和原生 Compose 输入面板。此入口是人工创建/编辑字幕 cue，不宣称具备语音识别或自动转录；“自动字幕”仍需真实 ASR 任务管线，未伪装为已实现。
- 初始 C ABI 列表读取未返回 subtitle ID；第 55 节已补充 `nc_subtitle_get_id`，列表现返回稳定 ID，Adapter 校验 ID 唯一并以 ID 删除，不用 index 冒充 ID。
- UI 回调仍需 Activity/ViewModel 注入；未接正式运行时入口。未执行 Kotlin/Compose/NDK 编译、JUnit 或真机验证。

## 55. 字幕稳定 ID 读取接口（2026-11-21）

- 发现既有 `nc_subtitle_get` 只返回时间与文本，不返回字幕 ID；若用列表 index 删除，会在插入/排序/删除后误删其他字幕。
- 新增跨平台 C ABI `nc_subtitle_get_id`，按字幕轨索引读取真实 `SubtitleId`；参数、越界均返回明确错误码。
- JNI `subtitle_list` 现在返回 `subtitle_id`；Adapter 的 `SubtitleCueState` 持有稳定 ID，并拒绝零 ID 与重复 ID。删除命令只接受真实稳定 ID，不将 index 作为删除标识。
- `tests/test_capi.cpp` 的 `Capi.SubtitleCrud` 补充稳定 ID 一致性、空输出指针、越界及相同时间/相同文本重复插入仍返回不同精确 ID 的测试；修复 `nc_upsert_subtitle` 原先按时间+文本搜索返回 ID 的歧义。原生字幕面板可展示前 30 条字幕，并在宿主注入删除回调时按稳定 ID 删除。
- 未执行 C++/NDK/Kotlin 编译及测试。

## 56. 字幕 ID 精确返回与列表删除闭环补强（2026-11-21）

- `nc_upsert_subtitle` 原实现通过时间戳和文本反查最后一条字幕 ID；同一时刻重复插入相同文本时可能返回错误记录 ID。现在在移动 cue 前捕获本次分配的 ID，成功后直接返回该 ID。
- C API 测试覆盖相同时间、相同文本的重复插入，确认两次返回 ID 不同、列表均可读取到对应 ID，且两条均可按 ID 删除。
- 原生字幕面板支持展示前 30 条字幕及按稳定 ID 删除；超过 30 条会显示提示。完整列表与删除回调由宿主注入，未声称 Activity/ViewModel 已接线。
- 未执行 C++/NDK/Kotlin 编译、CTest/JUnit 或真机验证。

## 57. Whisper 自动字幕模型契约与推理门禁修复（2026-11-21）

- 发现模型下载脚本提供 `whisper-small`，但 `SpeechRecognizer` 的 KV cache 结构固定为 Whisper-tiny 的 4 层 × 6 heads × 64 dim（16 个 cache 输入）；small 模型结构不匹配，可能在推理时失败。
- 模型下载源统一改为 `onnx-community/whisper-tiny`，使用 encoder + `decoder_model_merged.onnx` + tokenizer，和现有单 decoder 自回归实现对齐。
- `SpeechRecognizer::load` 现在在模型加载阶段检查 encoder 输入形状 `[1,80,3000]`、decoder 19 输入/至少 17 输出、Int64 token / Float32 encoder / Bool cache 分支，以及 16 个 KV cache 的名称、类型、rank/head/head_dim 和 `present` 输出；错误模型立即拒绝并释放会话。
- 解码期间若没有返回完整 KV cache，不再静默沿用旧 cache，而是明确失败。
- 修正旧 `RealInferenceChain` 测试注释：现有核心代码已有完整 KV cache 自回归循环；测试可从 `/workspace/deps` 或仓库 `models/whisper_zh` 查找 tiny 模型文件。实际 ONNX 模型推理仍需具备 ORT 和模型文件的环境验证。
- Android JNI 尚未提供 PCM `FloatArray` 直传及 ASR 后台执行入口，编辑器“自动字幕”尚未运行时接通；本切片只修复模型/推理契约，不宣称自动字幕已可用。
- 未执行 C++ 构建、CTest、模型下载或真实语音推理。

## 第58节：Whisper 密文模型的内存加载基础

- 已确认模型管理契约要求模型文件磁盘态始终为 AES 密文、密钥只在内存，推理前才解密；旧版 AiSession 和 SpeechRecognizer 仅接受 ONNX/Tokenizer 文件路径，不能直接满足该安全契约。
- AiSession::loadFromMemory 新增 ONNX Runtime 内存构造路径，允许从解密后的字节缓冲创建 Session，不写出明文模型文件；原路径加载接口保持不变。
- SpeechRecognizer::loadFromMemory 新增 Whisper-tiny encoder/merged decoder 内存加载，并复用同一套张量结构门禁（encoder [1,80,3000]、decoder 19 输入/至少17输出、16组 KV cache）。
- SpeechRecognizer::loadTokenizerJson 新增内存 JSON 词表加载；文件路径接口改为读取后委托内存解析，原行为保持兼容。
- tests/test_ai_engine.cpp 增加内存 ONNX 空缓冲失败路径和真实微型 ONNX 模型内存加载/推理测试（需要 ORT 与测试模型时执行）。
- tests/test_speech_ai.cpp 增加内存词表解析/解码与空模型缓冲失败路径。
- 这只是安全加载基础：尚未实现平台层读取密文文件、密钥获取、AES 解密后将三份缓冲传入 C ABI/JNI，也尚未实现后台 ASR 任务状态机和字幕回写；不能视为自动字幕已经接通。
- 尚未运行 C++ 编译、CTest、Android 构建或真实设备推理。

## 第59节：内存模型缓冲区生命周期与清理

- 复核 ONNX Runtime 的内存 Session 生命周期时，不再假定调用方缓冲区在创建 Session 后即可立即释放；`AiSession` 现在保存自己的模型字节副本，确保缓冲区至少覆盖 Session 整个生命周期。
- `AiSession::unload()` 先销毁 ORT Session，再对持有的模型字节执行 volatile 覆写并释放容量；这属于尽力清理，不等同于可证明的安全内存擦除，也无法清除 ORT 内部副本、系统交换区或崩溃转储。
- `loadFromMemory()` 先复制模型数据，再从受控副本创建 Session；失败路径复用 unload 清理。
- 调用方仍应在 loadFromMemory()/loadTokenizerJson() 返回后尽快清零其自有解密缓冲。平台密钥获取、AES 解密调用和 Android JNI 接入仍未完成。

## 第60节：Whisper 识别器释放语义

- `SpeechRecognizer` 析构时显式调用 `unload()`，确保 encoder/decoder 的 `AiSession` 按序销毁并触发模型缓冲清理。
- `SpeechRecognizer::unload()` 同时清空 tokenizer 的词表、反查表和 added-token 表，并重置 `tokenizerReady`，避免释放模型后仍保留可被误用的词表状态。
- 新增 `UnloadClearsTokenizerState` 测试，验证卸载后 tokenizer 状态关闭、token 解码不再返回旧文本。
- 注意：标准容器释放不保证底层 allocator 内存被密码学意义擦除；本改动保证对象生命周期和状态一致，不宣称完整安全擦除。

## 第61节：自动字幕密文模型接入安全审计（2026-11-21）

- 复核 `ModelManager`、`crypto`、Android `NativeEngineBridge.installModel` 与 Whisper 内存加载接口，确认当前分层边界：
  - Core 的 `verifyModelPayload` 校验密文长度/ SHA-256；`decryptModelPayload` 使用 AES-256-CTR 解密，但它只返回普通字节向量，不负责平台密钥获取或密钥生命周期。
  - `SpeechRecognizer::loadFromMemory` 可接收已解密的 encoder/decoder 缓冲；`loadTokenizerJson` 可接收已解密的 tokenizer JSON。
  - Android 现有 `installModel` 从传入路径读取完整文件并计算 SHA-256，再调用 `nativeInstallModel` 记录安装状态；这不是推理时的密文解密加载 API，也没有将模型明文缓冲交给 Whisper recognizer。
  - 目前未发现 Android Keystore 密钥获取、模型密钥注入、AES 解密到受控 native 内存、SpeechRecognizer JNI 装载及 ASR 后台任务接口的完整闭环。
- 发现必须在多组件模型包设计前解决的密码学约束：当前 `deriveModelCounter(modelId, version)` 仅由模型 ID 与版本确定 AES-CTR 初始计数器。如果多个密文组件使用相同 AES key，且复用相同 modelId/version，则会复用同一密钥流。不得在没有服务端协议证据的情况下，直接把 encoder、decoder、tokenizer 作为同一 ID/版本下的多个独立 CTR 密文文件接入。
- 后续组件协议必须由服务端与客户端共同确认唯一性：每个加密组件使用独立密钥，或使用经过版本化、双方一致实现的组件级唯一 nonce/counter 派生标识；不得仅在客户端擅自更改派生规则。还需确认 SHA-256 摘要及组件清单来自可信的已认证元数据，并定义组件 ID、版本、量化等级、密文长度、摘要和模型结构标识。
- AES-CTR 本身不提供密文完整性认证。当前 SHA-256 校验只有在期望摘要通过可信渠道认证时才能防止恶意替换；后续协议需明确摘要/清单的签名或认证来源，不能把“计算并比较 SHA-256”误称为 AEAD。
- 因服务端密钥/组件格式尚未从仓库代码中得到证明，本轮不擅自改动现有加密格式、不新增猜测性的 manifest、不将密钥写入 JSON/日志/文件，也不生成明文临时模型文件。
- 下一步先补齐并审查服务端组件清单与密钥交付契约；再实现 Android Keystore 密钥桥接和 native 内存解密；最后建立独立于编辑器锁的 ASR task handle、PCM 分块传递、取消/进度/失败/重试及字幕事务回写。音频解码统一采用系统解码器输出 16 kHz mono float32 PCM 的跨平台契约，Android/iOS/桌面分别调用平台能力。
- 本节为源码审计结论，不代表密文模型推理或自动字幕已经接通；未执行 Android/NDK/C++ 构建、CTest、JUnit 或真机安全验证。

## 第62节：独立 Whisper 会话 C ABI 与 Android direct-buffer 桥接（2026-11-21）

- 新增 opaque `NcSpeechSession`，与 `NcEditor` 生命周期和编辑器状态锁分离；语音识别不再必须占用单个编辑器句柄。
- 新增 C ABI：
  - `nc_speech_session_create/destroy`：独立会话生命周期；销毁前调用方必须等待识别工作线程结束。
  - `nc_speech_session_load_memory`：从已解密 encoder/decoder/tokenizer 内存加载，三件套均为必需，tokenizer 限制 32 MiB；不创建明文临时模型文件。
  - `nc_speech_session_transcribe`：单声道 float32 PCM 同步识别，平台必须在专用工作线程调用；返回字幕分段 JSON。
  - `nc_speech_session_cancel`：原子登记取消，不等待推理线程；由 Whisper 进度回调协作式停止。
  - `nc_speech_session_status`：原子读取状态/进度；会话忙碌时不等待推理互斥锁，避免 UI 轮询阻塞。
- 状态枚举：0 未加载/空闲、1 已加载、2 识别中、3 成功、4 失败、5 已取消。进度单调由识别器回调更新，不伪造推理阶段。
- 会话内部使用独立识别器和忙碌门禁；禁止并发重复识别或在识别过程中重新加载模型。取消请求与已取消状态分离，只有推理退出后才报告状态 5。
- Android `NativeEngineBridge` 增加会话创建/释放、加载、识别、取消、状态接口；JNI 使用 direct `ByteBuffer` 传输模型和 PCM，不把大块数据编码成 JSON，也不经过编辑器的 `stateLock`。输入 buffer 按当前 position/remaining 切片，PCM 按 native float32 字节序读取。
- 增加 C++ 单测覆盖会话创建、初始状态、非法模型/PCM 入参及空闲取消拒绝。
- **尚未完成**：Android Keystore 密钥获取与 AES 解密仍未接入；目前 load 接口要求平台传入已解密 direct buffer。MediaExtractor/MediaCodec 音频解码、16 kHz 单声道重采样、Kotlin 后台 Executor/协程封装、任务重试/生命周期托管、字幕结果批量事务回写尚未接通。
- 风险边界：C ABI 要求 destroy 前 join 工作线程；当前 JNI 层不替调用方管理 worker 生命周期。取消为协作式，若推理内核暂时未回调进度，取消响应会延迟。direct buffer 明文由调用方持有，需在确认 C++ 已复制/装载后主动清理；本接口本身不承诺清除调用方缓冲区或 ONNX Runtime 内部副本。
- 本节只证明接口与桥接源码已提交，不代表 Android/NDK/C++ 构建、CTest、JUnit、模型加载、真机识别或端到端自动字幕已验证。


## 第63节：Android Whisper 后台会话工作器与销毁竞态保护（2026-11-21）

- 新增 `mobile/native/android/NativeSpeechSessionWorker.kt`，为第62节的独立 `NcSpeechSession` JNI API 提供 Kotlin 异步封装。
- 所有模型加载和 PCM 推理都通过专用单线程 Executor 执行；状态轮询使用独立 ScheduledExecutor，避免同步推理占用任务线程时 UI 状态读取被阻塞。
- 暴露 `load()` / `transcribe()` 的 `CompletableFuture` 结果、协作式 `cancel()` 和状态监听；拒绝同一会话上的并发任务，状态进度限定在 0..1。
- 修复提交/关闭竞态：任务提交与 close() 通过同一生命周期锁排序；close 先登记取消，再把 native destroy 排在工作队列末尾，确保不在 JNI 推理仍使用句柄时释放它。销毁前不强行中断 native 线程；若核心暂不响应取消，关闭会安全等待。
- 模型 encoder/decoder/tokenizer direct buffer 在 native load 返回后执行 best-effort 清零；PCM 默认在推理返回后清零，可由调用方显式关闭 PCM 清理。该操作不保证清除 JVM/ORT/native 副本、分配器残留、交换区或崩溃转储。
- Listener 在状态轮询线程回调，宿主必须自行切回主线程更新 Compose；此工作器尚未接入 Activity/ViewModel、媒体选择或编辑器“自动字幕”按钮。
- 尚未实现 MediaExtractor/MediaCodec 音轨解码、声道混合与 16 kHz 重采样，也未实现模型服务端组件 nonce/密钥协议、Keystore 解密、字幕批量单次 Undo 事务。不能据此宣称端到端自动字幕已启用。
- 该工作器源码已静态回读；未执行 Gradle/Kotlin 编译、JUnit、NDK/C++ 构建、CTest、真实模型推理或设备生命周期验证。

## 第63节：Android Whisper 会话后台工作线程与安全释放（2026-11-21）

- 新增 `NativeSpeechSessionController`，将独立 Whisper 会话的模型加载与转录操作串行投递到专用 daemon Executor；两类工作均不占用 Compose 主线程，也不持有编辑器状态锁。
- 控制器在进入任务前检查 direct ByteBuffer、当前 position/remaining、PCM 帧数、采样率及语言枚举；保留切片后的 buffer 引用直到对应任务结束，避免调用方提前释放底层缓冲。
- 提供状态/进度读取、协作式取消和结构化结果回调；转录结果仍为带 start/end/text 的 JSON 分段，供后续字幕事务适配器消费。回调运行在 ASR worker 线程，UI 宿主必须切回 Main dispatcher 更新 Compose。
- 生命周期关闭时先请求取消，再关闭 Executor 并等待工作线程退出，只有确认线程终止后才调用 native destroy。若 30 秒内无法终止，控制器选择保留 native handle 并记录错误，而不是销毁仍可能被 JNI 使用的对象造成 use-after-free。
- 该控制器是可复用的任务执行基础，不代表已经接入 Activity/ViewModel、媒体解码、模型密钥/解密或自动字幕 UI；PCM 仍需上游提供 mono float32，推荐 16 kHz。加载模型时调用方仍须保证 direct buffers 在完成回调前有效，并在安全时机尽力清理自有明文缓冲。
- 尚未实现 Android MediaExtractor/MediaCodec 解码与重采样、Android Keystore/AES 协议闭环、任务持久化/重试 UI、字幕批量单次撤销事务。关闭超时的保守策略可能暂时保留 native session，属于防止悬空指针的安全取舍。
- 本轮只完成源码与静态回读；未执行 Gradle/Kotlin/NDK 编译、JUnit、CTest、模型加载、真实音频识别或真机生命周期验证。


## 第64节：Android 系统音频解码与 16 kHz 单声道 PCM 管线（2026-11-21）

- 新增 `AndroidAudioPcmDecoder.kt`，从 SAF/ContentResolver 可访问的 `Uri` 读取所选媒体，使用 Android 系统 `MediaExtractor` 选择音轨，并使用系统 `MediaCodec` 解码；不捆绑第三方音频/视频编解码实现。
- 输出格式统一为 Whisper 所需的 mono float32、16 kHz、[-1,1] PCM direct ByteBuffer；对解码器交错 PCM 支持 8-bit、16-bit 和 float32，逐帧多声道平均下混。
- 使用跨 MediaCodec 输出缓冲区连续状态的线性流式重采样器，保留前一输入采样点与全局源采样位置，避免按单个 codec buffer 分段重采样造成边界跳变。
- 为防止异常长音频导致内存无界增长，默认最多 10 分钟；输出使用按需扩容的 direct ByteBuffer，解码有取消标志和 10 分钟墙钟超时。超过时长、PCM 位宽不支持、声道数/采样率非法或解码器输出帧不完整时明确失败，不静默截断。
- 返回的 PCM direct buffer 由调用方持有，可直接交给第63节 `NativeSpeechSessionWorker.transcribe()`；该 worker 默认在推理结束后尽力清零 PCM。
- 仍未将媒体解码、Whisper worker、项目选择状态及字幕事务回写接入 Activity/ViewModel；也未完成真实设备兼容矩阵验证。MediaCodec 实际 PCM 输出格式因设备/编解码器而异，当前对 24-bit packed / 32-bit integer PCM 明确拒绝，需通过设备实测决定是否扩展。
- 仅完成源码提交与静态回读；未执行 Gradle/Kotlin 编译、JUnit、NDK/C++ 构建、CTest、真机音轨解码或 ASR 端到端验证。


## 第65节：Whisper C ABI PCM 输入边界防御（2026-11-21）

- 复核 Android 系统解码输出与共享核心重采样入口后，发现独立会话 C ABI 原先只检查 sample_rate 为正数，且没有验证 PCM 是否为有限值及是否遵守 [-1,1] 归一化契约。
- `nc_speech_session_transcribe` 现在限制采样率 8–192 kHz、单次音频最多 10 分钟、结果 JSON 输出指针必需；逐样本拒绝 NaN/Infinity 和超出 [-1,1] 的输入，避免非法浮点进入 Mel/FFT/ONNX 推理。
- C ABI 头文件同步记录输入约束；`tests/test_speech_ai.cpp` 增加非法采样率、超范围幅值、NaN 和空结果指针的拒绝测试。
- 该防御会对最多 10 分钟 PCM 做一次线性扫描；Android 解码器输出已经做有限值与幅值钳制，C ABI 校验作为跨平台调用边界的二次保护。
- 测试源码尚未执行；未进行 C++/NDK 编译、CTest、Gradle/JUnit、真实模型推理或设备性能测量。


## 第66节：音频解码失败路径的 PCM 明文缓冲清理（2026-11-21）

- 补强第64节解码器的失败/取消路径：如果音频解码未成功移交 PCM，流式重采样器会尽力覆写其 direct buffer；每个 codec 输出块转换后的临时 mono FloatArray 在送入重采样器后立即清零。
- 成功返回时 PCM 所有权明确交给调用方，由第63节 worker 在识别完成后负责尽力清理；解码失败时不会把未完成 PCM 留作可复用对象。
- 该清理仍是 best-effort，不构成密码学安全擦除，也无法控制 MediaCodec 内部缓冲、系统媒体服务副本、JVM/ART direct-buffer 回收时机或设备崩溃转储。
- 仅源码静态回读，未执行 Android 编译或真机内存/取消验证。


## 第67节：自动字幕端到端写回与单步撤销事务（2026-11-21）

- 新增 C ABI `NcSubtitleCueInput` 与 `nc_upsert_subtitles_batch`：先完整校验批量字幕参数，再在 `TimelineEditor` 事务内逐条 upsert；成功时形成名称为“自动字幕”的单条历史记录，任一核心命令失败时结束事务并撤销已应用部分，避免留下半批字幕。拒绝空批次、超过 10000 条、空文本、单条超过 16 KiB、负起点、非正时长和 int64 时间溢出；拒绝与已有编辑事务嵌套。
- 新增 Android JNI `nativeUpsertSubtitlesBatch`，以 LongArray / String[] 传递字幕，不把大批字幕拼成 JSON；Kotlin `NativeEngineBridge.upsertSubtitlesBatch` 在引擎生命周期锁下执行。
- 新增 `NativeEditorCommandAdapter.addSubtitlesBatchAndReadEditorState`：读取工程时长、排序和清理字幕文本、将超出工程末尾的字幕丢弃并将尾部时长钳制到工程边界，成功后刷新时间轴与历史快照。
- `NativeSpeechSessionWorker.decodeTranscribeAndInsert` 把系统音频解码、Whisper 推理、识别片段时间转换和批量字幕事务写回连成单个后台任务；以选中素材时间线起点作为偏移。取消标志同时覆盖解码阶段与推理阶段，并在写入前再次检查，避免取消后才开始写入。
- 复核会话销毁竞态：状态轮询与取消 JNI 调用现在与 close() 使用同一生命周期锁，close 标记后不再发起新的状态查询；native 销毁仍排在 worker 队列末尾。
- 新增 `tests/test_capi.cpp` 覆盖批量插入、唯一稳定 ID、单次 undo/redo、非法批次失败原子性和空批次拒绝。测试源码已提交但尚未执行。
- 此链路尚未接入具体剪辑页的自动字幕按钮、媒体选择/素材 ID 状态、Activity/ViewModel 和 UI 线程回调；目前 worker API 需要宿主明确传入可访问 Uri、时间线偏移、已加载的 Whisper session 和 adapter。
- 模型文件的服务端组件 nonce/密钥契约、Android Keystore 解密和模型下载/安装生命周期仍未解决；当前 worker 接收已解密 direct buffers，不能视为已完成生产级模型安全交付。
- 本节所有改动均为源码级实现和静态回读；未执行 CMake/NDK、CTest、Gradle/Kotlin 编译、JUnit、设备解码或真实模型端到端验证。


## 第68节：ASR worker 取消竞态与缓冲区生命周期复核（2026-11-21）

- 将取消标志复位移动到任务通过单会话 busy gate 之后；此前并发的第二次任务提交即使被拒绝，也可能提前清除第一项正在执行任务的取消标志。现在未获准进入队列的请求不会改动当前任务的取消状态。
- 将 Whisper 状态 JNI 查询和取消 JNI 调用纳入同一生命周期锁。close() 设置关闭状态后，状态轮询不会再启动新的 native 句柄访问；若查询先获得锁，close 会等它退出后再安排销毁。
- 将模型缓冲区与 PCM 参数校验放入清理保护范围；已提交任务即使因非 direct buffer 或容量不足被拒绝，也会对可写 direct buffer 执行 best-effort 清零。
- 以上调整仅强化 Kotlin worker 生命周期，不代表能清除 ORT 内部张量、MediaCodec 内部缓冲或系统分配器副本。
- 静态回读确认 worker 的生命周期锁、任务准入复位和缓冲区 finally 路径存在；仍未运行 Kotlin 编译、JUnit、Android 设备测试或 C++ 构建。


## 第69节：ASR 时间戳与解码音频长度一致性校验（2026-11-21）

- `decodeTranscribeAndInsert` 现在根据解码后实际 PCM 帧数计算音频时长；拒绝开始时间落在音频末尾之外、结束时间明显超过音频长度或非有限时间戳的识别片段。
- 允许 Whisper 末尾时间戳最多超出解码时长 50 ms 的浮点/帧边界误差，并将该片段结束时间钳制到实际音频末尾；避免识别模型的轻微尾部误差生成越界字幕。
- 仍需宿主在接入时传入正确的时间线起点；源素材入点/裁剪区间、音轨选择与用户所选片段的精确媒体范围需要 Activity/ViewModel 绑定后才能完整验证。
- 仅完成静态源码回读，未运行模型或设备验证。


## 第70节：字幕提交与取消/关闭的最终提交门（2026-11-21）

- 将最后一次取消/关闭检查与字幕批量事务提交放在同一生命周期锁内。若 cancel/close 先取得锁，识别结果不会再写入工程；若字幕事务先取得锁，则整批原子写入完成后 cancel/close 才能继续。
- 这避免了“识别完成后刚检查完取消标志，用户随即关闭/取消，但字幕仍然落盘”的竞态；不在 native 推理期间持有生命周期锁，状态轮询与取消仍可响应。
- 仅源码静态回读，未执行并发压力测试、JUnit、Gradle、NDK 或真机验证。


## 71. Native timeline snapshot identity hardening (2026-11-21)

- The strict timeline snapshot parser previously rejected duplicate clip IDs only within an individual track. The core model uses stable project-wide track/clip identities, so a duplicate clip ID on a different track could still be published to the native UI as an apparently valid snapshot.
- The parser now rejects duplicate track IDs and duplicate clip IDs across the entire project. It also rejects duplicate asset IDs and any non-zero clip asset reference that does not resolve to the asset list; malformed asset records are no longer silently skipped.
- Added `NativeTimelineSnapshotValidationTest` covering distinct identities, duplicate track IDs, cross-track duplicate clip IDs, and non-positive IDs.
- This is defensive snapshot validation only. It does not change the C ABI, project serialization, rational-time contract, or host navigation.
- Source files were read back after commit. No Kotlin/Gradle compile, JUnit, CI, Android runtime, or device validation was executed.

## 72. Native editor toolbar accessibility semantics (2026-11-21)

- Both first-level tool groups and expanded second-level tools now use Compose `Modifier.selectable` with `Role.Tab`, exposing their selected state to accessibility services rather than relying only on background color.
- The horizontal expansion behavior is unchanged: second-level tools remain immediately to the right of their first-level group, clicking the active first-level group collapses it, and switching groups replaces the previous second-level row.
- This change does not alter command routing or enable any legacy-fallback tool.
- Source readback confirms the selected semantics are attached to both levels. TalkBack/Accessibility Service and physical-device interaction remain untested.

## 73. Current S5 gate and CI status refresh (2026-11-21)

- Current S5 branch: `work/v3.3.3-s5-native-editor-foundation`; PR #32 remains open and Draft, based on `development/v3.3.3`.
- The repository CI workflow currently triggers on pushes to `main` and `development/v3.3.3`, but pull requests are filtered to `main` only. PR #32 therefore has no workflow run/check evidence for its current head. The successful main-branch CI run is not evidence for this S5 branch.
- The user has explicitly instructed full-scope S5 implementation. This authorizes continued native UI implementation; it does not authorize deleting the WebView fallback. A-6 remains a separate approval gate for S5-8/S5-9.
- S5-0B remains blocked until S1-3 and S3-1 are genuinely accepted. S5-1 remains blocked on S1-8D and the shared Manifest file. S5-5 still requires the accepted Rational time contract; S5-6/S5-7 still require the S1-2 command contract.
- MainActivity still loads `pages/01-home.html`; NativeAppShell is not yet a live host. The 14 Compose page components and native editor panels are implementation assets, not proof of runtime migration. The six `nairoclip-*-*.html` static files are generated snapshots excluded from APK packaging; they are not six additional product screens.
- No Activity/Manifest switch, WebView deletion, or legacy asset cleanup was performed in this update.


## 74. 原生自动字幕工具栏入口与状态面板（2026-11-22）

- 新增 `NativeAutoSubtitlePanel.kt`，为剪辑页“文本 → 自动字幕”提供原生 Compose 状态面板：显示服务状态、有限进度、错误信息、开始与取消入口。
- `NativeEditorScreen` 新增宿主注入的 `NativeAutoSubtitleUiState`、`onStartAutoSubtitle` 和 `onCancelAutoSubtitle`；自动字幕二级工具现在进入原生面板，不再因缺少专用路由而直接落入旧编辑器回退。
- 面板只调用宿主回调，不自行访问媒体 URI、加载模型、调用 JNI 或写入字幕。未提供可用宿主时开始按钮保持禁用，并明确显示服务尚未接入；不展示虚构进度或识别成功。
- 当前 `NativeSpeechSessionWorker.decodeTranscribeAndInsert` 已具备系统音频解码、识别、时间戳换算及字幕批量事务写回的工作器接口，但 MainActivity/ViewModel 尚未提供选中素材 URI、精确时间线偏移、已加载会话和主线程状态发布。因此本节只完成剪辑页 UI 入口与宿主状态契约，**不代表自动字幕已经在运行时可用**。
- Android Keystore/模型组件加密协议及 S1-3/S3-1/S1-8D 前置门仍未解除；本轮未改 Activity、Manifest、WebView 回退或模型安全协议。
- 本轮仅完成 GitHub 源码静态回读；未执行 Gradle/Compose 编译、JUnit、NDK/CTest、真实模型推理或真机验证。


## 75. 自动字幕 PCM 输入契约与缓冲区清理修复（2026-11-22）

- 检查 \`NativeSpeechSessionWorker.transcribe()\`：原实现仅校验采样率大于 0，但该 Whisper Android 会话契约是 16 kHz 单声道 float32 PCM；现在明确要求 16 kHz，并校验语言代码为支持范围 0–4，避免不匹配的输入进入 JNI。
- 修复 \`zeroRemaining()\` 的清理边界：原实现从 ByteBuffer 当前 position 清到 limit；若 JNI/解码调用推进了 position，前段 PCM/模型明文可能残留。现在对 duplicate 调用 clear()，清理当前 buffer view 的整个 capacity。
- 该清理是 best-effort：不能保证抹除 JNI、运行时或底层分配器中的副本；不应对外宣称达到密码学安全擦除。
- 未改变模型格式、精度、模型下载或 Activity 入口；没有绕过 S1-3/S3-1 等前置审批。
- 本轮为源码静态审查与 GitHub 文件回读；未执行 Gradle 编译、单元测试、NDK 测试、模型推理或 Android 真机验证。


## 76. 自动字幕宿主协调器与取消语义（2026-11-22）

- 新增 \`NativeAutoSubtitleCoordinator.kt\`：将选中素材 URI、片段时间线起点、语音会话就绪状态、启动/取消操作及完成/失败状态集中为生命周期中立的宿主适配器，供 Activity/ViewModel 后续注入 \`NativeEditorScreen\`。
- 所有 UI 状态通知统一投递主线程；协调器不会自行选择素材，也不会伪造模型加载成功。
- JNI Worker 当前未暴露真实百分比进度，因此运行时使用不确定进度指示，不再显示虚假的 0%。
- 修复 Worker 取消语义：只要任务处于忙碌状态，就记录协作式取消令牌；即使 JNI 当前未处于可取消推理阶段，MediaCodec 解码循环和字幕事务提交前检查仍会响应取消。状态文案明确为等待安全结束，不承诺立即中断底层推理。
- 协调器关闭时关闭其持有的 Worker；宿主应保证协调器与 Worker 的生命周期一致，不得在多个协调器之间共享同一 Worker。
- 尚未将协调器接入 MainActivity：选中素材来源、模型解密/加载与项目时间线状态的生命周期所有权仍受前置架构审批约束。
- 本轮仅做源码静态回读；未执行 Gradle 编译、JUnit、NDK/CTest、模型推理或真机验证。


## 77. 自动字幕 JNI 输入边界补强（2026-11-22）

- 对自动字幕专用路径复核发现：通用 \`transcribe()\` 已校验语言代码、direct PCM、16 kHz 和帧数/缓冲区关系，但 \`decodeTranscribeAndInsert()\` 直接调用 JNI，绕过了这些入口校验。
- 已在专用路径补齐语言代码 \`0..4\`、可写 direct ByteBuffer、16 kHz、非空帧数以及帧数不超过缓冲区剩余 float32 容量的校验，避免错误输入直接进入 JNI。
- 此修复属于 JNI 边界防御；不改变识别模型或语言映射，不代表已完成所选时间轴片段的源入点/出点裁剪。当前解码器仍从传入 URI 解码整段媒体；若宿主传入的是原始素材 URI 而非已裁剪音频 URI，字幕时间与素材源区间仍需后续按正式工程媒体映射契约接通。
- 本轮源码静态回读；未执行 Gradle 编译、JUnit、NDK/CTest、模型推理或真机验证。


## 78. 自动字幕批量写入后的快照失败与重复提交保护（2026-11-22）

- 发现字幕批量 JNI 写入成功后，若紧接着读取工程/历史快照失败，原适配器会返回普通失败。用户可能误以为未写入并再次点击，造成重复字幕。
- 已修复：批量写入返回有效且唯一的字幕 ID 后，如果快照刷新失败，适配器仍在持有同一引擎状态锁时尝试撤销刚才的批量事务，再读取恢复快照；错误信息区分“已自动撤销”和“撤销后仍无法确认”。
- 若引擎返回的 ID 数量/唯一性本身异常，则写入状态无法证明，明确提示先刷新工程、不要直接重试；不盲目调用 Undo，避免在实际未创建批量历史记录时误撤销用户之前的编辑。
- 该策略依赖 C ABI 的批量字幕写入作为单条历史事务，以及 Undo 命令按当前引擎历史栈顺序执行。虽然适配器持有状态锁可防止本适配器之外通过同一 bridge 锁的并发命令插入，但仍需集成测试验证 JNI 的实际历史事务语义。
- 本轮静态源码审查与回读；未执行 Gradle 编译、JUnit、NDK/CTest、CI 或真机测试。


## 79. 自动字幕提交阶段的取消边界（2026-11-22）

- 发现取消请求与字幕批量提交存在竞态：工作线程持有生命周期锁执行不可中断的引擎事务时，取消调用可能在事务结束后、任务标记空闲前取得锁，从而返回“已接受”，但字幕其实已经写入。
- 已增加 \`subtitleCommitStarted\` 阶段标记。进入批量引擎事务前在同一生命周期锁内设置；提交阶段的取消请求返回 \`false\)，不再声称已接受。任务成功、失败或执行器拒绝任务时都会复位标记。
- 协调器同步调整拒绝文案为“当前阶段无法接受取消请求，正在等待任务结束”，避免把拒绝请求表述成已发起取消。
- 仍需真机/集成测试覆盖“取消与事务开始同时发生”“事务提交成功后立即取消”“事务失败后再次启动”等时序；本轮仅静态检查与代码回读。


## 80. Direct ByteBuffer position 与 JNI 地址语义（2026-11-22）

- 复核 Android JNI 语音接口后确认：\`GetDirectBufferAddress()\` 返回 direct buffer 视图的基址，不会自动按 Java \`ByteBuffer.position()\` 前移；JNI 侧容量检查也以传入视图容量为准。
- \`NativeSpeechSessionWorker.transcribe()\` 和 \`load()\` 原先直接把调用方 buffer 传给 JNI。调用方若传入 position 非零的 direct buffer，native 可能从错误偏移读取模型/PCM，且按 remaining 做的 Kotlin 校验与 JNI 实际读取范围不一致。
- 已修复：模型缓冲区要求 direct 且 remaining 非空，传 JNI 前分别创建 \`slice()\`；PCM 传 JNI 前创建从当前 position 开始的 zero-based slice，并设置 native byte order。原始缓冲区仍由 worker 在 finally 中做 best-effort 清理。
- 自动字幕解码器输出 position 为 0 的 direct PCM，行为不变；修复同时让通用 worker API 对 position 非零的调用方缓冲区符合 Java ByteBuffer 语义。
- 已核对 JNI 实现确实使用 \`GetDirectBufferAddress\` 与 \`GetDirectBufferCapacity\`。本轮未运行 Kotlin/JNI 编译或设备测试；建议增加 position 非零的 direct-buffer 集成测试，验证模型加载与 PCM 推理只读取 remaining 区间。


## 81. 自动字幕协调器状态回调竞态（2026-11-22）

- 发现协调器从后台线程投递状态快照时，若主线程随后已发布更新状态，旧快照仍可能晚到并覆盖新状态；此外，缺少素材时 \`start()\` 曾在持有协调器锁期间直接触发 UI 回调。
- 已增加单调递增的状态版本号：每次发布生成版本，真正交付前再次核对版本，只交付最新快照；后台回调不会再回滚较新的界面状态。
- \`start()\` 的无素材分支改为先释放锁再发布状态，避免在协调器锁内执行外部 UI 回调。
- 取消请求返回期间若任务已结束或协调器已关闭，不再覆盖完成回调写入的终态提示。
- 本轮源码静态回读；尚未运行 Android/Kotlin 编译或并发单元测试。


## 82. 自动字幕与时间轴片段源区间的接口缺口确认（2026-11-22）

- 对照 \`core::Clip\`、\`nc_clip_info()\`、JNI \`BuildState()\` 与 \`NativeTimelineSnapshot\` 后确认：核心 \`Clip\` 保存 \`sourceIn\`，但当前 C API 的 \`nc_clip_info\` 只输出片段 ID、时间轴起点/时长、素材 ID、速度分子/分母和启用状态；没有输出源入点。JNI 工程快照因此也无法给宿主提供准确源区间。
- 自动字幕 worker 当前仅接收 URI 与时间轴起点，解码器从媒体开头解码整段音频，且识别时间按 1x 直接平移；它没有应用片段源入点、片段源出点或变速映射。
- 因此当前实现只适用于“URI 对应的音频内容本身就是所选片段范围、且按 1x 放置”的情况；对已裁剪、变速或 URI 指向完整原素材的片段，不能保证字幕时间准确。宿主在未取得完整片段区间之前不应将其包装为全场景可用。
- 下一轮需先扩展并测试 C API 源区间读取契约，再同步 JNI 快照和 Kotlin 时间轴模型，最后实现按源区间解码及时间轴映射。该跨层契约需同步检查 ABI 调用点和现有时间轴测试，不能只在 Kotlin 层猜测源入点。

## 83. 自动字幕专用 PCM JNI 视图对齐（2026-11-22）

- 复核自动字幕专用路径时发现：通用 `transcribe()` 已将 direct ByteBuffer 按当前 position/limit 转成 zero-based slice，并显式设置 native byte order；但 `decodeTranscribeAndInsert()` 仍把解码器原始 ByteBuffer 直接传入 JNI。
- 当前系统解码器返回的缓冲区 position 通常为 0，因此常规路径不一定触发错误；但专用路径没有维持 JNI 地址语义与通用入口一致的契约，未来缓冲区视图或解码器实现调整后可能读取错误起点，且没有显式保证 float32 的 native byte order。
- 已修复：自动字幕专用路径现在传递 `decoded.samples.slice().order(ByteOrder.nativeOrder())`，JNI 看到的 capacity 与校验使用的 remaining 范围一致；原始 direct buffer 仍在 finally 中整体 best-effort 清零。
- 本修复不解决第82节记录的 sourceIn/sourceOut/变速映射缺口；自动字幕仍不能宣称支持所有裁剪与变速片段。
- 仅静态源码修改与回读；未运行 Kotlin/Gradle、JNI、NDK/CTest 或真机测试。

## 84. 片段源素材映射元数据 C API 与 JNI 快照扩展（2026-11-22）

- 第82节缺口进一步拆解：仅增加 sourceIn/sourceOut 仍不足以让 ASR 安全映射。核心片段还可能倒放、定格或启用非恒速曲线；宿主必须知道这些标志，不能把所有片段误当作 1x 正放。
- 新增独立 C ABI `nc_clip_source_info`，不改变既有 `nc_clip_info` 签名；读取源入点、按基础速度计算的源区间终点，以及 reversed/frozen/has_speed_curve 标志。输出指针可选，错误身份会返回明确错误码。
- JNI `BuildState()` 现在把上述字段写入每个片段快照；Kotlin `NativeTimelineClip` 和严格解析器保留这些字段，并对源区间范围与标志位做校验。为兼容旧快照，完全缺少新字段时将 `sourceRangeKnown` 设为 false；部分字段缺失或值非法则拒绝快照。
- 重要边界：`sourceOut` 仅在非定格、无速度曲线时可作为线性源区间终点；定格或速度曲线片段将 `source_out_us` 设为等于 `source_in_us` 的未知区间哨兵，并由标志位区分。倒放、定格和变速曲线仍必须由 ASR 宿主显式拒绝或实现相应映射。新增测试还覆盖定格片段的未知区间哨兵。此提交只补齐元数据，不宣称完成裁剪/变速自动字幕。
- 本轮只做源码修改与 GitHub 静态回读；未执行 CMake/NDK、CTest、Gradle/Kotlin 编译、JUnit、CI 或真机测试。

## 85. 原生时间轴源映射能力判定与解析测试（2026-11-22）

- 在 `NativeTimelineClip` 增加 `supportsLinearSourceMapping` 派生属性：只有源区间已知、源终点大于源起点、恒速、非倒放、非定格且速度分子/分母有效时才返回 true。
- 严格快照解析器现在区分旧快照（全部缺少源映射字段，明确标记未知）与新协议不完整快照（只要出现任一源映射字段，就必须提供完整区间和三个合法标志位）；同时拒绝所有非定格/非速度曲线片段（包含倒放）的零长度源区间，以及定格/速度曲线片段伪报非零线性区间。
- 新增 Kotlin 单元测试源码覆盖线性片段元数据解析、旧快照兼容、部分/非法元数据拒绝，以及倒放/定格/变速/未知源区间的能力门控。
- 该属性只提供可靠的宿主准入判定，不自动执行源区间裁剪或时间映射；自动字幕仍需把选中 clip 的源区间和速度映射传入解码/字幕转换链路。
- 仅静态源码回读；未运行 Gradle、JUnit 或真机验证。

## 86. ASR Worker 提交拒绝时的明文缓冲区清理（2026-11-22）

- 生命周期复核发现：模型/PCM 清零原先只写在 `submit { ... }` 任务体的 `finally` 中；当 worker 因 busy 或 closing 在执行任务体前就拒绝请求时，该清理分支根本不会运行。
- 已将模型缓冲区清理与 PCM 清理挂到返回的 `CompletableFuture.whenComplete`：无论任务执行成功、执行异常，还是提交前被拒绝，都会尽力清理传入 direct buffer；`clearPcmAfterUse=false` 仍保留显式不清理选项。
- 任务体内不再重复清理，避免重复覆写；清理仍是 best-effort，不能覆盖 JNI/ORT 内部副本或分配器残留。
- 本轮静态回读；未执行 Kotlin 编译、并发单测、Gradle 或真机验证。

## 87. 原生工具二级菜单切换时清理旧面板上下文（2026-11-22）

- 检查 `NativeEditorScreen` 的二级工具路由发现：只有会打开原生参数面板的分支会更新 `activeSubTool`；分割/删除/复制/波纹删除等立即执行型命令，以及转交旧编辑器的工具分支，可能保留上一个工具的 `activeSubTool`，导致旧参数面板在切换工具后继续显示。
- 已在每次二级工具选择入口统一设置当前一级工具并清空旧二级面板上下文；需要原生面板的分支随后再显式设置新的 `activeSubTool`。立即执行型命令不再继承上一个工具的面板状态。
- 保留二级工具横向展开行为、既有命令回调和旧编辑器回退路径；未改 MainActivity 或运行时入口。
- 仅源码静态回读；未运行 Compose 编译、UI 测试或真机交互验证。

## 88. ASR 明文缓冲区可清理性约束

- 进一步复核发现：direct read-only ByteBuffer 可以被 JNI 读取，但 `zeroRemaining()` 会跳过只读视图；若调用方启用自动清理，原契约会出现“推理成功但缓冲区实际无法覆写”的情况。
- 模型 encoder/decoder/tokenizer 缓冲区现在必须是可写 direct buffer；通用 PCM 推理在 `clearPcmAfterUse=true` 时也要求可写。调用方显式选择 `clearPcmAfterUse=false` 时仍允许只读 PCM。
- 这让清理承诺与输入约束一致；仍只属于 JVM 可见 direct buffer 的 best-effort 清理，不覆盖共享副本、JNI/ORT 张量或系统分配器残留。
- 仅静态源码回读；未运行 Kotlin 编译或设备验证。

## 89. 旧版 ASR Controller 输入契约与缓冲区生命周期统一

- 仓库还保留独立的 `NativeSpeechSessionController`，不能只修复新 Worker 而遗漏旧 Controller 的 JNI 调用路径。
- 已统一模型输入约束：三个模型缓冲区必须是非空、可写 direct ByteBuffer；被 worker 接受后，无论推理成功还是异常，均 best-effort 清理原始缓冲区。
- PCM 输入现在强制 16 kHz、可写 direct buffer、正帧数且剩余容量足够；被 worker 接受后在 finally 清理原始缓冲区。
- 该 Controller 的取消/关闭与 handle 生命周期策略未改变；只对成功入队的任务执行清理，未接受的调用仍由调用方持有缓冲区。
- 仅静态源码回读；未运行 Kotlin 编译、JUnit 或设备测试。

## 90. 远程 C++ 编译发现并修复 RenderClip 调色快照缺失

- S5 验证工作流首次运行时，C++ 编译器与条件编译检查均发现 `export_executor.cpp` 多处读取 `RenderClip::colorGrade`，但导出 IR 结构没有该成员，导致桌面构建和 ASAN/UBSan 构建失败。
- 根因是 `Clip` 已有完整 `ColorGrade`，导出执行器也已实现单片段、转场两侧和多层下层的调色消费，但 `RenderClip` 与 `compileClip()` 漏掉了这份调色快照。
- 已在 `RenderClip` 增加显式 `ColorGrade` 字段，并在 `compileClip()` 从源 `Clip` 复制；新增导出计划测试验证非恒等主曲线会完整进入 IR。
- 此修复恢复导出计划与渲染执行器之间的结构契约，不改调色算法或工程序列化格式。
- 修复后必须等待新的 C++ Release/ASAN/UBSan/CTest 结果，以及 Android NDK/Kotlin 工作流结果；旧提交的失败记录不会被追溯改写。

## 91. JNI BuildState 片段 JSON 提前闭合对象缺陷

- 对照新增的源区间字段继续审查 `BuildState()`，发现原有 `effects` 数组结束处拼接的是 `"]}"`，这会同时关闭片段对象；后续仍继续拼接同一片段的 `xforms` 字段，导致生成的工程状态 JSON 结构非法。
- 这会使严格的 `NativeTimelineSnapshot.tryFromEngineState()` 无法解析状态，原生时间轴宿主即使拿到 JNI 返回值也无法构建完整快照。
- 已修复为只关闭 `effects` 数组，片段对象保持打开，待 `xforms` 数组完成后再由原有闭合逻辑关闭片段对象。
- 该问题说明仅靠 Kotlin 解析器单测不足以覆盖 JNI 手工 JSON 拼接；后续需增加 BuildState 的真实 JNI/Android 集成测试，对含空/非空 effects、空/非空 xforms 及源映射字段的返回 JSON 做解析断言。
- 仅源码回读确认拼接顺序；必须由后续 Android NDK/Kotlin 工作流和真机工程快照验证。

## 92. S5 远程验证链路建立与首轮编译缺陷修复（2026-11-22）

- 将主 CI 的 PR 目标分支扩展到 `development/v3.3.3`，使当前 S5 Draft PR 不再因为只监听 main 而缺少自动验证。
- 新增 `.github/workflows/s5-validation.yml`：Android Kotlin 编译 + JVM 单元测试；Android NDK arm64-v8a 核心库交叉编译；JNI 桥接翻译单元语法编译。该工作流不发布 APK 或 Release 资产。
- 首轮真实编译暴露并修复：
  - `RenderClip` 漏掉 `ColorGrade` 快照字段，导致导出执行器编译失败；已补字段、`compileClip()` 复制和导出计划测试。
  - `tests/test_serialization.cpp` 新增调色测试的 JSON 字符串转义多了一层反斜杠；已修复为合法 C++ 字符串字面量。
  - Kotlin 编译发现快照读取函数表达式体内非法 return、EditorStateSnapshot 轨道访问层级错误、Compose KeyboardActions/KeyboardOptions 导入包错误、选中片段变量作用域错误、曲线面板缺少 @Composable、曲线 identity 工厂放错类型、时间轴进度无障碍 action ID 使用错误，以及 Double/Float max 参数不匹配；均已逐项修复。
  - Kotlin 编译另发现 NumericField 把普通 `() -> Unit` 直接传给带 KeyboardActionScope receiver 的回调；已改为显式 lambda 适配。
  - NDK JNI 语法检查发现 `JEsc()` 两个转义分支的 C++ 字符串字面量本身无法编译；已修正引号和反斜杠输出。
  - Android SDK setup 误安装已移除的 legacy `tools` 包；工作流已限定安装 platform-tools。
- 验证状态：在提交 `20e89a6e14f17936cae3ebca4549e7cebec674af` 上，Android NDK arm64-v8a 核心库构建与 JNI 桥接 `-fsyntax-only` 检查已通过。该提交的 Kotlin 编译/JVM 测试与桌面 C++ Release/ASAN/UBSan/CTest 仍在运行，尚不能记为通过。
- 以上均以实际 Actions 日志为准；失败的旧提交运行保留为历史证据，不用新提交结果覆盖或伪装。

## 93. Kotlin 快照单测失败根因：本地 JVM 缺少真实 org.json 实现

- Android `compileDebugKotlin` 已通过，但首次 `testDebugUnitTest` 中新增的两项快照解析测试失败，失败点是解析结果为 null。
- 复核发现测试运行在本地 JVM，原先只依赖 Android mockable android.jar 提供的 `org.json` API；该环境不保证提供真实可用的 JSON 实现，而生产解析器会捕获 JSON 初始化异常并按契约返回 null。
- 已在 `testImplementation` 增加独立的 `org.json:json:20240303`，仅供 JVM 单测使用，不进入 APK 的生产运行时依赖。
- 必须重新运行 `testDebugUnitTest` 验证两项快照解析测试；本修复尚未取得新运行结果。

## 94. 自动字幕按片段源区间解码与恒速时间轴映射

- 第82/84/85节记录的“只拿 URI + 时间轴起点、从素材头部识别整段音频”缺陷已进入实现修复，不再只保留为后续说明。
- 新增 `NativeSpeechSourceRange`，携带时间轴起点/时长、素材源入点/出点、速度分子/分母和倒放/定格/速度曲线标志；验证源区间跨度必须与时间轴时长×恒速比一致（允许 2µs 的整数微秒舍入误差），并用 BigInteger 防止映射乘法溢出。
- 自动字幕目前只允许正向恒速片段。倒放、定格、变速曲线、缺失源区间或不一致的源/时间轴跨度会明确拒绝；旧的“仅时间轴起点”协调器重载保留为 deprecated 兼容入口，但不再允许启动识别，防止对裁剪素材错误生成字幕。
- `AndroidAudioPcmDecoder` 支持按 sourceIn seek 到前一同步点，并在 sourceOut 处结束输入；记录解码首个 PCM PTS，检查后续输出 PTS 连续性，拒绝无法可靠映射的音频时间戳。
- Worker 按首个 PTS 裁掉 seek preroll，并裁掉 sourceOut 之后的 PCM；若解码器跳过源入点或未覆盖源出点，则失败，不会用整段原素材假装片段内容。
- Whisper 输出段落再按 speedDenominator/speedNumerator 映射回项目时间轴，并限制在所选片段时长内；字幕仍通过既有原子批量事务写回。
- 新增纯 Kotlin 映射测试覆盖 2x、0.5x、倒放/定格/速度曲线拒绝和源跨度不一致。
- 验证边界：这些新增 Worker/Coordinator/MediaCodec 代码已提交，但最新提交上的 Gradle/JVM、NDK/JNI 和 C++ CI 仍需重新完成；不能把之前仅验证到 source-range helper 的成功运行视为本节端到端验证通过。真机 MediaExtractor/MediaCodec 轨道时间戳行为仍需后续设备验证。

## 95. 主机侧 JNI 契约测试桩与生产 include 缺失修复

- 桌面 C++ CI 的 S1-2 契约测试直接包含生产 `nc_jni.cpp`，但主机桩 `tests/jni_stub/jni.h` 仍停留在旧 JNI API，未声明字幕批量接口新增的 `jlongArray/jobjectArray`、LongArray 区域访问和 direct-buffer 查询成员，导致主机测试编译失败。
- 同一编译还发现 `nc_jni.cpp` 使用 `std::isfinite/std::floor` 却未显式包含 `<cmath>`；已补齐生产 include。
- 已扩展仅供主机测试使用的 JNI 桩头，增加缺失类型别名和方法占位实现；这些 stub 不进入 Android 产品构建，S1-2 测试仍只调用纯 C++ `DispatchEdit`，不会执行桩 JNI 方法。
- 需要等待新的 Release/ASAN/UBSan 构建和 CTest 结果验证。

## 96. 片段 PCM 裁剪器拆分与单元测试

- 将 Worker 内部的 seek-preroll/source-out 裁剪逻辑提取为纯 Kotlin `NativeSpeechPcmCropper`，使时间戳边界逻辑可在 JVM 中独立验证。
- 新增测试覆盖：从完整文件 PCM 裁出指定源区间、按非零首个 PTS 正确裁掉 seek preroll、解码器跳过源入点时拒绝、解码器未覆盖源出点时拒绝。
- Worker 现在只调用该裁剪器，解码原始缓冲区在裁剪成功或失败后均 best-effort 清零；识别用裁剪后的 direct buffer 在推理/写回结束后清零。
- 本节测试尚待最新 GitHub Actions 运行验证；Android MediaCodec 实际 seek/PTS 连续性仍需真机验证。

## 97. 自动字幕源映射从权威时间轴快照构建

- 新增 `NativeSpeechSourceRange.fromTimelineClip()`，直接从 `NativeTimelineClip` 生成 ASR 所需映射，统一传递片段时间轴起点/时长、源入点/出点、速度比和倒放/定格/曲线标志。
- 对旧快照或缺少 sourceIn/sourceOut 的片段返回 null，不允许宿主自行猜测 0 点或按 1x 处理。
- 新增工厂测试验证完整元数据可构建、源区间校验通过，未知源区间不会生成映射对象。

## 98. 全量 CTest 暴露的测试夹具问题修复

- 首轮 CTest 的序列化非法曲线测试用 `"enabled":true` 精确子串定位片段，但当前 JSON 序列化格式的空白布局不保证该子串存在；测试提前 return，未真正覆盖非法曲线拒绝逻辑。
- 已改为从 `clips` 数组定位第一个片段对象，并在对象起始处注入非法曲线字段，保持测试目标明确。
- S1-2 关键帧契约测试把 `property=0`（透明度）分别设置为 5、7、2，超出引擎 `0..1` 合法范围，导致测试在命令层被正确拒绝；已改用 0.5、0.7、0.2，保留时间字段映射/删除的原测试意图。
- 最新 CTest 在上述两类测试修正后不再报告这些失败；当前剩余失败转为真实音节突发降噪保留率问题，见第99节。

## 99. 音节突发降噪过度抑制修复尝试

- 真实 FFmpeg 混音集成测试测得 Medium 档语音突发 300Hz 分量保留率约 `-11.76 dB`，低于验收门槛 `>-8 dB`；同一素材的纯噪声段抑制约 `-18.41 dB`。
- 根因指向噪声估计在 50ms 音节间隙释放过慢，上一音节的高能量估计延续到下一音节，使后续语音被过度当作噪声扣除。
- 已将快速下降系数由 0.30 调整为 0.50；并基于独立高斯实/虚部形成的 Rayleigh 幅度谱长序列数值仿真，将偏差补偿系数从 3.81 校准为 4.54，以抵消更快释放对稳态噪声估计均值的影响。
- 该修改不是验收通过结论；必须以最新提交的真实媒体 CTest、ASAN 与 UBSan 运行结果确认语音保留和噪声抑制两项指标同时满足。

## 100. JNI BuildState 片段 JSON 结构回归测试

- 第91节修复了 `BuildState()` 中 effects 数组结束时提前关闭 clip 对象的问题。
- 新增主机侧 S1-2 契约测试：直接调用生产 `BuildState()`，用核心 JSON 解析器解析完整返回信封，并断言首个轨道/片段包含数组类型的 `effects` 与 `xforms` 字段。
- 该测试直接覆盖 JNI 手工 JSON 拼接，不再只依赖 Kotlin 对人工构造 JSON 的解析测试；避免未来再次出现“源码拼接顺序错误但 Kotlin parser 单测仍通过”的盲区。
- 最新提交的桌面 C++ Release/ASAN/UBSan 与 Android NDK/Kotlin 结果待 Actions 完成后记录；主机测试桩不执行 JNI 方法，只调用纯 C++ C API。

## 92. BuildState 片段 JSON 闭合修复补全

- 最新 CI 的 `S1_2.BuildStateClipJsonIsWellFormed` 在 `parseJson(BuildState(...))` 失败，证明第 91 节修复只移除了 effects 处过早关闭 clip 的问题，却遗漏了 xforms 数组结束后必须关闭 clip 对象。
- 已在 xforms 数组结束处追加 clip 对象闭合；随后原有外层代码继续关闭 clips 数组与 track 对象，形成正确嵌套：effects 数组 → xforms 数组 → clip 对象 → clips 数组 → track 对象。
- 保留 `effects` 结束处只关闭数组的修复。
- 需要新一轮 C++ 全量测试验证 `S1_2.BuildStateClipJsonIsWellFormed`；当前尚未运行修复后的测试。

## 93. 增加主机 CMake + CTest 定向验证门

- 现有主 CI 虽然构建 `nairoclip_tests` 并直接运行自研测试程序，但没有通过 CTest 注册入口执行；为满足 JNI/C ABI 变更的独立验证要求，在 S5 验证工作流新增 `core-ctest` 作业。
- 新作业在 Ubuntu 主机安装 FFmpeg 开发依赖，配置 CMake 测试构建，编译 `nairoclip_tests`，再通过 `ctest --test-dir build-ctest` 运行 `S1_2.` 与 `Capi.` 前缀测试。
- 该定向 CTest 覆盖 JNI BuildState JSON 契约、DispatchEdit 契约和 C ABI 字幕批量事务等相关测试；它与 Android NDK arm64 交叉编译、Kotlin/JVM 测试互补，不替代主 CI 全量回归。
- 新作业已提交，等待 GitHub Actions 实际执行；尚不能标记为通过。

## 94. C ABI 字幕批量写入失败时的输出 ID 原子性

- 继续审查 `nc_upsert_subtitles_batch` 发现：项目状态虽会在 cue 写入失败时撤销，但输出 ID 数组此前逐项写入；若后续 cue 失败，调用方仍会收到一部分看似有效的 ID。
- 已改为调用开始时清零输出 ID，使用局部数组收集新 ID，并且仅在整个事务成功结束后一次性发布。
- 扩展 `Capi.SubtitleBatchIsAtomicAndUndoable`：失败输入使用非零哨兵 ID，断言失败后所有输出 ID 均为零，同时确认字幕数量和撤销栈未变化。
- 这补齐了 C ABI 的结果输出原子性，不改变函数签名和成功路径语义；等待新的 CTest/全量 C++ 回归验证。

## 95. BuildState JSON 测试覆盖非空嵌套数组

- 扩展 `S1_2.BuildStateClipJsonIsWellFormed`：除原先空 effects/xforms 数组外，现在先通过真实 C ABI 为测试片段添加一条 effect 和一个启用的 xform，再解析 JNI `BuildState()` 返回值。
- 断言两组数组各有一个元素，且元素均为 JSON 对象，覆盖嵌套对象、逗号和数组/对象闭合的非空分支。
- 该用例将随新的主机 CTest 工作流执行；当前尚未收到新提交的测试结果。

## 96. Medium 降噪档真实媒体语音保留率回归

- 最新主 CI 全量用例中，唯一失败为 `AudioDenoise.MixerAppliesDenoiseToRealMedia`：真实确定性 Gaussian-noise 素材的语音突发保留率为 -9.906 dB，低于 -8 dB 验收线；同次噪声段抑制为 -17.495 dB，明显超过至少 -10 dB 的要求。
- 根因表现为 Medium 档过减系数偏激进，而 Strong 档本来就承担更激进降噪职责。已将 Medium 的 over-subtraction 从 1.8 调整为 1.5，保留谱底 0.06 和 Weak/Strong 两档阶梯关系。
- S5 定向 CTest 已扩展纳入 `AudioDenoise.` 全部测试，除 JNI/C ABI 测试外同时验证真实媒体语音保留、纯噪声抑制、强度单调性、流式样本守恒与多声道隔离。
- 此修改是待验证的参数修正；必须以新一轮 CTest 与主 CI 实测为准，不能预先宣称通过。

## 97. 降噪真实媒体逐音节诊断

- Medium 调整至 1.5 后，真实媒体指标改善到语音保留 -9.200 dB、噪声抑制 -14.513 dB，但语音仍未达到 -8 dB 验收线。
- 当前现象与纯噪声抑制结果不匹配此前单体 DSP 合成测试，不能继续盲目降低过减系数；需要先判断衰减是否集中在特定音节/音频块边界。
- 已给真实媒体测试增加 7 个音节窗口的逐段输入/输出幅度诊断日志，以区分算法过减与 AudioMixer 连续流/块边界问题。
- 诊断日志只用于测试输出，不进入 APK/产品运行路径；等待下一轮 CTest 和全量 CI 结果后再决定 DSP 或 AudioMixer 修复方向。

- 后续边界回归补充：新增 JVM 用例覆盖完整片段首尾映射、片段末尾之后的字幕拒绝、源时间越界超过 1µs 的拒绝、时间轴终点整数溢出拒绝，以及接近 `Long.MAX_VALUE` 的合法大时间戳映射。该组测试已写入，等待本次最新 Actions 结果。

- C ABI 源映射测试继续扩展：真实调用 `nc_trim_clip_start` 验证裁剪后 sourceIn 前移且 sourceOut 保持；再调用 `nc_set_clip_speed(2/1)` 验证恒速变速只改变时间轴时长、不改变源区间，并断言线性映射标志仍有效。该测试等待当前 CTest 运行验证。

- 继续追查 JNI 桥接层发现：`NativeEngineBridge.transcribeSpeechSession()` 原先按原始 `capacity()` 校验帧数，却把 `slice()`（只暴露 remaining 区间）交给 JNI；当调用方 position 非零时，校验与 native 实际可见长度不一致。现改为按 `remaining()` 校验，并在桥接边界统一强制 16 kHz、语言代码 0..4、可写 direct PCM；模型缓冲区也在桥接边界强制非空、可写 direct buffer。这样 Worker、旧 Controller 与 JNI 桥接层使用同一输入契约。

## 98. AudioMixer 降噪流跨 feed 边界丢帧修复

- 对真实媒体降噪集成测试的逐音节指标继续向下追查，定位到 `AudioMixer::serveDenoisedBlock()`：当一次 feed 的剩余需求小于 `MediaReader::readAudioBlock()` 返回块的帧数时，旧代码复制前半段后直接 break，丢弃当前解码块未消费的尾部样本；下一轮从读取器后续块继续，导致降噪器输入流发生时间跳跃。
- 该实现错误地假设 MediaReader 当前永远返回恰好可整除 feed 的块大小，接口本身并无此保证。丢帧会使 300Hz 音节突发与 50ms 间隙的时序被破坏，进而把后续语音误判为噪声。
- 已在每片段 `DenoiseState` 增加待消费 `AudioBlock` 与帧偏移，feed 需求不足时保留剩余样本，下次从原块偏移继续复制；仅在整块消费完成后才读取下一块。对声道数/交错样本尺寸异常的解码块 fail closed。
- 该修复不调整降噪 DSP 参数、不降低验收阈值，也不改变正常输出样本数契约；真实媒体语音保留与噪声抑制必须由新一轮 CTest/全量 CI 同时验证。

- 新增确定性回归测试 `AudioMixer.DenoisePreservesSamplesAcrossVariableReaderBlockSizes`：使用现有 `MockMediaReader` 分别以 1024 帧和不对齐的 3000 帧返回解码块，对同一 1 秒降噪片段完整混音，断言输出长度一致且逐样本最大差小于 (10^{-6})。该测试直接锁定“feed 需求与解码块大小不一致时不得丢帧”的接口契约；等待最新 Actions CTest 运行。

## 2026-11-28 跟进：轨道控制羽化与受控 Activity 接入核验

- 核验基准：PR #32 分支 `work/v3.3.3-s5-native-editor-foundation`；本次跟进代码提交见分支最新 HEAD。
- 轨道控制列固定宽度为 40dp，包含眼睛、喇叭、锁三个独立命中区域；Canvas 在轨道素材上方绘制渐变遮罩，再单独绘制控制图标。渐变强度从最左侧 0.88 线性下降到控制列右缘 0，图标不继承遮罩透明度。
- 羽化实现已升级为“模糊副本 + 渐变 alpha 遮罩”：Android 12 / API 31 及以上使用 `RenderEffect.createBlurEffect`（3dp blur radius），随后通过 `DST_IN` 渐变遮罩让模糊从控制列左缘 0.88 alpha 平滑衰减至右缘 0；控制图标在遮罩后重新绘制，保持清晰。API 24–30 没有 `RenderEffect`，保留同一渐变遮罩作为兼容降级，因此旧系统只有 alpha 羽化、没有真实高斯模糊。不得把 API 24–30 描述成已完成高斯模糊。
- 眼睛、喇叭、锁分别连接 `nc_set_track_hidden`、`nc_set_track_muted`、`nc_set_track_locked`；JNI 对三个操作分别调用对应 C ABI。锁定状态由 `BuildState()` 输出、`NativeTimelineSnapshot` 解析，并由 Android 命令适配器在素材编辑操作前拒绝已锁定轨道。
- `MainActivity` 已注册 `NCNativeEditor` bridge，并提供仅 DEBUG + 显式 Intent extra + 本地编辑器页面校验的 Compose overlay pilot。它复用原有 engine 和 PreviewSurfaceView，关闭 pilot 时恢复仍存活的 WebView。正式 Activity / launcher 入口未切换，WebView fallback 保留。
- 本次补充纯 JVM 羽化 alpha ramp 测试（起点、终点、中点、单调性、非有限输入、40dp 宽度契约），以及锁定状态快照解析与非法标志值拒绝测试。
- 验收边界：上述源码与测试已提交；GitHub Actions 对先前提交的 CI / S5 Android Validation 运行被取消，随后通过 GitHub 内容提交产生的新 HEAD 尚未关联新的 workflow run。因此本次新增测试尚无真实 CI 结果；Android Gradle/JVM、主机 CTest、NDK/JNI、真机手势与渲染均不得标记为本次通过。
- S5 整体仍未完成：外围页面原生迁移、宿主完整生命周期、播放/音频时钟、导入/恢复与真机端到端验收仍需继续；本节仅关闭轨道控制这一项源码接线与单测覆盖缺口。

## 2026-11-28 follow-up: quick-action parity and compact-height layout

- Native quick-action single/double-tap and marker long-press semantics are now explicitly implemented through `NativeQuickActionGestureContract` and Compose `combinedClickable`; the visual circles remain 30dp inside 48dp touch targets.
- Import, Home, Images and AI remain legacy-backed, but their fallback now dispatches the corresponding existing editor action after restoring the same WebView page. The route is allowlisted and only dispatches while the local editor document is active. These four actions are not counted as native conversions.
- Added an initial height-class policy for the native editor. Compact layouts reduce timeline height and bound the parameter/marker/prewarm panel to a scroll viewport, while keeping transport and the bottom tool strip outside that scroll region. The reference-height class retains the existing 282dp timeline.
- Added JVM contract tests for the quick-action fallback allowlist and responsive height budgets.
- The latest branch includes these source and test changes. The previous Actions attempt was cancelled after subsequent branch updates; a fresh run must be checked against the final HEAD before any test is marked passed.
- S5 remains incomplete and gated. S1-3 persistent recovery, S3-1 checked-time/export-plan acceptance and A-12 approvals remain unresolved; this UI work does not bypass those gates or authorize formal Activity cutover, Manifest changes, PR merge, or WebView removal.
