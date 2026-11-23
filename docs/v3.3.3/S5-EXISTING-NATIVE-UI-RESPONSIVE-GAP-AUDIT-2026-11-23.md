# S5 已有原生 UI 响应式适配差距盘点（2026-11-23）

- 仓库：`73kyyy/NairoClip`
- 分支：`work/v3.3.3-s5-native-editor-foundation`
- 审查基线：`039d90fb19ccd06f39a4515fdc6f3b773843e6d8`
- 方法：只读检查现有 Compose 页面、NativeTimelineView 和 NativeAppShell；没有修改运行时代码或启动入口。

## 1. 总体结论

现有原生页面并非完全没有响应式能力：多处使用 `fillMaxWidth`、`weight`、`LazyColumn` 和横向滚动。但目前仍可见固定 dp 尺寸、页面内独立间距/字号、缺少统一尺寸令牌，以及缺少应用级原生宿主等问题。

**不能把现有页面标记为全设备适配完成。** 本轮是源码级差距盘点，不等同于截图回归、模拟器矩阵或真机验收。

## 2. 全局基础设施差距

### 2.1 NativeAppShell 尚未实现

文件：`mobile/native/android/NativeAppShell.kt`

当前仅有 package 声明和注释，没有根 Compose 宿主、导航、窗口尺寸分发、全局 Insets 策略或统一 Design Tokens 注入点。

影响：目前缺少一个可统一实施全应用响应式策略的根层；不能仅依赖每个页面自行设置 Modifier 来保证一致性。

### 2.2 统一尺寸令牌未建立

已检查的页面直接使用各自的 dp/sp 数值，例如 8/12/14/16/18/20/24/28 dp、8–24 sp 等；当前审查范围内没有看到统一的间距/控件尺寸/触控尺寸/字号/圆角令牌系统。

影响：相似组件在不同页面可能出现尺寸和间距不一致；未来全局调整需要逐文件修改。

### 2.3 系统 Insets 与窗口类别没有形成统一入口

在本轮检查的 NativeAppShell、首页/设置、创建流程、账号支持、编辑器页面和时间线 View 中，没有发现统一 Window Size Class/窗口尺寸策略或统一 WindowInsets/IME 处理入口。

这不代表项目其他文件绝对没有 Insets 处理；但现有页面层尚未显示由统一宿主保证的完整策略，需要全仓继续核查并在宿主建立后统一接入。

## 3. 页面级已发现问题

### 3.1 原生剪辑器

文件：`mobile/native/android/NativeEditorScreen.kt`

- 时间线面板使用固定 `height(218.dp)`。小屏横屏或可用高度被键盘/系统区域压缩时，固定高度可能挤占预览与参数区；大屏也不会按空间重新分配。
- 预览区使用 `weight(1f)`，但需要与固定时间线高度、顶部栏、快捷操作栏和下方参数区联合验证，避免在短窗口下被压缩到不可用。
- 参数控件大量使用页面内独立固定宽度，例如文本数值字段 52/64 dp、转场时间输入 100 dp、滤镜数值字段 48 dp；窄窗口和大字体下需验证溢出/挤压。
- 面板内字号可低至 8–10 sp，需在系统字体缩放和可读性验收中重点检查。
- 存在多个横向滚动工具组，这对窄屏是有效策略，但需检查滚动提示、焦点/无障碍和操作可发现性。

结论：剪辑器必须作为独立的响应式工作区重构布局约束，不能只调整整体高度。

### 3.2 原生时间线 View

文件：`mobile/native/android/NativeTimelineView.kt`

当前直接以密度换算固定几何参数：

- 时间尺高度：27 dp。
- 左侧轨道标签区：120 dp。
- 轨道高度：47 dp。
- 轨道间距：4 dp。
- 初始缩放：42 px/dp 对应的每秒像素比例（代码中以 density 缩放）。

已存在横向/纵向拖动、双指缩放和滚动边界处理，这是良好基础；但左侧标签区、轨道行高、时间尺与点击热区仍是固定值。需要根据窗口宽度、字体缩放、轨道类型与触控需求建立有边界的配置，并确保绘制几何与命中测试使用同一组参数。

另需专项检查：横屏/窄窗口下 120dp 标签区占比、轨道控件命中范围、辅助功能可访问性、极端密度和字体大小。

### 3.3 首页/项目列表与设置

文件：`mobile/native/android/NativeHomeSettingsScreens.kt`

- 首页顶部固定水平 padding 18 dp，快捷操作采用两列 `weight(1f)` 布局。
- 快捷卡片标题单行显示、说明文字字号 10 sp；窄屏或字体放大时需验证文字裁切和卡片最小宽度。
- 项目卡片、设置分组使用独立固定 padding 和圆角数值。
- 列表采用 LazyColumn，具备长内容滚动基础，但页面整体仍需接入统一窗口和 Insets 策略。

### 3.4 创建流程与 AI 向导

文件：`mobile/native/android/NativeCreationFlowScreens.kt`

- 多处采用固定 12/16/20/24 dp 内边距和 10–18 sp 字号。
- 分类、镜头槽位等部分区域使用横向滚动，方向合理，但要验证窄屏触控、焦点和滚动状态。
- 进度页面使用固定 padding 与纵向排列；极短高度/横屏下需要可滚动或重排，避免按钮离开可视区。
- 多步骤 AI 向导的列表与底部操作需要统一适配键盘、窗口变化和字体缩放。

### 3.5 账号与支持页面

文件：`mobile/native/android/NativeAccountSupportScreens.kt`

- 登录页包含固定 48 dp、30 dp 垂直 Spacer，并以单列布局堆叠登录入口；短屏/横屏可能导致下方操作被挤出可视区域。
- 手机号登录表单直接纵向排列，未在该页面看到滚动容器；键盘弹出时需检查提交按钮和状态提示是否仍可访问。
- 反馈页、FAQ、法律页面等使用 LazyColumn 或权重布局，具备部分自适应基础，但仍需统一尺寸令牌与 Insets。
- 多个页面使用 10–12 sp 小字号，需覆盖系统字体缩放和无障碍可读性测试。

## 4. 已具备的可复用基础

- 页面普遍使用 Compose Modifier、weight、fillMaxWidth 和 LazyColumn。
- 模板类别、剪辑工具和参数分类存在横向滚动设计。
- 时间线使用单一 Android View Canvas 绘制大量轨道内容，避免为每个片段创建大量 Compose 节点。
- 时间线已经有水平/垂直滚动、双指缩放及绘制/命中测试共用部分几何参数的实现基础。

这些基础可保留并完善，不需要为了响应式适配而全部推倒重写。

## 5. 整改优先级

### P0：全局宿主与公共尺寸系统
1. 在审批和依赖允许后实现 Native App Shell、导航宿主与窗口尺寸分发。
2. 建立 Design Tokens：间距、字号、控件尺寸、最小触控目标、圆角、图标尺寸、面板约束。
3. 统一 WindowInsets、Edge-to-Edge、IME、DisplayCutout 和系统手势区处理。
4. 建立可复用的自适应按钮、图标按钮、输入框、列表行、卡片、面板和弹窗。

### P1：剪辑器与时间线
1. 将 218dp 固定时间线高度替换为有最小/最大边界、可按窗口类别调整的面板策略。
2. 时间线标签区、轨道行高、尺高和热区统一由一个几何配置对象驱动。
3. 对窄屏、横屏、平板和字体放大分别定义布局行为。
4. 验证预览、工具栏、参数面板和时间线空间分配，保证主要任务可操作。

### P1：登录/表单/短屏页面
1. 登录页去除依赖固定 Spacer 的垂直居中假设。
2. 手机号登录、反馈表单在键盘出现时可滚动并保持提交操作可达。
3. 对进度页、确认弹窗和短高度窗口定义滚动/重排策略。

### P2：外围页面统一化
1. 首页快捷卡片根据窗口宽度选择双列/单列，保证文字完整。
2. 设置、账号、FAQ、法律与支持页面迁移到公共间距和字号令牌。
3. 为横向滚动组补齐无障碍焦点、可见滚动状态和键盘操作。

## 6. 验收要求

- 小屏竖屏、窄屏横屏、常规手机、大屏手机、平板宽窗口。
- 系统字体和显示大小放大。
- 软键盘弹出/收起、状态栏/导航栏/手势区域变化。
- 窗口尺寸变化、分屏与折叠状态变化。
- 自动断言按钮形状比例、控件边界、最小触控面积、间距令牌和内容可见性。
- 对剪辑器重点检查时间线高度、轨道标签宽度、预览区最小空间、参数面板滚动和选中状态保持。
- 对所有页面执行截图回归、交互测试和真机/等效设备验证。

## 7. 审批边界

本盘点不授权切换 Manifest/Launcher Activity，不授权删除 WebView，不改变 A-5/A-6/A-12、S1-3、S3-1、S1-8D 等既有审批和依赖。当前 NativeAppShell 尚未实现且 S5-0B 仍受前置门阻塞；本轮仅形成源码证据和整改清单，没有声称页面已完成响应式适配。


## 8. Level-1 / level-2 toolbar parity and visibility follow-up

Reviewed at `mobile/native/android/NativeEditorScreen.kt` in commit `820972bee04c3fdf25515b48bbdda8dfc016d620`.

The current implementation does satisfy the structural direction that level-2 items expand inline after their level-1 item in the same horizontal scrolling row. The parent selection callback also toggles the same level-1 item closed and replaces the expanded item group when another level-1 item is selected.

However, it does not yet fully satisfy the user's "same horizontal row and same visual style" requirement:

- Level-1 item width is 54dp; level-2 item width is 58dp.
- Level-1 circular icon is 25dp; level-2 circular icon is 22dp.
- Level-1 label uses 9sp; level-2 label uses 8sp and `maxLines = 1`.
- Level-1 and level-2 items use different vertical padding and different background treatment.
- The row uses `horizontalScroll(rememberScrollState())`, but the inspected implementation does not request an automatic scroll to reveal the newly expanded level-2 group. If the selected level-1 item is near the right edge, its newly inserted subtools may remain off-screen until the user manually scrolls.
- The toolbar height is fixed at 82dp. Font scale and compact-height behavior are not handled by a shared responsive metrics policy.

Therefore the correct conclusion is: **inline expansion and toggle/switch state behavior are present; visual parity, auto-reveal, and narrow-window usability are not yet proven and currently have measurable differences.**

Required follow-up:

1. Define one shared toolbar item metrics/style token for level-1 and level-2 items (same icon geometry, text style, touch target, spacing and corner treatment); use label wrapping or a deliberate accessible compact label policy rather than silent single-line clipping.
2. Preserve the user's inline horizontal expansion; do not convert it to a drawer or vertical sheet.
3. After expanding a group, bring the entire visible subtool range into view without changing the selected tool state. Handle groups wider than the viewport with predictable horizontal scrolling.
4. Test first, middle and last level-1 tools, longest subtool group, repeated tap collapse, switching groups, and compact widths.
5. Add Compose UI assertions for item bounds, circular icon width/height equality, touch target size, label visibility and scroll visibility at font scales 1.0/1.3/1.5/2.0.

This is an additional source-level gap; no toolbar implementation was changed in this audit.


## Follow-up audit: account, creation, home/settings and subtitle panels (2026-11-23)

Reviewed source at the current S5 work branch for NativeAccountSupportScreens.kt, NativeCreationFlowScreens.kt, NativeHomeSettingsScreens.kt and NativeAutoSubtitlePanel.kt. This is source inspection only; no runtime UI files were modified.

### Account and support
- NativeFeedbackScreen places its description field in a fillMaxSize Column with a weighted editor, contact field and action row. The reviewed screen body has no local verticalScroll/LazyColumn, imePadding/imeNestedScroll, or BringIntoView handling. When the IME consumes a substantial portion of a short viewport, the contact/status/action controls can be compressed or obscured unless an eventual host supplies correct insets and scroll behavior.
- NativeAuthScreen uses fixed vertical spacers (48dp and 30dp) and a non-scrollable Column. A small-height landscape window, split-screen window, or large font scale can push provider actions/status below the visible area. It also has no local IME-aware scroll behavior.
- Account/profile and FAQ/log screens use LazyColumn, which is a good basis for vertical overflow, but their header/content insets and minimum touch targets still need measured UI tests.

### Creation flows
- Template selection and the four-step reference-creation flow use LazyColumn for long content, but the fixed header and bottom action button sit outside the list. The screen needs window-height tests to prove the list retains a usable viewport and the bottom action does not overlap the IME or gesture navigation area.
- Template search uses an OutlinedTextField outside the list. The source does not show IME padding or bring-into-view handling at this screen boundary.
- Reference creation contains horizontally scrolling category/order/overlay rows. Horizontal scrolling avoids forced wrapping, but the selected item and final options must be automatically brought into view and tested at narrow widths; the current source does not establish that behavior.
- Dropdown menus are used for per-slot media, overlay asset and blend mode. Their anchor positioning, clipping and dismissal behavior need compact-window and edge-of-screen UI tests.

### Home and settings
- Home quick actions are hard-coded as two rows of two equal-weight cards. On narrow widths or large font scale, labels/details can wrap or be clipped because titles/details are constrained to one/two lines. A responsive policy should switch to a single column or otherwise reflow when the measured minimum card width cannot be maintained.
- The top navigation keeps Search, Me and Settings as adjacent text buttons in one row. There is no responsive overflow strategy in this screen source for very narrow widths or large font scale.
- Settings uses LazyColumn and expandable groups, which supports vertical content growth, but row title/summary/value combinations can compete for width. Choice controls and switches need explicit narrow-width behavior and touch-target assertions.

### Automatic subtitle panel
- NativeAutoSubtitlePanel uses fixed 12dp padding, 9dp vertical spacing, 12dp corners and 9–12sp text. The Start/Cancel actions share one non-wrapping Row with an 8dp gap. Under narrow panel width or large font scale, the row can exceed available width; it needs a measured wrap/stack strategy and minimum button target checks.
- Status/error text is rendered in the same fixed panel without a local max-height/scroll policy. Long service errors or accessibility-scaled text need an overflow test.

### Cross-cutting finding
The reviewed screen composables contain no local IME-aware insets/bring-into-view strategy in the inspected layout paths. Because NativeAppShell.kt is still only a package/comment stub and MainActivity remains on WebView, there is not yet an active Compose root host whose insets policy can be verified end-to-end. The eventual host must establish one consistent safe-drawing/system-bar/IME policy, while individual form screens must provide scroll and bring-into-view behavior for focused fields.

### Required responsive test coverage
- Account feedback: IME open/closed, description/contact focus, long status/error text, 360dp width and short landscape height.
- Auth: portrait/landscape, 320–360dp width, short-height split screen, provider list overflow and large font scale.
- Template/reference creation: list viewport with bottom action, search focus/IME, long template names, long localized labels, dropdown anchoring at screen edges.
- Home: quick-action reflow threshold, 320/360/393/430dp widths, font scales 1.0/1.3/1.5/2.0.
- Settings: long title/summary/value combinations, switch/choice alignment, expanded group overflow.
- Subtitle panel: compact width, large font scale, long status/error, Start/Cancel wrap and 48dp target bounds.

All items above remain source-level gaps, not claims of observed device failures. The global responsive specification and all S1-3/S3-1/A-12/A-5/A-6/S1-8D sequencing gates remain unchanged.
