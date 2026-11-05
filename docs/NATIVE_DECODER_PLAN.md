# 原生解码器路线（Native Decoder Plan）

> **本文是一份决策与实施文档，也是一个待开发任务书。**
> 姊妹篇：`docs/PERF_XFORM_PLAN.md`（渲染性能，含 P7 硬解+GPU 合成）、
> `docs/COMPLIANCE.md`（编解码合规，H.264 只走系统硬件）。
>
> 最后更新：2026-11-05 · HEAD `6bb967d`
> 状态：**当前未开发**。现有实现是「FFmpeg 包装系统解码器」，本文描述的是
> 「直接调平台解码器」的替代路线及其取舍。

---

## 0. 三句话结论

1. **两条路线在授权上完全等价** —— 都不构成 AVC Product，都不需要交专利费。
   真正解码的是手机的硬件，不是我们的代码。
2. **差别在工程：FFmpeg 包装省事但受限，原生 reader 难写但能零拷贝接 GPU。**
3. **现在不该动**。先真机验证 FFmpeg 包装能不能打开 H.264，
   暴露具体问题后再决定是否投入 1–2 周写原生 reader。

---

## 1. 两条路线是什么

### 路线 A（当前实现）：FFmpeg 包装系统解码器

```
文件
  ↓ FFmpeg avformat（解封装）
  ↓ FFmpeg h264 码流解析（NAL 拆分 / SPS / PPS）
  ↓ h264_mediacodec 包装层      ← FFmpeg 编解码器
  ↓ Android MediaCodec          ← 系统 API，真正解码在这里
  ↓ 硬件解码器（SoC 里的 IP 核）  ← 解码运算在这里
  ↓ 输出 Surface / ByteBuffer
  ↓ FFmpeg 转 RGBA
App
```

关键：`--enable-decoder=h264_mediacodec`，**没有** `--enable-decoder=h264`。

### 路线 B（本文讨论）：平台专用 reader

```
文件
  ↓ Android MediaExtractor       ← 系统 API（解封装）
  ↓ Android MediaCodec            ← 系统 API
  ↓ 硬件解码器（SoC）              ← 解码运算在这里
  ↓ 输出 Surface / ImageReader
  ↓ （可选）直接把纹理交给 GLES   ← 零拷贝
App
```

FFmpeg 完全不参与。C++ 内核通过一个新的 `MediaReader` 实现接入。

---

## 2. 授权分析（这是用户最关心的问题）

### 2.1 判断标准只有一条

**你的 App 二进制里有没有 H.264 编解码实现？**

| 内容 | 是否属于 AVC 专利范围 | 路线 A | 路线 B |
|---|---|---|---|
| 熵解码（CABAC/CAVLC） | ✅ 是 | ❌ 不在 App 里 | ❌ 不在 App 里 |
| 反变换 / 反量化 | ✅ 是 | ❌ 不在 App 里 | ❌ 不在 App 里 |
| 运动补偿 / 去块滤波 | ✅ 是 | ❌ 不在 App 里 | ❌ 不在 App 里 |
| NAL 拆分 / SPS/PPS 解析 | ❌ 否（格式规范） | ✅ 在 FFmpeg 里 | ✅ 在 MediaExtractor 里 |
| 容器解析（MP4 box） | ❌ 否 | ✅ 在 FFmpeg 里 | ✅ 在 MediaExtractor 里 |

**两条路线里，专利核心（解码算法）都跑在芯片里。**

路线 A 唯一的「自家代码」是 NAL 拆分与参数集解析 ——
这是**比特流格式规范**，不是编解码方法，不在 AVC 专利权利要求范围内。

### 2.2 结论

**换成原生 reader 不会降低专利风险，也不会增加。**

因为风险本来就不存在：两条路线下 App 都不含 H.264 编解码实现。

**「换成原生更保险」是个误解。** 真正的保险是「不含实现代码」，
这一点在 `6bb967d` 那次提交里已经做到了。

### 2.3 一个常见的顾虑

> 「我们调用了系统 H.264 解码功能，算不算使用？」

**不算「实现」，算「使用已授权设备功能」。**

类比：用系统相机拍 JPEG 不需要付 JPEG 专利费；
用系统播放器放 MP4 不需要付 AVC 专利费。
App 调用系统解码器播放/导入 H.264 视频，性质相同。

Via LA 的许可协议针对的是 **Codec Product（实现编解码器的产品）**，
不是「调用系统编解码能力的应用」。

**注意**：这是业界通行理解 + 零判例支持，但没有 Via LA 的书面确认。
如需书面答复，向 `info@via-la.com` 发函询问。

---

## 2.4 音频也必须走系统 API（与视频同一原则）

**决定（2026-11-05）**：音频的 AAC 编解码同样改为调用系统 API，
不再在 APK 里打包 FFmpeg 的 AAC 实现。

### 为什么

和 H.264 完全相同的逻辑：

| 项 | 说明 |
|---|---|
| **AAC 专利池仍在收费** | Via LA 的 AAC 池至今在授权（核心专利陆续到期，但仍有有效专利）。此前文档里「AAC 专利已基本到期」的表述**不准确**，此处更正。 |
| **判断标准** | App 二进制里有没有 AAC 编解码实现。有 = 构成实现者，需要授权。 |
| **现状** | FFmpeg 内置的 `aac` 解码器打包在 APK 里 → **严格讲构成实现者**。 |
| **目标** | 与视频一致：App 里不含 AAC 实现，只调用系统能力。 |

### 一条必须承认的不对称

**AAC 不能像 H.264 那样「删掉软解只留硬解」**，因为：

> 用户手机拍的视频，音轨几乎全是 AAC。
> 去掉 AAC 解码 = 用户导不进自己的素材。

所以音频的路径**只能是「换成系统 API」**，不能是「直接删除」。
这与 H.264 的处置方式不同（H.264 可以删除软件实现，只保留硬件）。

### 各平台系统音频 API

| 平台 | 解码 | 编码 | 备注 |
|---|---|---|---|
| **Android** | `MediaExtractor` + `MediaCodec`（`audio/mp4a-latm`） | 同左 | API 16+；编码 API 21+ |
| **iOS** | `AVAssetReader` | `AVAssetWriter` | AAC 是系统原生格式，支持最完整 |
| **Windows** | Media Foundation | Media Foundation | Win7+ |
| **macOS** | AudioToolbox / AVFoundation | 同左 | 10.7+ |
| **Linux** | ⚠️ 无统一方案 | ⚠️ | 需 GStreamer 或 pulseaudio；**这是唯一难点** |

### 工作量与风险

| 项 | 说明 |
|---|---|
| **工作量** | 3–5 天（音频系统 API 的封装比重采样链更琐碎） |
| **主要风险** | 现有 `swr`（libswresample）重采样链依赖 FFmpeg 已解码的音频；改成系统解码后，**重采样仍可保留 FFmpeg**（swr 是纯算法库，不含 AAC 实现），但要重新对接数据格式 |
| **另一个风险** | 系统解码器输出的格式不统一（S16 / F32 / 交织 / 平面），需要一层格式适配 |
| **Linux 桌面** | 无统一系统 API，可能只能**明示不支持 AAC**，或保留 FFmpeg（那就仍有授权问题） |

### 与视频的协同

音频与视频的解码器选择应该**同一条路径**，避免「视频走系统、音频走 FFmpeg」
这种半吊子状态 —— 那样既没省掉授权风险，又增加了两套链路的同步复杂度。

所以实施顺序：

```
① 视频原生 reader（N1/N2）—— 已有详细计划
② 音频在同一 reader 内一并实现（MediaExtractor 本来就同时出音视频轨）
③ 移除 FFmpeg 的 aac 编解码器（--disable-decoder=aac --disable-encoder=aac）
④ 验证：导入手机拍摄的视频，音画同步、无爆音、seek 正确
```

**注意 ② 的顺序优势**：`MediaExtractor` 一次解封装就能同时拿到音频和视频轨，
音视频同步（PTS 对齐）在同一个 reader 内解决，比跨两条链路简单得多。

### 明确不做

- ❌ **不把 AAC 换成 Opus** —— 解决不了「导入用户素材」的问题（用户素材是 AAC），
  只会新增一个不兼容的导出格式
- ❌ **不同时保留两套音频解码路径** —— 双链路会带来音画同步的长期隐患
- ❌ **不在 Linux 桌面强行保留 FFmpeg AAC 而不做说明** —— 要么明示不支持，
  要么在许可文档里如实披露

---

## 3. 各平台的芯片与系统要求

### 3.1 Android — MediaCodec

**最低系统版本：Android 5.0（API 21）**

H.264 解码自 Android 1.5 就有，但 API 21 起成为 CDD（兼容性定义文档）
**强制要求**，即「任何声称兼容 Android 5.0 的设备都必须支持 H.264 解码」。

**芯片要求：实际上不存在门槛。** H.264 解码是 SoC 里的标准硬件 IP 核：

| 芯片档位 | 例子 | H.264 解码能力 |
|---|---|---|
| 旗舰 | 骁龙 8 系 / 天玑 9000 系 | 4K60 ~ 8K |
| 中端 | 骁龙 6/7 系 / 天玑 7000 系 | 1080p ~ 4K30 |
| 入门 | 骁龙 4 系 / 紫光展锐 T 系 | 1080p |
| 超低端（2015） | 联发科 MT6580 | 720p |

**下限：约 2011 年之后的任意 Android 手机。**

**真正的限制是分辨率上限**（由芯片世代决定）：

| 芯片世代 | 解码上限 |
|---|---|
| 2013–2015 入门 | 1080p30 |
| 2016–2018 中端 | 1080p60 |
| 2018+ 中高端 | 4K30 |
| 2020+ 旗舰 | 4K60 / 8K |

超出规格（如 2014 年低端机解 4K）会返回 `ERROR_UNSUPPORTED`，
必须给用户明确提示，而不是报内部错误。

### 3.2 iOS — VideoToolbox

**最低系统版本：iOS 8.0**（`VideoToolbox` 公开 API 引入版本）

**芯片要求：无。** 全部 iOS 设备都支持 H.264 硬件解码：

| 设备 | 解码能力 |
|---|---|
| iPhone 4（2010，A4） | 720p |
| iPhone 5（2012，A6） | 1080p |
| iPhone 8 及以后 | 4K60 |

**iOS 侧实际无门槛。**

### 3.3 桌面三端

| 平台 | 系统 API | 最低版本 | 备注 |
|---|---|---|---|
| Windows | Media Foundation | **Windows 7+** | Vista 需另装解码器 |
| macOS | VideoToolbox | **macOS 10.7+** | 全部支持 |
| Linux | VA-API / V4L2 | 视发行版 | ⚠️ **唯一有风险的一端** |

**Linux 是例外**：无 GPU 的服务器 / 虚拟机没有硬件解码器，
VA-API 需要 `libva` + 对应驱动。必须准备降级提示
（「此设备不支持 H.264 解码」），而不是崩溃或静默失败。

---

## 4. 路线 B 的优点与代价

### 4.1 优点

| 维度 | 说明 |
|---|---|
| **零拷贝** | `ImageReader` / `CVPixelBuffer` 可直接出 GL 纹理，跳过 RGBA 转换。这是 **P7（GPU 合成）的前置条件** |
| **设备兼容性** | 系统原生路径，绕开 FFmpeg mediacodec 的已知问题（部分机型/码流打不开） |
| **功耗** | 少一次全帧 RGBA 转换与内存拷贝（1080p 每帧 8MB） |
| **HDR** | 能拿到 10-bit / HDR 原始数据；FFmpeg mediacodec 输出格式受限 |
| **精确 seek** | `MediaExtractor.seekTo` 的 `SEEK_TO_PREVIOUS_SYNC` 等模式更可控 |

### 4.2 代价

| 维度 | 说明 |
|---|---|
| **工作量** | 1–2 周（Android Kotlin/JNI + iOS Swift 各一套） |
| **维护** | 三套实现（Android / iOS / 桌面），接口改动要改三遍 |
| **测试** | 必须有真机；CI（无设备）无法覆盖 |
| **音频** | 现有 FFmpeg 音频解码路径要保留，等于「视频走原生 + 音频走 FFmpeg」，两条链路要各自处理 seek 同步 |
| **回退** | 原生 reader 不支持某些容器/编码（如 MKV 里的冷门编码），需要回退到 FFmpeg |

### 4.3 关键判断

**路线 B 的核心价值只有一条：为 P7 的 GPU 合成铺路。**

如果暂时不做 P7，路线 A 完全够用，不值得为「兼容性略好」单独投入 1–2 周。

---

## 5. 什么时候该切换

按以下顺序判断，**每一步都要有实际证据**：

### 第 1 步：真机验证路线 A（必须先做）

拿一台真机（Android + iOS 各一），导入一个 H.264 MP4：

- 能打开并播放 → **路线 A 可用，不切换到 B**
- 打不开 / 报错 / 花屏 → 记录具体错误，进第 2 步

### 第 2 步：先修路线 A 的配置

多数 `h264_mediacodec` 失败是配置问题，不是路线问题：

| 症状 | 可能原因 | 修法 |
|---|---|---|
| `avcodec_open2` 失败 | 未设置 `AV_PIX_FMT_MEDIACODEC` | 设置 `ctx->get_format` 回调 |
| 花屏 / 绿屏 | 输出格式不是 NV12 | 检查 `MediaCodec` 的输出 format |
| 部分设备失败 | 缺少 `AMediaCodec` 权限 | 检查 NDK 版本与 `--enable-jni` |
| 4K 打不开 | 超出设备规格 | 检测 `MediaCodecInfo` 的能力上限并提示 |

**很多「mediacodec 不好用」其实是这一步没做对。**

### 第 3 步：决定是否做 P7

| 情况 | 建议 |
|---|---|
| 短期只需要「能导入、能导出、能预览」 | 保持路线 A，**不做 B** |
| 要做 GPU 多层合成 / HDR / 4K 流畅预览 | 做路线 B（它是 P7 的前置） |
| 真机暴露多个 mediacodec 无法修复的问题 | 做路线 B |

---

## 6. 路线 B 的实施设计（真要做时按此执行）

### 6.1 接口层（不改现有抽象）

现有 `MediaReader` 接口保持不变：

```cpp
class MediaReader {
public:
    virtual Result open(const std::string& path) = 0;
    virtual const MediaProbeResult& probe() const = 0;
    virtual bool readVideoFrame(VideoFrame& out) = 0;
    virtual bool readAudioBlock(AudioBlock& out) = 0;
    virtual bool seek(const Time& t) = 0;
    virtual void close() = 0;
};
```

新增实现：

```
AndroidMediaReader  (Kotlin + JNI)   — MediaExtractor + MediaCodec
IOSMediaReader      (Swift + C ABI)  — AVAssetReader + VideoToolbox
```

**C++ 内核零改动**，因为接口未变。

### 6.2 注册与选择

在 `MediaReaderFactory` 层做能力探测与择优：

```cpp
std::unique_ptr<MediaReader> createReader(const std::string& path) {
    const auto probe = quickProbe(path);   // 只读容器头
    if (probe.videoCodec == "h264" && nativeDecoderAvailable()) {
        return std::make_unique<AndroidMediaReader>();  // 或 IOSMediaReader
    }
    return std::make_unique<FFmpegMediaReader>();       // 其余交给 FFmpeg
}
```

**必须保留 FFmpeg 回退**：原生 reader 不覆盖的容器/编码（MKV 冷门编码、
AV1 in MP4 等）仍走 FFmpeg。

### 6.3 零拷贝设计（P7 前置）

```
MediaCodec 输出 Surface
    ↓ 作为 EGL 外部纹理（GL_TEXTURE_EXTERNAL_OES）
    ↓ GLES 着色器采样 + 合成
    ↓ 渲染到输出 Surface
```

关键点：
- `MediaCodec.configure(..., surface, ...)` 直接输出到 Surface，**不落地到 CPU**
- 用 `SurfaceTexture` 包裹 Surface，`updateTexImage()` 拿帧
- 纹理采样用 `samplerExternalOES`（外部纹理不支持普通 `sampler2D`）
- 色彩空间：`MediaCodec` 输出常为 `COLOR_FormatYUV420Flexible`，
  走 Surface 后由 GPU 做 YUV→RGB（省一次 CPU 转换）

### 6.4 分阶段（每阶段可独立验证）

| 阶段 | 内容 | 工期 | 验证方式 |
|---|---|---|---|
| N1 | Android: `MediaExtractor` + `MediaCodec` 出 RGBA（不零拷贝） | 3–4d | 真机导入 H.264 并预览 |
| N2 | iOS: `AVAssetReader` + `VideoToolbox` 出 RGBA | 3–4d | 真机导入 H.264 并预览 |
| N3 | Android: 零拷贝（Surface → EGL 外部纹理） | 2–3d | 对比 CPU/GPU 逐像素一致性 |
| N4 | iOS: 零拷贝（`CVPixelBuffer` → `CVMetalTextureCache` 或 GL） | 2–3d | 同上 |
| N5 | 能力探测 + 回退 + 真机矩阵测试 | 2–3d | 多机型矩阵 |
| — | **合计** | **12–17d** | |

### 6.5 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 零拷贝路径的平台差异大 | iOS/Android 代码不能复用 | 接受（本来就是平台专用） |
| CI 无法验证运行时行为 | 改动只能靠人工真机测 | 建立真机回归清单 |
| 音频仍走 FFmpeg | 音画同步要跨两条链路 | 统一用 PTS 对齐，不依赖帧序号 |
| 原生 reader 的 seek 语义与 FFmpeg 不同 | 「预览=导出」一致性可能破坏 | 加跨路径一致性测试（同一时间点两路取帧比对） |

---

## 7. 当前状态与后续动作

### 7.1 已完成

- ✅ 软件 H.264 编解码器全部移除（`6bb967d`）
- ✅ H.264 只走系统硬件解码器（`h264_mediacodec` / `h264_videotoolbox` / 桌面后端）
- ✅ 解码器候选循环：逐个 `avcodec_open2` 尝试，失败换下一个
- ✅ 无硬件解码器时 fail-closed 拒绝，并给出可操作的错误提示
- ✅ 测试素材改为 VP9，测试导出改用 `nc_export_run_ex(..., "vp9", ...)`

### 7.2 未完成

- ⬜ **真机验证 H.264 硬件解码路径**（最高优先级，本地 aarch64 无 GPU、
      CI 无设备，至今零次实际运行）
- ⬜ **音频走系统 API**（AAC 专利，见 §2.4；与视频 reader 一并实现）
- ⬜ Android/iOS 原生 reader（本文路线 B，**待真机验证后决定是否启动**）
- ⬜ P7 GPU 合成（依赖路线 B）

### 7.3 下一步

**先做 7.2 的第一项**：拿真机导入一个 H.264 视频。

- 成功 → 路线 A 够用，本文档搁置，去做 P7 以外的事
- 失败 → 按 §5 第 2 步修配置；仍失败再启动路线 B

---

## 8. 明确不做

- ❌ **不为了「显得更合规」而写原生 reader** —— 授权上两条路线等价
- ❌ **不在没有真机验证的情况下投入 1–2 周** —— 可能白做
- ❌ **不彻底删除 FFmpeg 解码路径** —— 它覆盖原生 reader 不支持的容器/编码
- ~~❌ **不做「音频也走原生」**~~ → **已改变决定**：AAC 专利池仍在收费，
  音频也应走系统 API。见 §2.4。修正理由：原判断「音频无专利问题」不准确。
