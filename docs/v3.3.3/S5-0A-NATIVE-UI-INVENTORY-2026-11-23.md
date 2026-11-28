# S5-0A 原生 UI 只读盘点（2026-11-23）

## 1. 范围与证据

- 仓库：`73kyyy/NairoClip`
- 分支：`work/v3.3.3-s5-native-editor-foundation`
- 基线：本次盘点从 PR #32 当前 HEAD `f7be63cf18ba79a449cd4956540cead7072ca7b4` 读取。
- 方法：GitHub 当前分支源码只读检查；未修改 Activity、Manifest、运行时导航、JNI 或业务行为。
- 本文是 S5-0A 盘点增量，不代表 S5-0B 接口冻结、S5 runtime integration 或设备验收完成。

## 2. 当前正式启动链路

### 2.1 Launcher Activity

`android/app/src/main/AndroidManifest.xml` 将 `.MainActivity` 注册为 `MAIN/LAUNCHER`。

### 2.2 MainActivity 实际行为

`android/app/src/main/java/com/nairoclip/app/MainActivity.kt` 当前：

1. 继承 `AppCompatActivity`，创建 `WebView`。
2. 配置 `WebViewAssetLoader`、JavaScript、DOM Storage 与原生 JS Bridge。
3. 创建 `NativeEngineBridge` 并绑定旧页面使用的 `NCEngine`。
4. 通过 `webView.loadUrl("https://appassets.androidplatform.net/assets/pages/01-home.html")` 启动首页。
5. 使用 `FrameLayout` 叠放 `PreviewSurfaceView` 与 WebView。
6. 返回键通过 JS 的 `NCL3BACK/NCBARBACK` 路由。

**结论：当前正式 Launcher 仍然是 WebView 页面入口；不是 Compose 原生入口。** SurfaceView 预览叠层也不改变 UI 主体仍由 WebView 绘制这一事实。

## 3. 已存在的原生页面组件（源码存在 ≠ 已接入运行时）

| 功能区域 | 当前 Compose 页面/模型 | 盘点结论 |
|---|---|---|
| 首页/项目列表 | `NativeHomeScreen`、`NativeHomeProject` | 页面组件存在；正式宿主/项目数据提供者未接通 |
| 设置 | `NativeSettingsScreen`、`NativeSettingGroup/Item` | 页面组件存在；设置持久化与宿主路由未接通 |
| 模板创建 | `NativeTemplateScreen`、`NativeTemplateFillScreen`、`NativeTemplateProgressScreen` | 页面组件存在；模板数据、媒体选择与生成任务宿主未闭环 |
| AI 编辑向导 | `NativeAIEditWizardScreen` | 页面组件存在；参考视频分析/生成链路须单独验证 |
| 我的/账号 | `NativeMeScreen`、`NativeAuthScreen`、`NativePhoneLoginScreen` | 页面组件存在；认证与账号服务回调仍需宿主接线 |
| 支持页面 | `NativeFeedbackScreen`、`NativeFaqScreen`、`NativeLegalScreen`、`NativeCrashLogScreen` | 页面组件存在；提交、内容加载、日志数据源和路由未形成完整宿主闭环 |
| 剪辑器 | `NativeEditorScreen`、`NativeTimelineView`、`NativeTimelineSnapshot` | 原生编辑器组件和部分适配存在；正式 Activity、项目生命周期、播放/Seek、工具命令及预览时钟仍未闭环 |

主要源码：
- `mobile/native/android/NativeHomeSettingsScreens.kt`
- `mobile/native/android/NativeCreationFlowScreens.kt`
- `mobile/native/android/NativeAccountSupportScreens.kt`
- `mobile/native/android/NativeEditorScreen.kt`
- `mobile/native/android/NativeTimelineView.kt`
- `mobile/native/android/NativeTimelineSnapshot.kt`

## 4. 原生宿主缺口

`mobile/native/android/NativeAppShell.kt` 当前仅有 package 声明与一行注释，没有 Compose 根组件、导航状态、页面路由或 ViewModel/状态宿主。

因此，当前状态不是“所有页面已经完成原生化”，而是：

- 多个页面级 Compose 组件已经存在；
- 这些组件尚未由统一 Native App Shell 组合成可运行的页面导航体系；
- MainActivity 尚未调用这些页面；
- Launcher 仍加载 WebView 的 `pages/01-home.html`；
- 页面回调需要真实数据源、持久化、媒体选择器、认证、任务执行和引擎命令处理，不能用空回调或假数据替代。

## 5. 本轮验证边界

- 已直接检查 MainActivity 的启动和 WebView 装配代码。
- 已直接检查 AndroidManifest 的 MAIN/LAUNCHER 声明。
- 已直接检查 NativeAppShell 当前内容。
- 已枚举并核对首页/设置、创建流程、账号/支持和编辑器页面的顶层 Composable/模型声明。
- 本轮没有执行 Android 设备启动、导航自动化、视觉回归或真机媒体导入。
- 本轮最新 CI 的成功只证明对应提交通过其工作流任务，不替代设备验收。

## 6. 依赖与审批边界

1. S5-0B 仍依赖 S1-3 工程保存/恢复验收与 S3-1 Rational 时间模型验收。
2. S5-1 仍受 S1-8D Manifest 文件串行约束。
3. A-5（原生 UI 迁移）未在任务审批记录中确认解除前，不切换 Launcher Activity/Manifest。
4. A-6（删除 WebView）未批准前，不删除旧页面和 WebView 回退路径。
5. 即使 A-5 后续解除，入口切换也必须等页面路由、真实业务回调、项目生命周期、预览 Surface、播放/Seek、失败回退和设备验收闭环后进行。

## 7. 建议执行顺序（不改变 DAG）

- 先完成 S5-0B 前置验收证据核对和接口冻结；不将“组件源码存在”当作前置验收。
- 解除相应审批与依赖后，先建立 Native App Shell 与页面路由，再接首页/设置/创建/账号等外围页面。
- 然后接编辑器宿主、引擎状态、媒体导入、预览 Surface、播放时钟、Seek、工具命令与状态回读。
- 通过自动化、设备与真机回归后，再评估正式 Launcher 切换。
- WebView 兼容回退最后单独验收；不得提前删除。

**最终结论：S5 原生 UI 已有较多页面组件，但应用级原生宿主和正式入口迁移尚未完成。**
