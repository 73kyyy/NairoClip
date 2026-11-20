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
