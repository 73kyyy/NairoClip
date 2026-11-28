# NairoClip 合规文档（合规基线 + 校验清单）

> 依据：《NairoClip 所有模型协议和其它要求》（模型协议终核 + 三端部署规格 +
> 法律法规要求）。本文件是**合规单一事实来源**，随依赖变更同步更新。

---


## 一、全平台媒体编解码总政策

NairoClip 不再分发或调用应用自带的软件视频/音频解码器与编码器。Android、iOS、macOS、Windows、Linux 均必须通过各自平台的系统媒体框架完成压缩媒体解码/编码；硬件加速优先。若系统硬件路径不可用，可评估系统框架本身提供的兼容实现，但不得加载 NairoClip 自带的 FFmpeg/libx264/OpenH264/libx265/libvpx/libaom/libopus/AAC 等软件编解码实现。系统框架也无法处理时必须 fail-closed。

| 平台 | 目标系统媒体框架 | 迁移要求 |
|---|---|---|
| Android | MediaCodec / MediaExtractor / MediaMuxer；AAudio / AudioTrack | 完善 codec 能力探测、AAC 系统编码、音频输出及 Surface/HardwareBuffer 路径 |
| iOS / iPadOS / macOS | AVFoundation / VideoToolbox / CoreAudio | 用系统 reader/writer 与硬件 codec 替代 FFmpeg codec 路径 |
| Windows | Media Foundation / WASAPI | 明确选择系统 MFT，优先硬件 MFT；不得打包软件 codec |
| Linux | VA-API / V4L2 与系统媒体栈、PipeWire/ALSA | 运行时探测系统后端；不支持时安全拒绝 |

FFmpeg 不能被整体粗暴删除：在平台媒体 API 尚未接管前，仍可能承担容器 demux/mux、bitstream parser、时间戳、流信息和必要的像素/采样格式转换。上述职责属于迁移过渡层，不得借此启用 FFmpeg 软件 decoder/encoder。每个平台完成系统 reader/writer 接管并有回归证据后，才删除该平台不再需要的 FFmpeg 依赖和构建产物。

## 二、Android 编解码器合规与系统媒体策略

### 1.1 Android 构建硬性规则

Android APK 不捆绑任何软件视频或音频编解码器。媒体编解码必须调用 Android 系统 MediaCodec；系统不支持时明确报错，不允许退回 FFmpeg 软件实现。

| FFmpeg 构建项 | Android 策略 |
|---|---|
| `--disable-decoders` | 先关闭全部内置解码器 |
| `--disable-encoders` | 先关闭全部内置编码器 |
| `*_mediacodec` 视频解码器 | 仅启用系统 MediaCodec wrapper |
| `*_mediacodec` 音频解码器 | 仅启用系统 MediaCodec wrapper |
| `*_mediacodec` 视频编码器 | 仅启用系统 MediaCodec wrapper |
| AAC / Opus / MP3 / FLAC 等软件编码器 | 不编入 Android 构建 |
| demuxer / muxer / parser | 保留容器读写和码流解析能力，不承担软件编解码 |

### 1.2 Android 系统编解码器白名单

| 类型 | 系统 MediaCodec 格式 | 规则 |
|---|---|---|
| 视频解码 | H.264、MPEG-2、MPEG-4、VP8、VP9、AV1 | 仅通过系统 MediaCodec wrapper；设备不支持则拒绝导入 |
| 视频编码 | H.264、VP8、VP9、MPEG-4、AV1 | 仅通过系统 MediaCodec wrapper；按设备能力选择 |
| 音频解码 | AAC、MP3、AMR-NB、AMR-WB | 仅通过系统 MediaCodec wrapper；其他格式暂不支持 |
| 音频编码 | AAC | 必须通过 Android 系统 MediaCodec + 容器封装链路；该链路接入前拒绝带音轨导出 |
| 音频播放 | AAudio / Oboe，兼容 AudioTrack | 使用系统音频输出 API；不等同于压缩音频解码 |
| GPU 呈现 | Vulkan / OpenGL ES | 使用系统图形 API 和设备驱动 |

H.264 在 Android 上只允许调用设备系统提供的 MediaCodec，不把软件实现打入 APK；HEVC/H.265 继续全线禁用，不因系统 API 存在而重新开放。系统 API 返回的实际 codec 能力仍取决于设备厂商、Android 版本、profile、分辨率和帧率；不能把“存在 MediaCodec API”误写成“所有设备都支持硬件加速”。

### 1.3 Fail-closed 与当前实现边界

- Android FFmpeg 构建先执行 `--disable-decoders --disable-encoders`，再显式启用 MediaCodec wrappers。
- Android 视频解码只枚举 `h264_mediacodec`、`mpeg4_mediacodec`、`vp8_mediacodec`、`vp9_mediacodec`、`av1_mediacodec` 等系统 wrapper。
- Android 音频解码只枚举 AAC / MP3 / AMR 对应的 MediaCodec wrapper；不支持的音轨明确拒绝，不走 FFmpeg 原生软件解码。
- Android 视频编码候选必须以 `_mediacodec` 结尾；即使未来构建配置意外启用了软件编码器，也会在运行时过滤。
- Android AAC 软件编码已从构建中移除。系统 AAC 编码和最终音视频封装尚未接通前，带音轨导出会明确失败，不允许静默生成无声视频。
- 当前 EGL/GLES 预览仍然接收 CPU Reference renderer 生成的 RGBA 数据；这不等于 MediaCodec Surface / HardwareBuffer 到 GPU 的零拷贝链路。
- 当前 Android 音频混音核心仍由 NairoClip C++ Audio Mixer 执行；AAudio / Oboe / AudioTrack 输出接线仍需完成。
- 以上是 Android 当前迁移范围。iOS、Windows、macOS、Linux 必须分别接入各自系统媒体框架后，才能声明达到同样的系统-only 策略；不能将 Android 状态外推为三端已完成。

### 1.4 软件编解码器禁令

- Android APK 不得包含 H.264、HEVC、AV1、VP9、AAC、MP3、Opus、FLAC、Vorbis 等格式的软件编解码实现。
- 不允许 GPL、nonfree 或其他未获批准的编解码器进入 Android APK。
- FFmpeg 在 Android 侧保留容器 demux/mux、bitstream parser、像素/采样格式转换等非编解码职责；FFmpeg 的 MediaCodec wrapper 只作为系统 API 适配层。
- 系统不支持时提示用户格式或设备不兼容；不得下载、加载或静默启用软件编解码器。

---

## 三、AI 模型协议（锁定清单）

### 2.1 锁定模型（协议已核对，均为 MIT/BSD/Apache-2.0）

| 功能 | 锁定模型 | 协议 |
|---|---|---|
| 人像/物品抠像 | MediaPipe Selfie Segmentation | Apache-2.0 |
| 光流补帧 | RIFE | MIT |
| 超分辨率 | Real-ESRGAN（+ OpenCV 人脸定向增强） | BSD-3-Clause |
| 人声分离 | UVR-MDX-NET-Voc_FT | MIT |
| 语音转字幕 | whisper.cpp | MIT |
| 字幕翻译 | **m2m100-418M**（`facebook/m2m100_418M`）<br>原定 MADLAD-400 已因"必须本地部署"放弃 | **MIT**<br>（核查记录 `docs/licenses/m2m100_418M.md`） |
| TTS | VITS（官方 LJSpeech/VCTK 权重） | MIT（权重限定） |
| **深度图** | **MiDaS** | MIT |
| 镜头追踪 | OpenCV CSRT/KCF（传统算法） | Apache-2.0 |
| 人脸检测 | OpenCV DNN | Apache-2.0 |
| 智能重构图 | OpenCV Saliency | Apache-2.0 |
| VAD | Silero VAD | MIT |
| 推理引擎 | ONNX Runtime | MIT |
| 渲染引擎 | bgfx（或自建抽象层） | BSD-2-Clause |
| 计算机视觉 | OpenCV | Apache-2.0 |

### 2.2 禁用清单（协议风险）

| 模型 | 原因 |
|---|---|
| RVM | GPL-3.0 强传染 |
| GFPGAN / CodeFormer | 非商用许可 |
| Demucs 官方权重 / Spleeter | 训练数据协议灰色地带 |
| NLLB-200 | CC-BY-NC-4.0 非商用 |
| **Depth-Anything-V2 Base/Large** | DINOv2 骨干 CC-BY-NC-4.0；**深度图锁定 MiDaS，不转正** |
| 未注明来源的社区 TTS 权重 | 训练数据协议不明 |

### 2.3 模型治理（持续核查机制）

- **季度核查**：每季度重新核对锁定模型的上游协议是否变更（责任人：项目负责人）；
- **变更流程**：任何模型选型变更必须提交独立协议核查，**不得通过“沙箱顺带验证”
  默认转正**；
- **上游协议突变预案**：已下载旧版本模型——评估后决定「继续允许已下载用户使用」
  或「推送新版本替换」；新安装一律使用新版本。

---

## 四、三端功能对等原则（强制）

- **三端（Android/iOS/桌面）功能集合与最终导出效果质量上限必须完全一致**；
- 唯一允许差异：**模型量化精度**（FP32/FP16/INT8，体积与速度差异）；
- 禁止：因设备性能隐藏功能入口或降低最终产出质量上限；
- 极端低端设备：设定「最低配置要求」+ 提前告知预期耗时，**不隐藏、不降级**；
- **预览速度差异不受约束**（允许降分辨率预览），**导出结果必须等效**；
- 量化分级参考：桌面 FP32 / 高性能移动 FP16 / 低性能移动 INT8；
  音频模型（UVR）低端设备亦可 FP16（无需降到 INT8）；
- TTS 音色数、超分最大倍率（4x）、补帧最大倍率（8x）等规格参数三端数值一致；
- 桌面端如需性能优化：**用户主动选择**的「性能模式」开关，而非自动分级。

### 3.1 分发机制（复用 model_manager）

- 下载请求新增「设备性能分级」字段（高/中/低，由 RAM/CPU 核心/加速单元探测
  + 轻量基准测试确定）；
- 服务端按「功能 ID + 设备分级」返回对应量化版本的签名 URL；
- **加密/校验机制不因量化精度而有差异**（AES-256-CTR + SHA-256 一体适用）。

---

## 五、AI 生成合成内容标识（《标识办法》2025-09-01 施行）

### 4.1 触发范围

| 功能 | 标识类型 |
|---|---|
| TTS 智能配音 | 音频显式标识（提示音）+ 隐式元数据 |
| 人声分离 / 字幕翻译 | 隐式元数据（+ 画面标识如触发） |
| AI 视觉增强（超分/抠像/景深） | 画面显式标识 + 隐式元数据 |
| 自动剪辑（参考风格生成） | 画面显式标识（既有 ai_label）+ 隐式元数据 |

### 4.2 实现（代码位置）

- **显式标识（画面）**：`core/src/ai_label.cpp`（导出帧附加标识渲染项，
  不可关闭；平台层渲染）；
- **显式标识（音频）**：`core/src/ai_marking.cpp`
  （`applyAiAudioMarks`：首尾 880Hz 提示音，含淡入淡出防爆音）；
- **隐式标识（元数据）**：`core/src/ai_marking.cpp`
  （`buildAiMetadata` → `WriterConfig::aiMetadata` → MP4/容器元数据；
  `comment` 含压缩机器可读字段，兼容 MP4 标准 tag 限制）；
- **职责分离**：隐形水印（账号溯源）≠ 隐式标识（AI 属性告知），两者共存。

### 4.3 日志留存（平台层职责）

- 提供「不含显式标识版本」的用户须依法留存不少于 **6 个月**日志；
- 与安全文档的日志审计机制打通（同一日志系统，非两套）。

---

## 六、开源许可与发布合规

### 5.1 NOTICE 页面（「关于-开源软件许可」）

- 每个引用项目需附**协议原文全文**（MIT/BSD 协议较短，必须全文）；
- 参考 Apache 协议的 NOTICE 格式（`docs/THIRD_PARTY_NOTICES.md`）；
- **纳入 CI/CD 自动检测**（依赖审计工具扫描，不靠人工台账）；
- 内容随每次新增/替换依赖自动更新。

### 5.2 加密合规声明（提审必读）

- **iOS**：提交 App Store 必须声明 `ITSAppUsesNonExemptEncryption`；
  使用标准 HTTPS/AES（豁免条件内）需正确声明，避免反复补充材料；
- **国内安卓商店**：部分商店对加密技术有资质审核（算法说明等），提前准备。

### 5.3 服务端灾备（平台层职责）

- 密钥签发 / 支付凭证校验等关键服务：多活部署 + 降级预案；
- 服务短暂不可用：客户端可用「上一次会话凭证」短暂宽限（与心跳机制衔接）。

---

## 七、强制校验清单（对照本文档逐项检查）

| # | 校验项 | 代码/文档位置 | 状态 |
|---|---|---|---|
| 1 | 深度图锁定 MiDaS（禁止 Depth-Anything 转正） | 本文档 2.1/2.2 | ✅ 已记录 |
| 2 | 模型变更走独立协议核查 | 本文档 2.3 | ✅ 机制已定义；m2m100-418M 已核查（`docs/licenses/`） |
| 3 | 三端功能集合/导出质量上限一致 | 本文档 3 | ✅ 已定义（平台层执行） |
| 4 | 量化精度为唯一允许差异 | 本文档 3 | ✅ 已定义 |
| 5 | 低端设备不隐藏/不降级（提示耗时） | 本文档 3 | ✅ 已定义（UI 层执行） |
| 6 | 预览速度差异不受约束 | 本文档 3 | ✅ 已定义 |
| 7 | TTS 音色/超分倍率/补帧倍率三端同值 | 本文档 3 | ✅ 已定义 |
| 8 | 分发机制复用 model_manager + 加密一致 | 本文档 3.1 | ✅ 已定义（服务端待接） |
| 9 | 桌面端性能模式为用户主动开关 | 本文档 3 | ✅ 已定义 |
| 10 | FFmpeg 编译参数锁定（禁 GPL/nonfree） | 本文档 1.1 | ⚠️ 平台层构建时执行 |
| 11 | 禁用 libx264/libx265（代码级） | `core/src/codec_policy.cpp` + 测试 | ✅ 已实现（源码级） |
| 12 | AV1/VP9/Opus 仅走系统媒体 API，不含应用自带软件 codec | `codec_policy` + 平台适配层 | ⚠️ 编码策略已收紧；各平台适配未全部完成 |
| 13 | 导出格式/导入范围三端一致 | 本文档 1.4 | ✅ 已定义（平台层执行） |
| 14 | 格式不支持时不得调用应用自带软件解码器；系统不支持则 fail-closed | 本文档 1.1/1.4 | ⚠️ Android 已收紧；其余平台系统 reader 迁移中 |
| 15 | AI 标识（显式画面 + 显式音频 + 隐式元数据） | `ai_label` / `ai_marking` | ✅ 已实现（源码级）+ 端到端验证 |
| 16 | 日志留存 ≥6 个月 | 本文档 4.3 | ⚠️ 平台层执行 |
| 17 | 开源 NOTICE 页面（协议全文 + CI 审计） | `docs/THIRD_PARTY_NOTICES.md` | ✅ 已产出（CI 待接） |
| 17b | 翻译模型协议核查（m2m100-418M = MIT） | `docs/licenses/m2m100_418M.md` | ✅ 已核查（待负责人签字） |
| 18 | 加密合规声明（ITSAppUsesNonExemptEncryption） | 本文档 5.2 | ⚠️ 提审时执行 |
| 19 | 服务端灾备 | 本文档 5.3 | ⚠️ 平台层执行 |

> 说明：⚠️ 项为平台层/发布流程职责（core 无法执行），已在文档中明确记录，
> 避免上线前遗漏。
