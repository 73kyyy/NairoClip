# NairoClip 总体框架（Master Framework）

> **本文是全项目唯一的总纲**：回答「现在有什么、还缺什么、按什么顺序补」。
> 其余文档各有专责，不重复：
>
> | 文档 | 职责 | 不要在此查 |
> |---|---|---|
> | **本文** | 全局盘点、能力矩阵、缺口排序、补全路线 | 具体 API 签名 |
> | `docs/COMPLIANCE.md` | 合规单一事实来源（协议/禁令/治理） | 功能实现细节 |
> | `docs/ARCHITECTURE.md` | 分层与数据模型不变量的设计推导 | 进度状态 |
> | `docs/ROADMAP.md` | 历史里程碑与早期扩展点约定 | 当前真实缺口（见本文 §3） |
> | `docs/TRANSLATE_FRAMEWORK.md` | 字幕翻译子系统深入 | 其它子系统 |
> | `docs/THIRD_PARTY_NOTICES.md` | 协议全文与版权声明 | 选型理由 |
>
> **本文所有数据均为 2026-10-26 仓库实测**，非规划值。复核方法见 §9。

---

## 1. 项目定位与硬约束

跨平台 AI 视频剪辑软件：**C++17 共享核心 + 原生平台层**。

不可违背的四条（来自 `COMPLIANCE.md` 与 README）：

1. **剪辑运算全部本地完成**；仅模型下载、会员校验、意见反馈联网；
2. **禁 GPL / nonfree**：`libx264` / `libx265` / `libfdk_aac` 代码级禁用（`codec_policy.cpp`）；
3. **三端功能集合与导出质量上限完全一致**；唯一允许差异是量化精度（FP32/FP16/INT8）；
4. **模型变更必须走独立协议核查**，不得"沙箱顺带验证"转正。

---

## 2. 现状盘点（实测）

### 2.1 规模

| 指标 | 数值 |
|---|---|
| `core/src` 实现文件 | **79** 个 `.cpp` |
| `core/include` 公开头文件 | **83** 个 `.h` |
| 核心代码量 | **45,231** 行 |
| C ABI 导出函数 | **241** 个 `nc_*` |
| 测试文件 / 实际执行用例 | **87** 个 / **1177** 个（**0 失败**） |
| 已落地模型 | **8** 个（另 2 个空目录） |
| 平台层 | Android（Kotlin+JNI）、桌面 C++ 构建；**无 iOS** |

### 2.2 端侧状态

| 端 | 状态 | 证据 |
|---|---|---|
| 桌面 / C++ 构建 | ✅ 可构建、可跑全量测试 | `CMakeLists.txt`、`build/tests/nairoclip_tests` |
| Android | ⚠️ 壳层存在，**JNI 未封装** | `android/app/src/main/java` 仅 4 个文件，`cpp/` 下 **0** 个 `.cpp` |
| iOS | ❌ **不存在** | 无 `ios/` 目录、无 `.xcodeproj` |
| WebView 前端 | ⚠️ 无构建体系（手写 HTML，无打包脚本） | `mobile/pages` 8 个，无 `package.json` |

> **结论**：目前是「核心库完成度高 + 端侧几乎空白」的形态。
> `COMPLIANCE.md` §3 的"三端对等"是**发布期约束**，当前远未满足。

### 2.3 AI 能力矩阵

| 子系统 | 代码 | 模型 | 模型文件 | 状态 |
|---|---|---|---|---|
| 语音转字幕 | `speech_ai.cpp` 692 行 | `whisper_zh` | 4 个 / 927M | ✅ 可用 |
| 字幕翻译 | `translate_ai.cpp` 835 行 | `translate_zh_en`（OPUS）<br>`translate_zh_en_m2m`（m2m100） | 8 个 / 1.1G<br>5 个 / 1.9G | ✅ 可用（m2m 质量更优，**协议已核 MIT**） |
| 深度图 | `depth_ai.cpp` 458 行 | `depth_midas` | 3 个 / 509M | ✅ 可用 |
| 人声分离 | `stem_ai.cpp` 391 行 | `stem_sep` | 1 个 / 51M | ⚠️ 变体待核对（非锁定 Voc_FT） |
| 光流补帧 | `interpolation_ai.cpp` 259 行 | `flow_interp` | 1 个 / 62M | ⚠️ 非锁定 RIFE，需核协议 |
| 超分辨率 | `super_res_ai.cpp` 282 行 | `super_res_2x4x` | 1 个 / 21M | ⚠️ 非锁定 Real-ESRGAN |
| 人像抠像 | `matting_ai.cpp` 243 行 | `matting_basic`<br>`matting_hq` | **0 个**<br>**0 个** | ❌ **空目录，无模型** |
| TTS | — | `tts_zh` | 1 个 / 116M | ❌ **无 `tts_ai.cpp`** |
| 音频降噪 | `audio_denoise.cpp` | 算法实现 | — | ✅ 谱减法（非 AI） |
| 色度抠像 | `chroma_key.cpp` | 算法实现 | — | ✅ 键控算法 |
| 场景/节拍检测 | `scene_detect.cpp` / `beat_detect.cpp` | 算法实现 | — | ✅ 信号处理 |

### 2.4 引擎与渲染

| 组件 | 实现深度 | 说明 |
|---|---|---|
| `export_executor` | 887 行 | ✅ 导出主链路完整 |
| `preview` | 270 行 | ✅ 单帧预览 |
| `media_ffmpeg` | 441 行 | ✅ 真实解码 |
| `ai_engine` | 612 行 | ✅ ONNX Runtime 封装，6 种后端枚举 |
| `model_manager` | 1010 行 | ✅ 下载/加密/断点续传 |
| **`render_engine`** | **60 行** | ❌ **仅参数校验，无 GPU 后端** |

> **渲染是纯 CPU 软件实现**。`render_engine.cpp` 只做质量档位校验，
> 不存在 bgfx / OpenGL / Vulkan / Metal 任何后端代码（已全库 grep 确认 0 命中）。
> 所有特效、转场、合成均由 CPU 逐像素计算。

---

## 3. 真实缺口（按阻塞程度排序）

### P0 — 阻塞发布

| # | 缺口 | 现状 | 影响 |
|---|---|---|---|
| 1 | **iOS 端完全缺失** | 无任何 iOS 代码 | 三端对等无法达成；App Store 无法上架 |
| 2 | **Android JNI 未封装** | `android/.../cpp/` 为空 | Android 无法调用 241 个 C ABI 中的任何一个 |
| 3 | **人像抠像无模型** | `matting_basic`/`matting_hq` 均为空目录；候选 ONNX 实测输出恒 0（详见 §3.1） | 该功能"有代码无模型"，等于不可用 |
| 4 | **TTS 无实现** | 有 `tts_zh` 模型（116M），无 `tts_ai.cpp` | 模型闲置 |

### 3.1 抠像模型排查记录（2026-10-26）

为补齐 P0-3，实测了两个公开 ONNX 导出，**均不可用**：

| 模型 | 协议 | 输入签名 | 实测输出 |
|---|---|---|---|
| `onnx-community/mediapipe_selfie_segmentation` | Apache-2.0 | `[1,3,256,256]` | **恒 0**（全白输入 max ≤ 0.001） |
| `Xenova/modnet` | Apache-2.0 | `[1,3,-1,-1]` | **恒 0**（全白输入 max ≤ 0.009） |

测试方法：分别用 RGB/BGR × [0,1]/[-1,1] 四种预处理组合，输入纯色与半亮半暗图，
输出均近似恒定。结论：**这两个 ONNX 导出本身失效**，不是预处理参数问题。

> 副作用：仓库中曾有注释声称"MediaPipe 用 BGR 通道序，实测 max≈0.44"——
> 该结论无法复现，已删除并改为 MODNet 官方约定（RGB + [-1,1]）。

**现有测试为何没发现**：`tests/test_matting_ai.cpp` 读的是
`/workspace/deps/modnet.onnx`（开发机本地文件），而非仓库内模型。
测试通过 ≠ 产品可用。已在本文件 §9.2 记录该类"测试与实物脱节"风险。

**后续可行路径**（未做，需要上游支持）：
1. 用 `optimum` 从官方 PyTorch 权重自行导出 ONNX（最可靠）；
2. 找提供 `sigmoid` 后处理的正确导出；
3. 改用 MediaPipe 官方 `.tflite` + TFLite 运行时（需引入新依赖）。

### P1 — 影响可用性

| # | 缺口 | 现状 |
|---|---|---|
| 5 | **无 GPU 渲染后端** | 纯 CPU；4K 导出性能受限 |
| 6 | 补帧/超分/人声分离**与锁定清单不符** | 实为 flow_interp / super_res_2x4x / kara2，非 RIFE / Real-ESRGAN / Voc_FT |
| 7 | 前端无构建体系 | 静态 HTML 原型，无 `package.json` / 打包产物 |
| 8 | `translate_ai.cpp/.h` 未被 git 跟踪 | 版本不可追溯 |

### P2 — 质量优化

| # | 缺口 |
|---|---|
| 9 | 短句过译、疑问语气丢失、时间消歧（详见 `TRANSLATE_FRAMEWORK.md` §5.1） |
| 10 | 批量翻译串行，长字幕吞吐低 |
| 11 | ~~12 个模块无对应测试文件~~ → 已补 `test_model_invariants.cpp`（Clip/Track/Sequence 26 例）；`stem_ai`、`render_engine` 仍缺 |

---

## 4. 分层架构与扩展点

### 4.1 依赖方向（强制，单向）

```
平台层（Android Kotlin / iOS Swift / 桌面壳）
        │  只能通过 C ABI 调用，禁止直接 include C++ 头
        ▼
   capi.cpp（241 个 nc_* 导出，ABI v1）
        ▼
业务门面（editor / export_runner / model_manager）
        ▼
能力模块（effects / subtitle / mask / ai_* / media_*）
        ▼
基础层（rational / time / types / json / crypto）
```

**规则**：下层不得引用上层；跨层调用必须经由门面。
`core/` 对第三方依赖为零（FFmpeg、ONNX Runtime 均为可选编译开关）。

### 4.2 新增功能的四个扩展点

| 扩展点 | 位置 | 用于 |
|---|---|---|
| **编辑命令** | `commands.cpp` + `command.h` | 任何需撤销/重做的变更 |
| **C ABI** | `capi.h` + `capi.cpp` | 新能力对平台层暴露 |
| **导出管线** | `export_plan.cpp` → `export_executor.cpp` | 需影响成片的处理 |
| **AI 推理** | `ai_engine.h` + 新 `*_ai.cpp` | 新模型接入 |

### 4.3 标准新增流程（七步）

1. **协议先行**：取得上游 LICENSE 一手证据 → `docs/licenses/<model>.md`；
   未通过**不得编码**（`COMPLIANCE.md` §2.3）。
2. **数据模型**：新状态进 `Project`/`Clip` 结构 → `serialization.cpp` 往返。
3. **命令化**：变更走 `Command`（apply/revert）→ 入 `UndoStack`。
4. **能力实现**：新 `core/src/xxx.cpp` + `core/include/.../xxx.h`。
5. **接入管线**：预览（`preview.cpp`）+ 导出（`export_executor.cpp`）**两条都要**。
   只接一条是最常见缺陷（历史上"假功能"多由此产生，见 M2.28）。
6. **C ABI + 测试**：`capi.h/.cpp` 导出 + `tests/test_xxx.cpp`。
7. **更新文档**：本文 §2/§3 + `COMPLIANCE.md`（若涉模型）。

---

## 5. 补全路线（按依赖排序）

### 阶段 A：地基补齐（P0-2、P0-3、P0-4）

> 目标：让"已有代码"真正可用。

| 任务 | 交付标准 | 依赖 |
|---|---|---|
| A1 Android JNI 封装 | 241 个 C ABI 中剪辑核心子集可调用；真机跑通时间轴编辑 | 需 NDK 环境 |
| A2 抠像模型落地 | `matting_basic` 有可用 ONNX；实测对真实人像输出非恒定值 | 需协议核查 |
| A3 TTS 实现 | 新增 `tts_ai.cpp`；`tts_zh` 模型可合成中文语音 | 需协议核查 |
| A4 `translate_ai` 入库 | `git add` 并提交 | 无 |

**A2/A3 的协议核查必须先于编码**——这是 `COMPLIANCE.md` 硬约束。

### 阶段 B：iOS 端（P0-1）

> 目标：达成三端对等。

| 任务 | 说明 |
|---|---|
| B1 建立 iOS 工程 | Xcode 工程 + `core` 交叉编译（arm64） |
| B2 Swift 绑定层 | 封装 C ABI（Objective-C 桥接） |
| B3 Metal 后端（可选） | 若做，必须保证与 CPU 路径**输出一致**（质量上限一致原则） |

### 阶段 C：渲染引擎（P1-5）

> 目标：替换纯 CPU 逐像素计算。

| 任务 | 说明 |
|---|---|
| C1 后端抽象 | `render_engine` 扩展为接口 + CPU 参考实现 |
| C2 GPU 实现 | bgfx / Metal / Vulkan 择一（`COMPLIANCE.md` 列 bgfx 为 BSD-2） |
| C3 一致性验证 | GPU 与 CPU 结果逐像素比对，阈值内视为通过 |

**风险**：三端 GPU 能力不同，必须保证"导出质量上限一致"，
不允许某端因 GPU 缺失而降低成片质量（只允许更慢）。

### 阶段 D：模型对齐（P1-6）

| 现状模型 | 锁定清单要求 | 动作 |
|---|---|---|
| `flow_interp`（1 个 ONNX） | RIFE（MIT） | 核查来源；不符则换 |
| `super_res_2x4x`（1 个 ONNX） | Real-ESRGAN（BSD-3） | 同上 |
| `stem_sep/kara2.onnx` | UVR-MDX-NET-Voc_FT（MIT） | 同上（`fetch_models.sh` 已标注"待核对"） |

### 阶段 E：前端工程化（P1-7）

静态 HTML 原型 → 可构建前端（含打包、路由、状态管理）。

---

## 6. 质量门槛（每次合并前）

| 门槛 | 标准 |
|---|---|
| 构建 | `cmake --build build` 零 error |
| 单元测试 | `nairoclip_tests` 无新增失败 |
| 已知环境依赖 | 测试需 `/tmp/media_gen/` 目录存在（见 §9.2） |
| 双路径接入 | 新特效/滤镜必须同时接入**预览 + 导出** |
| 合规 | 涉模型变更须有 `docs/licenses/` 记录 |
| 文档 | 本文 §2/§3 与实际同步 |

---

## 7. 风险登记

| 风险 | 等级 | 缓解 |
|---|---|---|
| 三端对等无法达成（iOS 缺位） | 高 | 阶段 B 优先；必要时调整发布范围 |
| GPU 渲染引入端间质量差异 | 高 | C3 逐像素一致性验证 |
| 模型协议突变 | 中 | 季度核查（`COMPLIANCE.md` §2.3） |
| 抠像模型长期无可用 ONNX | 中 | 已实测 Qualcomm 版恒输出 0.5，需备选方案 |
| 前端与核心能力脱节 | 中 | 阶段 E 与 A1 并行 |

---

## 8. 术语与约定

- **C ABI**：`nc_*` 函数，ABI 版本 `NC_ABI_VERSION = 1`；
- **门面**：对外唯一入口（如 `TimelineEditor`），内部模块不直接暴露；
- **双路径**：预览路径（可降质）与导出路径（不可降质）；
- **fail-closed**：无合规实现时拒绝执行，而非降级到不合规实现。

---

## 9. 如何复核本文数据

### 9.1 常规复核

```bash
# 规模
find core/src -name '*.cpp' | wc -l
wc -l core/src/*.cpp core/include/nairoclip/core/*.h | tail -1

# C ABI
grep -oE 'nc_[a-z0-9_]+\(' core/include/nairoclip/core/capi.h | sort -u | wc -l

# 测试（注意：grep 得 1147，实际运行 1146，差 1 来自 #if 条件编译）
ls tests/test_*.cpp | wc -l
grep -h '^NC_TEST' tests/*.cpp | wc -l

# 模型
for d in models/*/; do printf '%-24s %s\n' "$(basename $d)" "$(ls $d | wc -l)"; done

# GPU 后端（应为 0 命中）
grep -rn -iE 'bgfx|vulkan|opengl|metal' core/src/*.cpp | wc -l
```

### 9.2 测试环境前置（已自动化）

**曾经的问题**（2026-10-26 修复）：

1. FFmpeg 用例依赖 `/tmp/media_gen/*.mp4`，但无任何步骤生成 → 9 个假失败；
2. 模型推理用例用相对路径 `models/...`，ctest 从 `build/` 运行时找不到
   → **全部静默 skip，表现为"通过"**。这是更危险的假安全：
   1146 个用例"全绿"，而真实推理一次都没跑过。

**现在的做法**：

| 问题 | 修复 |
|---|---|
| 素材缺失 | `tests/CMakeLists.txt` 的 POST_BUILD 调用 `fixtures/generate_media.sh`（幂等） |
| 路径找不到 | `test_translate_ai.cpp` 的 `repoRoot()` 自动向上探测仓库根 |
| ctest 工作目录 | `set_tests_properties(... WORKING_DIRECTORY <repo>)` |

**当前基线**：1177 用例，**0 失败**（需 ffmpeg CLI 与模型在 `models/` 下）。

> 仍存在的脱节风险：`test_matting_ai.cpp` 用的是 `/workspace/deps/modnet.onnx`
> （开发机绝对路径），而不是仓库内模型。这类"测试通过但产品无模型"的情况
> 需要在接入真实模型时一并改为仓库内路径。

---

## 10. 文档维护规则

- 本文 §2/§3 的数据**必须随代码变更同步**；
- 新增模块 → 更新 §2.3 或 §2.4；
- 修复缺口 → 从 §3 移除并在 §5 记录完成情况；
- 本文不重复 `COMPLIANCE.md` 的合规结论，只引用。
