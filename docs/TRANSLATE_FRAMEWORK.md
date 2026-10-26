# 字幕翻译框架（Translate Framework）

> 状态：**M3.2 阶段文档**。记录字幕翻译的架构、模型选型与协议状态、
> 解码链路的关键约束、以及转正前必须补齐的合规动作。
> 合规单一事实来源仍是 `docs/COMPLIANCE.md`；本文不覆盖其结论，只做补充与落地。

---

## 1. 现状速览（代码实测，非规划）

| 项 | 状态 |
|---|---|
| 推理链路（分词 → encoder → beam decode → 反分词） | ✅ 可用 |
| C ABI（`nc_translate_*`） | ✅ 已实现 |
| 单元测试 `Translate.*` | ✅ 7/7 通过 |
| 当前分发模型（`tools/fetch_models.sh`） | `Xenova/opus-mt-zh-en`（OPUS-MT 77M，Apache-2.0） |
| 试验模型（本地，**未入分发**） | `facebook/m2m100_418M`（418M，**协议待核查**） |
| m2m100 质量 | ✅ 14 句抽样全部无复读、无退化标记 |
| m2m100 协议核查 | ✅ **已完成 = MIT**（见 §3，记录 `docs/licenses/m2m100_418M.md`） |
| `translate_ai.cpp/.h` 版本状态 | ⚠️ **未被 git 跟踪** |

**一句话结论**：m2m100 在质量上已解决 OPUS-MT 的复读退化问题，
协议已核查为 **MIT**，满足锁定清单要求；剩余待办是负责人签字与 NOTICE 落地（见 §3.4）。

---

## 2. 分层结构

```
┌─────────────────────────────────────────────┐
│ UI / 业务层                                  │
│   · Translator::lastResultDegenerate() 判病句│
│   · Translator::knownLimitation()  取边界说明│
└──────────────────┬──────────────────────────┘
                   │ C ABI: nc_translate_*
┌──────────────────▼──────────────────────────┐
│ translate_ai（core/src/translate_ai.cpp）    │
│   ├─ Tokenizer   Unigram(Viterbi) / BPE     │
│   ├─ Encoder     encoder.onnx               │
│   ├─ Decoder     decoder.onnx（首步）        │
│   ├─ DecoderPast decoder_with_past.onnx     │
│   └─ Search      beam search + KV cache     │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│ ai_engine（ONNX Runtime 封装）               │
│   · 会话加载 / 张量生命周期 / CPU·GPU 后端   │
└─────────────────────────────────────────────┘
```

### 2.1 模型族差异（`Impl::useBpe` 守卫）

代码用 `useBpe` 区分两类模型。**新增模型必须先判定归属**，否则会静默产生退化输出。

| 特性 | OPUS-MT（Unigram） | m2m100（BPE） |
|---|---|---|
| `decoderStartId` | 65000（显式） | 2（= EOS） |
| `eosId` / `padId` / `unkId` | 0 / 65000 / 2 | 2 / 1 / 3 |
| 源语言标记 | 不需要 | **必须** `__zh__` 前置 |
| 源序列结尾 | 不需要 | **必须** `</s>` |
| 目标语言指定 | 无 | `__en__` 作为 forced BOS |
| 语言标记缺失 | 跳过校验 | `load()` 直接失败 |

---

## 3. 协议状态（关键，勿跳过）

### 3.1 核查结论

| 模型 | 协议 | 核查记录 |
|---|---|---|
| `facebook/m2m100_418M` | ✅ **MIT** | `docs/licenses/m2m100_418M.md` |
| `Xenova/opus-mt-zh-en` | ✅ Apache-2.0 | `tools/fetch_models.sh` 注释 + 上游标签 |

**m2m100 证据链**（2026-10-26 核查）：

1. HF 模型卡 YAML：`license: mit`（归档 `m2m100_418M-MODELCARD.md`）；
2. 官方发布仓库 `facebookresearch/fairseq` LICENSE：
   MIT 全文，`Copyright (c) Facebook, Inc. and its affiliates.`
   （归档 `m2m100_418M-LICENSE.txt`）；
3. HF API 元数据：`tags` 含 `license:mit`，`gated: false`
   （归档 `m2m100_418M-hf-api.json`）。

> **瑕疵（如实记录）**：上游 HF 仓库**没有独立 LICENSE 文件**，
> 许可仅以模型卡 YAML 标签形式声明；ONNX 导出来源 `Xenova/m2m100_418M`
> **自身未声明协议**。二者均不改变 MIT 授权，但正式发布前建议法务确认，
> 或自行从官方权重重新导出 ONNX（见核查记录 §6）。

### 3.2 合规动作已落地

| 动作 | 状态 |
|---|---|
| 独立核查记录 | ✅ `docs/licenses/m2m100_418M.md` |
| `COMPLIANCE.md` §2.1 锁定清单更新 | ✅ 字幕翻译 → m2m100-418M (MIT) |
| `COMPLIANCE.md` §6 校验项 17b | ✅ 已登记 |
| `THIRD_PARTY_NOTICES.md` 增列 + 版权行 | ✅ 已更新 |
| `tools/fetch_models.sh` 下载项 | ✅ 已加入（源已验证存在） |

### 3.3 剩余待办

- [ ] 项目负责人签字确认核查记录
- [ ] 法务确认"模型卡标签声明"的效力（或改用自带 LICENSE 的导出）
- [ ] 记录分发版本的 ONNX 校验和（`sha256`）
- [ ] `translate_ai.cpp/.h` 纳入 git 跟踪

## 4. 解码链路：五个必须遵守的约束

以下每条都对应一次已复现的真实故障。**修改此处前请先读原因。**

### 4.1 源序列格式（仅 BPE 族）

```
[__zh__, ...tokens..., </s>]
```

- **缺 `__zh__`** → 模型判不出源语言 → 输出错语种并复读
  （实测混入谚文/希伯来文、`one one one ...`）；
- **缺结尾 `</s>`** → encoder 末位不是句界标记 → 语义方向对但严重劣化
  （实测 `I I I I ...`、`We We We ...` 直至长度上限）。

实现位置：`translate()` 内 `srcIds` 组装处，`core/src/translate_ai.cpp:477-478`。

### 4.2 首步必须传 `[</s>, __en__]`

m2m100 用 forced BOS 指定目标语言：

| 首步输入 | 结果 |
|---|---|
| `[</s>]` | `128046 128077 ...`（非英文语言标记竞争） |
| `[__en__]` | `to to to ...` |
| `[</s>, __en__]` | ✅ 正确首词 |

### 4.3 KV cache 只覆盖 decoder 侧

实测签名差异：

| 模型 | 输出数 | 含 `present.*.encoder.*` |
|---|---|---|
| `decoder.onnx` | 49 | ✅ 有（12 层 × 4） |
| `decoder_with_past.onnx` | 25 | ❌ **无**（仅 12 层 × 2 decoder KV） |

因此 cache 更新**必须从父假设拷贝起底**：

```cpp
std::map<std::string, AiTensor> nextCache = h.cache;   // 继承 encoder KV
for (...) { if (suffix.find(".encoder.") != npos) continue; ... }
```

整体重建 cache → encoder 槽位缺失 → `AiSession::run` 报
「输入张量无效（形状/数据不匹配）」。

### 4.4 已完成的假设必须与活跃假设分离

**最容易踩的坑。**

若把生成 EOS 的假设与活跃假设混在同一向量里按累积分数排序：

1. 累积分随长度单调增加 → 长序列天然占优；
2. 正确的短译文被挤出 top-k；
3. 表现为「正确译文 + 尾部无限复读」：
   `... great. . . .`、`... English. (more...) (more...) ...`。

正确做法：`finished` / `beams` 两容器，各自维护，
最终**优先在 `finished` 中择优**；仅当无任何假设生成 EOS 时才回退到被截断的活跃假设。

### 4.5 不要设置 `minLen`

历史版本用 `minLen = min(8, 源token数)` 抑制"过早收尾"——那是对 OPUS-MT 的临时规避。
m2m100 的 EOS 预测本身可靠（正确译文末尾 p(EOS) 即为最高分），
强制最小长度反而逼出多余 token（实测 `This video clip is great.` → `... great. . . .`）。

当前值：`minLen = 1`（不限制）。

### 4.6 首步假设数的取舍（已知权衡）

实测对照（同一份代码，仅改首步保留的假设数）：

| 输入 | 首步 1 条 | 首步 6 条 |
|---|---|---|
| 我们明天去公园散步 | ✅ `Tomorrow we take a walk in the park.` | `I am going to a walk in the park.` |
| 谢谢 | `Thanks to.` | ✅ `Thank you.` |

**当前选择 1 条**：整句自然度权重高于极短句；分叉从第二步的 top-k 展开仍可产生多束。
若后续做短句专项优化，可改为**按源句长度动态选择**（短句 → kBeams）。

---

## 5. 质量基线（m2m100 最终代码实测，14 句）

| 输入 | 输出 | 耗时 |
|---|---|---|
| 我爱中国 | I LOVE CHINA | 928 ms |
| 今天天气很好 | The Weather Is Good Today. | 794 ms |
| 这是一个测试 | This is a test. | 582 ms |
| 我们明天去公园散步 | Tomorrow we take a walk in the park. | 1264 ms |
| 这个视频剪辑得很精彩 | This video clip is wonderful. | 709 ms |
| 请帮我把字幕翻译成英文 | Please help me to translate the subtitle into English. | 1591 ms |
| 我不明白你的意思 | I don't understand what you mean. | 1548 ms |
| 请在明天下午三点前提交报告 | Please submit the report before 3 a.m. tomorrow | 1726 ms |
| 人工智能正在改变我们的生活方式 | Artificial intelligence is changing our way of life | 1387 ms |
| 他昨天生病了所以没来上班 | He was sick yesterday, so he didn't go to work. | 2172 ms |
| 孩子们在操场上快乐地玩耍 | Children have fun playing on the playground. | 1524 ms |
| 这部电影的配乐非常动人 | The performance of this film is very exciting. | 1238 ms |
| 请问最近的火车站在哪里 | Ask where the nearest railway station is | 1202 ms |
| 我们需要更多时间来验证这个方案 | We need more time to verify this program. | 1298 ms |

**指标**：14/14 无复读、无退化标记；单句 0.6~2.2 s（CPU 后端）。

### 5.1 已知能力边界（模型固有，非解码缺陷）

- **极短歧义输入易过译**：`你好 → Hello you.`、`这个多少钱 → How much is this.`
  （冗余虚词与标点，不影响理解但不够地道）；
- **疑问语气丢失**：`请问最近的火车站在哪里 → Ask where the nearest railway station is`；
- **时间消歧偏差**：`下午三点 → 3 a.m.`（模型误判为凌晨）；
- **`program` / `plan` 混用**：`这个方案 → this program`。

以上应通过**翻译后处理或术语约束**解决，不应靠解码参数硬压。

---

## 6. 失败处置约定

| 场景 | 行为 |
|---|---|
| 模型未加载 | 返回 false，`"模型未加载"` |
| 分词失败 | 返回 false，`"分词失败（词表未就绪）"` |
| 仅生成 EOS | 返回 false，`"译文为空（模型仅生成结束符）"` |
| 检测到复读 | **返回 true 但不截断**，`lastResultDegenerate() == true` |
| 能力边界说明 | `knownLimitation()` 返回静态文案 |

**原则：不静默给出病句，也不激进截断。**
实测任何截断阈值都会把正常句削成残句，因此只标记、不修改，
由上层决定是否提示用户复核。

---

## 7. 新增/替换翻译模型的标准流程

1. **协议先行**：先取得上游 LICENSE 一手证据，写入 `docs/licenses/<model>.md`；
   未通过**不得进入编码阶段**（`COMPLIANCE.md` §2.3）。
2. **判定模型族**：Unigram 还是 BPE？是否需要语言标记？据此确认 `useBpe` 分支。
3. **确认导出形态**：必须三件分体（`encoder` / `decoder` / `decoder_with_past`）。
   merged 版在 cache 分支下 encoder KV 会收缩，导致退化（OPUS-MT 已踩过）。
4. **校验输入签名**：`decoder` 需含 `encoder_hidden_states`；
   `decoder_with_past` 的 `input_ids` 必须支持 `[1,1]`。
5. **冒烟验证（缺一不可）**：
   - 分词可逆：`decode(encode(x)) == x`
   - 首步 logits：正确首词应显著领先
   - **增量 vs 全序列等价**：同一位置 logits 应一致（验证 cache 正确性）
   - 检查 `present.*` 是否含 encoder 键（决定 cache 更新策略）
   - EOS 可达性：正确译文末尾 p(EOS) 应为最高
6. **回归**：`nairoclip_tests` 中 `Translate.*` 全绿，且另一模型族路径不受影响。
7. **更新文档**：`COMPLIANCE.md` §2.1、`THIRD_PARTY_NOTICES.md`、
   `ROADMAP.md` §5.4/§5.6、`fetch_models.sh`。

---

## 8. 待办（按优先级）

| 优先级 | 事项 | 状态 |
|---|---|---|
| P0 | m2m100 协议核查 | ✅ 已完成（MIT） |
| P0 | 更新 `COMPLIANCE.md` §2.1 锁定清单 | ✅ 已完成 |
| P1 | `fetch_models.sh` 增加 m2m100 下载项 | ✅ 已完成（源已验证） |
| P1 | `THIRD_PARTY_NOTICES.md` 增列 + 版权行 | ✅ 已完成 |
| P1 | 负责人签字确认核查记录 | ⬜ 待办 |
| P1 | `translate_ai.cpp/.h` 纳入 git 跟踪 | ⬜ 待办 |
| P2 | 短句过译、疑问语气、时间消歧的后处理 | ⬜ 待办 |
| P2 | 批量翻译并行化（当前逐句串行） | ⬜ 待办 |
| P2 | `ROADMAP.md` §5.6 同步本文结论 | ⬜ 待办 |

---

## 附：相关文档

- `docs/MASTER_FRAMEWORK.md` — **全项目总纲**（能力矩阵、缺口排序、补全路线）
- `docs/COMPLIANCE.md` — 合规单一事实来源（§2 模型协议，§2.3 变更流程）
- `docs/THIRD_PARTY_NOTICES.md` — 协议全文与版权声明
- `docs/licenses/m2m100_418M.md` — m2m100 协议核查记录
- `docs/ROADMAP.md` §5.6 — 字幕翻译本地部署结论（部分内容已被本文取代）
- `docs/ARCHITECTURE.md` — 分层总览
