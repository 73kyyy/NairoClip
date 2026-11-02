# 预览性能架构 与 8 项功能特效 开发计划

> 状态：已定案，可开工
> 决策依据：用户确认「用代理渲染」
> 本文所有「现状」结论均来自对仓库的实际检索，非推测。

---

## 0. 现状核查（实测）

| 项 | 现状 | 位置 | 影响 |
|---|---|---|---|
| 多线程 | ❌ 零 `std::thread` / `std::async` | 全 `core/src/` | 单核跑满，多核闲置 |
| SIMD | ❌ 零 NEON 代码 | 全 `core/src/` | 只有 `model_manager.h:267` 一句注释 |
| LUT 实现 | ⚠️ **非查表**：`std::vector<double>` + 三线性插值 | `core/include/nairoclip/core/lut.h:42` | 每像素数十次浮点，慢 5–10 倍 |
| `RenderQuality.scale` | ❌ **已定义，零处使用** | `render_engine.h` | 代理渲染开关已存在但是空壳 |
| `allowFrameDrop` | ❌ **已定义，从未接入** | `render_engine.h` | 丢帧策略空壳 |
| 帧缓存失效粒度 | ⚠️ **整帧失效**（按 `editRevision` 全清） | `capi.cpp:3864` | 改一个字幕，5 层视频全重算 |
| 硬件解码 | ❌ `hwaccel` 出现 0 次 | 全仓库 | 4K 高码率软解可能仅 10–15fps |
| 预览传输 | ⚠️ PNG + Base64 跨 JNI | `nc_jni.cpp` `nativeFrame` | 1080p 25fps ≈ 65MB/s 字符串 |
| `nativeFrame(width)` 参数 | ⚠️ **声明了但函数体从未使用** | `nc_jni.cpp:1316` | 工程 4K 就渲 4K 全帧 |
| 8 项功能特效 | ❌ 全部 `ready: false` | `mobile/assets/editor.js:2893` | 面板标「待接入」 |
| 几何变换位置 | `renderFrameFit` 已做双线性重采样 | `core/src/preview.cpp:299` | **8 个效果必须并入这里，见 §2** |

---

## 1. 定案：代理渲染

**结论：采用代理渲染。** 预览按渲染分辨率算，导出按用户选择的全分辨率算。

| 场景 | 渲染分辨率 | 依据 |
|---|---|---|
| 播放 / 拖动 | `min(画布尺寸, 屏幕物理像素)` | 超出屏幕的像素 100% 被丢弃 |
| 暂停 / 单帧 / 放大查看 | 画布原始尺寸 | 用户要看细节时按需全质量 |

**这不是"压缩画质"**：屏幕上能显示的像素上限就是屏幕物理像素。渲染 4K 再缩到 400px 宽的预览窗口，多出的像素没有到达用户眼睛。导出路径完全不受影响。

**必须做的产品配套（借鉴剪映，见 §5.1）**：
- 设置项「预览画质：流畅 / 标准 / 高清」，默认「标准」，用户可自行改回全质量；
- 自动降级**有记忆**：记住该工程实测能跑的档位，下次打开直接用，不逐帧试探；
- 导出前提示或提供「全质量预览一帧」入口，消除「预览≠成片」的心理落差。

---

## 2. 关键架构决策：几何效果必须并入 fit 阶段

### 问题

现管线（`core/src/preview.cpp`）：

```
解码 → renderFrameFit（双线性重采样，1 次） → 特效栈（原地逐像素） → 输出
```

8 个功能特效里 7 个是**仿射变换**（平移/旋转/缩放/拉伸）。若照搬现有 `applyMotionEffectToRgba` 的「原地逐像素」模式，每个效果都要读源写目标 → **7 次重采样**。后果：

- 内存带宽 ×7（1080p RGBA 单帧 8.3MB，7 次读+写 ≈ 116MB/帧）；
- 每次双线性插值都损失锐度，叠加 7 次后画面明显发糊；
- 与「预览=导出」的质量要求冲突。

### 决策

**新增 `renderFrameTransformed()`，把 7 个仿射效果 + 画幅适配合并为单次重采样。**

```cpp
// core/include/nairoclip/core/frame_fit.h 新增
struct Affine2D {
    double m[6];   // [a b tx; c d ty] 行主序，源坐标 → 目标坐标
    static Affine2D identity();
    Affine2D then(const Affine2D& next) const;   // 矩阵右乘，支持链式合并
    bool invert(Affine2D& out) const;            // 行列式≈0 时返回 false
};

enum class EdgeMode {
    Background = 0,  // 露出的部分填背景色（默认，最保守）
    Mirror     = 1,  // 镜像延展（无黑边，观感自然）
    Clamp      = 2,  // 边缘像素拉伸（最省，适合小位移）
};

bool renderFrameTransformed(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                            std::uint8_t* dst, int dstW, int dstH,
                            const Affine2D& srcToDst, EdgeMode edge,
                            std::uint32_t backgroundRgba,
                            bool bilinear);   // false = 最近邻（预览快速档）
```

- 逆映射采样：遍历目标像素 → 反变换回源坐标 → 采样。保证目标无空洞；
- `bilinear=false` 走最近邻，配合 `RenderQuality` 分级（预览快速档）；
- `renderFrameFit` 保留为 `renderFrameTransformed(identity, …)` 的特例，**不删旧接口**（导出管线、测试在用）。

### 收益

| | 改造前 | 改造后 |
|---|---|---|
| 重采样次数 | 7 + 1 = 8 | **1** |
| 1080p 每帧带宽 | ≈ 133MB | ≈ 17MB |
| 锐度损失 | 累积 8 次 | 1 次 |

**这是整个计划里单点收益最大的一项，优先做。**

### 例外：栅格转换不是单一仿射

`栅格转换` 是「画面切成 R×C 网格，每格独立偏移/缩放」→ 每格仿射、格间不连续。实现为 **N 次小 blit**（N = R×C，默认 6×6 = 36）。因为单格面积小（1080p 下约 180×180），总带宽仍远低于全帧多次重采样。

---

## 3. 分阶段开发计划

每阶段独立可交付、可回滚。**顺序按「风险从低到高、收益从高到低」排列。**

### P0 — 性能埋点（0.5 天）

**先测量，再优化。** 没有分阶段计时就无法判断瓶颈，容易优化错地方。

- 新增 `core/include/nairoclip/core/perf_probe.h`：`ScopedTimer` + 环形统计（decode / fit / effect / composite / transfer 五段）；
- 默认关闭，`NC_PERF=1` 环境变量或 C ABI 开关启用；
- 新增 C ABI `nc_perf_probe_get(stage, out_avg_ms, out_p99_ms, out_count)`。

**验收**：跑一段 1080p 3 层播放，能打印五段耗时占比，且总量与墙钟时间误差 < 10%。

---

### P1 — LUT 改整型查表（0.5 天）

**现状**：`Lut3D::data` 是 `std::vector<double>`，`lutSample` 做三线性插值 → 每像素 3 通道 × 8 次查表 + 插值运算。

**改法**：
- `Lut3D` 增加 `std::vector<std::uint8_t> fast`，在 `parseCubeLut` / `loadCubeLutFile` 成功后构建一次（`size³ × 3` 字节，33³ 仅 107KB，L1/L2 友好）；
- `applyLutToRgba` 走整型路径：`idx = ((b*size + g)*size + r) * 3`，无浮点；
- 保留 `lutSample`（double 版）供导出高质量路径与单元测试比对，**不删**；
- 强度混合用整型 lerp：`out = (a*(256-w) + b*w) >> 8`。

**验收**：
- 单测：整型路径与 double 路径逐像素差 ≤ 2（8bit 量化误差内）；
- 基准：1080p 单帧 LUT 耗时下降 ≥ 4 倍（CI 打印，不做硬门槛避免抖动误报）。

---

### P2 — 接通代理渲染与丢帧（1 天）

**两个空壳字段落地。**

- `RenderQuality.scale` 接入 `preview.cpp`：canvas 尺寸 = `画布 × scale`，最终由平台层缩放到控件尺寸；
- 新增 `RenderQuality` 自动档：按 P0 实测的单帧耗时选 scale（>40ms → 0.5，>25ms → 0.75，否则 1.0），**每 30 帧评估一次**，避免抖动；
- 档位记忆：随工程文件持久化（`.nclip` 增加可选字段 `previewScale`，默认不写 = 1.0）；
- `allowFrameDrop` 接入：播放时若上一帧未完成则跳过本帧（丢帧而非排队），并统计 `droppedFrames` 供 UI 显示。

**验收**：
- 1080p 5 层工程在 scale=0.5 下单帧耗时 ≤ 40ms；
- 播放 30s 不出现帧队列积压（`droppedFrames` 单调但不爆增）；
- 暂停时自动回到 scale=1.0 渲一帧。

---

### P3 — CPU 多线程（1.5 天）

- 新增 `core/include/nairoclip/core/thread_pool.h`：固定 `std::thread::hardware_concurrency()` 线程，任务队列 + 条件变量；**无第三方依赖**；
- **按行条带切分**（非按层）：每线程处理连续的 `H/N` 行，无跨行依赖，缓存友好；
- 逐像素类效果（调色 / LUT / 混合 / 模糊）全部改造为条带并行；
- 小图（< 64K 像素）走单线程，避免调度开销反超。

**验收**：
- 8 核机器上，1080p 调色+LUT 单帧加速 ≥ 4 倍；
- **逐像素结果与单线程完全一致**（bit-exact，不是「近似」）—— 这是硬性要求，浮点顺序不能变；
- 无数据竞争：CI 跑一次 TSan。

---

### P4 — NEON 优化核心循环（2 天）

只优化**热点且可向量化**的三处，不做全面 SIMD 化：

| 循环 | 手段 |
|---|---|
| RGBA→BGRA 通道交换 | `vld4q_u8` / `vst4q_u8` 去交织，一次处理 16 像素 |
| 调色（亮度/对比度/饱和度） | `float32x4_t`，定点化避免精度漂移 |
| 整型混合（`>>8` lerp） | `vmull_u8` + `vrshrn_n_u16` |

- 用 `#if defined(__ARM_NEON)` 守卫，**保留标量回退路径**（x86 CI、无 NEON 设备）；
- 标量与 NEON 双路径必须 bit-exact 一致，写成单测。

**验收**：三处循环加速 ≥ 3 倍；NEON 与标量结果逐字节相同；x86 构建无警告。

---

### P5 — 8 项功能特效（内核 3 天 + UI 2 天）

见 §4 规格。依赖 P2（`renderFrameTransformed` 在 P5 前置实现）。

---

### P6 — SurfaceView 零拷贝（3–5 天）

砍掉 `PNG → Base64 → JNI → <img>` 全链路。

- Kotlin 侧新增 `PreviewSurfaceView`（`SurfaceHolder` + `ANativeWindow`）；
- JNI 新增 `nativeAttachSurface(Surface)` / `nativeDetachSurface()` / `nativeBlitFrame(timeUs)`；
- C++ 侧 `ANativeWindow_lock` 写 RGBA 后 `unlockAndPost`；格式 `WINDOW_FORMAT_RGBA_8888`；
- 布局：SurfaceView 置于 WebView **之下**，WebView 预览区背景设为透明，矩形由 JS 上报（`setPreviewRect(x,y,w,h)`）；
- API 24/25 无 `AHardwareBuffer`，走 `ANativeWindow` 即可（无需该 API）。

**风险**：Z-order、圆角裁剪、WebView 透明层性能、面板弹出遮挡。**这是全计划唯一需要动 UI 层级的步骤，单独排期、单独回滚。**

**验收**：1080p 25fps 播放，CPU 占用下降 ≥ 40%；传输段耗时（P0 埋点）从数十毫秒降至 < 2ms。

---

### P7 — 硬解 + GPU 合成（后续，1 周+）

- **MediaCodec 硬解**：4K 高码率素材。FFmpeg 侧通过 `AVHWDeviceContext` + `MediaCodec` 或直接用 NDK `AMediaCodec`；软解保留为兜底（老设备/异常码流）；
- **GLES 3.0 合成**：只做多层纹理合成 + 色彩空间转换，不重写全部滤镜；
- **CPU/GPU 逐像素比对测试进 CI**：同一帧两路径各渲一遍，最大通道差 ≤ 1，否则失败。防「预览对、导出偏色」。

---

## 4. 8 项功能特效规格
**统一定义**：均为**几何变换**，不重新着色。`value` ∈ [0,1]，0 = 关闭（零改动）。

**统一实现路径**：7 项仿射 → 合并进 `Affine2D` → 单次 `renderFrameTransformed`；栅格转换单独走网格 blit。

**统一边界**：默认 `EdgeMode::Mirror`（镜像延展，无黑边）。用户可切换为 `Background`。

**统一时间语义**：时变效果用**量化到 30fps 网格的帧序号**作种子（沿用 `preview.cpp` 现有约定），保证静止画面不闪噪点、同一时刻预览稳定。

| # | key | 名称 | 类型 | value 映射 | 算法 |
|---|---|---|---|---|---|
| 1 | `offset` | 偏移 | 仿射 | 位移幅度 = `value × 0.5 × 画布短边` | 平移；方向由 `phase` 参数定（默认右） |
| 2 | `stretch` | 拉伸轴 | 仿射 | 轴长比 = `1 + value` | 各向异性缩放；`axis` 参数选 X/Y/对角 |
| 3 | `spin` | 旋转 | 仿射 | 角度 = `value × 360°` | 绕中心旋转；`speed` 参数控是否随时间累加 |
| 4 | `raster` | 栅格转换 | 网格 | 格偏移 = `value × 格边长 × 0.5` | 切 R×C 格，每格独立平移+缩放，格间留 `gap` |
| 5 | `path` | 沿路径移动 | 仿射 | 路径半径 = `value × 0.3 × 短边` | 绕中心椭圆/圆形轨迹运动，`t` 由帧序号驱动 |
| 6 | `scale` | 缩放辅助 | 仿射 | 缩放 = `1 + value × 0.5` | 中心锚点缩放；缩放后自动镜像延展补边（避免黑边） |
| 7 | `pulse` | 脉冲大小 | 仿射 | 峰值缩放 = `1 + value × 0.3` | 时间正弦脉冲：`1 + A·sin(2π·f·t)`，f 默认 2Hz |
| 8 | `randpos` | 随机位移 | 仿射 | 抖动幅度 = `value × 0.04 × 短边` | 确定性伪随机（种子 = 帧序号），每帧独立偏移 |

### 参数承载

现有 `Effect` 是**单值**结构（`{id, type, value, enabled}`），塞不下 `axis`/`phase`/`speed`/`gridR/gridC`。

**方案**：不扩展 `Effect`。新增 `Clip::xforms` 为**固定 8 槽数组**（每类一槽），而非单个 struct：

```cpp
enum class XformKind : int { Offset=0, Stretch, Spin, Raster, Path, Scale, Pulse, RandPos, Count=8 };

struct XformParams {
    bool   enabled = false;
    double value   = 0.0;    // 0..1 主强度
    double angle   = 0.0;    // 方向/相位，弧度
    double freq    = 2.0;    // 频率（Hz 或 rev/s，按类解释）
    int    axis    = 0;      // stretch：0=X 1=Y 2=对角
    int    cols    = 6;      // raster
    int    rows    = 6;
    double gap     = 0.0;    // raster 格间隙（相对格边长）
};

struct Clip { ...; XformParams xforms[8]; };
```

**为什么是 8 槽而非单个 struct**：8 项效果必须**可同时叠加**（用户加「旋转 + 偏移」是常见操作）。单槽会强制互斥，是产品缺陷。8 槽同时带来三个好处：叠加天然支持、序列化只写 `enabled` 的槽、指纹计算就是 8 个槽的哈希。

**合并顺序**（固定，保证可复现）：按 `XformKind` 枚举值从小到大依次 `Affine2D::then()`。不按添加顺序——否则同一工程在不同端渲染结果可能不同。

**序列化**：`.nclip` 中 `xforms` 全部 `enabled=false` 时整块不写，仅写启用的槽。

---

### 4.1 前端 UI（8 项效果的面板）

**现状（实测，骨架已存在）**：

| 已有 | 位置 |
|---|---|
| 宫格容器（3 列）+ 项样式 + 「待接入」角标 | `editor.js:2942 openTransformPanel()`；`.xf-grid/.xf-item/.xf-thumb/.xf-name/.xf-en/.xf-soon/.xf-badge` |
| 12 项缩略图（CSS 绘制，无外部图片） | `xformThumb()`，12 个 key 全覆盖 |
| 参数面板骨架：「左侧圆角标签 · 中间刻度尺 · 右侧数值框」 | `openShakePanel()` |
| **数据驱动的参数行**：`SPEC = { key: {name,min,max,step,unit\|pct,def} }`，行由表生成 | `openShakePanel()` 内 |

**结论：8 项不需要新写面板，需要的是 8 张 `SPEC` 表 + 2 个新控件类型。**

**所以「前端接入」不是「把 ready 改成 true」**（这是我上一版文档的错误），实际清单是：

1. 8 项 `ready: false → true`（1 行 × 8）
2. 8 张 `SPEC` 表（数据，非逻辑）
3. 2 个新控件：`xr-chips`（横向选项组）、`xr-step`（`− 值 +` 步进器）
4. `nc_set_clip_xform` / `nc_get_clip_xform` 的 C ABI 与 JNI op
5. 宫格项「已添加」态（右上角小圆点），复选叠加
6. 面板内「移除」入口（复用 `⋯` 菜单）

**8 项的面板行定义（完整：字段 / 范围 / 默认值 / 显示格式）**：

> `value` 是**主强度**，各效果 UI 标签不同但都是同一个字段（避免 6 个独立字段）。
> 默认值必须与 `core/src/xform.cpp` 的 `defaultXformParams()` 一致，改一处同步另一处。

| 效果 | UI 行 | 结构体字段 | 范围 | 默认 | 显示 |
|---|---|---|---|---|---|
| **偏移** | 距离 | `value` | 0–1 | **0.25** | `25%`（×短边） |
| | 方向 | `angle` | 0–360° | **0** | `0°`（0=右，顺时针） |
| | 边缘 | `edge` | 0/1/2 | **1** | 背景 / 镜像 / 拉伸 |
| **拉伸轴** | 轴 | `axis` | 0/1/2 | **0** | X / Y / 对角 |
| | 倍率 | `value` | 0–1 | **0.3** | `30%`（1+value 倍） |
| **旋转** | 角度 | `angle` | 0–360° | **0** | `0°` |
| | 自转速度 | `freq` | 0–5 rev/s | **0** | `0.0 /s` |
| **栅格转换** | 列 | `cols` | 2–12 | **6** | `6` |
| | 行 | `rows` | 2–12 | **6** | `6` |
| | 偏移强度 | `value` | 0–1 | **0.3** | `30%` |
| | 间隙 | `gap` | 0–0.5 | **0.05** | `5%` |
| **沿路径移动** | 半径 | `value` | 0–0.5 | **0.2** | `20%`（×短边） |
| | 周期 | `period` | 0.5–10s | **2.0** | `2.0s` |
| | 轨迹 | `trajectory` | 0/1/2 | **0** | 圆 / 椭圆 / 直线 |
| **缩放辅助** | 缩放比例 | `value` | 0–2 | **0.3** | `130%`（1+value） |
| | 锚点 | `anchor` | 0..8 | **4** | 中心（九宫格） |
| **脉冲大小** | 峰值强度 | `value` | 0–0.5 | **0.2** | `20%` |
| | 频率 | `freq` | 0.1–5 Hz | **2.0** | `2.0Hz` |
| | 相位 | `phase` | 0–360° | **0** | `0°` |
| **随机位移** | 幅度 | `value` | 0–0.2 | **0.04** | `4%`（×短边） |
| | 频率 | `freq` | 1–30 /s | **8** | `8/s` |
| | 种子 | `seed` | 0–999 | **0** | `0`（`xr-step` 步进） |

**默认值的设计原则**：每项默认 `value > 0`，保证「点了就能看到效果」。若默认 0，用户点开面板拖动前画面毫无变化，会以为功能坏了。`enabled` 默认 `false`（未添加时不参与渲染）。

**钳制而非报错**：越界值一律钳到范围内（`XformParams::clamp()`），与现有 `Effect.value` 处理一致。前端也做同样钳制，双保险。

**交互沿用现有模式，不新造**：

- 点宫格项 → 打开该项参数面板（当前值由 `xform_get` 回读；未设置则用默认）
- 「应用」→ `nc_set_clip_xform` → 刷新 → 宫格项点亮小圆点
- `⟲` 重置为该效果默认值（不删除，与现有 `openShakePanel` 一致）
- `⋯ → 移除` 删除该槽（`enabled=false`）

**不做的事**：不实时渲染效果缩略图。`xformThumb()` 的 CSS 示意是正确取舍（剪映同样用预渲染静态图）。

### 每个效果的必测断言

| 效果 | 断言 |
|---|---|
| 全部 | `value = 0` 时输出与输入 **逐字节相同** |
| 全部 | `value = 1` 与 `value = 0.5` 输出不同（防「参数没接上」） |
| `offset` | 位移 `dx` 像素后，原 `(x,y)` 像素出现在 `(x+dx, y+dy)` |
| `stretch` | 横轴拉伸 2 倍后，画面中心列像素列数减半 |
| `spin` | 旋转 90° 后，原左上角像素出现在右上角（误差 ≤ 1px） |
| `raster` | `gridR×gridC` 格数正确；`gap=0` 时无空洞 |
| `path` | 帧序号推进 → 位移方向按椭圆轨迹变化 |
| `scale` | 缩放后**无背景色像素**（镜像延展生效） |
| `pulse` | 同一帧序号两次调用结果相同（确定性） |
| `randpos` | 相邻帧位移不同；同一帧序号可复现 |

### 4.2 前端接入清单

详见 §4.1。要点：`ready: false → true`（8 行）、8 张 `SPEC` 表、2 个新控件（`xr-chips` / `xr-step`）、`nc_set_clip_xform` + `nc_get_clip_xform`。**不改动 `ready` 语义**：仍由该字段决定是否可点，避免出现假按钮。


---

## 4.3 端到端接线清单（7 项，缺一不可）

> 以下 7 项是「内核做完但功能不能用」的典型断点。每项都对应仓库里已存在的范式，照抄即可。

### ① 撤销/重做命令

**缺什么**：`SetClipXformCommand`。

**范式**：`core/include/nairoclip/core/commands.h:684` 的 `SetClipShakeCommand`（按值持有 `ShakeParams`，记录 `before_/after_` 快照）。

```cpp
class SetClipXformCommand : public Command {
public:
    SetClipXformCommand(SequenceId, TrackId, ClipId, XformKind kind, XformParams params);
    // revert(): 写回 before_；apply(): 写回 after_
private:
    XformParams before_{}, after_{};
    XformKind kind_;
};
```

- 必须在 `Editor::setClipXform()` 里经 `undoStack_.push(...)` 提交，**不得直接改 clip**（否则 `editRevision` 不递增，缓存不失效）；
- 一次「应用」= 一个命令 = 一次撤销步。

### ② 导出路径接入

**缺什么**：`core/src/export_executor.cpp` 的 xform 调用点。

现状该文件有 **5 处** `applyEffectStackToFrame`，但几何变换必须在 **`renderFrameFit` 之前**（否则特效算在未变换的画面上，结果错误）。

正确顺序：
```
解码 → 【xform 仿射合并】→ renderFrameFit → 特效栈 → 输出
```
即：把 xform 矩阵并入 `renderFrameTransformed` 的 `srcToDst`，让它和 fit 共用同一次重采样。**预览与导出必须调用同一个函数**，这是「预览=导出」的唯一保证。

**验收**：同一工程同一帧，预览路径与导出路径输出**逐字节相同**（分辨率相同时）。

### ③ JNI op + BuildState 状态推送

**缺什么**：
- `nc_jni.cpp` 新增 op：`xform_set`（写）、`xform_get`（读）、`xform_clear`（清空某槽）
- **BuildState 的 clip JSON 目前只输出 `effects`，不输出 `shake` 也不输出 `xform`** → 前端无法知道「已添加哪些效果」，宫格小圆点无数据来源

需在 clip 对象中补：
```json
"xforms": [{"kind":0,"enabled":true,"value":0.5,"angle":1.57,...}, ...]
```
（8 槽全量输出，`enabled=false` 的槽可省略以省带宽，前端按 kind 对齐）

**注意**：`nc_jni.cpp` 与 `NativeEngineBridge.kt` 是**两份手工副本**，必须同步改（见 ⑤）。

### ④ `design/tools/api_dispatch.inc` 派发分支

**缺什么**：3 个新 ABI 的派发分支。

**为什么必做**：该文件头部注明「生成器已移除，此文件为权威产物，手工编辑」。**上一轮 CI 编译失败正是因为这里漏了新参数**（`nc_effect_get` 少传 `out_effect_id`）。新增 ABI 必须同步：

```cpp
if (fn == "nc_set_clip_xform") { ... }
if (fn == "nc_get_clip_xform") { ... }
```

**必须按 `capi.h` 的签名逐个核对参数个数与类型**，`.inc` 不在编译器保护范围内（它被 include，报错会指向宿主文件的行号，极难定位）。

### ⑤ Kotlin 双份同步

`mobile/native/android/nc_jni.cpp` 与 `android/app/src/main/java/com/nairoclip/app/NativeEngineBridge.kt` 是手工维护的两份副本，此前已 `diff` 验证一致。

新增 op 时**两边必须同时改**，且 CI 需要加一步 `diff` 校验（或脚本比对导出符号），否则会漂移。

### ⑥ 返回链集成

面板打开时必须上报深度：`updatePanelDepth(depth)` / `notifyDepth()`。

否则系统返回键行为错误 —— 现有机制是 `MainActivity` 的 `panelDepth > 0` 时走 `window.NCL3BACK()`，未上报会导致：
- 返回键直接退出 App（而不是关闭面板）
- 或多层面板一次退两层

**范式**：`mobile/assets/l3-panel.js` 的 `back()` + `notifyDepth()`，以及 `window.NCL3BACK = back`。

### ⑦ C ABI 层测试

文档 §4 的断言是 **core 级**。还缺 **ABI 级**：

- 参数校验：空指针 → `NC_INVALID_ARGUMENT`；`track/clip` 不存在 → `NC_NOT_FOUND`；`kind` 越界 → `NC_INVALID_ARGUMENT`
- `value` 越界钳制（0..1）而非报错
- 8 槽独立：写第 3 槽不影响其余 7 槽
- 幂等：重复写相同参数，`editRevision` 不变（避免污染撤销栈）

---

## 4.4 UI 配色规范（按样板图，套用本 App 主题）

样板图是浅色，我们统一改为深色主题，**结构与尺寸 1:1 保留**：

| 样板图元素 | 本 App 实现 | 依据 |
|---|---|---|
| 左圆角标签（灰底黑字） | `.xr-label`：`background: rgba(255,255,255,.06)`，`color: var(--text-hi)` | 已存在，无需改 |
| 左标签**选中态**（黑底白字反白） | `.xr.active .xr-label`：`background: var(--text-hi)`，`color: #14161A` | 已实现（对应样板「频率」被选中） |
| 中间刻度尺（细竖线阵 + 长刻度） | `.xr-scale` + `.xr-ticks i` / `i.maj` | 已实现；`maj` 每 6 格一根 |
| 刻度尺内滑块（一根竖线） | `input[type=range]` 的 thumb：`var(--accent-pick)`，3×26px，带辉光 | 已实现 |
| 右数值框 | `.xr-val`：`var(--font-num)`，13px | 已实现 |
| 右上三圆角按钮组（⋯ / ⟲ / 图标） | `.xf-actions` + `.xf-act` | 已实现 |

**关键**：现有 `.xr-*` 样式与样板图**同构**，只需确认新控件（`xr-chips`/`xr-step`）沿用同一套尺寸与圆角（12px），**不要引入第二套视觉语言**。

**新增的两个控件必须复用现有 token**：
```css
.xr-chips { display:flex; gap:6px; flex:1; }
.xr-chip  { flex:1; height:40px; border-radius:12px;
            background:rgba(255,255,255,.045); color:var(--text-mid);
            font-size:12px; display:grid; place-items:center; }
.xr-chip.on { background:var(--accent-pick); color:#14161A; font-weight:700; }

.xr-step  { display:flex; align-items:center; gap:8px; flex:1; height:40px;
            border-radius:12px; background:rgba(255,255,255,.045); padding:0 8px; }
.xr-step button { width:26px; height:26px; border-radius:8px;
                  background:rgba(255,255,255,.08); color:var(--text-hi); }
.xr-step .v { flex:1; text-align:center; font-family:var(--font-num); color:var(--text-hi); }
```

### 其它功能面板统一改造

用户要求：**除滤镜外，其它功能面板统一改成样板图这套版式**。

| 面板 | 现状 | 改造 |
|---|---|---|
| 转换（`openShakePanel` 等） | 已是 `.xr` 三段式 | ✅ 基准 |
| 效果调节 | 需核查 | 改 `.xr` |
| 画中画 / 蒙版 / 调色 | 需核查 | 改 `.xr` |
| 音量 / 速度 | 需核查 | 改 `.xr` |
| 文字 | 需核查 | 改 `.xr` |
| **滤镜** | — | **不改**（用户明确排除） |

**做法**：不逐个手改，抽出 `buildParamRows(host, spec, draft)` 公共函数（现 `openShakePanel` 内联的逻辑），所有面板调用它。这样「统一版式」变成一处维护。

---

## 4.5 按素材类型的能力矩阵（点选素材 → 功能栏变化）

### 现状（实测，必须先看清）

| 事实 | 位置 | 影响 |
|---|---|---|
| 预览只渲染 `TrackType::Video` | `preview.cpp:233` | 字幕/音频不产生画面 |
| 字幕是 `frame.subtitles` 数据，**由 UI 层绘制** | `preview.cpp:227` | 内核不碰字幕像素，xform 无法作用于字幕 |
| `TrackType::Adjustment` **拒绝放置 Clip** | `track.h:22`；`validateClipPlacement` | 调整图层无法放素材 |
| 调整图层实际是**轨道级特效叠加**（`track.adjustmentEffects`），非 Clip | `preview.cpp:397`；`track.h:99` | 无 `Clip` 可挂 xform |
| 音频轨无任何画面 | — | 几何变换对音频无意义 |

### 结论：8 项几何变换**只对视频类素材有效**

这是数学事实，不是实现取舍：几何变换改的是像素位置，音频没有像素，字幕不在内核像素管线内。

### 能力矩阵

| 功能 | 视频 | 图片 | 字幕 | 调整图层 | 音频 |
|---|---|---|---|---|---|
| **转换 8 项**（偏移/拉伸/旋转/栅格/路径/缩放/脉冲/随机位移） | ✅ | ✅ | ⛔ | ⛔ | ⛔ |
| 不透明度 / 混合模式 | ✅ | ✅ | ⛔ | ⛔ | ⛔ |
| 音量 / 增益 / 淡入淡出 / 降噪 | ⛔ | ⛔ | ⛔ | ⛔ | ✅ |
| 调色（滤镜/LUT/调节） | ✅ | ✅ | ⛔ | ✅ | ⛔ |
| 速度 / 变速曲线 | ✅ | ⛔ | ⛔ | ⛔ | ✅ |
| 遮罩 / 抠像 | ✅ | ✅ | ⛔ | ⛔ | ⛔ |
| 文字样式（字号/颜色/描边） | ⛔ | ⛔ | ✅ | ⛔ | ⛔ |
| 波形/包络 | ⛔ | ⛔ | ⛔ | ⛔ | ✅ |

### 为什么字幕不能直接套 8 项（三条真实路径，按成本排序）

**方案 A：字幕转成「视频片段」（推荐）**
把字幕渲染成带 alpha 的 RGBA 位图，作为 `Video` 轨上的一个 Clip。此后 8 项、调色、遮罩全部自动可用，**零新增渲染代码**。
- 代价：字幕不再可编辑文本（需保留原始 cue 作为「源」，编辑时重新栅格化）
- 这正是剪映/AE 的做法：文字层本质是栅格化图层

**方案 B：字幕单独走一遍 applyXformStack**
在 UI 层栅格化字幕 → 交给内核 `applyXformStack` → 返回位图。
- 需要新增一条「字幕位图化」链路（内核当前无字体渲染）
- 工作量大于方案 A

**方案 C：直接把 xform 加到 `frame.subtitles`（❌ 不可行）**
`SubtitleCue` 只有文本与样式，没有像素；且渲染在 UI 层，不在内核。

### 为什么调整图层不能挂 xform

调整图层当前是 `Track::adjustmentEffects`（轨道级、作用于整帧），**不是 Clip**。它没有「源像素」可供几何变换——变换谁？只能变换整帧，而那与「给某个素材加效果」的语义不符。

**若要支持「调整图层也能几何变换」**，需先把调整图层改成「作用于其下所有视频层并输出整帧」的独立渲染节点，属架构改动，**不在本次范围**。

### UI 侧实现（前端）

点选素材后按类型过滤功能栏，**做成数据驱动，不散落在各处 `if`**：

```js
// mobile/assets/l3-spec.js
const CAPABILITY = {
  video:      new Set(['xform','opacity','blend','color','speed','mask','keying','volume']),
  image:      new Set(['xform','opacity','blend','color','mask','keying']),
  subtitle:   new Set(['text','color']),
  adjustment: new Set(['color']),
  audio:      new Set(['volume','gain','fade','denoise','speed','waveform']),
};
function canUse(assetKind, feature) { return CAPABILITY[assetKind].has(feature); }
```

**必须做到两点**（否则比灰掉更糟）：
1. 不可用的功能**隐藏或灰掉 + 长按提示原因**，不做成「点了没反应」
2. 已添加但随后切换素材类型导致不可用 → **保留数据但不渲染**，不可静默删除用户配置

### 调整图层开放的前置条件

`track.h:22` 明确写着「等 B2-6 落地后再开放，UI 侧也应禁止用户创建」。当前如果强制开放，用户能建调整图层但放不进素材，是**假入口**。

**建议**：本次只做「调整图层 → 调色类」可用（已实现），几何变换明确标注不支持。

---

## 4.6 文本（字体）素材化 —— 架构改造

### 目标（用户原话归纳）

「工具栏 → 字体 → 二级工具栏 → 新建文本」创建后，**文本作为素材直接显示在轨道上**，与视频/图片同等地位：
- 可用 8 项几何变换
- 可加动画、打关键帧、调曲线
- 字幕轨就是文本轨，二者统一

### 现状（实测，必须先看清）

| 项 | 现状 | 位置 |
|---|---|---|
| 字幕存放 | `Sequence::subtitles`，**独立集合，不在轨道里** | `subtitle.h` |
| `TrackType::Text` | ❌ **不存在**（只有 Video/Audio/Subtitle/Adjustment） | `track.h:13` |
| 字幕像素 | ❌ 无。只把文本塞进 `frame.subtitles`，**前端 HTML 画** | `preview.cpp:227` |
| 字形栅格化 | ❌ 全仓库零 `glyph`/`FreeType`/`stb_truetype` | — |
| `nc_add_subtitle` | ❌ **不存在**（只有 count/get/remove） | `capi.h:328` |
| 「新建文本」提交 | ❌ `commit()` **无 `newtext` 分支**（grep = 0） | `l3-panel.js:731` |
| 字体文件 | ❌ `fontFamily` 只是字符串，不指向字体文件 | `subtitle.h:17` |
| 镜头追踪 | `SubtitleCue::tracking` 存在 | `subtitle.h:39` |

### 结论

**「新建文本」当前是空壳**：点开只有 4 个样式滑块（字号/颜色/描边/对齐），没有输入框、没有创建动作、没有渲染。要达成目标必须补齐整条链路。

### 改造方案

#### 第 1 步：数据模型 —— 文本成为 Clip

新增 `TrackType::Text = 4`（**数值必须追加，不可插入**，否则历史工程轨道错位）。

```cpp
// track.h
enum class TrackType {
    Video = 0, Audio = 1, Subtitle = 2, Adjustment = 3,
    Text = 4,   // 新增：文本轨（值不可变，只能追加）
};

// 新增 TextContent 数据，挂在 Clip 上（与 effects/xforms 同级）
struct TextContent {
    std::string text = "";              // 文本内容
    std::string fontPath = "";          // 字体文件路径（空 = 内置默认）
    SubtitleStyle style;                // 复用现有样式结构
    // 镜头追踪迁移至此（原 SubtitleCue::tracking）——迁移后与原子幕互不干涉
    std::vector<SubtitleCue::TrackingPoint> tracking;
    int trackingMode = 0;
};
// Clip 增加：std::optional<TextContent> text;
```

**为什么要迁到 Clip**：用户已确认「迁移过去的是独立的，互不干涉更好」。迁移后：
- 文本享有 Clip 的全部能力（8 项变换、动画、关键帧、调色、遮罩）
- 原 `Sequence::subtitles` 保留用于 SRT 导入/导出（作为批量数据源），**但不参与像素渲染**

#### 第 2 步：字形栅格化（核心）

**必须做，否则文本没有像素，8 项变换/动画/关键帧全部无从谈起。**

**已定案：走平台侧**（Android `Canvas` / iOS `CoreText` / Windows `DirectWrite` / macOS `CoreText`）。
选型理由与三端分工见 **§4.10**（那里有完整论证，本节不重复）。

栅格化产物：带 alpha 的 RGBA8 位图 + 基线偏移，作为 `Video` 轨的输入源参与后续全部管线。

**缓存**：文本/样式/字号任一变化才重新栅格化，按 `hash(text+style+font+平台标识)@导出尺寸` 缓存；预览复用同一份位图缩放。

#### 第 3 步：C ABI 补齐

```c
// 新增（当前完全缺失）
int32_t nc_add_text_clip(NcEditor*, int32_t track_index, int64_t start_us,
                         int64_t duration_us, const char* text,
                         const char* font_path, double font_size, uint32_t color_rgba);
int32_t nc_set_text_content(NcEditor*, int32_t track_index, uint64_t clip_id,
                            const char* text, const char* font_path,
                            double font_size, uint32_t color_rgba);
int32_t nc_get_text_content(const NcEditor*, int32_t track_index, uint64_t clip_id,
                            char** out_text, char** out_font_path,
                            double* out_font_size, uint32_t* out_color_rgba);
```

#### 第 4 步：前端「新建文本」补全

现状 `l3-spec.js:276` 只有 4 个样式控件，缺输入框与创建动作。补齐：
- 文本输入框（`textarea`，多行）
- 字体选择（列出 `assets/fonts/` 下可用字体）
- 起始/时长（默认：当前播放头，3 秒）
- 「添加」按钮 → 调 `nc_add_text_clip` → 轨道出现 Clip → 自动选中

#### 第 5 步：统一字幕轨与文本轨

- 文本 Clip → `TrackType::Text` 轨
- SRT 导入的字幕 → 批量转成多个文本 Clip（可选，保留原 `subtitles` 供导出 SRT）
- 二者共用同一渲染路径

### 验收标准

| 项 | 断言 |
|---|---|
| 创建 | 点「添加文本」→ 轨道出现 Clip → 预览区看到文字 |
| 编辑 | 改文字 → 预览实时更新 |
| 8 项变换 | 文本 Clip 可加旋转/偏移等，画面可见 |
| 动画 | 文本 Clip 可打关键帧（透明度/缩放/位置/旋转） |
| 导出 | 成片包含文本，且与预览一致 |

---

## 4.7 调整图层 —— 开放全部功能

### 目标（用户原话）

「调整图层相当于一个调节的图层，可以在里面加任何功能，之后应用到它下面的所有轨道。」

### 现状与阻碍

| 项 | 现状 | 位置 |
|---|---|---|
| `TrackType::Adjustment` | 值 = 3，已存在 | `track.h` |
| 放置 Clip | ❌ **`validateClipPlacement` 拒绝任何 Clip 落入** | `track.h:22` 注释 |
| 渲染 | ✅ 已实现：叠加 `adjustmentEffects` 到整帧 | `preview.cpp:397` |
| 支持能力 | 只有特效栈 + LUT | `track.h:99` |

**核心阻碍**：调整图层不是 Clip，**没有对象可挂 8 项变换 / 动画 / 关键帧**。

### 改造方案：给 Adjustment 轨一套与 Clip 平行的参数

不改成「放 Clip」，而是：

```cpp
// track.h — Adjustment 轨专用
struct AdjustmentLayer {
    // 已有
    EffectStack effects;
    std::string lutPath;
    double lutStrength = 1.0;

    // 新增：8 项几何变换（作用于整帧）
    XformStack xforms;

    // 新增：图层自身视觉参数
    double opacity = 1.0;

    // 新增：动画通道（与 ClipAnimation 同构，但独立实例）
    ClipAnimation animation;
};

// Track 增加：AdjustmentLayer adjustment;
```

**渲染顺序**（扩展现有 `preview.cpp:397` 循环）：

```
1. 下方所有视频层已合成完毕
2. 应用 xforms（整帧几何变换）
3. 应用 opacity（整帧 alpha）
4. 应用 effects（已有）
5. 应用 LUT（已有）
```

**关键约束**：调整图层作用于**其下方所有轨道**，必须按轨道下标从上到下依次叠加。

### 关键帧支持

`ClipProperty` 已有 6 通道（Opacity/Scale/PositionX/PositionY/Rotation/Volume）。调整图层需**独立关键帧容器**，C ABI 需新增 `nc_adjustment_keyframe_*`（现有 `nc_insert_keyframe` 需要 `clip_id`，而调整图层无 clip）。

### 验收标准

| 项 | 断言 |
|---|---|
| 叠加 | 调整图层加调色 → 其下方所有视频层都变色 |
| 顺序 | 两层调整图层叠放，上层后应用 |
| 8 项变换 | 加旋转 → 整帧旋转 |
| 关键帧 | 打透明度关键帧 → 随时间淡变 |
| 不越界 | 不影响其**上方**轨道 |

---

## 4.8 「复制背景」—— 填充背景的第 4 种模式

### 目标（用户原话）

「复制背景是一个功能，当用户添加填充背景那个画布的时候用户就可以选择复制背景这个效果，然后用户也可以在这个填充背景画布这个是颜色随机的也可以选择，在它上面打关键帧，可以应用下面工具栏里面的所有功能。」

### 现状（实测）

`background_fill.h:32` 已有三档，**互斥**：

```cpp
enum class BackgroundMode {
    None = 0,   // 未设置
    Color = 1,  // 颜色（可为随机色）
    Blur = 2,   // 模糊（素材本身放大模糊铺满）
    Style = 3,  // 画布样式（16 种程序化纹理）
};
```

其中 `Blur` 已经是「用素材自身画面做背景」——**与「复制背景」最接近，但不完全相同**：
- `Blur`：取**当前片段自己的画面**，放大模糊
- **复制背景**：取**当前帧下方所有轨道的合成结果**（可以是多层的合成画面）

### 方案：新增第 4 档

```cpp
enum class BackgroundMode {
    None = 0,
    Color = 1,
    Blur = 2,
    Style = 3,
    CopyBackground = 4,   // 新增：复制下方合成画面作背景
};
```

**关键差别（必须说清）**：`Blur` 只复制本片段，`CopyBackground` 复制**下方所有轨道叠加后的完整画面**。所以竖屏视频放进横屏工程、下方有多层时，`CopyBackground` 得到的是完整背景，`Blur` 只有本层。

### 调节参数

| 参数 | 范围 | 默认 | 说明 |
|---|---|---|---|
| 不透明度 | 0–100% | 100% | 快照整体透明度 |
| 模糊 | 0–50px | 12 | 背景虚化（最常用） |
| 缩放 | 50–200% | 110 | 放大避免边缘露出 |
| 亮度 | -100–100 | 0 | 压暗/提亮背景 |
| 饱和度 | 0–200% | 100 | 去色做灰度背景 |
| 填充模式 | 适应/填充/拉伸 | 填充 | 快照与画布比例不符时 |
| 边缘羽化 | 0–100 | 0 | 快照边缘柔化 |

**为什么默认模糊 12 + 缩放 110**：这是「竖屏素材进横屏工程」最常用组合 —— 放大避免边缘露出，模糊让主体不抢视线。剪映里就是这个默认。

### 可打关键帧

用户明确要求「在它上面打关键帧」。因此这 7 个参数需全部接入 `ClipProperty` 或新增通道：

```cpp
enum class ClipProperty {
    Opacity, Scale, PositionX, PositionY, Rotation, Volume,
    // 新增（背景填充专用）
    BgOpacity, BgBlur, BgScale, BgBrightness, BgSaturation, BgFeather,
};
```

`Color` 模式的色相/亮度同样可打关键帧（「随机颜色」也是 Color 模式的一个生成方式）。

### 「应用下面工具栏的所有功能」

用户要求背景填充画布也能用全部功能。技术上：
- 背景填充的产物是**片段级别的背景层**，随片段一起参与后续合成
- 因此 8 项变换、调色、特效栈**自动作用于整个片段画面（含背景）** —— 无需额外实现
- 若希望「只作用于背景、不影响素材」，需拆成独立图层（属 §4.7 的调整图层范畴）

### 验收标准

| 项 | 断言 |
|---|---|
| 模式互斥 | 选 CopyBackground 后 Color/Blur/Style 自动取消 |
| 画面 | 竖屏素材 + 横屏工程 → 背景为虚拟画面的模糊放大版 |
| 多层 | 下方 3 层合成 → 背景是 3 层合成结果，不是单层 |
| 关键帧 | 背景模糊随时间变化 |
| 导出 | 与预览一致 |

---

## 4.9 「填充」入口与「填充整个预览区」（用户补充需求）

> 本节是 §4.8 的补充，**不改变 L1 结构**。

### 需求（用户原话归纳）

1. **L1「背景」不改名**；「填充」作为**二级项**加入 `背景` 之下
2. 用户点「填充」时，**若未选颜色则默认随机填充**（不新增随机按钮，默认即是随机）
3. 填充调节面板已有一个**「拉伸」**；在同一面板再加一个 **「填充组合区域」**按钮，点击后**填满整个预览区**

### 现状（实测）

| 项 | 现状 | 位置 |
|---|---|---|
| L1「背景」 | ✅ 存在，值 = `bg` | `editor.js:238` |
| 二级菜单 | 已有 [颜色 `bgcolor`] [模糊 `bgblur`] [画布样式 `bgstyle`] | `editor.js:318-322` |
| 三个面板 | ✅ 已存在 | `l3-spec.js:378-380` |
| 「拉伸」参数 | ✅ 已有：`FrameFit::Crop`（1:1 不缩放居中） | `frame_fit.h:28` |
| 随机色 | ❌ 无按钮（**按用户要求不加按钮**，改为默认行为） | — |
| 「填充组合区域」 | ❌ 不存在 | — |

### 方案

#### ① 「填充」作为二级项

```
L1「背景」
  ├─ 颜色      bgcolor   （已有）
  ├─ 模糊      bgblur    （已有）
  ├─ 画布样式  bgstyle   （已有）
  └─ 填充      bgfill    （新增）
```

**「填充」与「颜色」的区别**（必须说清，否则用户不知道选哪个）：
- **颜色**：用户**明确指定**一个颜色
- **填充**：用户**不指定**，系统随机生成一个颜色

即「填充」= 「颜色」的 **随机版**。二者写入同一个 `BackgroundMode::Color`，区别只在颜色值由谁决定。

#### ② 默认随机色

- 点「填充」时若当前没有颜色 → 生成一个随机色写入
- 随机策略：在 HSV 空间取 `H` 随机、`S` 0.25–0.55、`V` 0.55–0.80
  —— **不用纯随机 RGB**，否则会出现刺眼的高饱和色（红/纯蓝）在深色 UI 里极难看
- 生成后即为普通颜色，用户可继续用取色器改

#### ③ 「填充整个预览区」按钮

**含义**：让背景填充**铺满整个画布**，而不是只填素材之外的留白区。

| 现状 | 目标 |
|---|---|
| 背景只填 `Fit` 模式下的信箱/邮筒条 | 背景铺满整帧，素材浮在其上 |

实现方式：把片段的 `FrameFit` 从 `Fit` 切到 `Fill`（等比放大铺满，溢出裁切），同时背景层按整帧铺满。

**与「拉伸」的关系**（两者都是 `FrameFit`，但语义不同，必须区分）：

| 参数 | 对应 `FrameFit` | 效果 |
|---|---|---|
| **拉伸** | `Crop` | 素材 1:1 不缩放，居中 |
| **填充组合区域** | `Fill` | 素材等比放大铺满，裁切溢出 |
| （默认）适应 | `Fit` | 素材完整显示，留白处填背景 |

**注意**：`Crop` 与 `Fill` 互斥，点「填充组合区域」会覆盖当前「拉伸」设置。UI 上应做成**三选一的段控件**（适应 / 拉伸 / 填充组合区域），而不是两个独立按钮 —— 否则用户能同时选中两个互斥项，产生歧义。

### 验收标准

| 项 | 断言 |
|---|---|
| 二级项 | L1「背景」下能看到「填充」 |
| 默认随机 | 点「填充」后画面立刻有颜色（非黑） |
| 随机不刺眼 | 生成的色 S ≤ 0.55、V ≤ 0.80 |
| 铺满 | 点「填充组合区域」后背景覆盖全部画布，无黑边 |
| 互斥 | 「拉伸」与「填充组合区域」不能同时选中 |

---

## 4.10 三端栅格化架构（最终定案）

> 本节取代 §4.6 第 2 步的路线比较。**已定案，后续不再修改。**

### 定案

**文本栅格化走平台侧**（Android `Canvas` / iOS `CoreText` / Windows `DirectWrite` / macOS `CoreText`），栅格化结果**缓存并作为 Clip 的源输入**。

### 为什么（修正此前的错误论证）

此前我把「三端像素完全一致」当作选 `stb_truetype` 的理由 —— **这是错的**，与本方案 §1 的代理渲染自相矛盾：预览本来就降分辨率。

正确的一致性边界：

| 层面 | 是否必须一致 | 原因 |
|---|---|---|
| **算法 / 几何 / 时序** | ✅ 必须一致 | 同一时刻同一参数，构图必须相同 |
| **像素级渲染细节**（字形 hinting、抗锯齿） | ❌ **不必一致** | 预览走代理分辨率，降质是预期行为 |
| **导出质量** | ✅ 最高 | 用户最终看到的成片 |

因此选型标准是「**导出质量最高 + 实现成本最低**」，平台侧胜出：系统字体引擎有完整 hinting/字距/抗锯齿，质量高于任何内置软渲染器。

### 三端分工

| 平台 | 栅格化 API | 输出 |
|---|---|---|
| Android | `Canvas.drawText` / `StaticLayout` | ARGB_8888 Bitmap |
| iOS | `CoreText` + `CGContext` | `CVPixelBuffer` / RGBA |
| Windows | `DirectWrite` + `D2D1RenderTarget` | DIB / RGBA |
| macOS | `CoreText` + `CGBitmapContext` | RGBA |

统一接口（内核侧只认位图，不关心来源）：

```cpp
// 平台层实现，内核通过回调取用
struct TextRasterizer {
    virtual bool rasterize(const TextContent& text, int maxWidth, int maxHeight,
                           std::vector<std::uint8_t>& outRgba,
                           int& outW, int& outH) = 0;
};
```

### 缓存策略

- 缓存键：`hash(text + fontPath + fontSize + color + outline + 平台标识)@目标尺寸`
- 目标尺寸取**导出分辨率**（预览用同一份位图缩放，不重新栅格化）
- 缓存条目上限按字节预算（与帧缓存一致，见 §6.5）

### 一致性保证（唯一硬约束）

**几何与时序必须一致**：同一 `timeSeconds`、同一参数，三端算出的变换矩阵必须逐位相同。这条由 `xform.cpp` 的纯数学实现保证（无平台依赖），已有单测覆盖。

**像素细节不追究**：字形的抗锯齿边缘允许略有差异，不属于缺陷。

---

## 5. 借鉴剪映 / AE

### 5.1 剪映（CapCut）

| 做法 | 我们的落点 |
|---|---|
| **代理渲染是自动多档，不是固定二档**：先 1.0，超时降 0.75，再超时降 0.5 | P2 的自动档策略 |
| **档位有记忆**：记住该工程能跑的档位，下次打开直接用，不逐帧试探 | `.nclip` 持久化 `previewScale` |
| **「预览画质」是用户可见设置**，不是偷偷降质 | 设置项「流畅/标准/高清」 |
| **拖动时跳帧、松手补精确帧** | `allowFrameDrop` + 暂停回 scale=1.0 |
| **缩略图与预览共用渲染管线**，只是尺寸不同 | 缩略图走同一 `renderFrameTransformed`，只改 dst 尺寸 |
| **效果面板缩略图是预渲染静态资源**，不实时算 | 我们的 `xformThumb()` 用 CSS 画示意 —— **方向正确，不要改成实时渲染** |
| **「功能」类特效本质是几何变换，在合成阶段做** | 正是 §2 的决策依据 |

### 5.2 After Effects

| 做法 | 我们的落点 |
|---|---|
| **Collapse Transformations（塌陷变换）**：连续几何变换合并，只重采样一次 | §2 的 `Affine2D::then()` 链式合并 |
| **分层缓存 + 磁盘缓存**：改一层只重算该层 | 帧缓存粒度从「整帧」改为「按 clip + 参数哈希」 |
| **持续栅格化**：缩放时按当前尺寸重新采样而非放大位图 | `renderFrameTransformed` 每帧按目标尺寸重采样（而非缓存缩放后的位图） |
| **16/32bit 中间缓冲**：多层叠加 8bit 会 banding | 中间缓冲升 16bit（见 §6.2） |
| **运动模糊基于快门角度**：快门角度 → 叠帧数 → 加权混合 | `motion_effects.h` 的 5.5 已按此设计，保持 |

---

## 6. 我认为方案仍未覆盖的优化点

按「收益 ÷ 成本」排序，供决定是否追加。

### 6.1 分层失效（收益最大，建议纳入 P2）

**现状**：`editRevision` 一变，整个帧缓存清空。改一个字幕颜色 → 5 层视频全部重新解码 + 渲染。

**改法**：每个 clip 计算「参数指纹」（`clipId + 源入点 + 特效栈哈希 + xform 哈希 + 遮罩哈希`）。缓存键从「时间戳」改为「时间戳 + 该帧所有可见 clip 的指纹组合」。任一 clip 指纹不变则复用其渲染结果（中间层缓存，非最终帧）。

**预期**：拖调色滑块场景提速 5–10 倍。**这比多线程的收益更大**，因为省掉的是整段工作而不是并行化。

### 6.2 16bit 中间缓冲（画质问题，非性能）

8bit 下「调色 → 混合 → 再调色」会产生 banding（天空渐变出现色阶断层）。中间缓冲升 16bit 后，最终输出再降到 8bit。

成本：内存 ×2、带宽 ×2。建议**只在导出路径启用**，预览保持 8bit（屏幕上分辨不出）。

### 6.3 解码与渲染流水线化

现在是「解码 → 渲染 → 传输」串行。应改为：解码线程预取 N 帧（N=4）进队列，渲染线程消费。**流畅度提升比单纯提速更明显**，因为消除了解码等待的毛刺。

P0 埋点会显示 decode 段占比；若 > 30%，此项优先级应提到 P3 之前。

### 6.4 缩略图独立路径消除

需确认时间轴缩略图是否走独立渲染。若是，改为复用主管线（只改 dst 尺寸），可省一份完整开销。

### 6.5 AE 式分层缓存 / 磁盘缓存能否搬到移动端

**结论：机制能搬，形态必须改。照搬桌面参数会 OOM 或拖死 I/O。**

#### 桌面 vs 移动端的硬差距

| 维度 | 桌面（AE） | 移动端 | 倍数 |
|---|---|---|---|
| 可用内存 | 32–128 GB | 前台通常 1–4 GB（iOS jetsam 更严，2–3GB 机型约给 1.2–1.5GB） | 10–30× |
| 顺序读 | NVMe 3–7 GB/s | UFS 2.1 ≈ 300–800 MB/s，UFS 3.1 ≈ 1.5–1.8 GB/s | 3–20× |
| 随机写 | 极强 | 弱，且小块写放大明显 | — |
| 闪存寿命 | 不受限 | TBW 有限，持续大写入加速老化 | — |
| 后台存活 | 一直活 | LMK / jetsam 随时回收 | — |

#### 为什么不能照搬

4K RGBA 一帧 = 3840×2160×4 = **33 MB**。

- 缓存 10 帧 = 330 MB → 吃掉中端机半个可用内存
- 缓存 100 帧 = 3.3 GB → 直接 OOM

磁盘侧更糟：8 MB/帧 × 25 fps = **200 MB/s 持续写入**。UFS 2.1 的随机写只有几十 MB/s，会直接卡住解码线程；同时半小时产生 360 GB 写入量，远超多数手机闪存的长期承受能力。

**所以：播放期间绝对不能把帧写盘。**

#### 移动端可用的三级形态

| 级 | 内容 | 分辨率 | 上限 | 触发/淘汰 |
|---|---|---|---|---|
| **L0 解码帧** | 某 clip 已解码的源帧 | 源分辨率 | **2–4 帧** | 距播放头最近者保留；解码是最贵操作，缓存收益最高 |
| **L1 分层渲染** | 某 clip 应用完特效+xform 的图层 | **代理分辨率** | 按字节预算（建议 192 MB） | 键 = `clipId + 源入点 + 特效指纹 + xform 指纹` |
| **L2 磁盘** | L1 的结果持久化 | 代理分辨率 | **预分配 256 MB，环形覆盖** | **仅在暂停 >300ms 且无触摸时写** |

L1 是核心：540p 一层 2.07 MB，3 层 × 30 帧 ≈ 186 MB，可行。它直接对应 AE 的 Layer Cache，也是「拖调色滑块不卡」的真正来源 —— 改字幕时视频层指纹未变，直接读 L1。

#### 两端差异必须处理

| 项 | Android | iOS |
|---|---|---|
| 内存压力回调 | `onTrimMemory()` / `ComponentCallbacks2` | `didReceiveMemoryWarning` |
| 内存上限线索 | `ActivityManager.getMemoryClass()` / `getLargeMemoryClass()` | `os_proc_available_memory()`（iOS 13+） |
| 缓存目录（系统可清） | `context.cacheDir` | `NSCachesDirectory` |
| 分层缓存容器 | 自管（`LruCache` 按字节） | `NSCache`（自动响应内存警告） |
| 硬件缓冲 | `AHardwareBuffer`（API 26+） | `CVPixelBuffer` + `IOSurface` |
| 被系统回收 | LMK 随时 | jetsam 随时 |

#### 一个移动端特有的关键手段

**用 `mmap` 文件后备（file-backed）承载 L2，而不是 `malloc` 大块堆内存。**

理由：内存压力时系统可以直接回收 page cache，而不是把整个进程杀掉。纯堆缓存（`std::vector` 上千帧）在 iOS 上是 OOM 被杀的典型死法；file-backed + `mmap` 则是「变慢但活着」。

**同时把 L1 也改为按字节预算 + LRU**（与现有 `preview.h` 的 `kFrameCacheBudgetBytes` 做法一致），不按帧数 —— 否则源分辨率一变，内存占用就失控。

#### 明确不可行

| 做法 | 结论 |
|---|---|
| 缓存全分辨率渲染帧（33 MB/帧） | ❌ 10 帧即 OOM |
| 播放中写磁盘 | ❌ 200 MB/s 持续写入，卡顿 + 伤闪存 |
| 磁盘缓存无上限增长（AE 的几十 GB） | ❌ 违反移动端存储配额，且被系统清理概率高 |
| 用 `AVAssetImageGenerator` / `MediaMetadataRetriever` 做序列解码 | ❌ 为单帧设计，逐帧调用开销极大，不能当解码缓存 |

#### 建议纳入的时点

- **L1 分层渲染缓存**：并入 **P2**（与分层失效同批）。这是收益最大的一步，且不引入新平台依赖 —— 纯 `core/` 内改动。
- **L2 磁盘缓存**：放 **P6 之后**。它需要平台层配合（目录、内存压力回调），且收益低于 L1。**先不做，留接口。**


### 6.6 效果顺序归一化

用户可能加「旋转 + 偏移 + 旋转」三个效果。同一帧内应按「仿射可交换群」合并同类项（旋转之间可合并为单次旋转），减少矩阵乘法次数。属微优化，放最后。

---

## 7. 风险与明确不做的事

### 风险

| 风险 | 缓解 |
|---|---|
| 多线程浮点求和顺序变化导致结果不一致 | 条带切分**不跨条带累加**，每像素独立计算，天然 bit-exact |
| NEON 路径与标量路径结果漂移 | 双路径单测逐字节比对，CI 强制 |
| SurfaceView 层级问题导致预览错位/黑屏 | P6 单独分支、单独回滚；提供「降级回 PNG 路径」开关 |
| 代理渲染被用户感知为「画质变差」 | 用户可见开关 + 导出全质量 + 全质量预览一帧入口 |
| 硬解在部分机型输出异常色彩 | 软解兜底 + 首帧校验（解码首帧与软解首帧比对，差异过大则切软解） |

### 明确不做

- ❌ 不做「把全部滤镜重写成 GLSL」—— 双份维护 + 逐像素一致性测试成本过高；
- ❌ 不引入第三方图像库（OpenCV 仅用于追踪，不用于渲染）；
- ❌ 不为了性能牺牲「预览=导出」的一致性：**代理渲染只改分辨率，不改算法分支**；
- ❌ 不做假的进度/假的状态：8 项效果未完成前，`ready: false` 保持「待接入」，不改成可点但无效果。

---

## 8. 排期汇总

| 阶段 | 内容 | 工期 | 依赖 |
|---|---|---|---|
| P0 | 性能埋点 | 0.5d | — |
| P1 | LUT 整型查表 | 0.5d | — |
| P2 | 代理渲染 + 丢帧 + 分层失效 | 1.5d | P0 |
| P3 | CPU 多线程 | 1.5d | P0 |
| P4 | NEON 核心循环 | 2d | P3 |
| P5a | 8 项效果 **内核**（Affine2D 合并 + 8 类实现） | 3d | P2 |
| P5b | 8 项效果 **UI**（8 张 SPEC 表 + 2 控件 + ABI/JNI） | 2d | P5a |
| P6 | SurfaceView 零拷贝 | 3–5d | P2 |
| P7 | 硬解 + GPU 合成 | 1w+ | P6 |
| **P8** | **文本素材化**（TrackType::Text + 字形栅格化 + 3 个 ABI + 前端输入框） | **4–6d** | P2 |
| **P9** | **调整图层全功能**（XformStack + animation + 独立关键帧 ABI） | **2–3d** | P5a |
| **P10** | **复制背景**（BackgroundMode 第 4 档 + 7 参数 + 关键帧） | **2d** | P2 |

**P0–P5b 全在 `core/` + `mobile/assets/`，不碰 Kotlin 层级，风险可控。做完后 1080p 3 层日常场景达标、12 项转换面板全部可用。**
**P6 起动平台层，需单独验证真机。**

> 注：P5 原为单阶段 3d，拆成 P5a/P5b 是因为内核与 UI 的验证手段完全不同 —— 内核靠单测断言，UI 靠真机点击。混在一起做，出问题时不好定位。

