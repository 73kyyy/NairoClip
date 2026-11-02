# NairoClip 互动引擎架构（Interaction Engine）

> **本文是自包含的开发任务书** —— 单读本文即可执行，不需要其它文档。
> 姊妹篇：`docs/PERF_XFORM_PLAN.md`（渲染管线性能，已写透单帧 CPU 耗时）。
> 本文处理它**没有覆盖的那一半**：界面怎么在 16ms 内响应手指。
>
> 状态：全部待开发（`§0` 的现状结论已实测）。
> **本文所有「现状」结论均来自对仓库的实际检索与代码阅读，非推测**；
> 每条断言都标了 `file:line`，可逐条复核。
>
> 最后更新：2026-11-02 · HEAD `e2c4b05`

---

## 0. 为什么需要本文：两个文档写的是不同的问题

`PERF_XFORM_PLAN.md` 把「流畅」定义成**单帧 CPU 渲染耗时 ≤ 40ms**。
这个定义对导出是充分的，对编辑界面是**不充分**的 —— 用户感知的卡顿有三个来源，
渲染耗时只是其中最小的一个：

| 卡顿来源 | 由谁决定 | `PERF_XFORM_PLAN.md` 覆盖了吗 |
|---|---|---|
| 一帧像素算得慢 | core 的 C++ 逐像素管线 | ✅ 覆盖到极致（P0–P7） |
| 一次拖动重建了整个时间轴 DOM | 前端 `editor.js` | ❌ 完全没提 |
| 预览帧以字符串形式阻塞 JS 线程 | JNI 桥 + Kotlin | ⚠️ **§0 已记录现象**（「PNG + Base64 跨 JNI」`PERF:46`、「`nativeFrame(width)` 声明未用」`PERF:47`），但只当现状条目记，没有调用链拆解、没有异步化方案；P6 讲的是传输通道替换（SurfaceView），不是「同步桥阻塞 JS 线程」这个根因 |
| 面板打开时同步构建 300 个 DOM 节点 | `l3-panel.js` | ❌ 完全没提 |
| 手势互相抢（捏合 vs 平移 vs 拖片段） | 前端 | ❌ 完全没提 |

> 本文与 `PERF_XFORM_PLAN.md` 的分工：**它记录现象且负责像素侧；本文把前端/桥侧的每条现象追到代码行、给出根因判断与实施规格。**
> 两处重叠项（PNG 链、`width` 参数）本文不复述现象本身，只补「调用链 + 为什么它是阻塞的 + 怎么改」。

**核心判断：当前编辑器的卡顿主要不来自渲染，来自 DOM 结构重建。**
证据在 §0.2，可以直接数出来。

---

## 0.1 现状实测总表

| 项 | 实测值 | 位置 | 影响 |
|---|---|---|---|
| `editor.js` 规模 | **4100 行，全全局函数** | `mobile/assets/editor.js` | 无模块边界，改一处怕影响别处 |
| `l3-panel.js` 规模 | **1790 行** | `mobile/assets/l3-panel.js` | 同上 |
| 前端构建体系 | ❌ 无 `package.json` / 无打包 / 无 lint | `mobile/` | 只能靠人工审查 |
| 拖动片段时每帧做的事 | **全量拆除 + 重建全部轨道行** | `editor.js:1516` | 见 §0.2 ① |
| 时间轴平移时每帧做的事 | **同上（全量重建）** | `editor.js:1618` | 见 §0.2 ② |
| 双指缩放时每帧做的事 | **同上（全量重建）** | `editor.js:1707, 1725` | 见 §0.2 ③ |
| 播放时每 1.5s | **一次全量重建**（兜底纠偏） | `editor.js:1878` | 周期性卡顿毛刺 |
| 播放时每帧 | 遍历所有轨道×所有片段写 `style.left/width` | `editor.js:1889` | 见 §0.2 ④ |
| 离屏片段处理 | 建节点 + `visibility:hidden`，**不回收** | `editor.js:1271` | 1000 片段 = 1000 个 DOM 节点常驻 |
| 音频波形 | **每个片段造最多 220 个 `<i>`** | `editor.js:1285-1301` | 300 个音频片段 ≈ 6.6 万节点 |
| 时间轴缩略图 | **每片段每次重建都同步调桥** | `editor.js:1276` | 见 §0.2 ⑤（最大嫌疑） |
| 缩略图缓存 | ❌ 引擎无，JS 无 | `capi.cpp:4151`（每次 `FFmpegMediaReader reader;`） | 每次 open+seek+decode |
| 预览帧传输 | **同步 JNI → Bitmap → PNG(100) → Base64 → `data:` URL** | `NativeEngineBridge.kt:211-250` | 见 §0.2 ⑥ |
| 预览帧像素拷贝次数 | **4 次全帧 + 2 次逐像素转换** | `preview.cpp:215` / `capi.cpp:4058` / `nc_jni.cpp:1422` / `.kt:238,246` | 420×746 ≈ 1.2MB/次 |
| `frameWDrag` 降质 | ⚠️ **Android 上是空操作**（宽度传到 JNI 但没用） | `nc_jni.cpp:1396`；`capi.h:395` 无 width 参数 | 见 §0.2 ⑦ |
| 桥调用并发 | 全部 `@Synchronized`，跑在 JavaBridge 线程 | `NativeEngineBridge.kt:109,115` 等 | 同步桥 = 阻塞 JS 线程 |
| 帧缓存失效粒度 | **任何编辑 → 清空全部帧缓存** | `capi.cpp:4003-4017` | 改一个字幕 → 全部重解码 |
| 预览层数 | **只渲染第一个可见视频轨就 `break`** | `preview.cpp:16-41` | 预览 ≠ 导出（导出支持多层） |
| 面板打开顺序 | 先显示面板 → 再 `renderBody()` 同步建 DOM | `l3-panel.js:144-150` | 见 §0.2 ⑧ |
| 面板动画 | `translateY` 260ms；遮罩无过渡 | `editor.css:636-648` | 遮罩突变 + 面板滑入 |
| `will-change` 使用 | **全仓库 1 处** | `editor.css:411` | 关键图层未提升 |
| `contain` 使用 | **全仓库 1 处（仅 `.canvas`）** | `editor.css:68-69` | 轨道区未隔离布局 |
| 虚拟化 / 节点池 | ❌ 无 `IntersectionObserver` / `ResizeObserver` / `content-visibility` | 全仓库 0 命中 | — |
| 布局读取（`getBoundingClientRect`） | **10 处，其中 3 处在事件热路径** | `editor.js:1494, 2612, 2613` | 强制同步布局 |
| 前端性能埋点 | ❌ 零（无 `performance.mark` / `longtask`） | 全仓库 0 命中 | 无法证明优化有效 |
| 前端 CI 门禁 | 仅括号配平 / 图标存在性 / `data-act` 分支 | `tools/check_mobile_frontend.py` | 无任何性能回归门 |
| 平台侧优化 | `LAYER_TYPE_HARDWARE` / 黑底 / 关回弹 / API33 提优先级 | `MainActivity.kt:73-91` | 已做，但只到这一层 |
| **预览多层合成** | ❌ **只渲染第一个可见视频轨** | `preview.cpp:35-36` | 预览 ≠ 导出（见 §8.1） |

---

## 0.2 八个卡顿源的代码证据

### ① 拖动片段：每次 `pointermove` 重建整个世界

`editor.js:1506-1519`：

```js
let next = Math.max(0, t0 + dx / S.pxPerSec);
...
c.start = +next.toFixed(3);
// 立即重绘受影响的两个轨道
renderTracks();          // ← 注释说「两个轨道」，实际是全量
markDropBlocked(...);
```

`renderTracks()`（`editor.js:705`）→ `renderTracksBody()`（`editor.js:975`）第一件事：

```js
let stale = E.tracks.querySelector('.trow');
while (stale) { stale.remove(); stale = E.tracks.querySelector('.trow'); }
```

**逐次 `querySelector` 摘除**（注释说「避免 querySelectorAll 快照清不干净」）——
100 个轨道行就是 100 次选择器查询 + 100 次 DOM 移除，然后全量重建。

`renderTracks()` 被 **27 处**调用（`editor.js` 内 `renderTracks();` 命中 27 次），
`renderTracksBusy` 守卫只防重入，**不减少工作量**。

一次拖动 = 60~120 个 `pointermove` × 全量重建。

### ② 时间轴平移：同样全量重建

`editor.js:1615-1620`：

```js
E.tracks.addEventListener('pointermove', (e) => {
  if (!drag) return;
  S.time = Math.max(0, drag.t - (e.clientX - drag.x) / S.pxPerSec);
  renderTracks();        // ← 全量
  syncPreview();         // ← 同步桥调用
});
```

**每移动 1px 就重建一次 + 同步取一帧预览。**

### ③ 双指缩放：同样全量重建

`editor.js:1704-1708` 与 `1723-1727`，两处 `onScale` 都是
`S.pxPerSec = ...; S.time = ...; renderTracks(); syncPreview();`。
`makePinch` 已用 rAF 节流（`editor.js:1673`），但节流后仍是全量。

### ④ 播放：每帧遍历所有片段写样式

`editor.js:1889-1912`：

```js
function scrollTimelineToTime() {
  const rows = E.tracks.querySelectorAll('.trow');      // 每帧分配 NodeList
  for (let i = 0; i < rows.length; ...) {
    const clipEls = rows[i].querySelectorAll('.clip');   // 每帧 × 每轨
    for (let j = 0; j < clipEls.length; ...) {
      el.style.left = `${x}px`;      // 每帧每片段 2 次样式写
      el.style.width = `${w}px`;
      ...
      if (el.style.visibility !== want) el.style.visibility = want;
    }
  }
}
```

3 轨 × 20 片段 = 60 元素 × 2 次写 = **120 次样式写/帧 × 60fps = 7200 次/秒**。
每次写都让该元素失效，随后一次布局把整条时间轴重算一遍。

`tick()`（`editor.js:1846`）每帧还做：

- `updateStrip()`（`editor.js:1349`）：`E.phRead.textContent = fmt(S.time)` ——
  播放头读数字号 **9px**（`editor.css` `.ph-read`），60Hz 刷新在人眼上等价于噪声，
  **纯浪费**；
- `drawRuler()` 只在整秒变化时调用（`editor.js:1863-1867`），但它内部
  `E.ruler.querySelectorAll('.ruler-label').forEach(n => n.remove())` 再逐个 `appendChild`
  （`editor.js:657, 666`）—— 每秒一次标签全量重建；
- 每 1.5s 一次 `renderTracks()` 全量重建（`editor.js:1878`）—— **播放中的周期性掉帧毛刺**；
- 隔帧 `syncPreview()`（`editor.js:1874`）—— 30fps 的同步桥调用。

### ⑤ 缩略图：在重建路径里同步解码

`buildClip()`（`editor.js:1263`）内：

```js
if (Engine.online && c.assetId) {
  const fill = document.createElement('div');
  const turl = Engine.thumbUrl(c.assetId, 0, NEED.thumbW);   // ← 同步桥
  if (turl) fill.style.backgroundImage = `url(${turl})`;
  ...
}
```

`Engine.thumbUrl` → `bridge.thumbDataUrl` → `nativeThumb` → `nc_thumbnail_generate`
（`capi.cpp:4151` 附近每次都新建 `FFmpegMediaReader` 并 open 文件：

```cpp
FFmpegMediaReader reader;
const std::vector<Thumbnail> thumbs =
    generateThumbnails(reader, asset->path, asset->duration, config);
```

`thumbnail.cpp:64-68` 内部 `seek` + `readVideoFrame`。

**没有缓存层。** 所以：一次全量重建（100 个片段）= 100 次
`open + seek + decode + PNG 编码 + Base64`。
拖动时每次 `pointermove` 都来一遍 —— **这是最大的单一卡顿源，必须先修**。

### ⑥ 预览帧：同步桥 + PNG + Base64

> `PERF_XFORM_PLAN.md:46` 已把「PNG + Base64 跨 JNI」列为现状条目（估 65MB/s 字符串）。
> 本节补的是**调用链逐段拆解**：为什么它在 JS 线程上是阻塞的、像素被搬了几次、
> 以及「P6 换通道」之外还有哪些中间档可以立刻做（§4.3.3 A/B/C）。

调用链（Android）：

```
JS syncPreview()
  → Engine.frameUrl(us, w)                    editor.js:602
  → bridge.frameDataUrl(us, w)                engine.js:108   [同步 JNI, JavaBridge 线程]
  → @Synchronized renderDataUrl               NativeEngineBridge.kt:211
  → nativeFrame(handle, timeUs, width)        .kt:216
  → nc_preview_render(...)                    nc_jni.cpp:1408
      → PreviewRenderer::renderAt()           复制整帧（preview.cpp:215 注释「返回缓存中的副本」）
      → new uint8_t[bytes] + memcpy           第 2 次拷贝（capi.cpp:4054-4058）
  → NewByteArray(header+pixLen) + SetByteArrayRegion   第 3 次拷贝（nc_jni.cpp:1416-1423）
  → IntArray(w*h) 逐像素 RGBA→ARGB            第 4 次遍历（.kt:238-244）
  → bmp.setPixels(...)                        第 5 次拷贝
  → bmp.compress(PNG, 100, out)               ★ PNG 编码（CPU，同步）
  → Base64.encodeToString(png)                ★ 字符串膨胀 4/3
  → "data:image/png;base64,..."
  → JS: E.still.src = url                     解码 PNG → 纹理上传
```

**整条链跑在 JS 线程上**（`@JavascriptInterface` 是同步调用）。
420×746 一帧 ≈ 1.2MB；PNG(quality=100) 编码 + Base64 在移动 CPU 上是
**十毫秒量级**（具体数值待 I0 阶段实测，此处为量级判断，不作为验收依据），
一次就吃掉整个 16.6ms 预算。播放时还隔帧来一次（15fps）。

坐标轴上还有一个隐藏成本：`E.still.src = url` 每次都是**新字符串**，
WebView 需要重新解码 PNG 并上传纹理，**没有任何复用**。

### ⑦ 拖动降质是空操作（真实缺陷）

> `PERF_XFORM_PLAN.md:47` 已把「`nativeFrame(width)` 参数声明了但函数体从未使用」列为现状条目。
> 本节补的是**为什么它致命**：这不是「优化没生效」，而是「拖动时唯一的降质手段不存在」，
> 直接决定 §2.4 的拖动预算能不能成立。

`editor.js:27-34` 定义了两档宽度：

```js
frameW: DPR >= 3 ? 420 : DPR >= 2 ? 320 : 282,
frameWDrag: 112          // 注释称「宽度减半」，但 420→112 实为 1/3.75
```

**顺带发现注释也算错了**：420 → 112 不是「减半」，像素数实际降到
`(112/420)² ≈ 1/14`（不是注释说的 1/4）。不过这不影响结论 —— 反正没生效。

`syncPreview()`（`editor.js:601`）按 `S.dragging` 选宽度，一路传到
`nc_jni.cpp:1396`：

```cpp
Java_..._nativeFrame(JNIEnv* env, jobject, jlong h, jlong timeUs, jint width) {
```

**函数体内 `width` 出现 0 次**（该函数 1395–1428 行内 `width` 唯一命中就是签名本身）。
`nc_preview_render`（`capi.h:395`）**根本没有宽度入参** —— `out_width/out_height` 是出参。

⇒ 无论请求 112 还是 420，引擎都按**画布原始尺寸**（如 1080×1920）渲染，
PNG 也按全尺寸编码。拖动降质的设计**从未生效**。

### ⑧ 面板打开：先显示，再同步建 DOM

`l3-panel.js:144-150`：

```js
el.panel.hidden = false;
el.mask.hidden = false;
requestAnimationFrame(() => el.panel.classList.add('open'));   // 开始 260ms 滑入

renderSeg();       // ← 同步构建分段控件
renderActions();   // ← 同步构建按钮
renderBody();      // ← 同步构建全部参数行（滑块 + 刻度线 25 个 <i> + 数值框）
```

`buildParamPanel`（`editor.js:3038-3052`）每行生成 **25 个刻度 `<i>`**；
8 行参数 = 200 个节点，全部在动画第一帧之前同步插入。
**动画开头就掉帧**，用户看到的是「panel 弹了一半卡住」。

---

## 1. 五条定案（不可偏离）

### 定案 1：帧路径上禁止调用原生桥

**同步桥是这套架构的物理上限。** 任何 `NCEngine.*` 调用都会阻塞 JS 线程，
而 `@Synchronized` 让所有桥调用串行（`NativeEngineBridge.kt:109/115/126`…）。

规则：
- 手势处理、滚动、播放的每一帧内，**桥调用次数必须为 0**；
- 需要数据时**提前取**（idle 预取）或**异步取**（回调）；
- 状态以「本地模型」为准，引擎只是持久化与权威校验端。

这条规则一立，「拖动时重建 + 同步取预览 + 同步取缩略图」这一整类卡顿
从根上消失，剩下的才轮到优化。

### 定案 2：时间轴改为 Canvas 单层绘制，DOM 只留可交互件

DOM 方案的成本是 **O(全部片段)**，Canvas 方案是 **O(可见片段)**。
1000 片段时差 60 倍以上，且这个差距**不随优化缩小**，只随节点数线性放大。

| 元素 | 归属 | 理由 |
|---|---|---|
| 刻度尺、网格线、片段矩形、缩略图条、波形、关键帧菱形 | **Canvas 2D 一层** | 只画可见区，一帧一次 `drawImage`/`fillRect` |
| 选中框、左右裁剪把手、转场按钮、播放头、吸附参考线 | **DOM 覆盖层** | 需要命中测试与焦点语义，数量恒定为 O(1) |
| 轨道头（眼睛/喇叭/锁/折叠） | **DOM** | 数量 = 轨道数（通常 < 10） |

保留 DOM 覆盖层的理由：`pointerdown` 命中测试、`:focus-visible` 无障碍、
系统长按菜单抑制（`MainActivity.kt:93-95` 已处理）都已基于 DOM 工作，
强行全 Canvas 化要重写这套，收益不如风险。

### 定案 3：一次手势 = 一帧一次样式写入

当前代码在事件回调里直接写样式（`editor.js:1516` 甚至在 `pointermove` 里重建）。
改为：**事件回调只更新内存模型 + 标脏；写入发生在 rAF 的 commit 阶段，且只写一次。**

这也是 React/Flutter/AE 的共同做法：输入 → 模型 → 提交 → 绘制，四段严格分离。

### 定案 4：预览帧「最新者胜，可丢弃，永不阻塞」

预览不是数据，是视觉反馈。它必须满足：

- **可丢弃**：渲染第 N 帧时用户已经拖到 N+3，第 N 帧应该直接扔；
- **异步**：结果通过回调送回，调用方立即返回；
- **单槽信箱**：同时最多 1 个在渲染 + 1 个在等待，不允许排队（排队 = 延迟累积）。

当前 `syncPreview` 有 `token` 守卫（`editor.js:596`）但**调用是同步的**，
token 永远相等，守卫形同虚设。

### 定案 5：所有优化必须可测，否则不算完成

禁止「感觉快了」。每个阶段必须有：

- 一个可复现的**基准场景**（固定片段数 / 轨道数 / 操作序列）；
- 一个**量化指标** + **门槛值**；
- 一个**回归门**（CI 或至少 `tools/` 脚本）。

现成可用的度量：`nc_preview_cache_stats`（`capi.h:409`，hits/misses/readerOpens/cached/capacity）
已在引擎侧，前端只是展示（`editor.js:2168-2175`）。前端侧一个都没有，需新建（§6）。

---

## 2. 帧预算模型（数字推导）

### 2.1 60Hz 下 16.67ms 怎么分

| 段 | 预算 | 内容 | 超了会怎样 |
|---|---|---|---|
| 输入分发 | ≤ 1.0 ms | `pointermove` → 更新模型（含吸附计算） | 手指与画面脱节（「粘手」） |
| 模型更新 | ≤ 1.5 ms | 命中测试 / 吸附候选 / 脏标记 | 同上 |
| 样式与布局 | ≤ 4.0 ms | DOM 覆盖层样式写入 → 布局重算 | 一帧内做不完就掉帧 |
| 绘制与合成 | ≤ 8.0 ms | Canvas 重绘 + WebView 合成 + 图层提升 | 掉帧 |
| 余量 | ≤ 2.2 ms | 系统抖动 / GC / 桥回调穿插 | — |

**关键约束**：Canvas 重绘与 DOM 布局不能同帧叠加。
所以 commit 阶段顺序固定为：**先 DOM 覆盖层（少量），再 Canvas 重绘（一次）**。

### 2.2 为什么是 60 而不是 120

移动端 WebView 的实际合成能力：中端机 Canvas 2D 全屏重绘在 1080p 等效像素下
约 4–8ms；120Hz 需要 8.3ms 总预算，做不到稳定。

**定案：目标 60fps 稳定，不追 120fps。** 但在高刷设备上
（`rAF` 时间戳步长 8.3ms）必须**按时间戳判断是否需要重绘**，
不能假设「每帧都来」—— 否则高刷机上会做双倍无用工作。

```js
// 提交器：按需绘制，不按 rAF 次数绘制
function commit(ts) {
  if (!dirty.timeline) return;              // 无变化 → 不做任何工作
  dirty.timeline = false;
  drawTimeline();                            // 唯一一次 Canvas 重绘
}
```

### 2.3 播放的三条时间线必须解耦

当前 `tick()`（`editor.js:1846`）用**一个 rAF 循环**同时驱动三件事，频率被绑死：

| 时间线 | 合理频率 | 依据 | 现状 |
|---|---|---|---|
| 画面（预览帧） | 项目帧率，上限 30 | 用户看画面判断内容 | 隔帧 = 15fps（`editor.js:1874`） |
| 播放头位置 + 时间码 | **10–15 Hz** | 9px 数字在 60Hz 下不可读（`editor.css` `.ph-read`） | 60Hz（`editor.js:1351`），纯浪费 |
| 刻度标签 | **整秒变化时** | 刻度本身是静态内容 | 已正确（`editor.js:1869`） |

**定案：三条时间线拆成三个独立调度器**，各自按需触发。
拆开后播放时的固定开销从「60fps × 全部片段」降到「15Hz × 2 个样式写 + 画布重绘」。

### 2.4 拖动时的预算再分配

拖动（尤其是片段移动）优先级高于一切：**手指跟手 > 画面清晰 > 时间轴细节**。

| 阶段 | 画面精度 | 时间轴细节 | 桥调用 |
|---|---|---|---|
| 按下 → 移动 | 上次缓存的帧（**不请求**） | 只重绘 Canvas 可见区 | 0 |
| 移动中（每 rAF） | 每 3 帧请求 1 次低清（≤ 240px） | 同上 | ≤ 20/s，异步 |
| 松手 | 立即请求 1 次全清 | 全量重绘一次 | 1 次，异步 |

「按下 → 移动」阶段完全不请求预览，是**反直觉但正确**的取舍：
拖动中用户看的是片段在时间轴上的位置，不是画面内容；
画面保持上一帧比闪低清帧更好看，还省掉全部桥开销。

---

## 3. 分层架构

```
┌──────────────────────────────────────────────────────────────┐
│ L4  输入层  InputArbiter                                      │
│     职责：原始事件 → 手势语义（pan/pinch/drag/trim/longpress）  │
│     约束：只做识别，不碰 DOM，不调桥                            │
├──────────────────────────────────────────────────────────────┤
│ L3  模型层  TimelineModel / SelectionModel / ViewportModel     │
│     职责：唯一事实来源 + 脏标记；预测式本地修改                  │
│     约束：纯数据，无 DOM 访问                                   │
├──────────────────────────────────────────────────────────────┤
│ L2  提交层  Committer（rAF 驱动，唯一写样式的入口）             │
│     职责：合并脏标记 → 一次 DOM 写入 + 一次 Canvas 重绘         │
│     约束：禁止读布局（无 getBoundingClientRect）                │
├──────────────────────────────────────────────────────────────┤
│ L1  绘制层  TimelineCanvas / OverlayDOM / RulerCanvas          │
│     职责：把模型画出来；时间轴只画可见区                        │
├──────────────────────────────────────────────────────────────┤
│ L0  同步层  EngineGateway（异步化 + 单槽信箱 + 预取队列）        │
│     职责：桥调用的唯一出口；隔离同步阻塞                       │
│     约束：所有对外 API 都是「发请求 / 收回调」                  │
└──────────────────────────────────────────────────────────────┘
```

**依赖方向**：L4 → L3 → L2 → L1；L0 被 L2/L3 反向调用，但不反过来。
（与 `docs/ROADMAP.md` 的 L4→L3→L2→L1 同构，只是把「前端」内部再分层。）

---

## 4. 各子系统规格

### 4.1 输入仲裁器（InputArbiter）

#### 问题

当前有 **6 套互不知情的手势实现**：

| 手势 | 实现位置 | 事件类型 |
|---|---|---|
| 时间轴平移 | `editor.js:1610-1622` | `pointerdown/move/up` |
| 刻度尺拖动 | `editor.js:1624-1634` | `pointerdown/move/up` |
| 片段拖动 / 裁剪 | `editor.js:1334` + `beginMove/beginTrim` | `pointerdown/move/up` |
| 双指缩放（时间轴 / 刻度 / 预览） | `editor.js:1696, 1715, 1734` | `touchstart/move/end`（`passive:false`） |
| 素材区拖动排序 | `editor.js:2600-2640` | `pointerdown/move/up` + 长按定时器 |
| 轨道头长按重排 | `editor.js:1135` 附近 | `pointerdown` + 定时器 |

`makePinch` 的 `touchmove` 是 `{passive:false}`（`editor.js:1689`），
且内部会 `e.preventDefault()` —— 它会**抢占**同一元素上的 `pointermove` 序列。
`E.tracks` 同时挂了 `pointerdown`（平移）和 `makePinch`（缩放），
**双指落下时两者都会触发**：`onScale` 重建一次、`pointermove` 再重建一次。

#### 设计

单一仲裁器，显式优先级 + 显式状态机：

```
IDLE
 ├─ 1 指按下 → PENDING（记录起点，不消费事件）
 │    ├─ 位移 > 8px 且在片段上 → CLIP_DRAG
 │    ├─ 位移 > 8px 且在裁剪把手上 → TRIM
 │    ├─ 位移 > 8px 在空白区 → PAN
 │    ├─ 位移 > 8px 在刻度尺 → SCRUB
 │    └─ 800ms 无位移且在轨道头上 → REORDER（长按）
 ├─ 第 2 指按下（任意状态）→ PINCH（**取消**当前单指手势并回滚）
 └─ 抬起 → IDLE
```

**三条硬规则**：

1. **`PENDING` 阶段不消费事件**。位移未超阈值前不 `preventDefault`、不 `setPointerCapture`，
   让浏览器的原生滚动仍有机会（虽然本工程时间轴是 `overflow:hidden`，
   但面板列表是 `overflow-x:auto`，`editor.css:412`）。
2. **`PINCH` 一经进入，立即回滚单指手势**。回滚 = 把模型恢复到按下快照，
   而不是「保留半途状态」。当前代码没有回滚，双指缩放时片段会跟着跑一段。
3. **状态机不产生样式写入**。`PAN`/`CLIP_DRAG` 只改 `ViewportModel.time` / `ClipModel.start`，
   标记脏位。

#### 惯性（fling）

当前**完全没有惯性** —— 抬手即停。剪映/AE 都有：抬手速度 > 阈值时按指数衰减继续平移。

```js
// 抬手时记录速度（px/s，取最近 100ms 的加权平均，不用瞬时值）
const v = weightedVelocity(samples);          // samples: [{t, x}] 环形缓冲
if (Math.abs(v) > FLING_MIN) {                // 300 px/s
  animate({
    from: viewport.time,
    v0: -v / viewport.pxPerSec,               // px/s → 秒/s
    decay: 0.94,                              // 每帧衰减系数（≈1.2s 停住）
    stop: () => viewport.time <= 0 || viewport.time >= seqEnd(),
  });
}
```

**必须做边界橡皮筋**：`viewport.time` 钳在 `[0, seqEnd()]`，
越界时用 `overshoot = (raw - clamped) * 0.35` 做阻尼位移，松手弹回。
否则用户拖到头会以为是「卡住了」。

#### 触摸延迟：消除第二层手势判定

`<meta name="viewport">` 已带 `width=device-width`（`mobile/pages/10-editor.html:5`），
所以**没有**经典的 300ms 双击缩放延迟 —— 这条不需要修。

真正需要修的是**第二层延迟**：浏览器仍要判断「这是滚动还是拖拽」。
`editor.css:602` 只给 `.clip, .trim-handle` 设了 `touch-action: none`（这几个正确），
而 `.trow`、`.tracks`、`.ruler`、`.l3`、工具栏按钮**都没有声明**，
在这些区域按住时浏览器会先等约 **100ms**（移动端滚动判定的启发式窗口）
再决定是否把事件交给你。

**规格**：逐区域显式声明，不用 `user-scalable=no` 一刀切。

| 区域 | `touch-action` | 理由 |
|---|---|---|
| `.tracks`（时间轴画布） | `none` | 全部平移/缩放由我们自己处理，不允许浏览器抢 |
| `.ruler` | `none` | 同上 |
| `.clip` / `.trim-handle` | `none` | 已有（`editor.css:602`） |
| `.l3` 面板内容 | `pan-y` | 面板内允许纵向滚动，横向由我们拦 |
| 工具栏 `.circles` `.l2-scroll` | `pan-x` | 横向可滚，不允许纵向抢 |
| `.l3-mask` | `none` | 遮罩只响应点击关闭 |

### 4.2 时间轴渲染（TimelineCanvas）

#### 4.2.1 为什么当前方案不可能达标

DOM 方案在 `renderTracksBody()` 里为**每个片段**创建：

1 个 `.clip` div + 1 个 `::after` 伪元素 + 1 个 `.clip-fill`（缩略图背景）
+ 1 个 `.clip-name` + 最多 220 个 `.wave i` + N 个 `.kf`。

1000 片段（其中假设 300 个音频片段）：

```
1000 × (1 + 1)          骨架与描边      ≈  2,000
1000 × 1                clip-fill      ≈  1,000
1000 × 1                clip-name      ≈  1,000
 300 × 220              波形柱         ≈ 66,000
────────────────────────────────────────────────
                                          70,000 节点
```

而且**无论可见与否都建**（`editor.js:1271` 只切 `visibility`）。
一次 `pointermove` 全部重建 —— 这就是 P-12 记录的「1000 片段 37ms」的来源，
实际项目数更多时会线性恶化到几百毫秒。

#### 4.2.2 Canvas 方案

**一个 `<canvas>` 画全部静态内容，一帧一次重绘，只画可见区。**

```js
function drawTimeline() {
  const { t0, t1 } = viewport.visibleRange();       // 可见时间区间
  ctx.clearRect(0, 0, W, H);

  for (let r = 0; r < tracks.length; r++) {
    const y = rowY(r), h = rowH(r);
    const clips = tracks[r].clipsInRange(t0, t1);   // ← 二分查找，O(log N + k)
    for (const c of clips) {
      const x = t2x(c.start), w = c.dur * pxPerSec;
      drawClipBody(ctx, c, x, y, w, h);             // 圆角矩形 + 一次 drawImage
      if (c.kind === 'audio') drawWaveform(ctx, c, x, y, w, h);
    }
  }
}
```

**关键点：`clipsInRange` 用二分查找。** 片段按 `start` 排序后，
两个 `lower_bound` 就能拿到可见区间，复杂度 O(log N + k)，
k = 可见片段数（约 15），与总片段数无关。

`drawClipBody` 的缩略图**不调桥**：从 L0 的 `ThumbCache`（内存位图）取，
命中失败就画纯色占位并在 idle 时补 —— 绝不在绘制路径上同步取。

#### 4.2.3 波形 LOD 阶梯（借鉴剪映的预览分级）

波形是最贵的部分（每片段 220 根柱）。做法：

```
pxPerSec 阶梯：{4, 8, 16, 32, 64, 128, 256, 512}   // 8 级，每级 ×2
```

- 每个素材在每个阶梯上预烘焙一张**离屏 canvas**（宽 = 时长 × pxPerSec 上限，高度 = 轨道高）；
- 缩放时**吸附到最近的阶梯**，直接用对应离屏位图 `drawImage` 缩放；
- 松手后 idle 时再烘焙新阶梯（用户感知不到）。

代价：每个音轨素材最多 8 张离屏图。1 小时音频在 4px/s 级 = 14400px 宽 ≈ 2.7MB（单张）。
**必须设预算上限**（与引擎的 `kFrameCacheBudgetBytes` 同思路）：总量 48MB，LRU 淘汰。

> 与 `PERF_XFORM_PLAN.md` §6.5 的 L1/L2 缓存同构：这里是前端侧的 L1。

#### 4.2.4 两种像素比对的可测断言

| 断言 | 方法 |
|---|---|
| 视觉等价 | 同一工程、同一 `pxPerSec`，Canvas 版与 DOM 版截图逐像素差 ≤ 2/255（缩略图边缘允许差异） |
| 命中测试正确 | 1000 片段下随机点 1000 次，Canvas 命中结果与几何计算一致率 100% |
| 只画可见 | 插入 1000 个离屏片段，`drawTimeline` 的 `fillRect` 调用次数 < 40（用 spy 计数） |

### 4.3 预览帧传输（PreviewPipeline）

#### 4.3.1 目标契约（三端统一）

```ts
// 平台层实现，前端唯一入口
interface PreviewSink {
  /** 请求一帧。立即返回；结果通过 onFrame 异步送达。
   *  generation 单调递增：收到比已消费更旧的 generation 直接丢弃。 */
  request(timeUs: number, targetW: number, generation: number): void;
  /** 平台层推帧。data 为可复用的位图句柄（Android: Bitmap，iOS: CVPixelBuffer） */
  onFrame(generation: number, timeUs: number, bmp: unknown): void;
  /** 停止（切页 / 暂停时调用），丢弃在途请求 */
  cancel(): void;
}
```

**关键差异**：`request` 不再返回值。这直接消灭了 §0.2 ⑥ 的整条同步链。

#### 4.3.2 单槽信箱（防排队）

```js
const Mailbox = {
  inflight: 0,          // 0 或 1
  pending: null,        // 最新一次请求（覆盖式，不是队列）
  request(timeUs, w) {
    if (this.inflight) { this.pending = { timeUs, w }; return; }  // 覆盖旧的
    this.inflight = 1;
    sink.request(timeUs, w, ++gen);
  },
  onFrame(g, bmp) {
    this.inflight = 0;
    if (g < consumedGen) return;        // 过期，丢弃
    consumedGen = g;
    present(bmp);
    if (this.pending) { const p = this.pending; this.pending = null; this.request(p.timeUs, p.w); }
  },
};
```

**为什么是覆盖不是队列**：用户拖动时位置在变，中间态没有任何价值。
排队只会让延迟随时间线性增长（经典的 backpressure 失败模式）。

#### 4.3.3 分阶段落地

| 阶段 | 做法 | 预期收益 | 风险 |
|---|---|---|---|
| **A（低风险）** | 保持 PNG，但改成**异步**：Kotlin 用 `HandlerThread` 渲染+编码，`evaluateJavascript` 回调送 `data:` URL | 消除 JS 线程阻塞（最大项） | 低，无层级改动 |
| **B** | 复用 `Bitmap` + `ByteArrayOutputStream`（不每帧 new）；缩略图加 LRU | 减少 GC 与分配 | 低 |
| **C** | 用 **RGBA_8888 原始像素** + `ImageData` 走 `OffscreenCanvas`，跳过 PNG | 省掉编码 + 解码两段 | 中：需 `OffscreenCanvas` 支持（WebView 69+，可接受） |
| **D（终极）** | `SurfaceView` / `TextureView` 置于 WebView 之下，`ANativeWindow` 直写（即 `PERF_XFORM_PLAN.md` P6） | 传输段 < 2ms | 高：Z-order / 圆角 / 透明层 |

**建议 A→B→C 连续做，D 单独排期单独回滚。**

#### 4.3.4 引擎侧必须同步修的三件事

1. **`nc_preview_render` 增加 `render_width` 入参**（`capi.h:395`）。
   当前无此参数，前端降质请求次次落空（§0.2 ⑦）。
   加参数而不是改签名：`nc_preview_render_scaled(editor, time_us, render_w, ...)`，
   老函数保留为 `render_w = 0`（= 画布原尺寸）的特例。**ABI 只增不改**（`docs/ROADMAP.md` D5）。
2. **缩略图缓存**（`capi.cpp:4135`）。按 `(assetId, width)` 缓存 `Thumbnail` 数组，
   命中直接返回，不 open 文件。当前每次 `FFmpegMediaReader reader;` 新建 —— 这是 §0.2 ⑤ 的根因。
3. **帧缓存失效粒度**（`capi.cpp:4003-4014`）。当前 `previewSignatureOf` 把
   `editRevision` 直接乘进签名，任何编辑都清空全部帧缓存。
   改为 `PERF_XFORM_PLAN.md` §6.1 的分层指纹（`clipId + 源入点 + 特效哈希 + xform 哈希`）。

### 4.4 面板与动效

#### 4.4.1 打开顺序必须反过来

```
现状：  hidden=false → rAF(加 open 类) → renderSeg → renderActions → renderBody
                                              ↑ 动画第一帧就在这里抖

目标：  renderSeg → renderActions → renderBody（离屏）→ hidden=false
        → 强制一次 layout → rAF(加 open 类)
```

「强制一次 layout」是必须的：`transform` 过渡需要起始值已生效，
否则浏览器可能合并两次样式变更导致过渡被跳过（首帧直接到终态）。

#### 4.4.2 动效分级（明确哪些能动画）

| 交互 | 动效 | 时长 | 属性 | 允许 |
|---|---|---|---|---|
| 面板展开/收起 | `translateY` | 260ms | transform | ✅ 已实现 |
| 遮罩 | `opacity` | 160ms | opacity | ⬜ 需补（当前无过渡，`editor.css:633`） |
| 工具栏 L1↔L2 | `translateX` | 260ms | transform | ✅ 已实现（`editor.css:411`） |
| 片段选中 | 描边色 | 120ms | border-color | ⚠️ border 触发重绘，改 `box-shadow` 或 Canvas 内画 |
| 轨道行禁止态 | `inset box-shadow` | 0 | — | ✅ 已用（`editor.css` 注释解释了为何不用 border） |
| 面板内容切换 | **无动效** | 0 | — | ✅ 正确（剪辑时多余动效是干扰，见 `design/spec/01-design-direction.md` §2） |
| 列表项悬停浮起 | — | — | — | ❌ 明确不做（同上） |

**规格**：只允许 `transform` / `opacity` 参与过渡。
任何触发 layout / paint 的属性（`width`、`height`、`left`、`top`、`box-shadow`、`border-width`）
一律不得进 transition。

#### 4.4.3 图层提升策略

`will-change` 当前只用 1 处（`editor.css:411`）。不能到处加 —— 每个提升的图层
都占独立 GPU 纹理，移动端纹理内存有限。

**规格**：

```css
/* 只在「动画期间」提升，动画结束移除 */
.l3.animating { will-change: transform; }
.bar-track.animating { will-change: transform; }
.canvas-still { will-change: opacity; }   /* 预览帧交叉淡入时 */
```

JS 侧监听 `transitionend` 移除类。**禁止**在样式表里静态写 `will-change`。

**隔离**：

```css
/* 轨道区：布局与绘制不溢出，避免时间轴重绘波及整页 */
.tracks { contain: layout paint; }
/* 面板：同理 */
.l3 { contain: layout paint; }
```

`contain: layout paint` 已在 `.canvas` 上用（`editor.css:68-69`），
但**轨道区没有** —— 时间轴是最大重绘源，最需要这个。

#### 4.4.4 面板内容构建的成本上限

`buildParamPanel` 每行 25 个刻度 `<i>`（`editor.js:3046-3052`）。
8 行 = 200 节点。**规格：刻度线改用 CSS `repeating-linear-gradient`，0 节点。**

```css
.xr-ticks {
  background-image: repeating-linear-gradient(to right,
      var(--text-low) 0 1px, transparent 1px calc(100% / 24));
}
.xr-ticks .maj { /* 只给 5 根主刻度建节点，或同样用第二层渐变 */ }
```

这是「用渲染换节点」的标准手法：一次 paint 代替 200 个布局盒。

### 4.5 数据流与提交（Committer）

#### 4.5.1 现状：编辑一次 = 两次全量

`edit()`（`editor.js:190-200`）：

```js
const r = engineCall(op, args);      // 同步桥
applyState(fetchState());            // 再同步取一次全量 state，重建整个 JS 模型
renderAll();                         // 全量渲染
```

一次编辑 = **2 次同步桥调用 + 1 次全模型重建 + 1 次全量渲染**。
拖动已经绕开它（只改 `c.start`，`onUp` 才提交），但所有面板滑块、开关、
按钮都走这条路。

#### 4.5.2 目标：乐观本地模型 + 单次提交 + 失败回滚

```js
async function apply(edit) {
  const before = model.snapshot(edit.scope);      // 只快照受影响部分
  model.applyPredictive(edit);                    // 本地先改（用户立刻看到）
  dirty.mark(edit.scope);
  try {
    await Gateway.call(edit.op, edit.args);       // 异步
    dirty.clear(edit.scope);
  } catch (err) {
    model.restore(before);                        // 回滚
    dirty.mark(edit.scope);
    toast(`引擎拒绝：${err.message}`);
  }
}
```

**关键：`model.snapshot(scope)` 按作用域快照**，不是整工程。
当前 `applyState(fetchState())` 是全量 JSON → 全模型重建，1000 片段时一次编辑
就要重新解析几 MB JSON。`scope` 通常是「一个 clip」或「一个 track」。

#### 4.5.3 编辑合并（batching）

滑块拖动会产生大量中间值。规格：

- `input` 事件：只改本地模型 + 标脏，**不提交**（`editor.js:3285-3290` 已是这个模式，正确）；
- `change` 事件（松手）：提交一次；
- 若用户连续操作同一属性 < 400ms 内多次，合并成一次提交（重命名、颜色拾取场景）。

`editor.js:3290` 的注释「拖动中不写，避免每帧一次引擎调用」说明这个模式已被理解，
但只有 `buildParamPanel` 用了，其它面板（`editor.js:3057`）仍是 `input` 直接写。

#### 4.5.4 状态同步的增量协议

`engine.state()`（`NativeEngineBridge.kt:95`）返回**全量**工程 JSON。
大工程下这是几 MB 字符串，同步传输 + 解析。

**规格（需 C ABI 新增）**：

```c
/// 增量状态：返回自 since_revision 以来变化的实体。
/// 无变化返回 NULL（不是空字符串 —— 区分「没变」与「变空了」）。
char* nc_state_delta(const NcEditor* editor, uint64_t since_revision,
                     uint64_t* out_revision);
```

前端保留 `lastRevision`，每次只取增量。
**在 ABI 落地前**，前端侧先做「按 scope 局部 refresh」的降级实现。

### 4.6 度量与回归门

#### 4.6.1 五个必测指标

| 指标 | 定义 | 采集方法 | 门槛（中端机） |
|---|---|---|---|
| `input_to_paint` | `pointermove` 到 rAF 提交完成 | `performance.now()` 打点 | p95 ≤ 20ms |
| `long_task_count` | 单次交互内 > 50ms 的任务数 | `PerformanceObserver({entryTypes:['longtask']})` | 0 |
| `dropped_ratio` | 拖动 5s 内掉帧比例 | rAF 时间戳间隔 > 20ms 计一次 | ≤ 5% |
| `preview_fps` | 播放时实际呈现的预览帧率 | `onFrame` 计数 / 秒 | ≥ 24（30 目标） |
| `commit_ms` | `commit()` 内部耗时（含 Canvas 重绘） | 前后时间戳 | p95 ≤ 8ms |

**基线工程**：`tests/fixtures/` 下需新增一个「1000 片段基准工程」
（10 轨 × 100 片段，3 条音频轨带波形），由 `nclip_tool` 生成并入库
（`.nclip` 是文本 JSON，可控体积）。

#### 4.6.2 开发期覆盖层

```js
// 仅在 localStorage['nc.perf']==='1' 时启用，生产不加载
PerfHUD.show({
  fps: rAF 间隔滑动窗口,
  input: p95(input_to_paint),
  commit: p95(commit_ms),
  long: 近 10s 内 longtask 数,
  nodes: document.querySelectorAll('*').length,   // 节点数是最直观的退化指标
});
```

节点数这一项特别有用：任何「又全量重建了」的回归都会让它跳变。

#### 4.6.3 CI 门禁

现有 `tools/check_mobile_frontend.py` 只做静态结构检查。新增：

| 门 | 检查内容 | 失败条件 |
|---|---|---|
| `check_perf_regression.py` | 扫描 `editor.js` 中热路径里的 `renderTracks()` / `getBoundingClientRect` | 出现在 `pointermove` / `rAF` 回调内即失败 |
| `check_dom_budget.py` | `mobile/pages/*.html` 里静态节点数 | > 400 即失败（软门，先警告） |
| 前端 perf 冒烟 | 用 `engine_static.js` 快照 + headless 跑 5s 拖动，输出 5 项指标 | 指标缺失即失败；数值仅记录不硬卡（避免 CI 抖动） |

**诚实的边界**：CI runner 是 x64 服务器，绝对耗时与真机不可比。
所以 CI **只做「相对回归」**（本次 vs 上次基线，退化 > 20% 报警），
绝对门槛必须在真机上测，写进发版检查单。

---

## 5. 跨端差异

三端不是「同一份代码跑三遍」，WebView 内核与合成能力差别很大。

| 项 | Android WebView | iOS WKWebView | 桌面 Qt WebEngine |
|---|---|---|---|
| 渲染引擎 | Blink + Skia | WebKit + CoreGraphics | Blink |
| Canvas 2D 性能 | 好（GPU 加速可关，需确认） | 好 | 好 |
| `OffscreenCanvas` | WebView 69+ | Safari 16.4+ | Chromium 同版本 |
| `touch-action` | 支持 | 支持 | 无触摸，走鼠标 |
| `will-change` 纹理上限 | 较低（中端机易爆） | 中 | 高 |
| 同步 JNI/桥 | `@JavascriptInterface`（同步） | `WKScriptMessageHandler`（**异步**） | `runJavaScript`（同步/异步均可） |
| 建议的预览通道 | `Bitmap` → 异步回推 | `CVPixelBuffer` → 异步回推 | 直接共享内存 |
| 长任务倾向 | 主线程阻塞直接掉帧 | 同上，且 jetsam 更狠 | 宽松 |

**三条跨端约束**：

1. **前端代码不得假设桥是同步的**。iOS 的 `WKScriptMessageHandler` 天然异步，
   若前端写成「调用即刻拿返回值」，iOS 端必然要伪造同步（阻塞 UI 线程），
   那是最坏的做法。**异步契约一次写对，三端都受益。**
2. **`will-change` 使用量按 Android 中端机的纹理预算设计**，不按桌面。
3. **像素一致性只约束「几何与时序」**，与 `PERF_XFORM_PLAN.md` §4.10 一致：
   同一时刻同一参数，三端的时间轴布局坐标必须逐像素相同；
   Canvas 抗锯齿差异不追究。

---

## 6. 借鉴：剪映 / CapCut 与 AE 的具体做法

| 做法 | 出处 | 我们的落点 | 是否采纳 |
|---|---|---|---|
| **时间轴整体 Canvas 绘制**，不建 DOM 节点 | CapCut 移动版（Skia 直绘） | §4.2 | ✅ 采纳（P4） |
| **波形按缩放级别预烘焙多档位图**（LOD 阶梯） | CapCut / Premiere Rush | §4.2.3 | ✅ 采纳（P5） |
| **拖动时画面不刷新，只跟手** | CapCut（拖片段时预览冻结） | §2.4 | ✅ 采纳 |
| **缩放吸附到档位，松手后精修** | CapCut 时间轴缩放 | §4.2.3 | ✅ 采纳 |
| **抬手惯性 + 边界橡皮筋** | CapCut / iOS 原生滚动 | §4.1 | ✅ 采纳 |
| **面板滑入前内容已就绪** | CapCut 面板（无白屏） | §4.4.1 | ✅ 采纳 |
| **动效只做「进入」，不做「离开」** | CapCut（关面板直接消失） | §4.4.2 | ⚠️ 折中：保留 260ms 收起（我们的面板更高，突兀） |
| **效果缩略图是预渲染静态图** | CapCut | 已采纳（`xformThumb()` CSS 绘制） | ✅ 已做 |
| **Collapse Transformations（塌陷变换）** | AE | 已采纳（`PERF_XFORM_PLAN.md` §2） | ✅ 已做 |
| **Layer Cache 分层缓存** | AE | 引擎侧 `PERF_XFORM_PLAN.md` §6.1；前端侧 §4.2.3 | ✅ 采纳 |
| **Scrub 时降质、暂停恢复全质** | AE / Premiere | §2.4 | ✅ 采纳 |
| **持续栅格化（缩放时重新采样而非放大位图）** | AE | 与 LOD 冲突时以 LOD 为先（移动端算力不够） | ⚠️ 有取舍 |
| **多线程渲染 + 主线程只合成** | AE | ❌ 移动 WebView 做不到；改为异步桥 + 单槽信箱 | ⚠️ 替代方案 |
| **磁盘缓存预热** | AE | §4.2.3 内存预算内 LRU；磁盘缓存留接口（`PERF_XFORM_PLAN.md` §6.5 L2） | ⏳ 后期 |

**明确不照搬的**：

- ❌ AE 的「每层独立变换 + 独立缓存」在 WebView 里做不了 —— 我们没有合成器控制权；
- ❌ CapCut 的「原生 TimelineView」不适用 —— 我们的前端是 WebView，
  真要做到那个水平只能把时间轴整个用 Kotlin/Swift 重写，
  那是平台层重构，不属于本文范围（但要在 §8 记录为长期选项）；
- ❌ 不做「手势轨迹预测」（AE 的笔触平滑）—— 剪辑不需要，成本高。

---

## 7. 分阶段计划

每阶段独立可交付、可回滚。**顺序按「收益 ÷ 风险」排列。**

### I0 — 度量先行（1 天）

**先测量，再优化。** 没有基线就无法证明任何一条改动有效。

- 新增 `mobile/assets/perf.js`：`PerfHUD` + 五个指标采集（§4.6.1）；
- 新增 `tools/gen_bench_project.py`：生成 1000 片段基准 `.nclip`；
- 新增 `tools/check_perf_regression.py`：静态扫描热路径违规（§4.6.3）。

**验收**：
- 打开基准工程，HUD 显示节点数与 `commit_ms`；
- 基线数据记录到 `docs/PERF_BASELINE.md`（真机，附机型）。

### I1 — 断开热路径上的桥调用（2 天）★ 收益最大

**只做三件事，不改渲染架构。**

1. `syncPreview()` 改为异步 + 单槽信箱（§4.3.2）；
2. 缩略图加两层缓存：JS `Map<assetId, dataUrl>` + 引擎 `(assetId,width)` 缓存（§4.3.4-2）；
3. `buildClip` 里不再同步取缩略图；未命中时画占位色，idle 补。

**验收**：
- 拖动 5s，`long_task_count = 0`；
- `input_to_paint` p95 ≤ 30ms（当前预期 > 200ms）；
- 引擎 `nc_preview_cache_stats` 的 `reader_opens` 不再随拖动增长。

### I2 — 一次手势一帧一次写入（2 天）

- 抽出 `Committer`：`dirty` 位 + rAF 提交（§4.5.2）；
- `renderTracks()` 的所有热路径调用点改为「标脏 + 定向重绘」；
- 拖动/平移/缩放**不再全量重建**，改为更新受影响的片段属性；
- 移除 `pointermove` 内 `renderTracks()`（`editor.js:1516`）与 `getBoundingClientRect`（`editor.js:1494`）。

**验收**：
- 拖动 200 片段工程，节点数**不增长**（HUD 观察）；
- `commit_ms` p95 ≤ 8ms；
- 拖动中无 `renderTracksBody` 调用（加 `console.count` 计数为 0）。

### I3 — 输入仲裁器（2 天）

- 实现 `InputArbiter`（§4.1），替换 6 处独立实现；
- 捏合进入时回滚单指手势；
- 惯性 + 橡皮筋。

**验收**：
- 双指缩放时片段不位移（回归用例）；
- 平移抬手后有惯性且 1.2s 内停住；
- 拖到 `t < 0` 有阻尼且松手弹回；
- 无 `touch-action` 缺失的元素（脚本扫描）。

### I4 — 时间轴 Canvas 化（5–7 天）★ 架构变更

- `TimelineCanvas`：刻度 + 片段 + 缩略图 + 波形 + 关键帧全部 Canvas 绘制；
- `clipsInRange` 二分查找；
- DOM 覆盖层：选中框 / 裁剪把手 / 转场按钮 / 播放头；
- 命中测试走几何计算，不依赖 DOM。

**验收**：
- 1000 片段工程拖动稳定 60fps（`dropped_ratio` ≤ 5%）；
- 节点数从 ~70,000 降到 < 500；
- 截图与 DOM 版逐像素差 ≤ 2/255；
- 所有既有片段交互（拖动/裁剪/选中/转场/关键帧）功能不回归。

### I5 — 波形 LOD（2 天）

- 8 级阶梯烘焙 + 离屏 canvas 缓存 + 48MB 预算 LRU；
- 缩放吸附档位，松手后 idle 精修。

**验收**：
- 30 分钟音频素材，缩放全程 `commit_ms` p95 ≤ 8ms；
- 缓存命中率 > 90%（HUD）；
- 内存增量 ≤ 48MB（Android `dumpsys meminfo` 实测）。

### I6 — 预览通道升级（3 天）

- Android：`HandlerThread` 异步渲染 + `Bitmap` 复用 + 原始像素 `OffscreenCanvas`（§4.3.3 B→C）；
- 引擎：`nc_preview_render_scaled`（§4.3.4-1）；
- iOS / 桌面：按同一契约实现（接口先行）。

**验收**：
- 播放时 `preview_fps` ≥ 24；
- 拖动时预览请求频率 ≤ 20/s 且全部异步；
- Android 上 `frameWDrag=112` **真实生效**（引擎输出宽度 = 112）。

### I7 — 面板与动效收尾（1.5 天）

- 打开顺序反转（§4.4.1）；
- 遮罩过渡补齐；`will-change` 动态化；`contain` 补到 `.tracks` / `.l3`；
- 刻度线改 CSS 渐变（§4.4.4）。

**验收**：
- 面板打开动画无掉帧（`dropped_ratio` = 0）；
- 面板打开耗时（点击 → 可交互）≤ 100ms；
- 单面板节点数减少 ≥ 150。

### I8 — 增量状态同步（3 天）

- C ABI `nc_state_delta`（§4.5.4）；
- 前端 `lastRevision` + 按 scope 局部 refresh；
- 落地前先用「局部 refresh」降级版。

**验收**：
- 1000 片段工程，一次改不透明度，前端耗时 ≤ 5ms；
- 传输字节数相比全量下降 ≥ 95%。

---

## 8. 本文未覆盖、但需要记录的问题

### 8.1 预览 ≠ 导出（多层合成缺失）★ 产品级问题

`preview.cpp:16-41` 的 `activeVideoClip` **只取第一个可见视频轨就 `break`**：

```cpp
for (const auto& track : seq->tracks) {
    if (track.type != TrackType::Video || track.hidden) continue;
    for (const auto& clip : track.clips) { if (...) return &clip; }
    break;              // ← 只取第一个可见视频轨
}
```

而导出侧**支持多层合成**（`export_executor.cpp:369`「收集当前时刻所有活动层」、
`:699`「多层合成：当前帧是顶层」）。

**后果**：画中画、混合模式、多层 alpha 在**预览里完全看不到**，
用户必须导出才知道效果。这违反「预览 = 导出」的核心承诺。

**为什么列在本文件**：它不是性能问题，但它比任何性能问题都严重 ——
用户第一次用画中画就会发现。**建议优先级高于本文任何阶段。**

（修法：预览侧按 `export_executor.cpp` 的层收集逻辑重写
`renderUncached`，把单层替换为多层合成。属 core 改动，不是前端。）

### 8.2 前端工程化（无模块、无构建）

`editor.js` 4100 行全全局函数。本文的改造会**继续增大**这个文件
（Canvas 绘制、仲裁器、提交器都要落进去）。

**建议**：在 I4 之前先做一次**最小模块化**——不引框架，只做三件事：

1. 加 `package.json` + Vite（仅打包，不改写代码）；
2. `editor.js` 按职责拆成 `timeline-canvas.js` / `input-arbiter.js` / `committer.js` / `panels.js`；
3. 加 ESLint（`no-undef` / `no-unused-vars`）+ CI。

**风险**：拆分可能引入回归。**先加 lint 与打包，再拆**。

### 8.3 无障碍与键盘

Canvas 化会**破坏**现有的 DOM 语义（片段是 `<div>`，可被读屏软件识别）。
`docs/UI_BACKLOG.md` P-13 已列为待办。

**必须在 I4 同步做**：Canvas 旁边维护一份**隐藏的语义 DOM 镜像**
（`<ul role="list">` + 每片段一个 `<li role="listitem" aria-label="片段名，起点，时长">`），
供读屏与键盘导航使用，`position:absolute; clip-path:inset(50%)` 隐藏但可聚焦。
这是 Canvas 时间轴的标准无障碍做法（Figma / Google Docs 都这么做）。

### 8.4 播放时钟与音频同步

当前 `tick()` 用 rAF 时间戳累加 `S.time`（`editor.js:1849`）：

```js
S.time += (ts - last) / 1000;
```

rAF 时间戳在掉帧时**不连续**（跳 2~3 帧），累加值会与真实墙钟漂移；
且与音频播放位置完全独立。

**规格（后期）**：播放时钟以「音频输出位置」为权威（有音频时），
无音频时以 `performance.now()` 差值计算，且**每 1s 校准一次**。
`S.time` 不得作为权威时间源直接累加。

### 8.5 缩略图与波形的「首次打开」卡顿

首次打开 1000 片段工程时，所有缩略图与波形都是冷的。
即使做了异步与缓存，**首屏仍需填充**。

**规格**：分三批填充——可见区（立即）→ 视口 ±1 屏（idle）→ 其余（后台，可中断）。
用户只要不滚到那里就永远不填。

### 8.6 缓存容量显示写死 12（真实缺陷）

`editor.js:2180`：

```js
parts.push(`缓存 ${after.cached_frames}/12 帧`);
```

而 `nc_preview_cache_stats` 的 `out_capacity` 是**按字节预算自适应**的
（`preview.h:71` `kFrameCacheBudgetBytes = 96MB`，`preview.cpp:99-105` 按画幅算出
`kFrameCacheMinFrames`~`kFrameCacheMaxFrames` 之间的值）。
`capi.h:411` 的注释明确写着「前端据此显示「已缓存 X/Y」，不要写死数字」。

**当前行为**：1080×1920 画布下容量是 12，恰好对；但换成 720p 或 4K 就会显示错误数字
（如「缓存 30/12 帧」）。用户看到的是「缓存超过上限」这种不可能状态。

**修法**：`readCacheStats()`（`editor.js:2112`）把 `capacity` 一起取出来，
显示 `${cached}/${capacity}`。一行改动。

---

## 9. 风险与明确不做

### 风险

| 风险 | 缓解 |
|---|---|
| Canvas 化引入交互回归（命中测试/选中/裁剪） | 保留 DOM 覆盖层做命中；几何计算与 DOM 版并存一期，用截图+行为双重比对 |
| 异步预览导致「画面比时间轴慢」 | 时间轴与预览用同一 `timeUs` 源；落后超过 2 帧时补一次全清 |
| 惯性手感调不好（太滑/太黏） | 参数外置到常量表，真机对比剪映调整；不追完美，够用即停 |
| LOD 缓存内存超预算 | 硬上限 48MB + LRU；超限降档位而非 OOM |
| 模块化拆分引入回归 | 先 lint 后拆；每次只拆一个文件，跑完整回归 |
| iOS 异步桥契约与 Android 不一致 | 契约文档先行（§4.3.1），两端各自实现 + 同构测试 |

### 明确不做

- ❌ **不做虚拟滚动（virtual list）** —— 时间轴不是列表，是二维画布，
  虚拟滚动的假设（等宽行）不成立；
- ❌ **不引入 React/Vue/Svelte** —— 现有 4100 行手写代码迁移成本远高于收益，
  且框架的 diff 成本在有 Canvas 后是纯浪费；
- ❌ **不做 120fps 目标** —— 移动 WebView 合成能力不够，追了会牺牲 60fps 稳定性；
- ❌ **不做「手势轨迹预测」** —— 剪辑不需要，AE 那套是给数位笔的；
- ❌ **不为了流畅牺牲正确性** —— 拖动中降质可以，拖动中**丢弃用户操作**不行；
- ❌ **不做假进度/假帧率** —— HUD 显示的必须是实测值，测不到就显示「—」。

---

## 10. 排期汇总

| 阶段 | 内容 | 工期 | 依赖 | 风险 |
|---|---|---|---|---|
| **I0** | 度量先行（HUD + 基准工程 + 静态门禁） | 1d | — | 低 |
| **I1** | 断开热路径桥调用（异步预览 + 缩略图缓存）★ | 2d | I0 | 低 |
| **I2** | 一次手势一帧一次写入（Committer） | 2d | I0 | 中 |
| **I3** | 输入仲裁器 + 惯性 + 橡皮筋 | 2d | I2 | 中 |
| **I4** | 时间轴 Canvas 化 + 语义 DOM 镜像 | 5–7d | I2, I3 | 高 |
| **I5** | 波形 LOD 阶梯 + 预算 LRU | 2d | I4 | 中 |
| **I6** | 预览通道升级（异步 + 原始像素 + 引擎宽度参数） | 3d | I1 | 中 |
| **I7** | 面板与动效收尾 | 1.5d | I2 | 低 |
| **I8** | 增量状态同步（`nc_state_delta`） | 3d | I2 | 中 |
| 前置 | 前端工程化（lint + 打包 + 拆文件） | 2d | — | 中 |
| 独立 | **8.1 预览多层合成**（建议最优先） | 3–4d | — | 中 |

**I0–I3 + I6 + I7（共 11.5 天）不碰 Canvas 化，风险可控，能把主要卡顿消掉。**
**I4 是全计划唯一的架构级改动，必须单独分支、单独验收、单独回滚。**

---

## 11. 如何复核本文

```bash
# 1) 热路径全量重建（应为多处命中 —— 这就是要修的）
grep -n "renderTracks();" mobile/assets/editor.js | wc -l

# 2) 拖动里重建
sed -n '1506,1520p' mobile/assets/editor.js

# 3) 缩略图同步取
grep -n "thumbUrl" mobile/assets/editor.js

# 4) 波形节点数
grep -n "n = Math.min(220" mobile/assets/editor.js

# 5) JNI 宽度参数未使用（应输出 0 或 1 —— 只有签名本身）
sed -n '1395,1428p' mobile/native/android/nc_jni.cpp | grep -c width

# 6) capi 无宽度入参
grep -n "int32_t nc_preview_render" -A 6 core/include/nairoclip/core/capi.h

# 7) 预览只取单层
sed -n '16,41p' core/src/preview.cpp

# 8) 面板先显示后建 DOM
sed -n '144,151p' mobile/assets/l3-panel.js

# 9) will-change / contain 使用量（应为 1 与 1）
grep -c "will-change" mobile/assets/editor.css
grep -c "contain:" mobile/assets/editor.css

# 10) 前端零埋点
grep -rc "performance.mark\|longtask" mobile/assets/*.js
```

---

## 文档维护约定

- 每个阶段完成后回填「实测值」到 §4.6.1 的指标表，不写估算；
- 修改 §1 的任一定案必须在此处留一行变更记录（日期 + 理由）；
- 新增前端热路径（事件回调 / rAF）时，**必须**同时确认不违反定案 1 与定案 3。
