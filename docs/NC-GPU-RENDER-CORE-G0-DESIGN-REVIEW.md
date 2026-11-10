# NairoClip GPU Render Core — G0 架构设计与接口评审材料

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-GPU-RENDER-CORE-G0 |
| 阶段 | GPU 工作流 · G0（架构设计与接口评审） |
| 审计基准 | `main` @ `28252751f6d04377514395ee6d1c8ec4271cc591`（本轮实测） |
| 文档性质 | **评审材料，不是开发任务书**。不含任何生产代码，不授权实施 |
| 状态 | 待人工评审 |

> **前置澄清（必须置顶）**
>
> 任务书提到两份已有材料：`NC-GPU-RENDER-CORE-AUDIT.md` 与
> `NC-GPU-RENDER-CORE-PLAN-V1.0.md`，并称"已经过技术审查"。
> 本轮实测：**这两份文件不在仓库中**（全仓 853 个文件，无任何 GPU 命名文件，
> `docs/` 30 个文件里也没有）。它们也不在本轮对话上下文中。
>
> 因此本材料**无法逐条"修订 V1.0"**。本材料的做法是：
> 以**当前源码实测**为准，重新给出 GPU 现状、必须解决的技术问题、
> 完整架构与分阶段路线。若 V1.0 中已有合理内容，评审时应以本材料覆盖，
> 而不是把两份拼在一起。

---

## 1. 当前 GPU 实现现状（源码实测）

### 1.1 检索结论：不存在 GPU 渲染后端

| 检查项 | 检索式 | 命中 |
|---|---|---|
| EGL 初始化 | `eglCreate` / `eglGetDisplay` / `eglMakeCurrent` | **0** |
| GLES Context | `EGLContext` / `GLES2` / `GLES3` | **0** |
| Texture 创建/上传 | `glGenTextures` / `glTexImage2D` | **0** |
| Shader | `glCreateShader` | **0** |
| FBO | `glBindFramebuffer` | **0** |
| GPU 外部纹理 | `GL_TEXTURE_EXTERNAL_OES` / `SurfaceTexture` | **0** |
| 后端实现 | `: public IRenderBackend` | **0** |

范围：`core/`、`android/`、`mobile/native/`（含 Kotlin/Java）。

### 1.2 三个易被误判为"GPU"的东西（逐项澄清）

| 对象 | 实际是什么 | 证据 |
|---|---|---|
| `IRenderBackend`（`core/include/nairoclip/core/render_engine.h:116`） | **纯抽象接口**：`info()` / `beginFrame()` / `endFrame()`，**全仓无任何派生类** | `grep ': public IRenderBackend'` = 0 |
| `core/src/render_engine.cpp`（60 行） | 只有**质量分级校验**（`validateRenderQuality` / `effectiveKernelRadius`），无任何图形 API 调用 | 全文通读 |
| `ANativeWindow_lock`（`mobile/native/android/nc_jni.cpp:2650`） | **CPU 把 RGBA 像素 memcpy 进 Surface 显示缓冲**（零拷贝「显示」，不是 GPU 渲染） | 该函数按 `window->stride` 逐行写 CPU 内存 |

**结论：`SurfaceView` / `ANativeWindow` 的零拷贝显示 ≠ GPU 后端。**
真机上看得到画面，是因为 CPU 算完后把像素贴到 Surface 上。

### 1.3 当前真实渲染链路（CPU 全链路）

```
媒体文件
  ↓ FFmpeg avformat 解封装
  ↓ 解码（FFmpeg 包装系统硬解码器）
  ↓ sws_scale → AV_PIX_FMT_RGBA          ← CPU 转换（media_ffmpeg.cpp:633）
  ↓ PreviewFrame{ Time, w, h, rgba }      ← CPU 内存（std::vector<uint8_t>）
  ↓ renderFrameFit / applyXformStack      ← CPU 像素循环 + blendStraightOver
  ↓ PreviewRenderer::compositeLayer       ← CPU 多轨合成（preview.cpp:830）
  ↓ ANativeWindow_lock + memcpy → Surface ← CPU 写显示缓冲
```

导出侧复用**同一组** CPU 函数（`export_executor.cpp` 调 `renderFrameFit` /
`renderFrameFitMasked` / `applyXformStack`），因此当前预览与导出的一致性
是"两边都用同一份 CPU 代码"带来的，不是两套实现对齐出来的。

### 1.4 可复用的资产 vs 完全缺失的能力

**可直接复用（不需要重写）**

| 资产 | 位置 | 复用方式 |
|---|---|---|
| `RenderPath` / `RenderQuality` + fail-closed 校验 | `render_engine.h` | 直接作为 GPU 后端的质量门控入参 |
| `blendStraightOver()` | `canvas.cpp` | **作为 CPU Reference oracle**（GPU 比对的黄金实现） |
| `PreviewFrame` 结构 | `preview.h` | 作为 GPU 读回后的统一帧表示 |
| Rational 时间模型 | `rational.h` / `time.h` | 帧请求的时间基准 |
| `ThreadPool` | `thread_pool.h` | 可复用于 CPU fallback 与 shader 编译并行 |
| 帧缓存字节预算机制 | `preview.h` | 可扩展为 GPU 纹理预算的上层策略 |
| 导出质量语义（scale≥1 / 不丢帧） | `render_engine.h` | GPU 离线渲染必须继承 |

**需要扩展（改接口，但可渐进）**

| 资产 | 需要的扩展 |
|---|---|
| `IRenderBackend` | 增加 `createTexture` / `upload` / `readback` / `submit` / `onContextLost` |
| `PreviewFrame` | 增加可选的 GPU 纹理句柄视图（不与 CPU 内存二选一，而是并列） |
| `PreviewRenderer` | 增加「后端选择 + CPU 回退」分支，但**保留现有 CPU 路径为默认** |
| 错误模型 | 新增 `NC-RENDER-*` 诊断码（当前只有 `ErrorCode` 枚举） |

**完全不存在（需要从零建设）**

- EGL/GLES 上下文生命周期
- 纹理管理与上传（含外部纹理 OES）
- Shader 编译/链接/反射
- FBO 与离屏渲染目标
- GPU 同步（fence / flush）
- GPU 显存预算与资源回收
- Context Lost 检测与恢复
- GPU 读回（readback）
- GPU 与 CPU 结果比对基线

---

## 2. V1.0 方案需要修订的技术问题

> 说明：V1.0 不在仓库（见文首），以下不是"对原文的逐条批注"，
> 而是**任何 GPU 方案在本仓库落地前必须解决的 10 个问题**，
> 每条都由当前源码事实推出。

| # | 问题 | 为什么在本仓库是真问题 | 本材料的处置 |
|---|---|---|---|
| P1 | **把 `ANativeWindow` 显示当成 GPU 渲染** | 当前只有 CPU 写 Surface 的路径；若方案把"能显示"记为 GPU 已通，会得出错误结论 | §1.2 明确澄清；G1 验收要求「纹理→Shader→FBO→Present」全链路可证 |
| P2 | **`IRenderBackend` 只有 3 个方法，不足以承载 GPU** | 缺纹理/读回/上下文丢失入口，直接实现会退化成"只画一次" | §3 给出扩展后的接口 |
| P3 | **Alpha 语义未在方案层定死** | 现有 CPU 是 **straight alpha**（`blendStraightOver` 整数实现）；GPU 若用预乘会整体偏暗 | §4 强制规定：对外统一 straight，GPU 内部可用预乘但必须显式转换并有比对用例 |
| P4 | **颜色空间未定义** | 当前 CPU 链路是 SDR BT.601/709 直通，无线性化。直接上 GPU 做线性合成会与 CPU 结果不一致 | §5 规定：**首期保持与 CPU 相同的 gamma 空间合成**，Linear Pipeline 作为 G3 的显式迁移项（须同时改 CPU 并重新基线） |
| P5 | **帧调度与过期帧未定义** | 拖动播放头时会产生大量请求；当前 CPU 路径靠"简单直接算"掩盖了这个问题 | §7 规定 `frameId` + 单调序号 + 过期丢弃 |
| P6 | **时间不是 GPU 友好的形式** | 现有 `Time` 是 `Rational`（int64 分子/分母），不能直接塞 shader | §6 规定：时间只用于**CPU 侧求值**，进 GPU 的只有已求值的参数快照 |
| P7 | **工程状态边界没写清** | `TimelineEditor` 非线程安全（头注释明写单线程）；若 GPU 线程直接读 `Project`，就是数据竞争 | §6 规定：GPU 只接收**不可变快照**，不得触碰 `Project` |
| P8 | **资源预算缺失** | 现有 `PreviewRenderer` 有帧缓存字节预算，GPU 若无预算会 OOM | §8 给出纹理预算与回收策略 |
| P9 | **Context Lost 无恢复路径** | Android 切后台/Surface 重建是常态；当前 `ensurePreviewRenderer` 有 Surface 重建处理，但没有 GL 资源重建 | §9 用 `contextGeneration` 世代号强制重建 |
| P10 | **预览/导出一致性只靠"共用 CPU 函数"** | 一旦引入 GPU，两侧必须共用**同一效果定义**，否则立刻分叉 | §11 绑定 `RenderQuality` 与统一效果定义 |

---

## 3. 跨平台 Render Core 架构

### 3.1 分层

```
┌──────────────────────────────────────────────────────────────┐
│ 平台层  Android(Kotlin) / iOS(Swift) / Desktop(Qt)            │  显示与 Surface 生命周期
├──────────────────────────────────────────────────────────────┤
│ 平台适配  SurfaceBinding / ContextFactory / CapabilityProbe   │  唯一允许出现 EGL/Metal/D3D 的地方
├──────────────────────────────────────────────────────────────┤
│ Render Core（跨平台 C++17，**不含任何平台 API**）              │
│   RenderGraph / Pass / Texture / Framebuffer / Shader         │
│   ResourcePool / Budget / Scheduler / Cache / Diagnostics     │
├──────────────────────────────────────────────────────────────┤
│ Backend 接口  IRenderBackend（GL / Vulkan / Metal / D3D）      │
├──────────────────────────────────────────────────────────────┤
│ CPU Reference  renderFrameFit / applyXformStack / blendStraightOver │
└──────────────────────────────────────────────────────────────┘
     ↑ 数据来自 Project Core 的**不可变帧快照**（只读）
```

### 3.2 关键原则

1. **Render Core 不含平台 API**：`#include <EGL/egl.h>` 只允许出现在
   `platform/android/` 目录，由 `ContextFactory` 注入函数指针或具体实现类；
2. **CPU 路径不是"过渡方案"，是永久 fallback + 参考实现**；
3. **GPU 不得持有工程状态**：只接受帧快照，不持有 `Project*`；
4. **同一效果定义**：预览与导出共用 `EffectStack` / `XformStack` / `BlendMode`
   的**参数求值结果**，不各写一套。

### 3.3 后端矩阵

| 后端 | 平台 | 阶段 | 说明 |
|---|---|---|---|
| CPU（现有） | 全平台 | 已有，永久保留 | 参考实现 + fallback |
| **GLES 3.0** | Android | **G0.5 / G1** | 首期唯一 GPU 后端 |
| Vulkan | Android / Linux | G5 | 独立评估，不首轮引入 |
| Metal | iOS / macOS | G5 | 与 GLES 共用 Render Core |
| Direct3D 11/12 | Windows | G5 | 同上 |
| OpenGL 3.3 | Linux/桌面 | G5 | 同上 |

---

## 4. Straight Alpha 与 Premultiplied Alpha 规范

### 4.1 对外语义（强制）

| 层 | 语义 |
|---|---|
| `CanvasBackground` / `PreviewFrame.rgba` | **Straight alpha**（非预乘） |
| CPU `blendStraightOver` | Straight（现行实现，作为 oracle） |
| GPU Shader 内部 | 允许预乘，但**必须在边界显式转换** |
| 导出（不支持 alpha 的格式） | 用 `flattenOnto()` 压到不透明背景 |

### 4.2 公式（straight）

```
C_out = C_f·A_f + C_b·A_b·(1 − A_f)
A_out = A_f + A_b·(1 − A_f)
```

### 4.3 GPU 侧约定

- 上传纹理时用 `glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`
  处理 straight 源；
- 若内部改用预乘，必须在 shader 入口 `rgb *= a`、出口 `rgb /= max(a, eps)`，
  且该转换必须有独立单测；
- **禁止**在同一 Pass 内混用两种语义。

### 4.4 验证基线

现有 `tests/test_b1_supplement.cpp` 的 6 档 Alpha 矩阵
（α ∈ {0,1,64,128,192,255}）即为 GPU 比对基准。
GPU 输出与 CPU oracle 的逐通道误差必须 ≤ 1 LSB（8-bit）。

---

## 5. Linear Color Pipeline

### 5.1 分阶段决策（关键）

| 阶段 | 合成所在空间 | 理由 |
|---|---|---|
| **G0.5 / G1 / G2** | **与 CPU 相同的 gamma 空间** | 现有 CPU 全链路不做线性化；若 GPU 单独线性化，预览与导出的像素立即不一致 |
| G3 | 引入 `ColorPipeline` 抽象，CPU 与 GPU **同时**迁移到线性 | 必须成对改，否则一致性无从谈起 |

### 5.2 接口预留

```cpp
enum class TransferFunction { SRGB, BT709, PQ, HLG, Linear };
enum class ColorSpace { BT601, BT709, BT2020 };

struct ColorConfig {
    ColorSpace primaries = ColorSpace::BT709;
    TransferFunction transfer = TransferFunction::BT709;
    bool fullRange = true;
};
```

G0.5/G1 阶段 `ColorConfig` 恒为默认值，不做任何转换 ——
**先保证与 CPU 一致，再谈色彩科学**。

---

## 6. 数据边界：Project Core → FrameRequest

### 6.1 边界规则

| 规则 | 说明 |
|---|---|
| GPU 线程**不得**访问 `Project` / `TimelineEditor` | 后者头注释明写"非线程安全，仅 UI 线程" |
| 帧请求只携带**已求值的不可变快照** | 关键帧、速度曲线、效果参数在 CPU 侧求值完毕 |
| 时间用 `Rational` 在 CPU 侧定位，进 GPU 的只有 `double` 参数与帧序号 | shader 不做时间运算 |
| 快照带 `revision` | 用于缓存失效与过期帧判定 |

### 6.2 接口草案（设计示意，**不落地**）

```cpp
// 设计示意：本文件不提交，仅用于评审
struct FrameRequest {
    std::uint64_t frameId;        // 单调递增
    std::uint64_t revision;       // 工程 revision（来自 TimelineEditor::editRevision）
    RenderPath path;              // Preview / Export
    RenderQuality quality;
    int width, height;            // 画布尺寸（权威值，不随预览缩放改变）
    Time timelineTime;            // Rational
    std::vector<LayerSnapshot> layers;  // 已求值：变换矩阵、不透明度、混合模式、纹理源
    std::uint32_t backgroundRgba;       // straight alpha
};
```

**约束**：`FrameRequest` 必须是**可拷贝的纯数据**，不含指针、不含 `Project&`。

---

## 7. 多帧调度与过期帧丢弃

### 7.1 问题

播放头拖动 / 参数拖动会在短时间内产生大量帧请求。
GPU 串行处理会积压，用户看到的是"滞后画面"。

### 7.2 设计要求

| 要求 | 实现要点 |
|---|---|
| 每帧有 `frameId` | 提交时分配，单调递增 |
| 有界队列 | 容量上限（如 2），满时**丢最旧**而非阻塞 |
| 过期丢弃 | 提交前检查 `frameId < latestSubmitted` → 直接丢弃 |
| 提交后丢弃 | GPU 完成时若 `frameId != latestCompleted` 且 path==Preview → 不上屏 |
| 导出路径**不丢帧** | `RenderQuality.allowFrameDrop` 对 `RenderPath::Export` 恒为 false（现有 fail-closed 已保证） |
| 取消语义 | 工程 revision 变化 → 旧快照全部作废 |

### 7.3 与现有线程模型的关系

- `ThreadPool` 可复用于 CPU fallback；
- **GPU 必须有专属渲染线程**（GL 上下文绑定线程），不得借用 UI 线程；
- 帧请求通过有界队列跨线程传递，禁止共享可变对象。

---

## 8. GPU 资源生命周期与预算

### 8.1 资源类型

| 资源 | 生命周期 | 回收策略 |
|---|---|---|
| Context | 进程级（可重建） | `contextGeneration` 变化时全部重建 |
| Texture（源帧） | 引用计数 / LRU | 预算驱动淘汰 |
| Framebuffer / RenderTarget | Pass 级 | Pass 结束立即回收或池化 |
| Shader / Program | 进程级 | 按需编译 + 缓存 |
| Vertex/Index Buffer | 进程级（全屏三角形可共享） | 常驻 |

### 8.2 预算

| 项 | 策略 |
|---|---|
| 纹理总字节 | 上限 = `min(设备可用显存估计, 画布帧字节 × N)`，N 可配（默认 8） |
| 单纹理上限 | 受 `GL_MAX_TEXTURE_SIZE` 约束，运行时查询 |
| 超预算行为 | LRU 淘汰 → 仍不足则**降预览分辨率**（不改画布尺寸） → 再不足则 CPU fallback |
| 预算来源 | `PreviewRenderer` 的字节预算机制可上移复用 |

### 8.3 禁止

- 不得把预览降级后的分辨率写回 `Sequence::width/height`（B1 已确立的边界）；
- 不得依赖 GC/析构顺序释放 GPU 资源（必须显式 dispose）。

---

## 9. Context Generation 与设备丢失恢复

### 9.1 世代号模型

```cpp
using ContextGeneration = std::uint64_t;

class IRenderBackend {
public:
    virtual ContextGeneration generation() const = 0;
    virtual bool isLost() const = 0;
    virtual bool recreate() = 0;     // 重建上下文与全部资源
};
```

规则：

1. 每个 GPU 资源记录创建时的 `generation`；
2. 使用前校验 `resource.generation == backend.generation()`，不等则视为**失效**；
3. Context 丢失（切后台、Surface 重建、驱动重置）→ `generation++` → 所有资源标记失效并重建；
4. 重建失败 N 次 → 永久降级到 CPU 并上报诊断码。

### 9.2 Android 特有触发点

| 触发 | 处理 |
|---|---|
| `SurfaceHolder.surfaceDestroyed` | 销毁 EGLSurface，保留 Context 与纹理（若可能） |
| `surfaceCreated` 重建 | 重建 EGLSurface + FBO；`generation` 递增 |
| 应用切后台 | 主动释放 GL 资源（可选），回来重建 |
| `GL_OUT_OF_MEMORY` | 清缓存 → 降级 → 上报 |

---

## 10. Render Graph 与 Pass

### 10.1 结构

```
Frame
 └─ Graph
     ├─ Pass: ClearBackground      (目标: 画布 FBO)
     ├─ Pass: CompositeLayer(0..n) (混合模式 + 不透明度)
     ├─ Pass: Transitions          (转场)
     ├─ Pass: Effects              (效果栈)
     └─ Pass: Present/Readback     (上屏或读回)
```

### 10.2 约束

- Pass 声明 **输入/输出纹理**，由 Graph 做拓扑排序与生命周期分析；
- Pass 不得直接操作全局 GL 状态，状态由 `RenderState` 统一管理；
- G1 只需 3 个 Pass（Clear / Composite / Present），不追求完整 Graph。

---

## 11. Render Cache 与失效

| 维度 | 规则 |
|---|---|
| 缓存键 | `(revision, timelineTime, width, height, quality, backendGeneration)` |
| 失效 | `revision` 变化 → 全部失效；`generation` 变化 → 全部失效 |
| 粒度 | 首期只缓存**解码后的源纹理**与**最终帧**，不做中间 Pass 缓存 |
| 预算 | 与 §8.2 共用预算 |
| 一致性 | 缓存命中与未命中的输出必须**逐像素一致**（有测试） |

---

## 12. Shader 工具链

| 项 | G0.5 要求 |
|---|---|
| 语言 | GLSL ES 3.00（`#version 300 es`） |
| 存放 | `platform/android/shaders/*.glsl`（纯文本，运行时编译） |
| 校验 | CI 中用 `glslangValidator`（主机侧）做语法/链接前校验；**不依赖设备** |
| 变体 | 首期不做变体系统，一个 Pass 一个 shader |
| 版本 | 编译失败必须返回明确诊断（行号 + 日志），不得静默回退 |

> 说明：`glslangValidator` 只验证语法，**不能**替代真机验证。

---

## 13. GPU 错误诊断与 CPU 回退

### 13.1 错误域（新增，与 07 规范对齐）

| 码 | 含义 |
|---|---|
| `NC-RENDER-BACKEND_UNAVAILABLE` | 无可用 GPU 后端 |
| `NC-RENDER-CONTEXT_LOST` | 上下文丢失 |
| `NC-RENDER-SHADER_COMPILE_FAILED` | 编译失败 |
| `NC-RENDER-OUT_OF_MEMORY` | 显存/内存不足 |
| `NC-RENDER-UNSUPPORTED_FORMAT` | 纹理格式不支持 |
| `NC-RENDER-DEVICE_BLACKLISTED` | 设备在已知问题黑名单 |

### 13.2 回退策略（fail-open 到 CPU）

```
尝试 GPU → 失败
  ├ 记录诊断码 + 设备信息
  ├ 标记本轮会话该后端不可用（避免每帧重试）
  ├ 切 CPU 渲染（现有路径，行为不变）
  └ 上报 UI（可选提示"已降级预览"）
```

**关键**：回退后**不得**改变导出结果语义；CPU 与 GPU 输出差异必须在容差内。

---

## 14. 预览与离线渲染的一致性

| 一致性维度 | 要求 |
|---|---|
| 时间语义 | 同一 `Rational` 时间、同一 `revision` 快照 |
| 画布尺寸 | 均取 `Sequence::width/height`，预览缩放**只在显示层** |
| 效果定义 | 共用效果参数求值结果（同一份 `EffectStack` 求值） |
| Alpha | 同一 straight 语义与同一公式 |
| 色彩 | 同一 `ColorConfig`（首期均为默认 gamma 空间） |
| 验证 | 同一帧在预览与导出两条路径下逐像素比对，误差 ≤ 1 LSB（8-bit） |

**导出路径禁止**：使用预览的降级分辨率、降级采样或丢帧
（`RenderQuality` 的 fail-closed 校验已强制）。

---

## 15. G0.5 Android GLES 3.0 技术原型计划（只做计划，不写代码）

### 15.1 原型目标（最小真实闭环）

```
EGL → GLES3 Context → Texture → Shader → Framebuffer → Draw → Surface Present
```

必须**每一步都有可验证证据**，不允许"能显示就算通"。

### 15.2 原型文件范围（预计）

| 文件 | 作用 |
|---|---|
| `platform/android/egl_context.{h,cpp}` | EGL display/config/context/surface 生命周期 |
| `platform/android/gles_backend.{h,cpp}` | `IRenderBackend` 的 GLES 实现（最小） |
| `platform/android/texture.{h,cpp}` | 2D 纹理创建/上传/释放 |
| `platform/android/shaders/blit.vert/.frag` | 全屏 passthrough + straight alpha |
| `platform/android/probe.{h,cpp}` | 能力探测（GLES 版本/最大纹理/扩展） |
| `tools/validate_shaders.sh` | `glslangValidator` 语法校验 |
| `tests/test_gles_reference.cpp`（主机侧） | 纯数据比对逻辑（不依赖 GL） |

**不在原型范围**：多轨合成、转场、效果、解码器纹理、导出。

### 15.3 技术依赖

- NDK + `EGL` / `GLESv3` 系统库（Android 平台自带，无第三方）
- 一台真实 Android 设备（`adb` 可连）
- 主机侧 `glslangValidator`（CI 用）

### 15.4 Shader 验证方式

| 层级 | 方式 |
|---|---|
| 语法 | CI：`glslangValidator -S vert/frag` |
| 链接 | 设备日志：编译/链接失败必须打印完整日志 |
| 行为 | 用 **纯色输入** 做像素级断言（不是"看起来对"） |

### 15.5 设备能力检测

必须**运行时查询**并记录：

```
GL_VENDOR / GL_RENDERER / GL_VERSION / GL_SHADING_LANGUAGE_VERSION
GL_MAX_TEXTURE_SIZE / GL_MAX_RENDERBUFFER_SIZE
GL_EXTENSIONS（关注 OES_external、PBO、float 纹理）
```

**禁止硬编码**任何上限。

### 15.6 Context 恢复

原型必须能演示：

1. 切后台 → 回前台 → 画面恢复且无花屏；
2. Surface 尺寸变化 → 重建 FBO；
3. 主动模拟 context loss（`eglDestroyContext` 后重建）→ 资源全部重建。

### 15.7 真实硬件验收 vs 软件模拟证据边界

| 环境 | 能证明 | 不能证明 |
|---|---|---|
| GitHub 托管 Runner（无 GPU） | —（连 GL 都起不来） | 任何 GPU 行为 |
| Mesa llvmpipe / SwiftShader | 图形 API 用法正确、shader 逻辑正确 | 真实 GPU 驱动行为、性能、格式支持 |
| **真实 Android 设备** | **唯一可作为 G0.5 通过的证据** | — |

**硬性要求**：G0.5 验收必须附**真实设备**的 `GL_VENDOR/GL_RENDERER` 日志与
**像素断言结果**。软件渲染结果只能标注为"仿真验证"，不得写成硬件通过。

### 15.8 原型验收标准

| # | 标准 |
|---|---|
| 1 | 真实设备上创建 GLES3 Context 成功，打印能力信息 |
| 2 | 上传一张已知 RGBA 纹理，shader 输出与输入**逐字节一致**（passthrough） |
| 3 | 半透明纹理叠加到纯色背景，结果与 `blendStraightOver` 参考实现 **≤1 LSB** |
| 4 | FBO 离屏渲染 + `glReadPixels` 读回，与预期一致 |
| 5 | `eglSwapBuffers` 上屏，截图与读回一致 |
| 6 | 切后台/回前台 3 次，画面正常、无泄漏（纹理数不增长） |
| 7 | 模拟 context loss 后重建成功 |
| 8 | `glslangValidator` 在 CI 通过 |
| 9 | 设备黑名单/能力不足时**正确回退 CPU** 并有诊断码 |

---

## 16. G1—G6 实施路线

| 阶段 | 前置条件 | 修改范围 | 具体交付 | 测试 | 验收证据 | 停止条件 |
|---|---|---|---|---|---|---|
| **G0** | B1 通过 | 仅文档 | 本材料 | — | 评审通过 | 人工批准后才能进 G0.5 |
| **G0.5** | G0 批准 | `platform/android/*`（新增）+ `tools/validate_shaders.sh` | GLES3 最小闭环 | 9 项原型验收 | 真机日志 + 像素断言 | 9 项全过；否则停止 |
| **G1** | G0.5 验收 | 预览路径接入 GPU 单层渲染 + CPU 回退 | Android GPU 最小预览闭环 | 预览正确性 + 回退 | 真机 1080p 播放 | 单层 GPU 预览与 CPU ≤1 LSB |
| **G2** | G1 验收 | 多轨合成 + Alpha | 多轨 GPU 合成 | Alpha 矩阵 + 多层 | 与 CPU 合成器比对 | 三轨以上合成一致 |
| **G3** | G2 验收 | 效果管线 + ColorPipeline | 专业 GPU 效果 | 效果逐像素 + 色彩 | 与 CPU 效果定义一致 | 效果栈全绿 |
| **G4** | G3 验收 | 解码器纹理（OES）+ 零拷贝 | 硬解纹理直通 | 真机多机型 | 无 CPU 中转 | 零拷贝可证 |
| **G5** | G4 验收 | Vulkan / Metal / D3D / GL 后端 | 跨平台后端 | 各平台冒烟 | 各平台真机 | 至少两平台通过 |
| **G6** | G5 验收 | 离线渲染 + 导出 | GPU 导出 | 预览/导出比对 | 导出文件校验 | 导出一致性通过 |

**每个阶段的授权**：前序阶段验收通过后，**单独授权**下一阶段。不得连做。

---

## 17. GPU 与 Command 主线的依赖关系

### 17.1 硬约束

| 约束 | 说明 |
|---|---|
| GPU **不得**维护第二套工程状态 | 只读帧快照，不持有 `Project*`，不缓存可编辑数据 |
| 帧快照必须带 `revision` | 与 `TimelineEditor::editRevision()` 同源；用于缓存失效与过期帧判定 |
| GPU 不得绕过 Command 修改工程 | 任何"GPU 侧改参数"都必须回到 Command 路径 |
| 预览参数（缩放/质量）不是工程数据 | 只影响 `RenderQuality`，不写回 `Sequence` |

### 17.2 与 B2—D 的文件所有权

| 文件 | B2—D 期间归属 | GPU 侧 |
|---|---|---|
| `mobile/native/android/nc_jni.cpp` | **主线（B2）独占** | G1 前不得改；G1 需要时先申请交接 |
| `core/src/preview.cpp` | 主线（D 阶段可能触碰） | G1 起需共同修改 → **必须先定交接顺序** |
| `core/include/nairoclip/core/render_engine.h` | GPU 线 | 主线只读 |
| `platform/android/**`（新增） | GPU 线独占 | — |
| `core/src/frame_fit.cpp` / `canvas.cpp`（Alpha oracle） | 主线 | **GPU 只读**（作为参考实现） |

**规则**：同一文件同一时间只允许一条工作线修改；交接必须有书面记录。

---

## 18. 本轮 GPU 授权边界

### 18.1 允许（本轮已做）

- ✅ 只读审计 GPU 相关源码
- ✅ 修订 GPU 架构设计（本材料）
- ✅ 设计 Render Core 接口（§3/§6/§9，仅示意）
- ✅ 制定 G0.5 技术原型任务书（§15）
- ✅ 规划 G1—G6 路线（§16）

### 18.2 禁止（本轮未做，且不得做）

- ❌ 编写生产 GPU Backend
- ❌ 新增 EGL/GLES/Vulkan/Metal/D3D 生产实现
- ❌ 修改现有 CPU Preview 生产路径
- ❌ 修改 Alpha 生产实现
- ❌ 修改 Project Core
- ❌ 修改 JNI / C ABI
- ❌ 修改 Android UI
- ❌ 开始 GPU Alpha 功能实现
- ❌ 把软件 GPU 结果标记为真实硬件验收

---

## 19. 待评审决策（需人工拍板）

| # | 决策点 | 选项 | 建议 |
|---|---|---|---|
| 1 | G0.5 是否需要先做「主机侧 GLES 仿真」再上真机 | ①直接真机 ②先 Mesa 仿真 | ②可提前暴露 shader 错误，但**不能**替代真机 |
| 2 | 首期是否引入 Vulkan | ①否 ②是 | ①（GLES3 足够，Vulkan 在 G5 评估） |
| 3 | 线性色彩迁移放在 G3 还是更晚 | ①G3 ②G6 后 | ①（越晚越贵，且必须 CPU/GPU 同改） |
| 4 | GPU 纹理预算默认值 | 帧字节 × 8？ | 需真机标定后确定 |
| 5 | 是否接受「G1 只做单层预览」 | ①是 ②G1 就要多轨 | ①（降低首阶段风险） |
| 6 | `nc_jni.cpp` 所有权交接时点 | B2 合并后？ | 是 |

---

## 20. 结论

| 项 | 结论 |
|---|---|
| 当前是否有 GPU 后端 | **没有**（0 命中，`IRenderBackend` 无派生类） |
| CPU 路径是否可复用 | 可复用为 **oracle + fallback** |
| 是否需要新建 Render Core | 是，且必须与平台 API 解耦 |
| V1.0 是否可直接作为开发任务书 | **不能**（且该文件不在仓库；本材料给出必须解决的问题清单） |
| 本轮是否产出生产代码 | **否** |
| 下一步 | 人工评审本材料 → 批准后**单独授权** G0.5 |

**本材料是 G0 评审材料，不是 GPU 开发授权。**
