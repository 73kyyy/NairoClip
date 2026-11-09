# NairoClip 00—07 规范符合性审计报告 V1.0

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-AUDIT-00-07 |
| 文档性质 | 只读审计结论（第一轮交付物之一） |
| 审计基线 | 分支 `main`，HEAD `90ebd9bbed8cceb2ef2716dedb293fa72999d28f` |
| 审计日期 | 2026-11-09 |
| 审计方式 | 全量文件树枚举 + 关键源码精读 + 静态检索，**未修改任何文件** |
| 依据规范 | 00—07 八份正式开发规范；《NC-RECT-00-07 整改与实施任务书》 |

> **声明**：本报告只描述"当前代码实际是什么"，不含任何整改动作。所有结论均附检索或文件证据；无证据支撑的条目一律标注「待审计」，不做推断。

> **V1.1 勘误（2026-11-09，已用 CI 实测数据修正）**
> 初版 D-02 称「`Android APK` 构建失败」，**该结论错误**，来源是 `docs/HANDOFF.md` 的过期记录。
> 按当前 `ci-status` 分支实测：
> - `Android APK`（run 42429168792，HEAD `90ebd9bb`）= **success**
> - `CI`（run 42429168790，HEAD `90ebd9bb`）= **failure**，失败点为 `build-and-test` 的第 10 步「C ABI 调用点检查」
> - 根因：`tools/check_capi_calls.py` 未剥注释，把 `core/src/capi.cpp:2793` 文档注释里的 `nc_surge_preset_describe()` 误判为 0 参数调用点；真实调用点均为 3 参数，**C++ 代码本身无缺陷**
> - 已于 PR #1 修复并合并（main `61dc318e`），`build-and-test` 转绿
>
> 本条同时印证 D-10：历史文档不可作为事实来源。

---

## 1. 审计范围与方法

### 1.1 覆盖面

| 项 | 数值 |
|---|---|
| 仓库文件总数（blob） | 816 |
| 目录分布 | `mobile/` 413、`core/` 191、`tests/` 125、`docs/` 22、`android/` 19、`tools/` 17、`design/` 13、`.github/` 7 |
| 精读文件 | 200+（全部 97 个核心头文件、93 个核心 cpp 抽样、Android 全部 19 个文件、JNI 桥、前端 JS、CI 全部 7 个 workflow） |
| 检索方式 | 关键字全仓扫描（契约字段、错误码、事件、平台 API） |

### 1.2 方法限制（必须声明）

1. 本环境为手机 aarch64 容器，**未执行编译、未运行测试**。凡是"能否编译通过"的结论均来自 CI 记录与源码静态判断，标注为「待 CI 验证」。
2. 未做真机运行验证，所有运行时行为结论标注为「待真机验证」。
3. 历史文档（`docs/HANDOFF.md` 等）**不作为事实来源**——本次审计已发现其多处与代码不符（见 §6 D-10）。

---

## 2. 仓库基线快照

| 检查项 | 实测结果 |
|---|---|
| 当前分支 | `main` |
| HEAD | `90ebd9bbed8cceb2ef2716dedb293fa72999d28f` |
| 其它分支 | `ci-status`（CI 结果回写只读分支） |
| 标签 | `android-builds`、`apk-latest`、`assets-relay` |
| 工作区 | 干净（审计全程只读） |
| Remote | `git@github.com:73kyyy/NairoClip.git` |
| 顶层构建 | CMake 3.16+，C++17，`NAIROCLIP_BUILD_TESTS=ON` |
| Android 构建 | Gradle + AGP + Kotlin，`compileSdk 34 / minSdk 24 / targetSdk 34` |
| ABI | **仅 `arm64-v8a`**（注释说明主动砍掉 armeabi-v7a / x86_64） |
| CI 工作流 | 7 个：`ci.yml`、`android-apk.yml`、`android-build.yml`、`ci-status.yml`、`report.yml`、`fetch-assets.yml`、`gen-assets.yml` |
| CI 触发 | 核心 workflow 均只对 `push: main` 与 `pull_request: main` 触发 |
| 第三方依赖 | FFmpeg（LGPL 动态链接）、ONNX Runtime（可选）、微信 SDK、QQ SDK、esbuild/eslint（仅前端构建） |
| 已知 CI 状态 | `Android APK` **success**（run 42429168792）；`CI` **failure**（run 42429168790，失败于「C ABI 调用点检查」误报，已于 `61dc318e` 修复） |

---

## 3. 代码资产清单（对应任务书 §2.2）

| 资产类型 | 位置 | 规模 | 职责 | 状态标注 |
|---|---|---|---|---|
| Android 原生代码 | `android/app/src/main/java/com/nairoclip/app/` | 3 个 Kotlin（MainActivity 274 行 / MemoryGuard 195 / PreviewBridge 155） | Activity 容器、内存压力、预览区坐标桥 | 部分完成 |
| Android 平台层正式实现 | `mobile/native/android/` | 5 个 Kotlin（NativeEngineBridge 20 KB、NativeAuthBridge 8 KB、PreviewSurfaceView 4 KB、AndroidTextRasterizer 6 KB、WXEntryActivity 6 KB） | JNI 句柄、登录桥、Surface 预览、文本栅格化 | 部分完成 |
| JNI 桥 | `mobile/native/android/nc_jni.cpp` | 113,949 字节，20 个 `JNIEXPORT`，**114 个 `op == "..."` 分支** | 字符串 op 派发到 C ABI | **存在缺陷**（业务逻辑下沉不足） |
| C ABI | `core/include/nairoclip/core/capi.h` | 137,275 字节，**332 个 `nc_*` 函数** | 跨语言唯一稳定入口 | 部分完成（无版本化结构体） |
| C++ 核心 | `core/include/`(97 h) + `core/src/`(93 cpp) | 头 693 KB / 实现约 1.8 MB | 时间/模型/命令/编辑/媒体/导出/AI/特效 | 已实现（本体质量高） |
| Web 页面 | `mobile/pages/`(15 个) + `mobile/static/`(6 个，单文件最大 597 KB) | — | 首页/我的/登录/设置/法律/反馈/FAQ/崩溃日志/编辑器/模板/AI | 部分完成 |
| JavaScript | `mobile/assets/` | **12,392 行**（editor.js 5,435 行、editor.css 75 KB） | 编辑器 UI、时间轴、工具栏、参数面板 | 已实现（但载体违规） |
| 时间轴 | `core/`（sequence/track/clip/keyframe/transition） + `editor.js` | — | 数据模型 C++ 权威，交互在 JS | 数据层已实现，UI 层载体违规 |
| 媒体模块 | `media_ffmpeg.cpp`、`media_io.cpp`、`media_writer.cpp`、`preview.cpp`、`thumbnail.cpp`、`proxy.cpp` | — | 探测/解码/预览/缩略图/代理 | 已实现 |
| GPU | `render_engine.h`（`IRenderBackend` 抽象） | 仅接口 | 无实现 | **未实现** |
| 音频 | `audio_mixer.cpp`、`audio_denoise.cpp`、`beat_detect.cpp`、`auto_duck.cpp` | — | 混音/DSP/节拍/闪避 | 部分（无实时输出） |
| 测试 | `tests/` | 125 文件 / 1,506 KB | 自研零依赖框架 | 已实现（用例数待 CI 复核） |
| 生成代码 | `design/tools/api_dispatch.inc`（131 KB） | 由 `gen_api_dispatch.py` 生成 | 设计桥通用派发 | 已实现（**自动生成，非手工**） |
| 工具链 | `tools/` 17 个脚本 | — | ABI 所有权检查、前端检查、C ABI 调用检查等 | 已实现 |

---

## 4. 功能基线（对应任务书 §2.3 的 20 项）

| # | 功能 | 证据 | 状态 |
|---|---|---|---|
| 1 | 项目创建/打开/保存 | `nc_project_save` / `nc_project_load` / `serialization.cpp` | 已实现 |
| 2 | 媒体导入与素材管理 | `nc_asset_*`、`media_ffmpeg.cpp`、`proxy.cpp` | 已实现 |
| 3 | 视频预览 | `nc_preview_render`、`preview.cpp`、`PreviewSurfaceView.kt` | 已实现（CPU 合成路径） |
| 4 | 时间轴显示与操作 | `editor.js` 5,435 行，Canvas 绘制 | 已实现（WebView） |
| 5 | 片段移动/裁剪/分割 | `nc_split_clip`/`nc_remove_clip`/`trim_start` 等 + `commands.cpp` | 已实现 |
| 6 | 多轨道管理 | `track.h`、`nc_add_track` 等 | 已实现 |
| 7 | 音频轨道 | `TrackType::Audio`、`audio_mixer.cpp` | 部分（无实时输出设备链路） |
| 8 | 关键帧 | `keyframe.h`、`animation.h`、`nc_insert_keyframe` | 已实现 |
| 9 | 参数调节 | `editor.js` 参数面板 + `XF_SPEC` | 已实现（JS 侧） |
| 10 | 转场与特效 | `transition*.cpp`、`effects.cpp`、`particle_engine.cpp` | 已实现 |
| 11 | 字幕与文字 | `subtitle.cpp`、`text_content.h`、`AndroidTextRasterizer.kt` | 部分（文本素材化未完成） |
| 12 | 调色 | `lut.cpp`、`chroma_key.cpp`、`blend_modes.cpp` | 已实现 |
| 13 | AI 功能 | `ai_engine.cpp`、`stem_ai.cpp`、`super_res_ai.cpp`、`matting_ai.cpp`、`depth_ai.cpp`、`model_manager.cpp` | 部分（依赖外部模型/运行时） |
| 14 | 导出 | `export_executor.cpp`、`export_plan.cpp`、`export_queue.cpp`、`media_ffmpeg_writer.cpp` | 已实现（需 CI/真机验证） |
| 15 | 登录与账号 | `NativeAuthBridge.kt`、`WXEntryActivity.kt`、微信/QQ SDK | 部分（真实回调待真机验证） |
| 16 | 设置与权限 | `mobile/pages/04-settings.html`、Manifest 权限声明 | 部分 |
| 17 | 撤销/重做 | `command.h` UndoStack、事务合并、`editRevision` | 已实现 |
| 18 | 工程序列化 | `serialization.cpp`、`.nclip` JSON | 已实现 |
| 19 | 崩溃恢复 | `recovery.cpp`、`crash_handler.cpp`、`MemoryGuard.kt` | 部分 |
| 20 | 设备适配 | `MemoryGuard.kt`（内存压力） | 部分（无设备画像/分级） |

---

## 5. 逐规范符合性矩阵

等级定义（任务书第十四章）：**A** 真实实现+测试+证据 / **B** 主要实现有已知限制 / **C** 部分实现有重要缺陷 / **D** 只有接口或设计 / **F** 未实现或不可用。

### 5.1 《00 项目总纲与开发治理规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §3 单一事实源 | 工程数据以 C++ Core 为准 | ✅ 符合。`Project` 为唯一权威，UI 无第二份可保存时间轴 | `project.h`、`editor.js` 仅持视图状态 | A |
| §3 原生优先 | Android 用 Kotlin 原生 UI；禁 WebView 长期承载 | ❌ 全屏 WebView 承载编辑器 | `MainActivity.kt` 注释「全屏 WebView + 两个原生桥」 | **F** |
| §3 接口契约 | 版本化契约+参数校验+错误码+线程约束 | ❌ 仅 `Result{code,message}`，无版本、无 commandId | `types.h` | **F** |
| §3 预览导出一致 | 同一效果语义 | ⚠️ 预览与导出共用 `applyXformStack`（xform 已统一） | `preview.cpp`/`export_executor.cpp` | B |
| §3 可观测性 | 结构化日志/耗时/错误分类 | ⚠️ 有 `perf_probe`、`heartbeat`、`crash_handler`，无统一错误分类 | 对应文件 | C |
| §5 功能状态 8 档登记 | 必须逐项登记 | ❌ 无登记表 | — | F |
| §6 DoD 七条 | 需正式记录 | ❌ 无 DoD 记录 | — | F |
| 治理 §4 ADR | 架构决策记录 | ❌ 无 ADR 目录 | — | F |

### 5.2 《01 核心引擎总体架构规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §1 分层边界 | UI 不得直接操作媒体/工程库 | ❌ JS 经 `window.NCEngine` 直接调 `nc_*`，无 Application/Editor 中间层 | `editor.js` 头部注释、`NativeEngineBridge.kt` | **F** |
| §2 模块边界 | 六模块职责隔离 | ✅ `Project/Timeline/Media/Render/Audio/Export` 命名与实现基本对应 | `core/src/*` | B |
| §3 线程域 | 媒体/解码/渲染/导出独立调度域 | ❌ 仅 `thread_pool.cpp` 使用线程；`TimelineEditor` 明示「非线程安全，仅 UI 线程」；Kotlin 侧 `@Synchronized` 全局串行 | `editor.h` 头注释、`NativeEngineBridge.kt` | **F** |
| §4 JNI 仅转发 | 不得堆积业务规则 | ❌ 114 个 `op` 分支 + 参数解析 + 状态拼装全在 JNI | `nc_jni.cpp:405-…` | **F** |
| §4 异常不穿越 JNI | 统一错误返回 | ⚠️ C ABI 用返回码，但无错误域分类 | `capi.h` | C |
| §5 GPU 抽象+CPU 回退 | 运行时探测后端 | ⚠️ 只有 `IRenderBackend`/`RenderBackendInfo`/`RenderQuality` 抽象，**无任何 EGL/GLES 调用** | `render_engine.h`；全仓 `eglCreate`/`glTexImage` 命中 0 | **D** |
| §6 错误域 7 类 | PROJECT/TIMELINE/MEDIA/RENDER/AUDIO/EXPORT/SERVICE | ❌ 只有 9 值 `ErrorCode` 枚举，无域前缀、无字段级原因 | `types.h` | **F** |
| §7 依赖检查 | 禁循环依赖/平台类型入领域模型 | ⚠️ 领域模型干净（无平台类型）；无自动化依赖检查 | `types.h`、`project.h` | B |

### 5.3 《02 多平台原生开发与扩展规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §1 共享边界 | 共享 schema/时间/命令语义 | ✅ 时间与模型在 C++ | `rational.h`/`project.h` | A |
| §2 平台分工 | Android Kotlin 原生 UI | ❌ WebView | 同 5.1 | F |
| §3 不应共享项 | 禁共享 Activity/Surface/像素坐标 | ✅ 核心层无平台类型 | `core/include` 全查 | A |
| §4 稳定 C ABI | 优先 C ABI | ✅ 332 个函数 | `capi.h` | A |
| §4 结构体 size/version | 跨版本兼容 | ❌ 全为扁平标量/数组参数，无 `size`/`version` 字段 | `capi.h` 抽样 | **F** |
| §4 时间统一有理数 | 禁浮点秒 | ✅ `Time = Rational`，128 位中间量 | `rational.h`/`time.h` | A |
| §6 桌面端专项 | 高 DPI/多显示器/路径重连 | ⚠️ `media_offline.cpp` 有离线重链；桌面 UI 未开始 | 对应文件 | C |

### 5.4 《03 全端连接与系统集成规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §1 连接原则 | UI 不直接调引擎实现 | ❌ 直接 `nc_*` | `editor.js` | F |
| §3 API 五类 | Command/Query/Event/Task/Capability | ❌ 只有同步 RPC；无 Event、无 Task、无 Capability | 检索 `eventBus`/`subscribe`/`EventListener` = 0 | **F** |
| §4 命令 8 字段 | commandId/schemaVersion/expectedRevision/origin/transactionId… | ❌ 全缺（检索命中 0）；`Command` 只有 `apply/revert` | `command.h` | **F** |
| §5 事件同步 | envelope + revision + 重连拉快照 | ❌ 无 | — | F |
| §6 桥接规则 | 禁仅凭函数名字符串判权限 | ❌ `op == "..."` 字符串派发即此模式 | `nc_jni.cpp` | **F** |
| §7 服务集成 | Auth/Entitlement/Billing/AI/Sync/Plugin | ⚠️ 有 `entitlement.h`、微信/QQ 登录桥；无 Billing、无 Sync、无 PluginHost | 对应文件 | C |
| §8 身份闭环 | PKCE/state/回调校验 | ⚠️ 有 `NativeAuthBridge.kt`，回调校验强度待审计 | 待审计 | C |

### 5.5 《04 兼容性、设备适配与质量保证规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §2 设备画像 9 维 | CPU/内存/GPU/编解码/存储/显示/热 | ❌ 仅内存压力 | `MemoryGuard.kt` | **F** |
| §3 能力分级 Tier A–D | 分级与差异化行为 | ❌ 无 | — | F |
| §4 动态资源策略 | 按压力缩缓存/降预览 | ⚠️ 内存分级有，缩略图/纹理缓存联动待审计 | `MemoryGuard.kt` | C |
| §5 编解码兼容 | 探测而非按扩展名 | ⚠️ `codec_policy.cpp` 存在，探测矩阵待审计 | 待审计 | C |
| §6 测试矩阵 6 层 | 单元/集成/设备/压力/恢复/兼容 | ⚠️ 单元测试强；设备/压力/恢复矩阵无记录 | `tests/` | C |
| §7 性能指标 | 首帧/掉帧/延迟/内存分组报告 | ⚠️ 有 `perf_probe.cpp`、`docs/PERF_XFORM_PLAN.md`，无按 Tier 的报告 | 对应文件 | C |

### 5.6 《05 多轨时间轴与专业剪辑系统规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §2 对象模型 | Sequence/Track/Clip/Transition/Keyframe/Marker/Compound/Gap | ✅ 全部有 | `sequence.h`/`track.h`/`clip.h`/`keyframe.h`/`transition.h` | A |
| §3 时间精度 | 禁 float 秒，有理数/tick | ✅ `Rational` | `rational.h` | A |
| §3 非整数帧率 | 30000/1001 等 | ✅ `fps2997()` 等 | `time.h` | A |
| §3 半开区间 | [in, out) | ✅ 模型遵循 | `clip.h` | A |
| §4 编辑语义 | 移动/裁剪/分割/波纹/吸附/复制/撤销 | ✅ C++ 命令齐备 | `commands.cpp` 116 KB | A |
| §5 轨道类型与混合 | 视频/音频/字幕/调整层/组 | ✅ `TrackType` 四类 + `TrackGroup` | `track.h` | A |
| §6 UI 布局交互 | 时间轴区可折叠/轨头固定/Trim Handle | ⚠️ JS 已实现大部分，需真机验证 | `editor.js` | B |
| §7 虚拟化 | 只画可见区 | ⚠️ JS Canvas 绘制，未见 View 爆炸；方案是否满足大工程待压测 | `editor.js` | B |
| §8 关键帧曲线 | Linear/Bezier/Hold | ✅ `keyframe.h`/`animation.h` | 对应文件 | A |
| §9 撤销事务 | 一次意图一事务 | ✅ `beginTransaction/endTransaction`、`canMergeWith` | `command.h` | A |
| §10 时间轴 API | 快照/增删/移动/裁剪/分割/revision | ⚠️ 有对应 C ABI，但无 `expectedRevision` 并发控制 | `capi.h` | C |

### 5.7 《06 编辑器 UI、工具栏与页面系统规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §1 原生 UI | 页面用原生 UI | ❌ 全 HTML/JS | `mobile/pages/*.html` | **F** |
| §1 时间轴自定义绘制 | 禁大量普通 View | ✅ 用 Canvas（JS） | `editor.js` | B |
| §2 页面注册表 12 页 | 稳定 pageId + 依赖模块 | ⚠️ 有 15 个 HTML 页面，但无注册表/无 pageId 契约 | `mobile/pages/` | C |
| §3 编辑器主界面分区 | 预览/播放控制/一级/二级/参数/时间轴/轨头 | ✅ JS 已实现 | `editor.js` | B |
| §4 Toolbar Registry | 稳定 toolId，禁各页面硬编码 | ❌ 工具表硬编码在 JS 数组（`XFORM_ITEMS`/`XF_SPEC`） | `editor.js` | **F** |
| §6 二级横向展开 | 一级右侧同行展开/再点收起/切换自动收 | ✅ 已实现 | `editor.js` 头部注释第 5 条 | B（载体仍违规） |
| §7 参数控件 6 类 | Slider/Numeric/XY/Curve/Color/Toggle | ⚠️ 已有多类，Curve 编辑器待审计 | `editor.js` | C |
| §8 可访问性 | 字体缩放/读屏/触控尺寸/高对比 | ⚠️ 有 `a11y.css`，覆盖度待审计 | `mobile/assets/a11y.css` | C |
| §9 路由与状态恢复 | 进程重建恢复 | ⚠️ 有 `_probe.html`、崩溃日志页，恢复策略待审计 | 待审计 | C |

### 5.8 《07 统一数据模型、Editor API 与接口契约规范》

| 条款 | 要求 | 现状 | 证据 | 等级 |
|---|---|---|---|---|
| §2 核心实体 9 类 | Project/Sequence/Track/Clip/MediaAsset/EffectInstance/Keyframe/Task/Revision | ⚠️ 8 类有；**Task 无独立模型**（导出有队列，非通用 Task）；**Revision 仅 `UndoStack::editRevision`**，未作为契约暴露 | `types.h`/`export_queue.h`/`command.h` | C |
| §3 ID 稳定 | 不依赖下标 | ✅ 强类型 `Id<Tag>` + `IdAllocator` | `types.h` | A |
| §3 单位显式 | 时间/色彩/坐标/采样率显式 | ✅ `Rational`/`FrameRate`/`Canvas`；色彩空间字段待审计 | `time.h` | B |
| §3 禁存平台对象 | 领域模型无指针/句柄 | ✅ | `core/include` | A |
| §4 命令契约 8 字段 | 全缺 | ❌ | `command.h` | **F** |
| §5 查询带 revision/分页/投影 | 无 | ❌ C ABI 无 revision 参数，全量拷数组 | `capi.h` | **F** |
| §6 事件契约 | envelope 7 字段 | ❌ 无 | — | F |
| §7 错误码体系 | `NC-` 前缀 8 域 | ❌ 检索 `NC-` 命中 0 | — | **F** |
| §9 版本迁移 | 临时文件+校验+原子替换+备份 | ⚠️ `recovery.cpp`/`project_doctor.cpp` 存在，原子性待审计 | 待审计 | C |
| §10 安全与权限 | 输入限长/拒绝超大工程/日志脱敏 | ⚠️ 有 `crypto.cpp`；输入上限检查待审计 | 待审计 | C |
| §11 契约测试 | schema/错误码/revision/幂等 | ❌ 无（现有 125 个测试是功能测试） | `tests/` | **F** |

---

## 6. 架构缺陷清单（按严重度）

### P0 — 阻断级

| ID | 缺陷 | 证据 | 影响规范 |
|---|---|---|---|
| D-01 | **编辑器由全屏 WebView 承载**，`editor.js` 5,435 行承担时间轴/工具栏/参数面板 | `MainActivity.kt`、`mobile/assets/editor.js` | 00 §3、02 §2、06 §1 |
| D-02 | **CI 的 `build-and-test` 失败**：`tools/check_capi_calls.py` 未剥注释，把注释里的 `nc_*()` 引用误判为调用点（已修复，`61dc318e`） | CI run 42429168790 第 10 步；`core/src/capi.cpp:2793` | 任务书 §3.5 |

### P1 — 严重

| ID | 缺陷 | 证据 | 影响规范 |
|---|---|---|---|
| D-03 | JNI 用 114 个 `op == "字符串"` 分支承载业务派发 | `nc_jni.cpp` | 01 §4、03 §6 |
| D-04 | 命令无信封（commandId/schemaVersion/expectedRevision/origin/transactionId 全缺） | `command.h`；检索命中 0 | 03 §4、07 §4 |
| D-05 | 无错误码体系（无 `NC-` 域、无字段级原因） | `types.h` | 01 §6、07 §7 |
| D-06 | 无事件总线（`eventBus`/`subscribe`/`EventListener` 命中 0） | 全仓检索 | 03 §5、07 §6 |
| D-07 | 单线程模型 + 全局 `@Synchronized`，无线程域划分 | `editor.h`、`NativeEngineBridge.kt` | 01 §3、任务书 P3 |
| D-08 | GPU 只有抽象接口，**零 EGL/GLES 调用** | `render_engine.h`；`eglCreate`/`glTexImage` = 0 | 01 §5、任务书 P4 |
| D-09 | 无实时音频输出链路（`AAudio`/`AudioTrack`/`OpenSL` 真实 API 命中 0） | 全仓检索 | 05 §5、任务书 §10.4 |

### P2 — 中等

| ID | 缺陷 | 证据 | 影响规范 |
|---|---|---|---|
| D-10 | **历史文档与代码不符**（`HANDOFF.md` 称 `api_dispatch.inc` 手工维护、称 `NativeEngineBridge.kt` 双份副本 —— 实测前者由 `gen_api_dispatch.py` 自动生成，后者已删除旧副本） | `HANDOFF.md` §0.3/§0.4 vs `design/tools/api_dispatch.inc` 头部、`build.gradle.kts` 注释 | 任务书 §16-14 |
| D-11 | 发布签名口令有弱默认值 `"nairoclip"`（env 未设时回落） | `android/app/build.gradle.kts:89-91` | §10 安全 |
| D-12 | 工具清单硬编码在 JS，无 Toolbar Registry | `editor.js` 的 `XFORM_ITEMS`/`XF_SPEC` | 06 §4 |
| D-13 | 无设备画像与 Tier 分级 | 仅 `MemoryGuard.kt` | 04 §2/§3 |
| D-14 | 无通用 Task 模型（导出队列 ≠ Task API） | `export_queue.h` | 03 §3、07 §2 |
| D-15 | `track.h` 存在重复注释块（19–28 行重复两遍） | `track.h` | 代码质量 |

---

## 7. 风险清单

| # | 风险 | 可能性 | 影响 | 缓解 |
|---|---|---|---|---|
| R1 | 直接推翻 WebView 导致现有功能全丢 | 高 | P0 | 采用任务书 P5 六阶段渐进迁移，WebView 保留至功能对等 |
| R2 | 一次性引入 GPU 路径造成真机黑屏/崩溃 | 中 | P1 | 保留 CPU fallback，按设备能力探测切换 |
| R3 | 契约层改造破坏现有 332 个 C ABI | 高 | P1 | 新增版本化入口，旧 ABI 保留并加弃用标注 |
| R4 | 手机环境编译导致过热/超时 | 高 | P2 | 严格遵守本地零执行，全部走 GitHub Actions |
| R5 | 历史文档误导后续开发 | 高 | P2 | 本报告已标注 D-10，整改时同步修订文档 |
| R6 | 签名口令弱默认值导致发布包可被伪造 | 低 | P0 | P0 阶段改为无默认值、缺失即失败 |
| R7 | CI 长期红导致无法验证任何改动 | 确定 | P0 | P0 优先修复，未修复前不做 P1+ |

---

## 8. 资产保护结论（对应任务书 §2.4）

| 资产 | 处置 | 理由 |
|---|---|---|
| `core/` C++ 核心（191 文件） | **保留** | 时间模型、命令模式、对象模型质量高，符合 05 规范 |
| `capi.h` 332 个 C ABI | **保留 + 增量** | 不删不改签名，新增版本化包装层 |
| `mobile/assets/*.js`（12,392 行） | **保留（过渡）→ 逐步淘汰** | 功能完整，是 P5 迁移的行为参照 |
| `mobile/native/android/*.kt` | **保留 + 重构** | 登录桥/Surface/文本栅格化可复用 |
| `android/app/src/**` | **保留 + 扩展** | 作为原生 UI 宿主 |
| `tests/` 125 文件 | **保留** | 回归基线 |
| `design/tools/*` | **保留** | 设计桥与派发生成器 |
| `docs/HANDOFF.md` | **修订**（不删） | 已过期，需按 D-10 更新 |
| 无 | **淘汰** | 本轮无确认可删资产 |

---

## 9. 验收等级汇总

| 规范 | 综合等级 | 说明 |
|---|---|---|
| 00 治理 | **D** | 单一事实源 A，但原生载体 F、契约 F、登记表 F |
| 01 引擎 | **C** | 模块边界好，JNI/线程/GPU/错误码四项 F |
| 02 多平台 | **C** | C ABI 与时间模型 A，UI 载体 F、结构体版本 F |
| 03 集成 | **F** | 五类 API 缺 4 类，命令信封与事件全缺 |
| 04 兼容 | **C** | 有内存保护，缺画像/分级/矩阵 |
| 05 时间轴 | **B** | 数据模型与编辑语义最强，仅 UI 载体与并发控制拖累 |
| 06 UI | **D** | 交互设计达标，载体与注册机制不达标 |
| 07 契约 | **D** | 实体与 ID 好，命令/查询/事件/错误码全缺 |

**总体结论**：C++ 核心（05）达到 B 级，是本项目最有价值的资产；**跨层契约（03/07）与原生载体（00/02/06）是两块最大短板，且二者相互依赖**——契约不立，原生 UI 迁移会返工；原生 UI 不做，契约无处验证。

---

## 10. 审计证据索引

| 结论 | 可复核命令/位置 |
|---|---|
| 无命令信封字段 | `grep -rn "commandId\|schemaVersion\|expectedRevision\|transactionId" core/` → 0 |
| 无错误码体系 | `grep -rho "NC-[A-Z]*-" core/` → 0 |
| 无事件总线 | `grep -rn "eventBus\|subscribe\|EventListener" core/` → 0 |
| 无 GPU 调用 | `grep -rn "eglCreate\|glTexImage\|GLES" core/src/` → 0 |
| 无实时音频 API | `grep -rn "AAudioStream\|AudioTrack()\|OpenSL" .` → 0 |
| WebView 载体 | `android/app/src/main/java/com/nairoclip/app/MainActivity.kt:12-25` |
| JNI op 分支 | `grep -c "op ==" mobile/native/android/nc_jni.cpp` → 114 |
| C ABI 规模 | `grep -o "nc_[a-z_]*" core/include/nairoclip/core/capi.h \| sort -u \| wc -l` → 332 |
| 派发表为生成物 | `design/tools/api_dispatch.inc:1-5` 头部标注 + `gen_api_dispatch.py` 存在 |
