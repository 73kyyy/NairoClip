# NairoClip GPU Render Core — G0 架构设计与接口评审材料

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-GPU-RENDER-CORE-G0 |
| **文档版本** | **V1.1（人工评审后修订）** |
| 阶段 | GPU 工作流 · G0（架构设计与接口评审） |
| 审计基准 | `main` @ `b3fe63155816c6b7bba4ba178fa70ef0eb5be859`（本轮实测） |
| 文档性质 | **评审材料，不是开发任务书**。不含生产代码，不授权实施 |
| 修订摘要 | 见 §0 |

> **前置澄清（必须置顶）**
>
> 任务书提到两份已有材料：`NC-GPU-RENDER-CORE-AUDIT.md` 与
> `NC-GPU-RENDER-CORE-PLAN-V1.0.md`，并称"已经过技术审查"。
> 本轮实测：**这两份文件不在仓库中**（全仓 853 个文件，无任何 GPU 命名文件，
> `docs/` 30 个文件里也没有），也不在本轮对话上下文中。
>
> 因此本材料**无法逐条"修订 V1.0"**，而是以**当前源码实测**为准重建。

---

## 0. V1.1 修订摘要（对应人工评审 7 条意见）

| # | 评审意见 | 本版处置 | 落点 |
|---|---|---|---|
| 1 | 重审 Straight/Premultiplied 公式与 GLES 混合配置，明确 RT 内部表示 | 明确 **RT 内部 = Premultiplied RGBA8**；给出两种公式与两种 `glBlendFuncSeparate` 的**正确/错误**对照；补半透明背景算例 | §4 |
| 2 | 明确三个 frameId 的职责与 revision 校验 | 新增三计数器职责表 + 五条判序规则 + revision 三处校验 | §7 |
| 3 | 区分 EGLSurface 与 EGLContext 生命周期 | 拆成 `surfaceGeneration` / `contextGeneration` 双世代；给出 6 类事件对照表；明确「Surface 重建 ≠ Context 丢失」 | §9 |
| 4 | 纹理预算不得把显存估算当可靠容量 | 改为「保守配置上限 + OOM 反馈下调」，显式禁止按设备内存推算 VRAM | §8 |
| 5 | 补 G0.5 的资源生命周期/FBO 完整性/Shader 状态/错误诊断/释放验收 | G0.5 验收由 9 项扩为 **14 项**，新增 5 项硬指标 | §15.8 |
| 6 | glReadPixels 仅用于验证，不得进实时预览 | 新增「回读边界」硬规则 + 静态检查要求 | §6.4 / §15.9 |
| 7 | 保留 CPU 永久 fallback / GLES3 首期 / 真实硬件验收 / 分阶段授权 | 均保留并强化 | §3 / §13 / §16 / §18 |

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
| `IRenderBackend`（`render_engine.h:116`） | **纯抽象接口**：`info()` / `beginFrame()` / `endFrame()`，**全仓无派生类** | `grep ': public IRenderBackend'` = 0 |
| `core/src/render_engine.cpp`（60 行） | 只有**质量分级校验**（`validateRenderQuality` / `effectiveKernelRadius`），无图形 API | 全文通读 |
| `ANativeWindow_lock`（`nc_jni.cpp:2650`） | **CPU 把 RGBA memcpy 进 Surface 显示缓冲** | 按 `window->stride` 逐行写 CPU 内存 |

**结论：`SurfaceView` / `ANativeWindow` 的零拷贝显示 ≠ GPU 后端。**

### 1.3 当前真实渲染链路（CPU 全链路）

```
媒体文件 → FFmpeg 解封装 → 解码 → sws_scale → AV_PIX_FMT_RGBA（media_ffmpeg.cpp:633）
  → PreviewFrame{ Time, w, h, rgba }（std::vector<uint8_t>，CPU 内存）
  → renderFrameFit / applyXformStack（CPU 像素循环 + blendStraightOver）
  → PreviewRenderer::compositeLayer（preview.cpp:830，CPU 多轨合成）
  → ANativeWindow_lock + memcpy → Surface
```

导出复用**同一组** CPU 函数，因此当前预览/导出的一致性是"共用同一份 CPU 代码"带来的。

### 1.4 可复用 vs 需扩展 vs 完全缺失

**可直接复用**

| 资产 | 位置 | 复用方式 |
|---|---|---|
| `RenderPath` / `RenderQuality` + fail-closed 校验 | `render_engine.h` | GPU 后端的质量门控入参 |
| `blendStraightOver()` | `canvas.cpp` | **CPU Reference oracle**（GPU 比对黄金实现） |
| `PreviewFrame` | `preview.h` | 统一帧表示（读回后） |
| Rational 时间模型 | `rational.h` / `time.h` | 帧请求时间基准 |
| `ThreadPool` | `thread_pool.h` | CPU fallback 与 shader 编译并行 |
| 帧缓存字节预算 | `preview.h` | 上层预算策略参考 |

**需扩展**：`IRenderBackend`（加纹理/读回/上下文丢失）、`PreviewFrame`（可选 GPU 纹理视图）、`PreviewRenderer`（后端选择 + 回退）、错误模型（`NC-RENDER-*`）。

**完全缺失**：EGL/GLES 上下文、纹理管理、Shader 编译、FBO、GPU 同步、显存预算、Context Lost 恢复、GPU 读回、GPU/CPU 比对基线。

---

## 2. 必须解决的 10 个技术问题（继承 V1.0 → V1.1）

| # | 问题 | 本版处置 |
|---|---|---|
| P1 | 把 `ANativeWindow` 显示当成 GPU 渲染 | §1.2 澄清；G1 验收要求全链路可证 |
| P2 | `IRenderBackend` 只有 3 个方法 | §5 给出扩展接口 |
| P3 | Alpha 语义未定死 | §4（**本版重写**） |
| P4 | 颜色空间未定义 | §5.3：首期保持 gamma 空间，G3 成对迁移 |
| P5 | 帧调度/过期帧未定义 | §7（**本版重写**） |
| P6 | Rational 时间不能直接进 shader | §6.2 |
| P7 | 工程状态边界不清 | §6.1 |
| P8 | 资源预算缺失 | §8（**本版重写**） |
| P9 | Context Lost 无恢复 | §9（**本版重写**） |
| P10 | 预览/导出一致性只靠共用 CPU 代码 | §14 |

---

## 3. 跨平台 Render Core 架构

### 3.1 分层

```
平台层  Android(Kotlin) / iOS(Swift) / Desktop(Qt)      显示与 Surface 生命周期
平台适配  SurfaceBinding / ContextFactory / CapabilityProbe   唯一允许出现 EGL/Metal/D3D 的地方
Render Core（跨平台 C++17，不含平台 API）
  RenderGraph / Pass / Texture / Framebuffer / Shader
  ResourcePool / Budget / Scheduler / Cache / Diagnostics
Backend 接口  IRenderBackend（GL / Vulkan / Metal / D3D）
CPU Reference  renderFrameFit / applyXformStack / blendStraightOver
        ↑ 数据来自 Project Core 的不可变帧快照（只读）
```

### 3.2 关键原则

1. **Render Core 不含平台 API**：`#include <EGL/egl.h>` 只允许出现在 `platform/android/`；
2. **CPU 路径是永久 fallback + 参考实现**，不是过渡方案；
3. **GPU 不持有工程状态**：只接受帧快照；
4. **同一效果定义**：预览与导出共用参数求值结果。

### 3.3 后端矩阵

| 后端 | 平台 | 阶段 | 说明 |
|---|---|---|---|
| CPU（现有） | 全平台 | 已有，**永久保留** | 参考实现 + fallback |
| **GLES 3.0** | Android | **G0.5 / G1** | 首期唯一 GPU 后端 |
| Vulkan | Android / Linux | G5 | 独立评估 |
| Metal | iOS / macOS | G5 | 共用 Render Core |
| D3D 11/12 | Windows | G5 | 同上 |
| OpenGL 3.3 | Linux/桌面 | G5 | 同上 |

---

## 4. Alpha 语义、数学公式与 GLES 混合配置（本版重写）

### 4.1 先纠正一处公式归属

评审材料此前引用的 `C_out = C_f·A_f + C_b·A_b·(1−A_f)`，
**这是 Premultiplied（预乘）形式的结果，不是 Straight 形式**。
把两者混为一谈会直接导致半透明背景合成偏暗/偏亮。以下分别定义。

### 4.2 记法

| 符号 | 含义 |
|---|---|
| `Cs` | 源色（straight，未预乘） |
| `As` | 源 alpha ∈ [0,1] |
| `Ps = Cs·As` | 源色的**预乘**表示 |
| `Cd, Ad, Pd = Cd·Ad` | 目标（destination）对应量 |

### 4.3 Porter-Duff `over` 的两种等价写法

**预乘形式（GPU 混合直接产生）**

```
Po = Ps + Pd·(1 − As)
Ao = As + Ad·(1 − As)
```

**直通形式（需要除法还原）**

```
Ao = As + Ad·(1 − As)
Co = (Cs·As + Cd·Ad·(1 − As)) / Ao      (Ao > 0)
```

两者关系：`Co = Po / Ao`（`Ao > 0`）。

### 4.4 Render Target 内部表示：**Premultiplied RGBA8**

**决策：GPU Render Target 一律存 premultiplied。**

理由：

1. GPU 混合是**逐通道线性运算**，只有 premultiplied 才能用一条 `glBlendFunc` 同时正确处理
   「RGB 加权」与「Alpha 累积」；
2. Android `Surface` 的默认像素格式（`PixelFormat.TRANSLUCENT`）**本身就是预乘**，
   直接上屏无需再转换；
3. 若 RT 存 straight，则 RGB 的混合权重会漏掉目标 alpha，
   在半透明背景场景下**数学上就是错的**（见 §4.5 陷阱）。

### 4.5 GLES 混合配置（正确 vs 陷阱）

| 场景 | 源数据 | 混合函数 | 结果 |
|---|---|---|---|
| **✅ 推荐：RT 预乘 + 源预乘** | `Ps` | `glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` | RGB: `Ps + Pd(1−As)` ✅；A: `As + Ad(1−As)` ✅ |
| ❌ 陷阱：RT 直通 + `GL_SRC_ALPHA` | `Cs` | `glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` | RGB: `Cs·As + Cd(1−As)` —— 少乘了 `Ad`；**仅当 `Ad == 1` 才正确** |

**结论**：只要画布背景可能半透明（`backgroundRgba` 的 A < 255），
就**不能**用第二行的配置。首期实现直接采用第一行。

### 4.6 边界转换表（关键）

| 边界 | 数据来源 | 语义 | 转换 |
|---|---|---|---|
| CPU → GPU 纹理上传 | `PreviewFrame.rgba`（straight） | straight | 上传时 `rgb *= a`（shader 内或预乘纹理） |
| **GPU RT 内部** | — | **premultiplied** | — |
| GPU → Surface 显示 | EGLSurface buffer | premultiplied（Android 默认） | **直接**，无需转换 |
| GPU → CPU 读回（导出/测试） | RGBA8 | 需与 CPU oracle 对齐（straight） | `rgb /= max(a, 1/255)`，`a==0` 时置 0 |
| 导出到不支持 alpha 的格式 | — | 不透明 | 用 `flattenOnto()` 压到背景 |

**Clear 颜色也必须预乘**：`glClearColor(R·A, G·A, B·A, A)`。
若直接 `glClearColor(R,G,B,A)`，半透明背景会偏亮。

### 4.7 半透明背景算例（G0.5/G2 必测向量）

背景 `Cb=(0,0,1), Ab=0.5`，前景 `Cf=(1,0,0), Af=0.5`：

```
Pb = (0, 0, 0.5)
Pf = (0.5, 0, 0)
Po = (0.5, 0, 0) + (0, 0, 0.5)×0.5 = (0.5, 0, 0.25)
Ao = 0.5 + 0.5×0.5 = 0.75
Co = Po / Ao = (0.6667, 0, 0.3333)
```

8-bit 期望值：`(170, 0, 85, 191)`（±1 LSB）。
**该向量同时用于 CPU oracle 与 GPU 输出比对。**

### 4.8 强制约束

- 对外语义（`CanvasBackground` / `PreviewFrame.rgba`）**保持 straight**（与现行 CPU 一致）；
- GPU 内部预乘，边界显式转换，**禁止同一 Pass 内混用两种语义**；
- 任何"直接把 straight 源喂给 `GL_ONE` 混合"的写法视为错误。

---

## 5. Render Core 接口设计

### 5.1 后端接口（扩展）

```cpp
// 设计示意，不落地
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual RenderBackendInfo info() const = 0;
    virtual bool beginFrame(int width, int height, const RenderQuality& quality) = 0;
    virtual void endFrame() = 0;

    // --- 新增 ---
    virtual ContextGeneration contextGeneration() const = 0;
    virtual SurfaceGeneration surfaceGeneration() const = 0;
    virtual bool isContextLost() const = 0;
    virtual bool recreateContext() = 0;      // 仅上下文丢失时
    virtual bool recreateSurface() = 0;      // 仅 Surface 变化时

    virtual TextureHandle createTexture(const TextureDesc&) = 0;
    virtual bool upload(TextureHandle, const std::uint8_t* rgba, int w, int h, int stride) = 0;
    virtual void destroyTexture(TextureHandle) = 0;

    virtual bool readback(RenderTargetHandle, std::uint8_t* out, int* w, int* h) = 0;  // 仅验证/导出
};
```

### 5.2 质量门控

直接复用 `RenderQuality`（`scale` / `sampleBudget` / `allowFrameDrop`），
导出一律 `scale=1, sampleBudget=1, allowFrameDrop=false`（既有 fail-closed 已保证）。

### 5.3 颜色空间

| 阶段 | 合成空间 |
|---|---|
| **G0.5 / G1 / G2** | **与 CPU 相同的 gamma 空间**（不做线性化） |
| G3 | 引入 `ColorPipeline`，CPU 与 GPU **同时**迁移到线性 |

`ColorConfig` 在 G0.5/G1 恒为默认值——**先保证与 CPU 一致，再谈色彩科学**。

---

## 6. 数据边界

### 6.1 边界规则

| 规则 | 说明 |
|---|---|
| GPU 线程**不得**访问 `Project` / `TimelineEditor` | 后者头注释明写"非线程安全，仅 UI 线程" |
| 帧请求只带**已求值的不可变快照** | 关键帧/速度曲线/效果参数在 CPU 侧求值完毕 |
| 时间用 `Rational` 在 CPU 侧定位 | 进 GPU 的只有 `double` 参数与帧序号 |
| 快照带 `revision` | 缓存失效与过期帧判定 |

```cpp
// 设计示意，不落地
struct FrameRequest {
    std::uint64_t frameId;     // 单调递增
    std::uint64_t revision;    // 来自 TimelineEditor::editRevision()
    RenderPath path;
    RenderQuality quality;
    int width, height;         // 画布权威尺寸
    Time timelineTime;         // Rational，仅 CPU 侧使用
    std::vector<LayerSnapshot> layers;  // 已求值
    std::uint32_t backgroundRgba;       // straight
};
```

`FrameRequest` 必须可拷贝、纯数据、无指针、无 `Project&`。

### 6.2 时间求值

时间不进 shader。`FrameRequest` 携带 `Time`，但**只在 CPU 侧**用于：
- 选取该时刻的片段/关键帧值；
- 生成 `LayerSnapshot`（含最终 4×4 变换矩阵与参数）。

### 6.3 缓存键

`(revision, timelineTime, width, height, quality, contextGeneration)`

### 6.4 回读边界（修订：评审意见 6）

| 用途 | 是否允许 `glReadPixels` |
|---|---|
| 实时预览上屏 | ❌ **禁止**（必须 `eglSwapBuffers` 到 Surface） |
| 导出/离线渲染 | ✅ 允许（本就不是实时路径） |
| 单测/验收比对 | ✅ 允许 |
| 调试抓帧 | ✅ 允许（受开关控制，默认关闭） |

**静态检查要求**：`platform/android/` 生产路径文件中出现 `glReadPixels` 必须
带 `// VALIDATION_ONLY` 注释并在评审中说明；预览路径文件中出现即视为缺陷。

---

## 7. 多帧调度：三个 frameId 与 revision 校验（本版重写）

### 7.1 三个计数器的职责（必须分开，不能合成一个）

| 计数器 | 写入方 | 写入时机 | 职责 |
|---|---|---|---|
| `latestRequestedFrameId` | **生产者**（UI / 编辑线程） | 每次构造 `FrameRequest` 时递增 | 表示"用户最新意图"。用于在**提交前**识别已过期的请求 |
| `latestSubmittedFrameId` | **生产者**（提交入队时，原子写） | 请求成功入队后 | 表示"已交给 GPU 的最新请求"。渲染线程据此跳过已被取代的入队任务 |
| `latestPresentedFrameId` | **消费者**（呈现线程） | `eglSwapBuffers` 成功后 | 表示"已上屏的最新帧"。防止乱序上屏（旧帧覆盖新帧） |

> 三个计数器各自回答不同问题：
> 「用户最新想要哪一帧」/「交给 GPU 的最新是哪一帧」/「屏幕上是哪一帧」。
> 合并成一个会同时丢失这三种语义。

### 7.2 判序规则（五条）

| 时机 | 规则 | 动作 |
|---|---|---|
| 请求构造后、提交前 | `request.frameId < latestRequestedFrameId` | **丢弃**，不提交 |
| 提交入队时 | 原子 `latestSubmittedFrameId = max(latestSubmittedFrameId, request.frameId)` | 继续 |
| 渲染线程取到任务后 | `request.frameId < latestSubmittedFrameId` | **跳过渲染**（仍记账，避免队列饥饿统计失真） |
| 呈现前 | `request.frameId <= latestPresentedFrameId` | **不 swap**（丢弃结果） |
| 呈现成功后 | `latestPresentedFrameId = request.frameId` | 更新 |

### 7.3 revision 校验（三处）

| 位置 | 校验 | 失败处理 |
|---|---|---|
| **提交前** | `request.revision != currentRevision` | **丢弃**请求 + 请求新快照（不渲染旧快照） |
| **渲染开始前** | 同左（双检，防提交后工程又变） | 丢弃 |
| **呈现前** | 同左 | **允许呈现**（避免黑屏）但标记 `stale=true`，并**立即请求新帧**；**不得据此更新缓存** |

### 7.4 导出路径例外

导出**不使用**上述丢弃规则：`RenderPath::Export` 必须渲染每一帧，
`allowFrameDrop` 恒为 false（`RenderQuality` fail-closed 已保证）。

### 7.5 线程模型

- GPU 必须有**专属渲染线程**（GL 上下文绑定线程），不得借用 UI 线程；
- 帧请求通过有界队列（容量 2）跨线程传递；
- 队列满 → **丢最旧**，不阻塞生产者。

---

## 8. GPU 资源生命周期与预算（本版重写）

### 8.1 资源类型

| 资源 | 归属 | 生命周期 | 回收 |
|---|---|---|---|
| Context | 后端 | 进程级（可重建） | `contextGeneration` 变化时全部重建 |
| Surface（EGLSurface） | 平台层 | 随窗口 | `surfaceGeneration` 变化时重建 |
| Texture | Context | 引用计数 / LRU | 预算驱动淘汰 |
| FBO / RenderTarget | Context | Pass 级 | Pass 结束回收或池化 |
| Shader / Program | Context | 进程级 | 按需编译 + 缓存 |
| Buffer（全屏三角形） | Context | 进程级 | 常驻 |

### 8.2 预算：不得把"显存估算"当可靠容量（修订：评审意见 4）

**Android 没有可靠的 VRAM 查询接口。**
`GL_MAX_TEXTURE_SIZE` / `GL_MAX_RENDERBUFFER_SIZE` 只给**单边上限**，
不告诉可用总量；厂商实现差异极大。因此：

| 项 | 做法 |
|---|---|
| 预算来源 | **保守配置上限**（默认 128 MiB，可按 Tier 调低） |
| 实际容量判定 | **只信运行时信号**：`GL_OUT_OF_MEMORY` / 分配失败 |
| 超预算 | LRU 淘汰 → 降预览分辨率（**不改画布尺寸**）→ CPU fallback |
| OOM 处理 | 预算 **减半** → 清缓存 → 重试 1 次 → 仍失败则 CPU fallback |
| 预算上调 | 默认**关闭**；仅在显式开启探测且连续 N 帧无 OOM 时小幅上调 |
| 状态标注 | `budgetSource = Configured \| ReducedAfterOOM`，随诊断上报 |

**明确禁止**：
- 用「设备总内存 × 系数」推算显存并当作权威容量；
- 把 `GL_MAX_TEXTURE_SIZE` 当作可用总量；
- 在 OOM 后静默重试而不记录。

### 8.3 释放顺序（必须固定）

```
1. 从 FBO 解绑全部纹理附件（glFramebufferTexture2D(..., 0)）
2. 删除 FBO
3. 删除纹理 / RenderBuffer
4. 删除 Program / Shader
5. 删除 Buffer
6. （Surface 变化时）删除 EGLSurface；Context 保留
7. （Context 丢失时）删除 Context 后按 1–5 全部重建
```

倒序或跳步会导致悬空引用与泄漏（G0.5 用资源计数验证）。

### 8.4 禁止

- 不得把预览降级后的分辨率写回 `Sequence::width/height`；
- 不得依赖 GC/析构顺序释放 GPU 资源（必须显式 dispose）。

---

## 9. EGLSurface 与 EGLContext 生命周期（本版重写）

### 9.1 两个世代号，不能混用

| 世代号 | 何时递增 | 影响范围 |
|---|---|---|
| `surfaceGeneration` | EGLSurface 创建/销毁/尺寸变化 | **只影响默认帧缓冲**；纹理与 FBO **不受影响** |
| `contextGeneration` | **仅**上下文真正丢失或显式重建 | **全部** context-owned 资源失效 |

> **评审意见 3 的落点**：Surface 重建 **不等于** Context 丢失。
> Android 上切后台/旋转/分屏常常只重建 Surface；若把它当 Context 丢失，
> 会导致每帧重建全部纹理，性能灾难且易泄漏。

### 9.2 事件对照表

| 事件 | surfaceGeneration | contextGeneration | 必须做什么 | 不需要做什么 |
|---|---|---|---|---|
| Surface 尺寸变化 | **++** | 不变 | 重建 EGLSurface + 重新绑定默认 FBO | **不重建纹理** |
| `surfaceDestroyed` | **++** | 不变 | `eglDestroySurface`；Context 保留 | 不销毁 Context |
| `surfaceCreated`（新窗口） | **++** | 不变 | 用**同一 Context** 创建新 EGLSurface + `eglMakeCurrent` | 不重建纹理 |
| `eglMakeCurrent` 返回失败且 `eglGetError()==EGL_CONTEXT_LOST` | ++ | **++** | 销毁旧 Context、重建 Context 与全部资源 | — |
| `eglSwapBuffers` 返回 `EGL_CONTEXT_LOST` | ++ | **++** | 同上 | — |
| 应用切后台（主动释放） | ++ | 视实现 | 可选释放 GL 资源 | 若保留 Context 则纹理可保留 |
| 进程重启 | 重置 | 重置 | 全新 | — |

### 9.3 检测点（必须显式检查，不能靠"感觉"）

```cpp
// 每次 eglMakeCurrent / eglSwapBuffers 之后
EGLint err = eglGetError();
if (err == EGL_CONTEXT_LOST) { ++contextGeneration; /* 全量重建 */ }
```

### 9.4 资源有效性校验

每个 GPU 资源记录 `createdAtGeneration`；
使用前断言 `resource.createdAtGeneration == backend.contextGeneration()`，
不等即视为失效（不得继续使用，否则是未定义行为）。

---

## 10. Render Graph 与 Pass

```
Frame → Graph
  ├─ Pass: ClearBackground   (画布 FBO，clear 颜色需预乘)
  ├─ Pass: CompositeLayer    (混合模式 + 不透明度)
  ├─ Pass: Transitions
  ├─ Pass: Effects
  └─ Pass: Present 或 Readback
```

约束：Pass 声明输入/输出纹理；Graph 负责拓扑排序与生命周期；Pass 不直接改全局 GL 状态。
G1 只需 3 个 Pass（Clear / Composite / Present）。

---

## 11. Render Cache 与失效

| 维度 | 规则 |
|---|---|
| 键 | `(revision, timelineTime, width, height, quality, contextGeneration)` |
| 失效 | `revision` 或 `contextGeneration` 变化 → 全部失效 |
| 粒度 | 首期只缓存源纹理与最终帧 |
| 预算 | 与 §8.2 共用 |
| 一致性 | 命中与未命中输出**逐像素一致**（有测试） |

---

## 12. Shader 工具链

| 项 | G0.5 要求 |
|---|---|
| 语言 | GLSL ES 3.00（`#version 300 es`） |
| 存放 | `platform/android/shaders/*.glsl`（纯文本，运行时编译） |
| 校验 | CI 用 `glslangValidator` 做语法校验（**不能替代真机**） |
| 变体 | 首期不做变体系统，一 Pass 一 shader |
| 失败 | 必须返回**完整编译日志**（含行号），不得静默回退 |

---

## 13. 错误诊断与 CPU 回退

| 码 | 含义 |
|---|---|
| `NC-RENDER-BACKEND_UNAVAILABLE` | 无可用 GPU 后端 |
| `NC-RENDER-CONTEXT_LOST` | 上下文丢失 |
| `NC-RENDER-SURFACE_LOST` | Surface 丢失（非 Context） |
| `NC-RENDER-SHADER_COMPILE_FAILED` | 编译/链接失败 |
| `NC-RENDER-OUT_OF_MEMORY` | 分配失败 |
| `NC-RENDER-UNSUPPORTED_FORMAT` | 纹理格式不支持 |
| `NC-RENDER-DEVICE_BLACKLISTED` | 设备在黑名单 |

回退流程：

```
尝试 GPU → 失败
  ├ 记录诊断码 + 设备信息（GL_VENDOR / GL_RENDERER / GL_VERSION）
  ├ 该会话标记后端不可用（避免每帧重试）
  ├ 切 CPU 渲染（现有路径，行为不变）
  └ 上报（可选提示"已降级预览"）
```

**回退不得改变导出结果语义**；CPU/GPU 输出差异必须在容差内。

### 13.1 诊断要求（G0.5 起强制）

- 每批 GL 调用后用 `glGetError()` 检查并清空（循环直到 `GL_NO_ERROR`）；
- 错误必须带**操作名 + 参数摘要**，不能只报数字；
- Shader 失败必须打印 `glGetShaderInfoLog` / `glGetProgramInfoLog` 全文。

---

## 14. 预览与离线渲染的一致性

| 维度 | 要求 |
|---|---|
| 时间 | 同一 `Rational` 时间、同一 `revision` 快照 |
| 画布 | 均取 `Sequence::width/height`；预览缩放只在显示层 |
| 效果 | 共用效果参数求值结果 |
| Alpha | 同一语义（对外 straight，内部 premultiplied + 边界转换） |
| 色彩 | 同一 `ColorConfig` |
| 验证 | 同帧两路径逐像素比对，误差 ≤ 1 LSB（8-bit） |

导出一律 `scale=1 / sampleBudget=1 / allowFrameDrop=false`。

---

## 15. G0.5 Android GLES 3.0 技术原型计划（只做计划）

### 15.1 目标闭环

```
EGL → GLES3 Context → Texture → Shader → Framebuffer → Draw → Surface Present
```

每一步都必须有可验证证据，不允许"能显示就算通"。

### 15.2 原型文件范围

| 文件 | 作用 |
|---|---|
| `platform/android/egl_context.{h,cpp}` | EGL display/config/context/**surface 分开管理** |
| `platform/android/gles_backend.{h,cpp}` | `IRenderBackend` 的 GLES 实现（最小） |
| `platform/android/texture.{h,cpp}` | 纹理创建/上传/释放 + 世代校验 |
| `platform/android/fbo.{h,cpp}` | FBO 创建 + **完整性检查** |
| `platform/android/shaders/blit.vert/.frag` | 全屏 passthrough + premultiply |
| `platform/android/probe.{h,cpp}` | 能力探测 |
| `tools/validate_shaders.sh` | `glslangValidator` 校验 |
| `tests/test_gles_reference.cpp`（主机侧） | 纯数据比对（不依赖 GL） |

**不在原型范围**：多轨合成、转场、效果、解码器纹理、导出。

### 15.3 依赖

NDK + 系统 `EGL`/`GLESv3`（无第三方）；一台真实 Android 设备；主机侧 `glslangValidator`。

### 15.4 Shader 验证

| 层级 | 方式 |
|---|---|
| 语法 | CI `glslangValidator` |
| 链接 | 设备：打印完整 info log |
| 行为 | **纯色输入做像素断言**，不是"看起来对" |

### 15.5 能力检测（运行时查询，禁止硬编码）

`GL_VENDOR` / `GL_RENDERER` / `GL_VERSION` / `GL_SHADING_LANGUAGE_VERSION` /
`GL_MAX_TEXTURE_SIZE` / `GL_MAX_RENDERBUFFER_SIZE` / 相关扩展。

### 15.6 Surface 与 Context 恢复（分别验证）

1. **仅 Surface 变化**：旋转/分屏 → 画面恢复，**纹理数不增长**、不重建纹理；
2. **Context 丢失**：模拟 `EGL_CONTEXT_LOST` → 全量重建，画面恢复；
3. 两者必须分别记录，不得合并成一个"恢复测试"。

### 15.7 软硬件证据边界

| 环境 | 能证明 | 不能证明 |
|---|---|---|
| GitHub 托管 Runner（无 GPU） | — | 任何 GPU 行为 |
| Mesa llvmpipe / SwiftShader | API 用法、shader 逻辑 | 真实驱动行为、性能、格式支持 |
| **真实 Android 设备** | **唯一可作 G0.5 通过的证据** | — |

G0.5 验收必须附真实设备的 `GL_VENDOR/GL_RENDERER/GL_VERSION` 日志 + 像素断言结果。

### 15.8 G0.5 验收标准（14 项，含评审新增 5 项）

| # | 标准 | 类别 |
|---|---|---|
| 1 | 真实设备创建 GLES3 Context，打印能力信息 | 基础 |
| 2 | 上传已知 RGBA 纹理，passthrough shader 输出**逐字节一致** | 基础 |
| 3 | 半透明纹理叠纯色背景，与 `blendStraightOver` **≤1 LSB** | Alpha |
| 4 | §4.7 半透明背景算例 `(170,0,85,191)` 命中（±1） | Alpha |
| 5 | FBO 离屏渲染 + `glReadPixels` 读回与预期一致 | 基础 |
| 6 | `eglSwapBuffers` 上屏，截图与读回一致 | 基础 |
| 7 | **每个 FBO 创建后 `glCheckFramebufferStatus == GL_FRAMEBUFFER_COMPLETE`** | **新增·FBO 完整性** |
| 8 | **Shader/Program 编译链接状态 + 完整 info log 落盘；失败路径有可读错误** | **新增·Shader 状态** |
| 9 | **每批 GL 调用后 `glGetError()` 清空；错误带操作名与参数** | **新增·错误诊断** |
| 10 | **资源生命周期：create/destroy 循环 100 次，`glIsTexture`/`glIsFramebuffer` 全为 false，计数归零** | **新增·资源释放** |
| 11 | **释放顺序符合 §8.3；乱序释放被断言检出** | **新增·释放顺序** |
| 12 | 仅 Surface 变化（旋转）→ 恢复且**不重建纹理**；Context 丢失 → 全量重建 | 恢复 |
| 13 | `glslangValidator` 在 CI 通过 | 工具链 |
| 14 | 能力不足/后端失败时**正确回退 CPU** 并有诊断码 | 回退 |

### 15.9 回读限制（评审意见 6）

- 原型阶段允许 `glReadPixels`（它就是验证手段）；
- **G1 起**：预览生产路径禁止 readback，静态检查 + 帧时间断言双重保证；
- 导出路径的 readback 属正常离线流程，不算违规。

---

## 16. G1—G6 实施路线

| 阶段 | 前置条件 | 修改范围 | 交付 | 测试 | 验收证据 | 停止条件 |
|---|---|---|---|---|---|---|
| **G0** | B1 通过 | 仅文档 | 本材料 | — | 评审通过 | 人工批准后才进 G0.5 |
| **G0.5** | G0 批准 | `platform/android/*`（新增）+ `tools/validate_shaders.sh` | GLES3 最小闭环 | 14 项 | 真机日志 + 像素断言 | 14 项全过，否则停止 |
| **G1** | G0.5 验收 | 预览路径接入 GPU 单层 + CPU 回退 | Android GPU 最小预览 | 预览正确性 + 回退 + **无 readback 断言** | 真机 1080p 播放 | 单层 GPU 预览与 CPU ≤1 LSB |
| **G2** | G1 验收 | 多轨合成 + Alpha | 多轨 GPU 合成 | Alpha 矩阵 + 多层 | 与 CPU 合成器比对 | 三轨以上一致 |
| **G3** | G2 验收 | 效果管线 + ColorPipeline | 专业 GPU 效果 | 效果逐像素 + 色彩 | 与 CPU 效果定义一致 | 效果栈全绿 |
| **G4** | G3 验收 | 解码器纹理（OES）+ 零拷贝 | 硬解纹理直通 | 多机型 | 无 CPU 中转 | 零拷贝可证 |
| **G5** | G4 验收 | Vulkan/Metal/D3D/GL 后端 | 跨平台后端 | 各平台冒烟 | 各平台真机 | 至少两平台通过 |
| **G6** | G5 验收 | 离线渲染 + 导出 | GPU 导出 | 预览/导出比对 | 导出文件校验 | 导出一致性通过 |

**每阶段单独授权，不得连做。**

---

## 17. GPU 与 Command 主线的依赖关系

| 约束 | 说明 |
|---|---|
| GPU **不得**维护第二套工程状态 | 只读帧快照，不持有 `Project*` |
| 帧快照必须带 `revision` | 与 `TimelineEditor::editRevision()` 同源 |
| GPU 不得绕过 Command 修改工程 | 任何"GPU 侧改参数"必须回到 Command |
| 预览参数不是工程数据 | 只影响 `RenderQuality`，不写回 `Sequence` |

### 17.1 文件所有权

| 文件 | B2—D 期间 | GPU 侧 |
|---|---|---|
| `mobile/native/android/nc_jni.cpp` | **B2 独占** | G1 前不得改 |
| `core/src/preview.cpp` | 主线（D 可能触碰） | G1 起需共改 → 先定交接 |
| `core/include/nairoclip/core/render_engine.h` | GPU 线 | 主线只读 |
| `platform/android/**`（新增） | GPU 线独占 | — |
| `core/src/frame_fit.cpp` / `canvas.cpp`（Alpha oracle） | 主线 | **GPU 只读** |

**同一文件同一时间只允许一条工作线修改。**

---

## 18. 本轮 GPU 授权边界

**允许（本轮已做）**：只读审计、修订架构设计、设计接口（示意）、制定 G0.5 任务书、规划 G1—G6。

**禁止（本轮未做，且不得做）**：编写生产 GPU Backend、新增任何 EGL/GLES/Vulkan/Metal/D3D 生产实现、
修改 CPU Preview / Alpha 生产实现、修改 Project Core / JNI / C ABI / Android UI、
开始 GPU Alpha 功能实现、把软件 GPU 结果标记为真实硬件验收、新增媒体编解码依赖。

---

## 19. 待评审决策

| # | 决策点 | 选项 | 建议 |
|---|---|---|---|
| 1 | G0.5 是否先做主机侧 GLES 仿真 | ①直接真机 ②先 Mesa | ②可提前暴露 shader 错误，但**不能**替代真机 |
| 2 | 首期是否引入 Vulkan | ①否 ②是 | ① |
| 3 | 线性色彩迁移时点 | ①G3 ②更晚 | ①（越晚越贵，必须 CPU/GPU 同改） |
| 4 | 纹理预算默认值 | 128 MiB？ | 需真机标定；**不接受**"按设备内存推算" |
| 5 | G1 是否只做单层预览 | ①是 ②多轨 | ① |
| 6 | `nc_jni.cpp` 交接时点 | B2 合并后？ | 是 |
| 7 | OOM 后预算是否允许自动上调 | ①否 ②允许 | ①（默认关，避免抖动） |

---

## 20. 结论

| 项 | 结论 |
|---|---|
| 当前是否有 GPU 后端 | **没有**（0 命中，`IRenderBackend` 无派生类） |
| CPU 路径是否可复用 | 是，作为 **oracle + fallback** |
| 是否需要新建 Render Core | 是，且必须与平台 API 解耦 |
| V1.0 是否可直接作为开发任务书 | **不能**（且该文件不在仓库） |
| 本轮是否产出生产代码 | **否** |
| 下一步 | 人工复审本材料 → 批准后**单独授权** G0.5 |

**本材料是 G0 评审材料，不是 GPU 开发授权。**
