# S1-5a-A · 编解码静态探针报告

- 仓库：`73kyyy/NairoClip`
- 分支：`work/v3.3.3-full-remediation`
- 探针类型：静态源码/构建配置审查（不代表设备能力或真实帧验收）
- 结论：**FAIL-CLOSED REQUIRED**。当前编码器许可分类和 FFmpeg Android 构建白名单存在与“不得引入需要支付专利授权费的编解码实现”不一致的风险，禁止据此直接发布。

## 1. 证据文件与基线

| 文件 | 本次读取的 Blob SHA | 观察 |
|---|---|---|
| `ffmpeg-build/VERSION` | `0f0fefae5ac96803e7227191df7a6974709eb45d` | 当前版本为 7.1 |
| `ffmpeg-build/build-android.sh` | `de443abdbe7aa460bf430084f15c601fa8406ad6` | 明确启用多个软件解码器及 AAC/PCM 软件编码器 |
| `core/src/codec_policy.cpp` | `767783511bdff40dd6160aec721b9540ebf95f41` | 将 AAC、VP9、Opus、AV1、FLAC 等归入宽松软件白名单 |
| `core/src/media_ffmpeg.cpp` | `f2a83ad8295959ceab6b28bf1242e7110642a5e4` | Android H.264 仅尝试 h264_mediacodec；非 H.264 视频通用选择 FFmpeg 解码器 |
| `core/src/media_ffmpeg_writer.cpp` | `c5abba7b9bbd264530105d1987a1d34fd09128e0` | 视频编码器由策略候选选择；音频默认 AAC，策略可选择 AAC/Opus |
| `core/include/nairoclip/core/codec_policy.h` | `99df4843cc9a66d215cad3efb70adf96b08c4d49` | 注释仍把 VP9/AAC 列为 royalty-free 宽松候选 |

## 2. 当前 Android FFmpeg 构建实际启用项

来自 `ffmpeg-build/build-android.sh`：

- 软件/包装解码器：`h264_mediacodec`、`mpeg4`、`vp8`、`vp9`、`av1`、`aac`、`mp3`、`opus`、`flac`、`vorbis`、`pcm_s16le`、`pcm_s16be`。
- 软件编码器：`aac`、`pcm_s16le`。
- demuxer：`mov`、`matroska`、`avi`、`mp3`、`wav`、`flac`、`ogg`、`aac`、`mpegts`。
- muxer：`mp4`、`mov`、`matroska`、`wav`、`mp3`。
- parser：`h264`、`hevc`、`vp9`、`av1`、`aac`、`mpegaudio`、`opus`、`flac`。

注意：parser/demuxer 存在不等于 decoder/encoder 存在；静态注册也不等于设备真实可用。

## 3. 当前策略缺陷

1. `codec_policy.cpp::isPermissiveSoftware()` 把 AAC、VP9 直接视为宽松许可实现，并把 AAC “专利已基本到期”、AV1/VP9 “royalty-free”写成结论。版权许可证（BSD/LGPL）不能证明专利许可覆盖。
2. `codecComplianceStatement()` 将 AV1/VP9/Opus/AAC 一概称为 BSD / royalty-free，范围过宽，不能作为面向用户的合规声明。
3. Android 构建脚本启用了 AAC 软件编码器，和“所有软件编解码路径都必须有免专利费依据”的要求冲突；默认导出音频路径亦以 AAC 为主。
4. 当前脚本启用 MPEG-4 Part 2、VP8/VP9、MP3、Vorbis 等软件解码器。它们不能仅凭 FFmpeg 的 LGPL 或实现代码的 BSD 许可证判定无需专利授权。
5. `media_ffmpeg.cpp` 的非 H.264 视频路径按 codec id 直接调用 FFmpeg decoder；尚未形成统一的“已审查格式 allowlist + 运行时拒绝”策略。
6. `media_ffmpeg.cpp` 的音频解码路径对容器中的音频 codec 直接尝试 FFmpeg decoder，未见与本项目零专利费策略绑定的格式白名单。

## 4. 专利/版权证据初步分类

| 格式/实现 | 当前判断 | 后续处理 |
|---|---|---|
| AV1 | AOMedia Patent License 1.0 对必要权利提供 no-charge / royalty-free 专利许可，但有许可条件及随产品附带许可文本的要求 | 可作为候选；核验实现代码许可证、实际专利许可覆盖和 NOTICE 交付后才允许启用 |
| Opus | Xiph/Opus 官方说明提供免版税专利许可，参考实现为 BSD | 优先候选；核对实际采用实现、许可文本及防御性终止条件 |
| FLAC | Xiph 官方说明格式及实现方法无已知专利，格式商业使用无费用；参考库为 BSD | 优先候选；核对实际 FFmpeg 内置实现所涉文件版权声明 |
| PCM | 未压缩样本表示，不属于有损压缩 codec | 可作为基础路径；仍需核对容器/封装与具体实现依赖 |
| AAC | 现有代码注释不足以证明全球目标市场/产品场景下零授权费 | 暂停软件编码/解码白名单，待法律/专利证据闭合 |
| VP8 / VP9 | BSD 代码许可证不等于专利许可；存在第三方专利主张/许可不确定性 | 暂不列入免授权白名单，完成独立专利审查前 fail-closed |
| MPEG-4 Part 2 | 存在专利许可历史和区域/期限差异 | 暂停软件解码，除非取得覆盖产品发行场景的零费用依据 |
| MP3 | 专利到期信息需按地区和实现/用途核实，不能用过期印象代替全球发布审查 | 暂不列入白名单，待证据闭合 |
| Vorbis | Xiph 的专利立场/实现许可需与目标市场及第三方权利主张分开核验 | 暂不列入白名单，待证据闭合 |
| H.264 / H.265 | 本项目既定红线禁止自有软件编解码实现；H.265 软件解码/编码禁止 | 仅评估操作系统硬件能力，不引入软件实现；不得宣称系统 API 自动免除应用方专利义务 |

## 5. 与任务注册表的执行顺序

- 本报告完成的是 S1-5a-A 的静态探针交付，不代表 S1-5a-B 设备能力探针或 S1-5a-C 真实帧编码探针完成。
- `ffmpeg-build/build-android.sh` 是 S1-5a-C、S6-1、S4-2a 三方共享文件。依照 `NC-V3.3.3-FILE-OWNERSHIP.md`，在对应探针/构建交付前不直接修改该文件，避免越过文件所有权和批次顺序。
- S4-2a 仍必须遵守任务注册表：允许评估 `hevc_mediacodec` 包装器，禁止启用 FFmpeg 自有 `hevc` 软件解码器；parser 不能作为解码能力证据。
- FFmpeg 当前版本 7.1 与此前拟定的 9.0.2 方案不一致。升级前必须核对 API 变更、FFmpeg wrapper、NDK 构建、ABI 与 APK 依赖，不能只改 VERSION 文件。

## 6. 官方证据来源

- FFmpeg 官方下载/版本页：<https://ffmpeg.org/download.html>（当前列出 9.0.2；本仓库当前 VERSION 仍为 7.1）。
- FFmpeg 官方法律与 LGPL 合规说明：<https://www.ffmpeg.org/legal.html>。明确 LGPL/GPL 合规不等于专利授权。
- AOMedia Patent License 1.0：<https://aomedia.org/license/patent/>。必要权利的 royalty-free 授权及随二进制附带许可文本等条件。
- AOMedia 专利政策 FAQ：<https://aomedia.org/about/faqs/>。
- Xiph FLAC License：<https://www.xiph.org/flac/license.html>。商业使用无格式授权费；官方称无已知专利。
- Opus License：<https://github.com/xiph/opus-website/blob/master/license.md>。官方说明参考实现及必要专利许可可免费商业集成。

## 7. 结论与验收状态

**S1-5a-A：静态探针已完成，发现阻断项。** 当前 Android FFmpeg 构建配置和 codec policy 不能证明满足“任何软件编解码器均不得引入需要付费的专利授权”要求。AAC、VP8/VP9、MPEG-4、MP3、Vorbis 等先按未获准处理；AV1、Opus、FLAC、PCM 作为候选而非未经条件的自动批准。

尚未完成：构建、真实 APK 二进制注册表核验、目标市场法律意见、S1-5a-B 设备探针、S1-5a-C 真实帧探针、S6-1 构建验证及 S4-2a 真机 HEVC 能力验证。不得把本报告写成最终法律意见或设备验收通过。
