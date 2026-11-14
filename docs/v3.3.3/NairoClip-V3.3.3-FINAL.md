# NairoClip V3.3.3 Final — Overall Implementation Blueprint

> 基线 `main` @ `2ad6fb22ab89675627681dde1bf3180bc13a19e7` · 快照 `/root/workspace/audit_snapshot`
> 模式 DOCUMENT-ONLY · 零源码/构建/CI/PR 修改

## 0. 证据等级与状态模型

**证据等级**：已确认 / 部分确认 / 待验证 / 不可验证 / 不适用
**完成状态（不得跨级）**：Planned → Implemented → Built → Tested → Integrated → Device-verified → Accepted

## 1. 事实基线

| 指标 | 值 | 等级 |
|---|---|---|
| HTML | 21 / 11,185 | 已确认 |
| JS | 24 / 14,531 | 已确认 |
| CSS | 12 / 3,609 | 已确认 |
| Kotlin | 8 / 1,911 | 已确认 |
| C++ .cpp | 94 / 45,679 | 已确认 |
| C++ .h | 107 / 16,191 | 已确认 |
| C ABI | 355 声明 / 355 定义 | 已确认 |
| NC_API(JS) | 349 | 已确认 |
| E6 | 13 / 54 / 44 | 已确认 |
| JNI op | 121 | 已确认 |
| APK .so | 6 | 已确认 |
| 编译用例 | **1,806** | 已确认 |
| CI 执行 | **1,805** | 已确认 |
| CI 门槛 | **1,458**（下界） | 已确认 |
| PR #28 | open / clean / 9 文件 / +2,448−69 | 已确认 |
| 真机证据 | 零 | 不可验证 |
| 本地工作区 | 不可判定 | 不可验证 |

**测试差值（F-07）**：`1806−1458=348`（编译用例与门槛差）；`1805−1458=347`（CI 实际执行与门槛差）。**不得把 348 写成 CI 执行差值**。

**原生化完成度 = 0%**（无 Compose 页面）。98.48% 仅「UI 宿主行数占比」，**不是完成度**。

> **术语区分（D-1）**：本文中「**15 个页面**」= `mobile/pages/*.html` 页面数（14 生产 + 1 调试）；与 S1 的「**15 条执行行**」是**两个不同概念**，不得混淆。

## 2. 总体架构

```
Compose UI → ViewModel → Repository → NativeEngineBridge → JNI → C ABI → C++ Core
C++ Core → EventBus → JNI → Kotlin Flow/StateFlow → ViewModel → Compose UI
```
数据所有权：持久化领域状态→C++；编辑会话状态→UI（需时作 Command 参数）；纯 UI 瞬态→Compose/ViewModel。**禁止双权威**。

## 3. S0—S6

- **S0 基线**（已完成）：只读核验，逐项标证据等级。
- **S1 基础链路**（10 单元）：S1-1 … S1-8（详见任务注册表）。
- **S2 命令/状态/事件**（4 串行）：Revision 原子化 → Command 8 条绕过 → EventBus/Undo → Query+任务扩展。
- **S3 时间/渲染基线**（3 串行）：Rational → Alpha/Oracle/Golden → 一致性。**严格顺序：CPU 修复 → Oracle → Golden → GPU G0.5 前置**。
- **S4 媒体/音视频/AI/GPU**（13 工作包）：见任务注册表。
- **S5 原生化**（11 单元 + 15 页面分类）。
- **S6 发布**（5 单元）：16KB / ABI / 性能 / 签名 / 真机。

## 4. 关键约束

CPU 永久 fallback · 不打包自有 H.264/H.265 · 系统 API ≠ 免专利 · DSP ≠ ONNX 推理 · 不删有调用者旧代码 · 模拟器 ≠ 真机 · CI 成功 ≠ 项目完成 · A-2 ≠ A-3 · A-5 ≠ A-6 · G0/G0.5 ≠ G1。

详见：`NC-V3.3.3-TASK-REGISTRY.md` / `-DEPENDENCY-MATRIX.md` / `-FILE-OWNERSHIP.md` / `-ACCEPTANCE-ROLLBACK.md` / `-FINAL-CHANGELOG.md` / `-FINAL-AUDIT.md`

---

# 附录 A · S5 十五页面 A~E 分类明细（★本轮补齐）

> 依据：`ls mobile/pages mobile/static` + `wc -l` + 引用检索。
> 运行时入口 = `assets/pages/01-home.html`（`MainActivity.kt:193`）。
> **A~E 定义**：A=必须迁移 Compose｜B=迁移为原生组件/系统界面｜C=静态资源保留（非可交互）｜D=调试页｜E=废弃（确认无引用后清理）

## A.1 `mobile/pages/`（**15 个页面**）

| 页面 | 行数 | 当前功能 | A~E | 迁移 Compose | 原生边界 | 保留视觉/交互 | 依赖 | 验收 |
|---|---|---|---|---|---|---|---|---|
| `01-home.html` | 105 | 首页/工程列表（**运行时入口**） | **A** | ✅ | 页面壳+导航 | 卡片布局/间距 | `home.js` | 迁移前后视觉回归 |
| `02-me.html` | 142 | 我的/账户 | **A** | ✅ | 页面壳 | 列表/头像尺寸 | `me.js` | 同上 |
| `03-auth.html` | 75 | 登录 | **A** | ✅ | 表单 | 输入框尺寸/圆角 | `auth.js` | 同上 |
| `03a-phone.html` | 58 | 手机验证 | **A** | ✅ | 表单 | 同上 | `auth.js` | 同上 |
| `04-settings.html` | 176 | 设置（10 项开关） | **A** | ✅ | 列表+开关 | 开关尺寸/分组 | `settings.js` | 同上 |
| `05-legal.html` | 72 | 法务条款 | **C** | ⚠️ 可保留 Web 或迁静态 | 静态文本 | 排版 | `legal/legal-page.js` | 内容一致性 |
| `06-feedback.html` | 50 | 反馈 | **A** | ✅ | 表单 | 同上 | `feedback.js` | 视觉回归 |
| `07-faq.html` | 23 | FAQ | **C** | ⚠️ 静态文本 | — | 排版 | — | 内容一致性 |
| `08-crashlog.html` | 45 | 崩溃日志 | **A** | ✅ | 列表 | 同上 | `crashlog.js` | 视觉回归 |
| **`10-editor.html`** | 153 | **编辑器主界面** | **A** | ✅ **核心** | 外壳+工具栏+面板容器+SurfaceView 容器 | **一级工具栏横向、二级在右侧同一水平区展开、多轨时间轴、参数面板、X/Y 调节、关键帧显示** | `editor.js`/`l3-*`/`engine.js` | 见 §A.3 |
| `20-template.html` | 53 | 模板列表 | **A** | ✅ | 列表 | 卡片 | `editor.js` | 视觉回归 |
| `21-template-fill.html` | 67 | 模板填充 | **A** | ✅ | 表单 | 同上 | `editor.js` | 同上 |
| `22-template-progress.html` | 56 | 模板进度 | **A** | ✅ | 进度 | 进度条 | `progress.js` | 同上 |
| `23-ai-edit.html` | 176 | AI 剪辑 | **A** | ✅ | 页面壳 | 同上 | `ai-edit.js` | 同上 |
| **`_probe.html`** | 9 | 调试探针 | **D** | ❌ | — | — | — | **不进发布包** |

## A.2 `mobile/static/`（6 个，**全部为生成物**）

> 由 `mobile/tools/make_static.py` / `make_pages_static.py` 生成；**运行时未加载**（唯一"引用"是 `MainActivity.kt:188` 的注释）。

| 文件 | 行数 | A~E | 说明 |
|---|---|---|---|
| `nairoclip-home.html` | 794 | **C** | 生成快照 |
| `nairoclip-editor-static.html` | **5,078** | **C**（**N-2** 拆分评估） | 生成快照，占 static 51% |
| `nairoclip-auth.html` | 1,168 | **C** | 生成快照 |
| `nairoclip-me.html` | 791 | **C** | 生成快照 |
| `nairoclip-phone.html` | 1,151 | **C** | 生成快照 |
| `nairoclip-settings.html` | 943 | **C** | 生成快照 |

## A.3 编辑器（`10-editor.html`）迁移不可变更项

**必须保留**：一级工具栏横向排列 · 点击后二级在**右侧同一水平区域**展开（**不得改抽屉**）· 再点收起 · 切换自动收起 · 多轨时间轴布局 · 参数面板结构 · X/Y 数值调节 · 关键帧/曲线显示 · 预览窗口 · 面板圆角/间距/颜色/图标风格。
**实现方式**：Design Tokens（`tokens.css` 26 变量）+ 布局约束 + 组件映射 + 视觉回归。
**禁止**：以"迁移到原生"为由改变现有界面。

## A.4 迁移前置依赖
`S5-0B`（需 `S1-3` 工程模型 + `S3-1` 时间模型）→ `S5-1` 宿主 → 逐页迁移。
`S5-2` 需 `S1-4A`（导入）；`S5-6` 需 `S1-2`（路由）。

---

# 附录 B · S1-7 十三项线程分析（★本轮补齐）

> 源码事实来自审计快照（`2ad6fb22`）。**未核实项已标「不可验证」**。

| # | 问题 | 当前源码事实 | 证据位置 | 风险 | 建议方向 | 需测试 | 验收条件 | 状态 |
|---|---|---|---|---|---|---|---|---|
| 1 | 谁拥有 TaskRegistry | `AiTaskManager` 持有 `contract::TaskRegistry registry_` | `ai_task.h:176` | 中 | 明确唯一所有者（Engine） | — | 所有权文档化 | **已确认** |
| 2 | 哪个线程可创建任务 | **无锁保护**，注释「由 Engine 线程独占」 | `task.h:76`、`task.cpp:57` | **高** | 限定 Engine 线程或加锁 | 并发创建 | 无数据竞争 | **已确认（当前无保护）** |
| 3 | 哪个线程可查询 | 同上 | `task.h:77` | 高 | 同上 | 并发查询 | 同上 | **已确认** |
| 4 | 哪个线程可取消 | `cancel()` 直接改 `cancelRequested` | `task.cpp:208-216` | 高 | 同上 | 并发取消 | 同上 | **已确认** |
| 5 | 哪个线程可更新进度 | 无锁 | `task.cpp` heartbeat 路径 | 高 | 同上 | 进度并发 | 同上 | **已确认** |
| 6 | 哪个线程可删除完成 | `clearResult()` 无锁 | `task.cpp:256` | 高 | 同上 | 并发删除 | 同上 | **已确认** |
| 7 | 完成回调在哪个线程 | **TaskRegistry 未提供回调接口**（`callback/onComplete/onDone/listener` 在 `task.h` **0 命中**；`ai_task.h` 0 命中；`nc_jni.cpp` 0 命中）。**完成通知为拉取模型**：C ABI 提供 `nc_task_query`（`capi.h:896`）轮询 + `nc_task_complete`（`:875`）状态写入 | `task.h`、`ai_task.h`、`capi.h:875/896`、`nc_jni.cpp` | 中 | 定义回调或明确轮询契约 | 回调线程断言 | 明确线程模型 | **部分确认**：TaskRegistry 无回调接口已确认；**系统级完成通知机制尚需调用链核验** |
| 8 | JNI 回调是否可能重入 | **仅 `nc_ai_task_*` 暴露；通用 `nc_task_*` 0 分支** | `nc_jni.cpp:1702+` | 中 | 定义重入规则 | 重入测试 | 无重入死锁 | **部分确认** |
| 9 | Engine 销毁时任务如何收尾 | **`nc_editor_destroy`（`capi.cpp:275-283`）仅 3 个 `delete`，无显式任务等待/清理**；`NcEditor` 为 `struct`（`:90`，隐式析构）；`AiTaskRunner`/`TaskRegistry` 均默认析构。**`ThreadPool::~ThreadPool`（`:25-32`）确有 `stop_`+`join()`，但 `ThreadPool` 是单例（`thread_pool.h:35`），不随 NcEditor 销毁** | `capi.cpp:275-283/90`、`thread_pool.cpp:25-32`、`thread_pool.h:35` | **高** | 定义收尾顺序 | 销毁测试 | 无泄漏/UAF | **部分确认**：未发现显式收尾已确认；**析构链为隐式、未证明存在后台线程继续运行、未证明存在 UAF**（唯一 `parallelFor` 调用 `effects.cpp:238` 为同步阻塞） |
| 10 | Activity 销毁时任务是否继续 | **存在 Android 侧协调**：`MainActivity.onDestroy`（`:262-272`）→ `engine?.release()` → `NativeEngineBridge.release()`（`:86-92`）= **先 `taskWatchdog.shutdownNow()`（`:87`）后 `nativeDestroy(editor)`（`:89`）**；`taskWatchdog` 为守护线程（`:64-66`） | `MainActivity.kt:262-272`、`NativeEngineBridge.kt:64-66/86-92` | 中 | 定义策略 | 销毁测试 | 行为明确 | **部分确认**：Android 侧协调已确认；**`nativeDestroy` 是否等待 C++ 侧在途任务收敛尚需调用链核验** |
| 11 | 工程切换时旧任务如何处理 | **`nc_project_load` 重建 editor，未见任务迁移** | `capi.cpp:429-443` | **高** | 定义隔离规则 | 切换测试 | 旧任务不污染 | **部分确认** |
| 12 | 取消能否真正中断 | **仅设标志位**，无中断机制 | `task.cpp:216` | **高** | 定义协作式检查点 | 取消测试 | 任务真停止 | **已确认（仅标志）** |
| 13 | ThreadPool 是否自等待死锁 | `parallelFor` 空段不递减 `jobRemaining_`（递减在 `if (localJob && end > begin)` 守卫内）；**`thread_pool.h:17` 明文自述「不可重入：任务里再调 parallelFor 会死锁」** | **`thread_pool.cpp:62-63`（递减）/`:100`（初始化）/`:107`（等待）** + `thread_pool.h:17` | **高** | 修复计数逻辑 | 满载/自等待测试 | 无死锁 | **已确认（源码明文记载）** |

**方案对比**：
- **方案 A 严格线程归属**（符合 `task.h:76` 现状）：无锁，跨线程经 mailbox；优点无死锁，缺点需投递机制。
- **方案 B 受控加锁**：须定义锁粒度/顺序/回调边界/销毁竞争；**禁止对所有方法简单加 mutex**。
- **V3.3.3 倾向 A**，但**须实测对比后决定**。

**13 份设计图**（线程归属矩阵/所有权图/Engine 生命周期图/调度图/状态转换图/回调规则/重入规则/工程切换规则/Activity 销毁规则/Engine 销毁规则/取消语义/死锁预防/释放顺序）：**未产出，标注「待 S1-7 实施时交付」**。

### B.1 D-4 三项证据等级最终表述（★本轮精确化）

| # | 最终表述 | 等级 |
|---|---|---|
| **#7** | **TaskRegistry 未提供回调接口**（`task.h`/`ai_task.h`/`nc_jni.cpp` 均 0 命中）；**完成通知为拉取模型**（`nc_task_query` 轮询 + `nc_task_complete` 状态写入）。**系统级完成通知机制尚需调用链核验** | **部分确认** |
| **#9** | **`nc_editor_destroy` 无显式任务等待/清理**（仅 3 个 `delete`）；`NcEditor` 为 `struct`（隐式析构）；`AiTaskRunner`/`TaskRegistry` 默认析构；**`ThreadPool::~ThreadPool` 有 `stop_`+`join()` 但为单例不随 NcEditor 销毁**。**析构链是否隐式收尾、是否存在后台线程继续运行、是否存在 UAF —— 均未证明** | **部分确认** |
| **#10** | **存在 Android 侧任务生命周期协调**：`onDestroy → engine.release() → taskWatchdog.shutdownNow() + nativeDestroy(editor)`（先停看门狗后销毁 editor）。**`nativeDestroy` 是否等待 C++ 在途任务收敛尚需调用链核验** | **部分确认** |

> **不得**把上述三项写成「系统级已确认缺失」或「UAF 已证实」——本表为**部分确认**，且已列明尚需核验的具体环节。

---

# 附录 C · V10 工作包完整说明（★本轮补齐）

> 14 字段：编号/目标/状态/关联产物/步骤/工具/命令/预期/失败判定/失败处理/证据/后续/需真机/需审批

| 编号 | 目标 | 状态 | 命令 | 预期 | 失败判定 | 后续 | 真机 | 审批 |
|---|---|---|---|---|---|---|---|---|
| **V10-01** | 真机安装/启动/功能/稳定 | **不可验证** | `adb install -r app-release.apk`；`adb shell am start -W -n com.nairoclip.app/.MainActivity` | 五段独立结论 | 安装失败/崩溃 | S6-5 | ✅ | A-12 |
| **V10-02a** | 设备 PAGE_SIZE | **不可验证** | `getconf PAGE_SIZE` 或 `getprop ro.product.cpu.page_size` | 输出值 | 无法执行 | S6-1 | ✅ | A-12 |
| **V10-02b** | ELF PT_LOAD 对齐 | **已确认** | `readelf -lW lib/*.so \| grep LOAD` | 6/6 `p_align=0x1000`（**当前不达标**） | — | S6-1 | ❌ | 否 |
| **V10-02c** | 最终 APK 全部 .so | **待验证** | 构建后逐 `.so` 复检 | 6/6 `≥16384` | 任一 <16384 | S6-1 | ❌ | 否 |
| **V10-03** | 官方 zipalign | **不可验证** | ①`zipalign -P 16 -v 4 in.apk out.apk` ②`apksigner sign` ③`apksigner verify --verbose` ④`zipalign -c -P 16 -v 4 out.apk` | ①②③④ 全通过 | 任一失败 | S6-1 | ❌ | A-12 |
| **V10-04** | GPU 真实能力 | **不可验证** | 探针 APK 14 项 + `GL_VENDOR/RENDERER/VERSION` | 14/14 + 不含 SwiftShader | 任一失败 | S4-5c | ✅ | **A-4** |
| **V10-05A** | 本地 ONNX 推理 | **不可验证** | 打包 ORT + 模型 → 推理 → 输出校验 | ≥1 功能链路可用 | 加载/推理失败 | S4-4A | ✅ | A-8 |
| **V10-05B** | 云端 AI | **不可验证** | 请求/响应/取消/超时/降级 | 字段边界明确、离线降级 | 泄漏媒体 | S4-4B | ✅ | **A-7** |
| **V10-06** | Git/工作区 | **部分确认** | `git rev-parse HEAD` 或 API；对比快照 blob | 差异清单 | — | S0 | ❌ | 否 |
| **V10-07** | 性能基线 | **不可验证** | `adb shell dumpsys gfxinfo` + 埋点 P99 | 13 指标记录 | 不达标 | S6-3 | ✅ | A-12 |
| **V10-08** | TaskRegistry 并发 | **待验证** | 并发/取消/回调/销毁测试 + TSan（**可用性待评估**） | 15 类测试全绿 | 竞态/死锁 | S1-7 | ❌ | A-12 |
| **V10-09** | 工程迁移链 | **部分确认** | 读 `serialization.cpp` 迁移段；**无真实历史样本** | 版本集合+路径明确 | 迁移缺失 | S1-3 | ❌ | 否 |
| **V10-10A-1** | FFmpeg 静态解码器 | **已确认** | 提取 `libavcodec.so` 内嵌 configure | H.264 仅 `h264_mediacodec`；**HEVC 无** | — | S4-2a | ❌ | 否 |
| **V10-10A-2** | MediaCodec 解码能力 | **不可验证** | 设备查询 `MediaCodecList` | 能力清单 | — | S4-2a | ✅ | A-12 |
| **V10-10A-3** | 真实硬解样本 | **不可验证** | 真实媒体解码 | 成功+回退可用 | 失败 | S4-2a | ✅ | A-12 |
| **V10-10B-1** | FFmpeg 编码器配置 | **已确认** | 同上 | **无任何视频编码器** | — | S4-0A | ❌ | 否 |
| **V10-10B-2** | MediaCodec 编码能力 | **不可验证** | 设备查询 | Profile/Level/输入方式 | — | S4-0B | ✅ | A-12 |
| **V10-10B-3** | 真实编码输出 | **不可验证** | 编码 → 输出文件 → 解析/播放 | 可播放+时间戳合理 | 空文件 | S4-1a | ✅ | **A-3** |

---

# 附录 D · A-1~A-13 / H-1~H-7 完整审批表（★本轮补齐）

> **全部待用户决策。Agent 不得自行批准，不得自行解除 H 类阻塞。**

| 编号 | 审批事项 | 触发条件 | 当前状态 | 所需证据 | 用户决策内容 | 未批准时处理 |
|---|---|---|---|---|---|---|
| **A-1** | PR #28 合并 | 完成 Diff 审查 | **待决策** | 完整 Diff/HEAD/CI SHA/测试 | 合并/关闭/暂缓 | S1-2/3/7/5c 全部阻塞 |
| **A-2** | 编码器**方案选择** | S1-5a-A/B/C 完成 | **待探针** | 三层探针报告 | 选 A/B/C 方案 | S1-5c 不启动 |
| **A-3** | 真实导出**实施** | A-2 已批 | **待 A-2** | 链路就绪证明 | 授权实施 | 不实施 |
| **A-4** | GPU **G1** | G0+G0.5 通过 | **待 G0.5** | 探针+真机能力 | 授权 G1 | G1 不启动（G0/G0.5 **不需** A-4） |
| **A-5** | 原生 UI 迁移 | S5-0B 草案 | **待 S1-3+S3-1** | 迁移规范 | 授权迁移 | S5 不启动 |
| **A-6** | 删除 WebView | S5-1~9 验收 | **待迁移完成** | 全页面验收证据 | 授权删除 | 旧代码保留 |
| **A-7** | 云端媒体处理 | 隐私边界方案 | **待决策** | 上传字段清单 | 授权范围 | S4-4B 不启动 |
| **A-8** | 付费/专有 SDK | 逐项清单 | **逐项待批** | 商业授权/许可/义务 | 逐项授权 | 不引入 |
| **A-9** | 重大工程格式变更 | 破坏性变更 | 待 | 迁移+兼容方案 | 授权 | 不实施 |
| **A-10** | 跨平台新技术栈 | 逐平台 | 待 | 平台方案 | 逐平台授权 | 不实施 |
| **A-11** | JNI op 表拆分 | 13 项前置完成 | **待分析** | 13 项结论 | 拆/不拆 | 保持串行 |
| **A-12** | S1 逐批授权 | 逐批次 | **待授权** | 批次说明 | 逐批授权 | 不启动 |
| **A-13** | ASAN/UBSAN 阻断 | 例外清单 | 待 | 例外原因+期限 | 阻断策略 | 维持 `continue-on-error` |

> **A-8 边界澄清**：**普通开源依赖走许可证/供应链审查，不得无理由升级为 A-8**；A-8 仅适用**商业专有 SDK**。

| 编号 | 决策事项 | 状态 |
|---|---|---|
| **H-1** | PR #28 决策等待 | **待用户**（已复核 open/clean/9 文件/+2,448−69/merge-base=base） |
| **H-2** | JNI 拆分等待 13 项分析 | **待 A-11** |
| **H-3** | S1-4/S1-8 同批边界确认 | **已建议并落实为 S1-4A+8A 同批；待用户确认** |
| **H-4** | 首批开发授权 | **待授权**（建议 S1-1 + S1-5b） |
| **H-5** | 编码器方案等待探针 | **待 S1-5a** |
| **H-6** | ONNX 暂缓 | **暂缓** |
| **H-7** | 云端 AI 隐私边界 | **待用户明确** |

---

# 附录 E · N-1~N-3 独立验收细节（★本轮补齐）

## N-1 JSON 深度与输入大小保护

| 项 | 内容 |
|---|---|
| 已知缺陷 | `json.cpp:155 parseValue` **无 depth 参数**；全文件**无输入大小检查**（grep `size() >` 0 命中） |
| 位置 | `core/src/json.cpp`（505 行），递归 `parseValue`（`:155`）调 `:210`/`:232` |
| 补充要求 | 最大输入字节 · 最大嵌套深度 · 超限错误码 · 递归栈保护 · 畸形 JSON 测试 · 深度边界测试 · 超大输入测试 · 内存安全测试 |
| **限制值** | **待标定**（须结合真实工程样本、手机内存预算、Parser 能力、压力测试确定；**不得编造 100MB/1000 条**） |
| 阻塞条件 | 无真实工程样本 → 限制值待标定 |
| 批次 | S1-8B（Parser 层）+ S1-8C（反序列化层） |

## N-2 超大 HTML 页面拆分

| 项 | 内容 |
|---|---|
| 已知事实 | `mobile/static/nairoclip-editor-static.html` **5,078 行**，占 static 总量 **51%** |
| 性质 | **生成物**（`tools/make_static.py`），**运行时未加载** |
| 补充要求 | 页面职责拆分 · 组件边界 · 资源依赖 · 事件处理 · CSS 依赖 · 页面加载 · 视觉回归 · 功能回归 · 拆分回退 |
| **约束** | **不得以拆分为由改变 UI 表现** |
| 批次 | S5-2（B 类，随外围页处理） |

## N-3 CI zipalign 检查缺失

| 项 | 内容 |
|---|---|
| 已知事实 | `.github/workflows/` **零调用 `zipalign`**（grep 0 命中） |
| 当前 CI APK 步骤 | `keytool -genkeypair`(279) → `assembleRelease`(458) → `apksigner verify`(564) |
| 补充要求 | Build-Tools 前置 · 官方命令 · 最终 APK 检查 · CI 失败阻断 · 工具版本记录 · 本地/CI 一致性 · 失败回退 |
| **命令** | `zipalign -P 16 -v 4 in.apk out.apk` → `apksigner sign` → `apksigner verify` → `zipalign -c -P 16 -v 4 out.apk` |
| **禁止** | **不得仅凭 APK 可安装即认定 zipalign 通过** |
| 批次 | S6-1 |
