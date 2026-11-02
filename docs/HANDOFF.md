# 交接文档（新对话窗口从这里开始）

> 最后更新：2026-11-02 · HEAD `c3cfa87` · 分支 `main`
> 仓库：`github.com:73kyyy/NairoClip.git`

---

## 0. 硬性约束（先看这条，违反会返工）

1. **所有构建与测试一律推 GitHub CI，不得在本地/本机执行。**
   本地只允许：只读检查、文本编辑、`-fsyntax-only` 语法检查（不产出二进制）。
2. CI 结果通过只读分支 `ci-status` 回写（`latest.json` + `RUNS.md` + `docs/ci-status/*.txt`）。
   取法：
   ```bash
   git fetch origin ci-status -q
   git show FETCH_HEAD:latest.json | python3 -c "import sys,json;d=json.load(sys.stdin);[print(k,v['conclusion'],v['head_sha'][:7]) for k,v in d['latest'].items()]"
   ```
3. `design/tools/api_dispatch.inc` 是**手工维护的权威产物**（生成器已移除）。
   新增/修改 C ABI 必须同步改这里，且参数个数必须与 `capi.h` 逐个核对 ——
   它不被编译器保护，报错会指向宿主文件行号，极难定位。
4. `mobile/native/android/nc_jni.cpp` 与 `android/app/src/main/java/com/nairoclip/app/NativeEngineBridge.kt`
   是两份手工副本，改动需同步（当前 op 是透传，Kotlin 一般无需改）。
5. `TrackType` 枚举**数值不可变**，只能追加（否则历史工程轨道错位）。

---

## 1. 当前进度总览

### 已提交（3 个提交）

| 提交 | 内容 | CI 状态 |
|---|---|---|
| `1975619` | 几何变换 8 项：内核 + 预览/导出接入 + C ABI + JNI + 前端面板 | 编译失败 |
| `e2962b9` | 修 `RenderClip` 缺 `xforms` | — |
| `5c1df75` | 补 `SetClipXformCommand` 构造函数 | — |
| `0fe3a50` | 修 `nc_jni.cpp`：JSON 工具误用 + `XformKind` 未引入 | — |
| `4fcbb14` | CI 诊断：APK 错误行落盘 + 上传 gradle 日志 | APK 仍失败（但抓到根因） |
| `69e9304` | 新增 `docs/HANDOFF.md` 交接文档 | — |
| `c3cfa87` | **定位 APK 真因：Kotlin 编译** + 显式声明 `androidx.activity` | 待跑 |

### CI 最新状态（`4fcbb14` 之前）

| 任务 | 结论 |
|---|---|
| `Android Cross-Compile` | ✅ success |
| `Build core for each ABI`（APK 内步骤） | ✅ **success** |
| `Report` | ✅ success |
| `Android APK` | ❌ failure（`Assemble release APK`） |
| `CI` | 随 APK 依赖，未完成 |

**关键**：core 在 NDK 下编译通过 ⇒ `xform.cpp` / `frame_fit.cpp` / `capi.cpp` 等**无编译问题**。

**已定位真因（`4fcbb14` 日志）**：
```
Caused by: org.jetbrains.kotlin.gradle.tasks.CompilationErrorException
  ... Kotlin Compiler Work Action
```
⇒ 失败是 **Kotlin 编译**，不是 C++。

**怀疑点（`c3cfa87` 已修）**：`MainActivity.kt` 用了
`androidx.activity.OnBackPressedCallback`，但依赖里只有
`androidx.appcompat:appcompat:1.7.0`（靠传递带入 activity）。
已显式加 `androidx.activity:activity-ktx:1.8.2`。

**另一处修正**：`gradle-log` artifact 之前只有 3971 字节 ——
worker 输出没进 `| tee`。`c3cfa87` 改为
`> /tmp/gradle.log 2>&1` 再 `cat` 回显，并加 `--console=plain`。

### 已完成的代码（可直接用）

**内核**
- `core/include/nairoclip/core/xform.h` / `core/src/xform.cpp`
  - `Affine2D`（链式合成/求逆/恒等判断）
  - `XformKind`（8 项）· `XformParams`（13 字段）· `XformStack`（8 槽）
  - `defaultXformParams()` · `applyXformStack()` · `matrixForSlot()`
  - `fitMatrix()` · `firstXformEdge()` · `xformKindName/FromString()`
  - 7 个仿射效果**合并为单矩阵，只重采样一次**
  - 栅格转换是唯一非仿射项，走独立网格路径
  - 随机位移用整数哈希（不依赖 `std::rand`，跨端可复现）
- `core/include/nairoclip/core/frame_fit.h` / `.cpp`
  - `Affine2D` · `EdgeMode`（背景/镜像/拉伸）· `renderTransform()`（逆映射采样）
- `Clip::xforms` + `hasXform()`（默认全 disabled，避免像素基线漂移）
- `RenderClip::xforms` + `hasXform()`（导出快照，`export_plan.h/.cpp`）
- `SetClipXformCommand`（可撤销，整槽替换）
- `TimelineEditor::setClipXform/getClipXform`

**接线**
- C ABI：`nc_set_clip_xform` / `nc_get_clip_xform` / `nc_clear_clip_xform`
  - 参数：**13 个扁平数组** `[value, angle, freq, phase, axis, cols, rows, gap, anchor, edge, period, trajectory, seed]`
  - 宏 `NC_XFORM_ARG_COUNT = 13`
- 预览：`preview.cpp` 改走 `applyXformStack`（替换 `renderFrameFit`），
  并给「尺寸一致」的零拷贝快路径加 `!hasXform()` 守卫
- 导出：`export_executor.cpp` 同上（两处：`memcpy` 快路径 + `renderFrameFit` 分支）
- JNI：`xform_set` / `xform_get` / `xform_clear` 三个 op + `BuildState` 推送 8 槽
- `api_dispatch.inc`：3 个派发分支

**前端**
- `XFORM_ITEMS` 8 项 `ready: true, xform: true`
- `XF_SPEC` 8 项 22 行参数表（含默认值，与 `defaultXformParams` 一一对应）
- `buildParamPanel()` 通用参数面板（复用三栏版式：左圆角标签 / 中刻度尺 / 右数值框）
- 新控件：`xr-chips`（选项组）、`xr-step`（步进器）、`xr-anchor`（九宫格锚点）
- `openXformPanel()` 接入宫格点击
- 已修 TDZ：`panel` 声明提前到滑块回调使用之前

**测试**
- `tests/test_xform.cpp`：19 个用例
  - 零改动字节一致（value=0 逐字节等于输入）
  - 8 项均生效、不同 value 结果不同
  - 确定性（同时刻两次调用相同）
  - 钳制、叠加、边界、与 `renderFrameFit` 等价

**文档**
- `docs/PERF_XFORM_PLAN.md`（56KB）—— 见下节

---

## 2. 开发文档索引（`docs/PERF_XFORM_PLAN.md`）

| 节 | 内容 |
|---|---|
| §0 | 现状核查（12 项实测，非推测） |
| §1 | **代理渲染定案**（播放按屏幕像素 / 暂停按源分辨率） |
| §2 | **单次重采样架构**（7 个仿射合并，1080p 每帧 133MB→17MB） |
| §3 | 分阶段计划 P0–P10 |
| §4 | 8 项效果规格（22 个调节项，含默认值/显示格式） |
| §4.1 | 前端 UI（8 张 SPEC 表 + 2 新控件 + 叠加态） |
| §4.2 | 前端接入清单 |
| §4.3 | **端到端接线 7 项**（撤销/导出/JNI/派发/Kotlin/返回链/ABI 测试） |
| §4.4 | UI 配色规范（样板图 → 本 App 深色主题映射） |
| §4.5 | **按素材类型的能力矩阵**（点选素材 → 功能栏变化） |
| §4.6 | **文本素材化**（5 步改造） |
| §4.7 | **调整图层全功能** |
| §4.8 | **复制背景**（填充背景第 4 档） |
| §4.9 | 填充入口 + 「填充整个预览区」 |
| §4.10 | **三端栅格化定案**（平台侧，一致性只约束几何与时序） |
| §5 | 借鉴剪映 / AE |
| §6 | 未覆盖的优化点（分层缓存 / 16bit / 流水线 / 移动端缓存可行性） |
| §7 | 风险与明确不做的事 |
| §8 | 排期汇总 |

---

## 3. 用户已确认的产品需求（不可擅自改）

### 3.1 8 项几何变换（已实现）
偏移 / 拉伸轴 / 旋转 / 栅格转换 / 沿路径移动 / 缩放辅助 / 脉冲大小 / 随机位移。
- `value` 是**主强度**，各效果 UI 标签不同但同一字段
- 默认 `value > 0`（点了就能看到效果），但 `enabled = false`
- 越界**钳制**不报错

### 3.2 文本（字体）素材化（未实现）
- 工具栏 →「文字」→ 新建文本 → **创建后直接出现在轨道上**，是可操作素材
- 文本可加：8 项变换、动画、关键帧、曲线调节
- **字幕轨 = 文本轨**，统一
- 原 `SubtitleCue::tracking`（镜头追踪）**迁移到文本 Clip**，与原子幕**互不干涉**

### 3.3 调整图层（未实现）
- 可加**任何功能**，作用于**下方所有轨道**
- 需要独立的关键帧 ABI（现有 `nc_insert_keyframe` 需要 `clip_id`，调整图层没有 clip）
- **注意**：当前 `track.h:22` 明确写着「拒绝任何 Clip 落入，等 B2-6 落地」，
  强行开放会出现「能建轨但放不进素材」的假入口

### 3.4 填充背景 / 复制背景（未实现）
- **L1「背景」不改名**；「填充」作为**二级项**加入（`bgfill`）
- 点「填充」时若未选颜色 → **默认随机**（不加随机按钮）
  - 随机策略：HSV 取 H 随机、S 0.25–0.55、V 0.55–0.80（避免刺眼高饱和）
- 填充面板已有「拉伸」（`FrameFit::Crop`），**再加一个「填充组合区域」**（`FrameFit::Fill`）
  - 二者互斥，应做成三选一段控件（适应 / 拉伸 / 填充组合区域），不是两个独立按钮
- **「复制背景」是填充背景的第 4 种模式**（不是调整图层的一部分）
  - `Blur` = 复制本片段自己的画面
  - `CopyBackground` = 复制**下方所有轨道合成后**的完整画面
  - 7 个参数：不透明度 / 模糊(默认12) / 缩放(默认110) / 亮度 / 饱和度 / 填充模式 / 边缘羽化
  - 这些参数需可打关键帧 ⇒ 需扩 `ClipProperty`

### 3.5 三端策略（已定案）
- 文本栅格化走**平台侧**：Android `Canvas` / iOS `CoreText` / Windows `DirectWrite` / macOS `CoreText`
- 栅格化结果**缓存并作为 Clip 源输入**
- **一致性只约束几何与时序**（同一 `timeSeconds` 同一参数，矩阵逐位相同），
  **不追究像素细节**（预览本就代理缩放，降质是预期）
- 后续会出 Windows / macOS / iOS 端

---

## 4. 已知未修的问题（按优先级）

### P0 — 阻断 CI
1. **`Android APK` 在 `Assemble release APK` 失败 —— 已确认是 Kotlin 编译错误**。
   `c3cfa87` 已做两件事：
   - 显式声明 `androidx.activity:activity-ktx:1.8.2`（怀疑 Unresolved reference）
   - 修 gradle 日志捕获（之前 `| tee` 丢 worker 输出，日志只有 3.9KB）
   **下一步：等 `c3cfa87` 跑完。**
   - 若转绿 → 就是 activity 依赖问题，收工
   - 若仍失败 → 这次 `/tmp/gradle.log` 会有完整原文，
     读 step summary 的「编译错误」段或下载 `gradle-log` artifact

### P1 — 功能缺口
2. **文本相关全部未做**：`TrackType::Text`、`TextContent`、字形栅格化、
   `nc_add_text_clip`/`set_text_content`/`get_text_content`、前端文本输入框、
   `commit()` 的 `newtext` 分支（当前 grep = 0，点「新建文本」什么都不发生）
3. **调整图层未开放**：`AdjustmentLayer`、`XformStack`、`animation`、独立关键帧 ABI
4. **复制背景未做**：`BackgroundMode::CopyBackground`、7 参数、6 个新关键帧通道
5. **填充入口未做**：`bgfill` 二级项、默认随机色、「填充组合区域」

### P2 — 技术债
6. `applyRasterInPlace` 里**每格都拷一次全帧快照**，6×6 网格 = 36 次全帧拷贝。
   应改为拷一次后复用。
7. `renderTransform` 是最内层热循环，当前标量 + `double`，**无 NEON**（P4 计划）
8. 帧缓存仍是**整帧失效**（按 `editRevision` 全清），
   改一个字幕颜色会让 5 层视频全部重算（P2 计划的分层失效）
9. `RenderQuality.scale` / `allowFrameDrop` 仍是空壳，未接入

---

## 5. 快速定位表

| 要找什么 | 去哪 |
|---|---|
| 8 项效果算法 | `core/src/xform.cpp` `matrixForSlot()` |
| 单次重采样 | `core/src/frame_fit.cpp` `renderTransform()` |
| 矩阵合成顺序 | `xform.cpp` `XformStack::composeAffine()`（按枚举值，非添加顺序） |
| 参数表（UI） | `mobile/assets/editor.js` `XF_SPEC` |
| 参数表（内核默认值） | `core/src/xform.cpp` `defaultXformParams()` ← 两者必须同步 |
| C ABI 参数顺序 | `core/include/nairoclip/core/capi.h` `NC_XFORM_ARG_COUNT` 注释 |
| 预览接入点 | `core/src/preview.cpp` 约 290 行 |
| 导出接入点 | `core/src/export_executor.cpp` 约 585 / 603 行 |
| JNI op | `mobile/native/android/nc_jni.cpp` 搜 `xform_set` |
| 派发分支 | `design/tools/api_dispatch.inc` 搜 `nc_set_clip_xform` |
| 能力矩阵 | `docs/PERF_XFORM_PLAN.md` §4.5 |

---

## 6. 下一窗口的第一件事

```bash
# 1. 看 CI 是否转绿
git fetch origin ci-status -q
git show FETCH_HEAD:latest.json

# 2. 若 APK 仍失败，读新加的诊断
git show FETCH_HEAD:docs/ci-status/apk-errors.txt

# 3. 若拿不到，从 artifact 下载
gh run download <run_id> -n gradle-log -D /tmp/g
```

拿到真实错误后再动手，**不要再靠猜**。
