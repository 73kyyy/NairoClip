# NairoClip 后续开发框架（Roadmap & Framework）

> 本文是**后续开发的接续框架**：定义模块边界、目录约定、扩展点与里程碑。
> 不含 UI 视觉稿/贴图方案——界面一律由代码生成（SVG 图标 + CSS token，
> 见 `mobile/assets/icons.js` 与 `docs/../design/spec/01-design-direction.md`）。
>
> 衔接基线：截至 M2.32，`core/` 73 个模块、`capi.h` 232 个 C ABI、
> `mobile/` 纯前端（WebView 承载）。
>
> 合规基线：`docs/COMPLIANCE.md` 是**单一事实来源**，任何模型变更必须先行核对。

---

## 一、分层与依赖方向（强制）

```
┌───────────────────────────────────────────────────────┐
│ L4 平台层  android(Kotlin) / ios(Swift) / desktop(Qt)  │
│     职责：生命周期、权限、存储、系统 UI                    │
├───────────────────────────────────────────────────────┤
│ L3 前端    mobile/assets/*.js（WebView 内运行）          │
│     职责：交互、布局、面板规格；不含算法                    │
├───────────────────────────────────────────────────────┤
│ L2 绑定    nc_jni.cpp / NativeEngineBridge.swift        │
│            C ABI（capi.h）：稳定 ABI，版本化              │
├───────────────────────────────────────────────────────┤
│ L1 服务    core/ 各功能域模块（下述六大域）                │
│     职责：所有算法、状态、序列化；零平台 API 依赖            │
├───────────────────────────────────────────────────────┤
│ L0 基础    rational / time / json / crypto              │
└───────────────────────────────────────────────────────┘
```

**依赖规则（代码审查硬约束）**

| 规则 | 说明 |
|---|---|
| D1 单向依赖 | 只允许 L4→L3→L2→L1→L0；反向依赖一律拒绝 |
| D2 core 零平台 API | `core/` 不得 include 任何平台头（`jni.h`/`Foundation`/`windows.h`） |
| D3 core 零第三方（除白名单） | 仅允许 FFmpeg(动态) / ONNX Runtime / zlib；新增需过合规 |
| D4 平台能力注入 | 解码器、编码器、渲染后端等由平台层实现接口后注入 |
| D5 C ABI 只增不改 | 已发布函数签名冻结；破坏性变更需新版本前缀 `nc2_` |

---

## 二、core 六大功能域

按现有 73 个模块归并，新增代码必须落在下列域之一，不得游离。

### 2.1 编辑内核（Editing Kernel）

| 模块 | 职责 |
|---|---|
| `rational` `time` | 精确时间模型（int64 有理数，禁浮点秒） |
| `clip` `track` `sequence` `project` | 数据模型与不变量 |
| `commands` `editor` | Command 模式 + 撤销栈 + 门面 |
| `keyframe` `speed_curve` `animation` | 关键帧/缓动/变速 |
| `subtitle` `subtitle_io` | 字幕模型与 SRT/WebVTT 交换 |
| `chapter` `beat_detect` `auto_duck` `auto_edit` | 智能化编辑 |
| `smart_movie` `batch_ops` `reframe` | 批量/重构/成片 |
| `serialization` `recovery` `project_doctor` | 持久化/崩溃恢复/自检 |
| `heartbeat` | 会员心跳 |

**扩展点**：`ICommand`（新增可撤销操作）、`IClipSource`（新素材类型）。

### 2.2 渲染与特效（Rendering & FX）

| 模块 | 职责 |
|---|---|
| `render_engine` | 质量分级（Preview/Export）+ 后端契约 `IRenderBackend` |
| `effects` `spatial_effects` `screen_effects` | 颜色/空间/画面特效 |
| `atmosphere_effects` `motion_effects` `person_effects` | 氛围/动感/人物 |
| `particle_engine` | 粒子系统（雪花/雨/光斑） |
| `filters` `blend_modes` `mask` | LUT 风格/混合/蒙版 |
| `transition` `transition_render` | 转场模型与渲染（34 种） |
| `frame_fit` | 画幅适配（fit/fill/crop） |

**扩展点**：`IEffectPass`（新增逐像素特效）、`IRenderBackend`（GPU 后端注入）。

**不变量**：新增特效必须同时接入**预览与导出**两条路径，否则视为假功能。

### 2.3 媒体 I/O（Media I/O）

| 模块 | 职责 |
|---|---|
| `media_io` `media_ffmpeg` `media_ffmpeg_writer` | 解码/编码抽象与 FFmpeg 实现 |
| `media_writer` `media_offline` | 写入契约与离线检测 |
| `codec_policy` | 编码器白名单（合规强制） |
| `export_plan` `export_executor` `export_runner` | 导出 IR / 执行 / 运行器 |
| `export_preset` `export_queue` | 预设与队列 |
| `audio_mixer` `audio_denoise` `waveform` | 混音/降噪/波形 |
| `preview` `thumbnail` `proxy` | 预览/缩略图/代理 |

**扩展点**：`IMediaReader` / `IMediaWriter`（平台编解码器注入）。

### 2.4 AI 推理（AI Inference）

| 模块 | 职责 | 合规状态 |
|---|---|---|
| `ai_engine` | `AiSession`：ONNX Runtime 封装、张量契约 | MIT ✅ |
| `model_manager` | 分发状态机/断点续传/SHA-256/AES | — |
| `ai_task` `ai_check` | 任务队列 / 质量检查 | — |
| `matting_ai` | 人像抠像 | ⚠️ 见 §5 |
| `depth_ai` | 深度图（MiDaS） | ✅ |
| `interpolation_ai` | 补帧 | ⚠️ 见 §5 |
| `super_res_ai` | 超分 2x/4x | ⚠️ 见 §5 |
| `stem_ai` | 人声分离 | ⚠️ 见 §5 |
| `speech_ai` `speech_cut` | 语音转字幕 / 口播精剪 | ✅ |
| `translate_ai` | 字幕翻译 | ⚠️ 见 §5 |
| `background_fill` `chroma_key` | 背景填充 / 绿幕 | — |

**扩展点**：新增 AI 功能的标准四步（见 §4）。

### 2.5 合规与安全（Compliance & Security）

| 模块 | 职责 |
|---|---|
| `ai_label` | 画面显式标识（不可关闭） |
| `ai_marking` | 音频提示音 + 隐式元数据 |
| `invisible_watermark` | 账号溯源隐形水印 |
| `entitlement` | 会员权益矩阵（单一事实来源） |
| `watermark` | 导出水印（免费版） |
| `crypto` | 加解密基元 |
| `codec_policy` | 编码器合规门 |

**不变量**：合规相关逻辑**不得**被功能开关绕过；`entitlement` 到期即降级（fail-closed）。

### 2.6 绑定层（Binding）

| 模块 | 职责 |
|---|---|
| `capi` | 232 个 C ABI（`nc_*`），JSON in/out |
| `json` | 零依赖 JSON（ABI 的数据格式） |

**契约**：所有 ABI 返回 `int`（0=OK，负=错误码）+ 出参；字符串由 `nc_free_string` 释放。

---

## 三、目录约定（新增文件必须遵守）

```
core/
  include/nairoclip/core/<module>.h    # 一个模块一个头，公开 API
  src/<module>.cpp                     # 实现，注册进 core/CMakeLists.txt
mobile/
  assets/                              # 前端 JS（无框架、无打包步骤）
    l3-spec.js                         # 面板/参数规格（单一事实来源）
    l3-panel.js                        # 面板渲染
    icons.js                           # SVG 图标（代码生成，禁图片）
    nc-api.js                          # C ABI 包装（自动生成）
  native/android/nc_jni.cpp            # JNI 桥
  native/ios/NativeEngineBridge.swift  # iOS 桥
tests/
  test_<module>.cpp                    # 与模块同名；注册进 tests/CMakeLists.txt
tools/
  fetch_models.sh                      # 模型权重获取（不入 git）
docs/
  ARCHITECTURE.md                      # 历史里程碑（只追加）
  ROADMAP.md                           # 本文（后续框架）
  COMPLIANCE.md                        # 合规单一事实来源
```

**命名约定**

| 对象 | 约定 | 示例 |
|---|---|---|
| 头文件/源文件 | snake_case | `super_res_ai.h` |
| 类型 | PascalCase | `SuperResEstimator` |
| 函数/变量 | camelCase | `upscaleBilinear` |
| 常量 | `k` + PascalCase | `kDefaultTile` |
| C ABI | `nc_` + snake_case | `nc_export_run` |
| 前端文件 | `l3-*.js` | `l3-panel.js` |

---

## 四、新增 AI 功能的标准流程（四步）

新增一个 AI 能力，按此顺序推进，每步可独立验证：

**Step 1 · 合规前置（不可跳过）**

- 在 `docs/COMPLIANCE.md` §2.1 查到锁定模型；**若目标模型不在清单**，
  先提交协议核查（§2.3 变更流程），**不得先写代码再补合规**。
- 记录：模型名、协议、来源、权重文件 SHA-256。

**Step 2 · 接入清单**

- `core/include/nairoclip/core/<name>_ai.h`：定义 Estimator 类 + 统计结构；
- `core/src/<name>_ai.cpp`：实现，失败一律 fail-closed（不伪造输出）；
- `core/CMakeLists.txt` 注册源文件；
- `tools/fetch_models.sh` 加入权重下载项（含 tokenizer/config 等附属文件）。

**Step 3 · C ABI 暴露**

- `capi.h` 增加 `nc_<name>_*` 函数（只增不改）；
- 参数走 JSON，错误信息带明确 reason；
- 重新生成 `mobile/assets/nc-api.js`。

**Step 4 · 端到端验证（真数据）**

- 用真实模型跑真实输入，**断言输出语义正确**（不只是"返回成功"）；
- 若模型能力受限，必须在头文件与文档中**如实记录限制**，不得包装成可用。

---

## 五、当前真实缺口（代码实测，非规划）

> 核对时间：2026-10-26。状态以代码与实测为准。

### 5.1 合规偏离（最高优先级）

`tools/fetch_models.sh` 实际下载的模型与 `COMPLIANCE.md` §2.1 锁定清单**不一致**：

| 功能 | 锁定要求 | 当前实际 | 处置 |
|---|---|---|---|
| 人像抠像 | MediaPipe Selfie Segmentation (Apache-2.0) | `briaai/RMBG-1.4` | **待换源**；RMBG 系列为非商用许可，**发布前必须替换** |
| 光流补帧 | RIFE (MIT) | `opencv/optical_flow_estimation_raft` | 待换源（RAFT 非锁定） |
| 超分辨率 | Real-ESRGAN (BSD-3-Clause) | `Xenova/swin2SR` | 待换源 |
| 字幕翻译 | MADLAD-400 (Apache-2.0，首阶段云端 API) | `Xenova/opus-mt-zh-en` | **待换源**；且当前导出质量不足（见 5.2） |
| 人声分离 | UVR-MDX-NET-**Voc_FT** | `uvr-mdxnet-kara2` | 同族但非锁定权重，待核对 |
| TTS | VITS **官方 LJSpeech/VCTK** 权重 | `ccds/vits_onnx` (jp_cn) | 来源不明，待换源 |
| 语音转字幕 | whisper.cpp | `onnx-community/whisper-small` | 同族；需确认权重协议一致 |
| 深度图 | MiDaS (MIT) | 未下载 | 待补 |

**处置原则**：本次不擅自更换模型（涉及协议核查与体积评估），
先在框架中登记为已知缺口，按 §2.3 流程单独处理。

### 5.2 功能实现缺口

| 功能 | 状态 | 说明 |
|---|---|---|
| 超分辨率 | ✅ 已实现 `super_res_ai` + C ABI + 测试 | 分块推理 + 羽化拼接 + 逐块双线性回退 |
| 字幕翻译 | ⚠️ 已实现 + C ABI + 测试，质量受模型导出限制 | 见下 |
| 深度图 | ✅ 已接入 MiDaS（锁定清单内）并修正预处理 | 见 §5.3 |
| 镜头追踪 | ⬜ 仅枚举 | 需 OpenCV CSRT/KCF |
| 滤镜 LUT | ⬜ 无 C ABI 入口 | `nc_set_lut` 待加 |
| 模型分发服务端 | ⬜ 仅客户端 | 服务端需另行部署 |

**字幕翻译受限详情**（必须如实记录）：

- 分词（SentencePiece Unigram Viterbi）、encoder 前向、KV cache 复用均已跑通；
- 首步 logits 正确（「我爱中国」→ `China` 8.95 / `I` 7.94 / `Chinese` 6.24）；
- **但第 4 个 token 起分布退化并复读**；
- 已排除：beam search(6)、no-cache 全序列重传、mask 变长、decoder_start 取值、
  KV 冻结/覆盖策略；
- 根因：该 merged decoder 导出在 cache 分支下 `present.*.encoder.*` 从源长度
  收缩为 1，属**上游导出缺陷**；
- 结论：需改用非 merged 导出或更换模型（走 §2.3 合规流程）后重测。

### 5.3 合规入口与实际进展（M3.1 部分完成）

**已落地**：

| 项 | 内容 |
|---|---|
| 法律文本 | `mobile/assets/legal/legal-text.js`：5 篇（用户协议 8 条 / 隐私政策 7 条 / 版权与许可 4 条 / AI 标识说明 6 条 / AI 功能说明 3 条），单一事实来源 |
| 阅读页 | `mobile/pages/05-legal.html` + `legal-page.js`：支持 `?doc=` 与 `#sec-N` 定位，可复制全文 |
| 全局入口 | 设置页 5 处、我的页 5 处、**每个功能面板标题右侧的「?」**（`l3-panel.js` 的 `LEGAL_BY_PANEL` 映射，按功能类别指向对应条款） |
| 文案一致性 | `AI_LABEL`（`ai_generated=true` / `ai_generated_by=NairoClip`）与引擎 `ai_marking.cpp` 逐字核对一致 |

**法律依据已文本化**：《人工智能生成合成内容标识办法》（2025-09-01 施行）第九条（不含显式标识版本需留存 ≥6 个月日志）写入用户协议 §5 与隐私政策 §4。

### 5.4 模型替换进度（按锁定清单逐项核实）

| 功能 | 锁定模型 | 核实结果 | 处置 |
|---|---|---|---|
| 深度图 | MiDaS (MIT) | ✅ `Xenova/dpt-hybrid-midas` 可用；实测输入 384、mean/std=0.5；已修正预处理并验证深度梯度 | 完成 |
| 人像抠像 | MediaPipe Selfie Seg (Apache-2.0) | ❌ Qualcomm 导出版**实测对真实人像输出恒 0**（sigmoid 后 0.5000）；官方仅 `.tflite`，环境无法装 TensorFlow 转换 | 已移除坏模型，保持 fail-closed |
| 光流补帧 | RIFE (MIT) | ❌ HF 上多个仓库 401，无可用 ONNX | 待取得可用导出 |
| 超分辨率 | Real-ESRGAN (BSD-3) | ❌ `ai-forever/Real-ESRGAN` 仅 `.pth`；`Xenova/real-esrgan` 401 | 需自行转换（需 torch） |
| 人声分离 | UVR-MDX-NET-**Voc_FT** (MIT) | ⚠️ 现为 kara2 变体，非 Voc_FT | 待核实差异 |
| TTS | VITS 官方权重 (MIT) | ❌ `Xenova/vits-ljspeech` / `vits-vctk` 均 401 | 待取得官方权重 |
| 字幕翻译 | MADLAD-400 (Apache-2.0) | ✅ 本地部署（**不做云端**，按项目决策）；改用 OPUS-MT-ZH-EN 分体导出 | 推理链路跑通，质量受限，见 §5.6 |

> 结论：7 项中 1 项完成、1 项按协议改道、5 项受限于「无可用 ONNX 导出」。
> 这些是**外部资源限制**，不是代码问题；均已在 `fetch_models.sh` 注释中写明原因，
> 避免"有文件但跑不通"的假可用状态。

### 5.5 本轮已修复

| 缺陷 | 修复 |
|---|---|
| 深度图预处理用 ImageNet 均值/方差（Depth-Anything 约定），与锁定模型 DPT-Hybrid/MiDaS 的 `preprocessor_config.json`（mean/std = 0.5）不符 → 深度对比度与远近关系失真 | `depth_ai.cpp` 改为 0.5/0.5 |
| 深度图输入边长写死 518（Depth-Anything 尺寸），与 MiDaS 要求的 384 不符 | `inputSize()` 改为 384 |
| 深度图模型缺失（`fetch_models.sh` 未列出） | 补入 `Xenova/dpt-hybrid-midas`（上游 Apache-2.0）；实测输出尺寸正确、左右亮度差 → 深度梯度 13.3 |
| 翻译缺 tokenizer/config 下载项 → 无法离线部署 | `fetch_models.sh` 补入三件 |
| 超分/翻译无 C ABI | 新增 `nc_superres_*` / `nc_translate_*`（共 4+5 个），前端包装 232 → 236 |
| 转场前端 id 从 1 起、与引擎枚举（0..33）错位 | 前端 34 项与枚举严格对齐，名称逐字一致 |
| 特效「发光轮廓」id 写成 37（引擎真实值 35） | 改为 35 |

> 测试基线：1146 用例 / 0 失败（新增 `test_super_res_ai` 与 `test_translate_ai` 共 14 例）。

### 5.6 字幕翻译：本地部署结论（不做云端）

**决策**：按项目要求，字幕翻译**必须本地运行**，不接云端 API。
因此放弃 MADLAD-400（3B，ONNX 转换需 torch + 数十 GB 显存），
改用 OPUS-MT-ZH-EN（77M，可本地实时运行）。

**已实现的正确部分**（均经实测）：

| 环节 | 状态 |
|---|---|
| SentencePiece Unigram 分词（Viterbi 最大似然） | ✅ 「我爱中国」→ 2 tokens |
| encoder 前向 | ✅ 45009→hidden[1,2,512] |
| **分体导出**（decoder_model + decoder_with_past_model） | ✅ 绕过 merged 版的 encoder KV 收缩缺陷 |
| KV cache 复用（encoder 侧首步固定、decoder 侧逐步更新） | ✅ 形状与语义均正确 |
| beam search（6 束 + GNMT 长度归一化） | ✅ 6 条 beam 正常收敛 |
| 首步 logits 正确性 | ✅ `China` 8.95 / `I` 7.94 / `Chinese` 6.24 |

**未解决的能力边界（模型固有，非代码缺陷）**：

实测 6 条 beam 对「我们明天去公园散步」**全部收敛到同一退化路径**：
```
We are going to the park park park for a walk - for a walk in the park park ...
```
- EOS logit 仅 1.0~2.4，普通 token 为 6~9 → 模型本身不倾向收尾；
- 前 3~6 个词准确（`China I love` / `Today was a very good`），之后进入循环；
- 已尝试并排除：贪心 / beam(6) / 长度惩罚 0.6~1.0 / minLen 约束 /
  n-gram 禁止 / 多档截断阈值 —— 任何激进截断都会把正常句削成残句。

**当前处置**（诚实优先）：

- 输出保留完整结果，通过 `Translator::lastResultDegenerate()` 标记是否含重复；
- `Translator::knownLimitation()` 如实返回能力边界文案；
- 上层可据此提示用户复核，而不是静默给出病句。

**根治路径 —— 已实施（2026-10-26）**：

已接入 `m2m100-418M` 并实测验证：

- **协议**：MIT（核查记录 `docs/licenses/m2m100_418M.md`，含 fairseq LICENSE 全文）；
- **效果**：14 句抽样**全部无复读**，OPUS-MT 时代的 `park park park` 类退化彻底消失；
- **代价**：模型体积 1.9GB FP32（OPUS-MT 为 1.1GB），单句 0.6~2.2s。

修复的 4 个叠加缺陷与完整约束说明见 `docs/TRANSLATE_FRAMEWORK.md` §4：
缺 `__zh__`、缺结尾 `</s>`、beam 混排已完成假设、`minLen` 强制长度。

> 下文表格中"EOS logit 仅 1.0~2.4 / 模型本身不倾向收尾"等结论
> **仅适用于 OPUS-MT**，不代表 m2m100 行为。

---

## 六、里程碑（M3.x）

按依赖排序，每项以「可独立验证」为完成标准。

### M3.1 合规兑现（阻塞发布）

- [ ] 逐项替换 §5.1 偏离模型，更新 `fetch_models.sh` 与 `COMPLIANCE.md`；
- [ ] 为每个锁定模型记录 SHA-256 与协议快照；
- [ ] 新增 `tests/test_compliance_models.cpp`：校验下载清单 ⊆ 锁定清单。

**验收**：清单一致性测试通过；无禁用清单模型（NLLB/RVM/GFPGAN 等）。

### M3.2 字幕翻译可用

- [ ] 按合规要求接入 MADLAD-400（或获批替代）；
- [ ] 实现多语言方向（至少 zh→en / en→zh）；
- [ ] 长句分段 + 长度约束（字幕单行 ≤ 42 字符）；
- [ ] `translateLines` 批量 + 失败逐句降级（保留原文）。

**验收**：100 句中文新闻稿，人工抽检语义正确率 ≥ 90%，无复读。

### M3.3 超分辨率端到端

- [ ] 换 Real-ESRGAN 权重，适配其输入尺寸与归一化；
- [ ] 接入导出管线（`export_plan` 增加 `superRes` 节点）；
- [ ] 预览路径按可视区域裁剪（避免整帧 4x 卡顿）；
- [ ] 4x 内存上限保护（分块失败即回退，不 OOM）。

**验收**：1080p → 4K 真实导出，PSNR 相对双线性提升 ≥ 2dB。

### M3.4 滤镜 LUT

- [ ] `nc_set_lut` / `nc_get_lut`（文件路径 + 强度）；
- [ ] `.cube` 解析（1D/3D，容错）；
- [ ] 与 `FilterStyle` 并存（LUT 优先）；
- [ ] 序列化 + 前端入口。

**验收**：加载标准 `.cube` 后导出像素与参考实现误差 < 1/255。

### M3.5 镜头追踪

- [ ] 接入 OpenCV CSRT（`tracking` 已成 C ABI 占位）；
- [ ] 追踪点 → 字幕/贴纸锚点插值（`SubtitleCue::tracking` 已定义）；
- [ ] 追踪失败自动回退静态锚点并提示。

**验收**：10 段手持素材，追踪框漂移 < 5% 画面短边。

### M3.6 GPU 渲染后端

- [ ] 实现 `IRenderBackend` 的 GLES3 后端（Android 先行）；
- [ ] 特效 pass 与 CPU 参考实现像素级对齐（容差 ≤ 2/255）；
- [ ] 预览走 GPU、导出默认 CPU（可切换）。

**验收**：预览 1080p ≥ 30fps（中端机）；导出结果与 CPU 路径一致。

### M3.7 模型分发服务端对接

- [ ] 客户端按设备分级请求（`model_manager` 已留字段）；
- [ ] 签名 URL 校验 + 断点续传 + SHA-256 校验；
- [ ] 服务端：`/models/{feature}/{tier}` 返回签名 URL。

**验收**：弱网（限速 200KB/s）下断点续传成功率 100%。

---

## 七、质量门（每次提交前）

| 门 | 命令 | 要求 |
|---|---|---|
| 构建 | `cmake --build build --parallel` | 零 error；warning 不新增 |
| 测试 | `cd build && ctest --output-on-failure` | 全绿 |
| 合规 | 人工核对 §2.1 | 未引入清单外模型 |
| 假功能 | 人工核对 | 新特效/新 AI 必须预览+导出双通 |

> 出包（APK/IPA）时由 CI 剔除 `tests/` 与 `*/tools/`，仓库内保留供回归，
> 见 `.github/workflows/android-apk.yml` 的 `Prune dev-only test assets` 步骤。

---

## 八、编码约定（补充）

- **禁止浮点秒**：所有时间用 `Time`（有理数）；
- **禁止静默降级**：能力不可用时返回明确错误，不返回"看起来成功"的假数据；
- **禁止占位实现**：`TODO`/空壳函数不得合入主干；
- **序列化兼容**：新增字段必须可缺省（旧工程可打开）；默认值不写入；
- **文本入库存疑即拒绝**：解析外部文件（`.nclip`/SRT/`.cube`）必须容错 + 边界校验。
