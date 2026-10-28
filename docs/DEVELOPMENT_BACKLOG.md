# 待开发功能清单（Backlog）

> **本文是功能开发的唯一待办清单**。按优先级与依赖排序，每项给出：
> 目标、现状依据、技术方案、交付标准、预估。
>
> 数据基线：2026-10-27 仓库实测（1326 用例 0 失败）。

> **测试分片**：全量单次约 9 分钟，在回合预算受限的环境里容易被截断。
> 测试框架支持 `NC_TEST_SHARDS=N NC_TEST_SHARD=i` 环墋变量分段执行，
> 按注册顺序取模分片（稳定可复现）。
> 骨架体检结论见 `docs/ARCHITECTURE_AUDIT.md`。
>
> **状态标记**：✅ 已完成 · 🔄 进行中 · ⬜ 待开发 · ⛔ 阻塞

---

## 一、总览

| 批次 | 主题 | 项数 | 阻塞 | 预估 |
|---|---|---|---|---|
| **B0** | 骨架补强（先做，避免技术债） | 8 | 无 | 1.7 周 |
| **B1** | 特效扩展（你已确认） | 5 | 无 | 1 周 |
| **B2** | 轨道与时间轴系统 | 11 | 无 | 2.5 周 |
| **B3** | 卡点快剪（动漫超燃品类） | 4 | 无 | 1 周 |
| **B4** | 参考视频分析（风格迁移） | 3 | 无 | 1 周 |
| **B5** | AI 能力补齐 | 3 | ⛔ 抠像模型 | — |
| **B6** | 性能与端侧 | 3 | 环境 | — |

**优先顺序理由**：B0 是其他一切的地基；B1/B3 不依赖外部模型且能立刻见到效果；
B2 是编辑器专业度的基础（轨道成组/重排/调整图层）；B4 是差异化；B5 被模型阻塞。

> **2026-10-27 变更**：原 B2「模板系统」调整编号为 B2，但内容扩展为
> 「轨道与时间轴系统」，模板功能移至 B2-1~B2-4 之后。新增 B2-5~B2-11。

---

## 二、B0 骨架补强（优先）

> 依据 `docs/ARCHITECTURE_AUDIT.md`。这些不是新功能，但**不做会在 B1~B4 中大爆**。

### B0-1 ✅ `Rational` 溢出保护（P0，最高）

**问题**：`core/src/rational.cpp:17` 在无 `__int128` 的平台（MSVC）
`Wide = std::int64_t`，乘法静默溢出。

```cpp
#if defined(__SIZEOF_INT128__)
using Wide = __int128;
#else
using Wide = std::int64_t;   // ← Windows 上溢出无声
#endif
```

**后果**：Windows 上长时间线（> 数小时）或高帧率下，时间计算错误 →
片段错位。这是**静默数据损坏**，比崩溃更危险。

**方案**：

1. `Wide` 改成不依赖编译器的 128 位（如自实现 mul64x64→128）；
2. 或在 `narrow()` 中检测溢出，溢出时饱和并记录（至少不静默）；
3. 补测试：`Rational::make(INT64_MAX, 2) * 2` 的行为必须被断言。

**交付**：三平台（Linux/macOS/Windows）测试一致。

---

### B0-2 ✅ 线程模型明确化（P0）

**问题**：全库 `std::mutex`/`std::atomic` **0 处**，`static` 可变状态 0 处
（这点很好），但因此**任何并发调用都不安全**。

已知受影响：

| 组件 | 共享可变状态 |
|---|---|
| `Translator` | `lastDegenerate_` |
| `AiSession` | ORT session（本身线程安全，但上层无保护） |
| `TimelineEditor` | `undoStack`、`project` |

**后果**：UI 线程与导出线程同时操作 `Editor` → 数据竞争 → 随机崩溃或数据损坏。

**方案**：

1. 明确契约：`TimelineEditor` 与 `Translator` **非线程安全**，写进头文件；
2. 导出走快照：导出开始时 `Project` 深拷贝，之后 UI 改动不影响导出；
3. 或加 `std::mutex` + `std::lock_guard`（成本低，先做这个）。

**交付**：头文件注明线程契约 + 一个多线程压力测试。

---

### B0-3 ✅ 错误路径统一（C ABI 层）

**问题**：`Result`（247 处）与裸 `bool`（465 处）混用。
`bool` 失败时不带原因，上层只能给用户"操作失败"。
C ABI 侧更严重：286 个返回 `int32_t` 的函数里只有 47 个能给出原因。

**方案选择（与原方案不同，理由如实记录）**：
原计划是「所有 `int32_t` 函数都能给出 `out_error`」。实测后改为
**线程局部 `nc_last_error()`**：

1. 给 286 个函数加 `char** out_error` 会改变全部签名 → 破坏已发布的
   `NC_ABI_VERSION 1`，平台层（Kotlin/Swift/Qt）与全部测试都要改；
2. 343 个字面返回点里 **259 个只是「句柄/参数为空」**。为这类错误强制每个
   getter 多一个出参，调用方负担远大于收益；
3. 头文件已约定「单个 editor 句柄不允许多线程并发调用，由平台层串行化」，
   因此线程局部的错误槽位在既有契约内是安全的。

**实施**：
- `thread_local std::string g_lastError` + `failNc(code, msg)` 两个重载；
- **337 个字面 `return NC_ERROR_X;` 全部改写为 `failNc(...)`**，原因按上下文
  自动生成（空指针 → 指出参数名；范围检查 → 说明越界；`NotFound` → 目标不存在）；
- `toNcError(Result)` 只取 `code` 丢掉 `message` 是根因之一 → 新增
  `failResult(const Result&)`，**61 个调用点改为保留 `Result::message`**；
- 15 处合并的 `if (a == nullptr || b == nullptr)` 拆成独立判断，
  让原因精确到具体参数（剩余 19 处是跨行条件/深层嵌套，原因仍并列列出候选，
  属可接受的表述，不影响可用性）。

**新增 API**：`const char* nc_last_error(void)`——无需释放，成功调用**不清空**
（只有失败时覆写），保证调用方先记账后取原因不会丢信息。

**测试**（`tests/test_capi_errors.cpp`，8 用例）：
- `NullEditorGivesReadableReason` / `NullStringArgIsDistinguishedFromNullHandle`
  —— 原因必须点出参数名，不能是空泛的「失败」；
- `ReasonMentionsSpecificInvalidValue` / `ResultMessageIsPropagated`
  —— 走 `Result` 路径的失败，`message` 能到达 `last_error`；
- `SuccessDoesNotClearPreviousError` —— 验证「不清空」的约定（这是设计选择，
  不是副作用）；
- `EveryFailurePathSetsReason` —— 抽样 4 类失败全部有原因，行为级守门人；
- `LastErrorIsThreadLocal` / `NoFailureMeansEmptyOrNull` —— 线程隔离。

**变异验证（第一次是无效变异，如实记录）**：
1. 只变异 `failNc(const std::string&)` 重载 → **8/8 仍通过**。
   原因：自动生成的原因全是 C 字符串字面量，走的是
   `failNc(const char*)` 重载，**变异根本没被触发**。这不是测试漏了，
   是变异无效。教训：变异必须覆盖被测代码的**实际执行路径**。
2. 两个重载都变异（丢弃原因）→ **7 个用例 / 16 个检查点全部报警** ✅
   还原后 8/8 通过，`grep -c MUTANT` = 0。

**未完成（已知局限，不是遗漏）**：
- **`nc_clip_count` 这类「返回计数」的函数无法报错**：参数非法时只能返回 0，
  与「该轨道确实没有片段」不可区分。这是 ABI 语义层面的设计缺陷，
  修它需要新增 `nc_clip_count_ex(..., int32_t* out_count)` 之类的接口，
  会改变调用方语义（现有代码靠 0 判断空轨道），故本轮不动。
- 内部 C++ 层 465 处裸 `bool` 未动——本轮聚焦 C ABI 交付标准。
- 剩余 19 处并列的原因为表述优化项，非正确性问题。

---

### B0-4 ✅ C ABI 所有权标注补齐（P1）

**问题**：`capi.h` 出参未标注由谁释放，平台层（Kotlin/Swift）容易内存泄漏
或 double-free。

**实测修正文档估算**：文档说「75 个出参未标注」，实际盘点后：
68 个声明带 `**` 出参，其中 **原先只有 14 个真的缺标注**——其余 54 个已在
签名行或上方注释里写了「需 nc_free_string」，只是格式不统一，用简单
grep 统计会误判。这 14 个逐个补齐（`nc_project_load` 的 detail、
各 `*_load` 的 out_error、`nc_chapter_*`、`nc_heartbeat_network_error` 等）。

**头文件顶部新增「内存所有权约定」总述**（7 条），覆盖：
- 出参三分类：需 `nc_free_string` / 需 `nc_free_buffer` / **库持有（勿释放）**；
- 入参：库不接管所有权，返回后即可释放；
- 字符串数组：用 `nc_free_string_array(items, count)` 一次性释放，
  **不要逐个 free**（double-free）；
- 失败时：出参要么未写入、要么置 NULL，调用方**都不需要释放**。

**自动化检查**：新增 `tools/check_abi_ownership.py`，扫描每个带 `**` 的声明，
要求签名或上方连续注释中出现 `nc_free_string`/`nc_free_buffer`/`库持有`/`勿释放`。
接入 CMake（`make check_abi_ownership`）。
**检查脚本本身做了变异验证**：插入一个无标注的测试声明
`nc_ownership_probe(..., char** out_x, uint8_t** out_y)` → 脚本报错退出 1
并指出行号；移除后恢复通过。否则「检查永远是绿的」等于没检查。

**行为级测试**（`tests/test_capi_ownership.cpp`，4 用例）：
标注写错比不写更危险——注释说「需释放」而实现返回静态指针，调用方照样炸。
- `ErrorStringIsHeapAllocated` — 真的 free 掉 `out_error`，证明是堆分配；
- `NullOutParamIsAccepted` — 「可 NULL」的约定要真的不崩；
- `FailureLeavesOutParamUsable` — 失败时出参可直接 free（未写垃圾指针）；
- `LastErrorNeedsNoFree` — `nc_last_error` 两次调用**返回同一地址**，
  证明是库持有存储，调用方绝不能 free（B0-3 引入的接口，语义与
  `nc_free_string` 相反，最容易误用）。

**未完成**：`uint8_t**` 缓冲区类的标注目前只覆盖 `nc_free_buffer` 语义，
`nc_export_*` 系列的 `_size` 出参与 buffer 生命周期未与该检查联动
（需在导出路径上补断言，属渲染侧工作）。

---

### B0-5 ✅ 序列化向后兼容（P1）

**问题**：`serialization.cpp:1174` 只拒绝**高版本**，没有声明**低版本迁移策略**。

```cpp
if (version > kNclipFormatVersion) {
    return Result::fail(ErrorCode::Unsupported, "文件版本高于当前程序支持的版本");
}
```

**后果**：一旦格式升级，老工程能否打开取决于字段是否有默认值——目前靠运气。

**方案**：

1. 建立版本迁移表：`v1 → v2 → v3` 的字段补齐规则；
2. 每个新增字段必须有默认值；
3. 补测试：加载 v1 工程 → 断言关键字段不为空。

**交付**：迁移表 + 至少一个跨版本加载测试。

---

### B0-6 ✅ 边界访问审计（P2）

**问题**：直接下标 `[]` 1529 处，`.at()` 仅 31 处。越界是 UB
（可能不崩溃、可能读到垃圾）。

**审计结果（三类路径逐一核实）**：

**1. C ABI 传入的 index/count —— 已完备，无需改动**
- 程序化扫描：69 处「容器下标访问」全部在同一函数体内有范围检查；
- **实际攻击验证**：写探针用越界参数（`-1/-1000/INT32_MAX/1000/99999`、
  `clip_id=0xFFFFFFFF`、`asset_index=-1`）打 `nc_remove_track` /
  `nc_rename_track` / `nc_clip_info` / `nc_remove_clip` /
  `nc_set_clip_opacity` / `nc_asset_get` 等 12 个入口，
  编译时加 `-D_GLIBCXX_ASSERTIONS`（能捕获容器越界）→
  **全部返回错误码，程序存活，未触发任何断言**。
- 说明：ASAN 在此容器内初始化失败（`sanitizer_allocator_primary64` 地址空间
  冲突），故改用 `_GLIBCXX_ASSERTIONS`，对「容器下标越界」这一类目标够用。

**2. JSON 解析的下标 —— 已完备**
`json.h` 提供的是 `at(std::size_t)`，自带边界检查；代码里没有裸
`jsonArray[i]` 式访问（`grep` 确认仅有 2 处 `items[i]`，均为**自己分配的
数组**在 `i < count` 循环内，安全）。

**3. 时间→帧号换算 —— 发现并修复一处真实隐患**
`core/src/speech_ai.cpp` 的 `resampleTo16k`：
```cpp
const std::size_t i1 = std::min(frameCount - 1, i0 + 1);
const float b = samples[i1];
```
`frameCount == 0` 时 `frameCount - 1` **下溢成 SIZE_MAX**，`std::min` 取到它
→ `samples[i1]` 越界读。当前唯一调用方 `computeMelSpectrogram` 已有
`frameCount == 0` 守卫，所以**触发不到**；但该函数是匿名命名空间的通用工具，
新调用者一旦漏检就会中招。已补自身守卫（`samples == nullptr || frameCount == 0
|| sampleRate <= 0` → 返回空）。

**回归测试**（`tests/test_bounds_audit.cpp`，7 用例）：
把「手工探针跑一遍没问题」固化为可重复验证——
- `NegativeTrackIndexRejected` / `HugeTrackIndexRejected` / `ClipIndexOutOfRangeRejected`
  / `UnknownClipIdRejected` / `AssetIndexOutOfRangeRejected`
  —— 每类都同时断言「非法被拒」**和**「合法仍可用」（防「一律拒绝」的假实现），
  且拒绝后**计数不变**（证明真的没动数据）；
- `NullEditorNeverCrashes` / `ZeroLengthInsertRejectedOrSane`。

**未完成**：`[]` 的绝对数量仍大（1529 处），本轮只审计了「外部输入决定」
的三类路径。剩余多为内部循环（`for i < size` 紧邻），风险低。
若要全量替换为 `.at()`，需先评估热点路径性能，属独立工作项。

---

### B0-7 ✅ 轨道类型校验（防混拖）

**问题**：`validateClipPlacement()`（`core/src/commands.cpp:22`）**不校验轨道类型**。
音频片段可拖进视频轨、视频可拖进字幕轨，目前**不报错**。

**后果**：导出时行为未定义。音频轨里放视频片段，混音器如何处理无人知道；
字幕轨里放视频，字幕渲染会尝试把视频当字幕文本解析。

**已实施（2026-10-27）**：

相容表与文档方案有一处**有意偏离**——音频轨**接受**含音轨的视频素材：

| 轨道类型 | 可接受 | 说明 |
|---|---|---|
| `Video` | 视频 / 图片 / 复合片段 | 音频进视频轨 = 黑屏，拒绝 |
| `Audio` | 音频 / **视频** | 视频进音频轨 = 取声音（音画分离依赖），允许 |
| `Subtitle` | 无（走 SubtitleEvent） | 任何 Clip 均拒绝 |
| `Effect` | 无（空壳，见 B0-8） | 任何 Clip 均拒绝 |

偏离原因：实施时全量套件有 3 个既有用例（`ExportRunner.RealAudioMixToEncodedFile`、
`ExportRunner.AudioGainAndFadeApplied`、`SmartMovie.ClearPreservesLockedTrack`）
依赖「视频素材放音频轨」，且 `DetachAudioCommand` 生成的音频片段本就沿用视频
asset。真实软件里把 mp4 拖到音频轨取声音也是标准操作。故音频轨拒绝的是
`Image`（无声可读）而非 `Video`。

宽容项：`MediaType::Unknown`（未 probe）一律放行——否则「刚导入就拖不进时间轴」。

**交付**：越界拖放被拒绝且给出中文原因（含类型名）；`tests/test_editor.cpp`
新增 9 个正反例（含音画分离回归保护）；变异测试确认规则被有效覆盖。

**方案**：在 `validateClipPlacement` 增加类型相容表：

| 轨道类型 | 可接受的片段 |
|---|---|
| `Video` | 视频 / 图片 / 复合片段 |
| `Audio` | 音频 |
| `Subtitle` | 字幕事件（不走 Clip） |
| `Adjustment` | 仅特效容器（见 B2-6） |

**注意**：视频轨**允许**放纯音频片段吗？按专业软件语义——**不允许**
（音频应放到音频轨，否则预览不出画面且导出无声）。

**交付**：越界拖放被拒绝且给出明确原因；补 6 个正反例测试。

**预估**：0.5 天。

---

### B0-8 ✅ `TrackType::Effect` 空壳清理

**已实施（2026-10-27，方案 A 的保守变体）**：

- 枚举更名为 `TrackType::Adjustment`，**数值仍为 3**；
- 磁盘字符串**继续写 `"effect"`**（格式版本未变，改字符串会让旧版 App 读不了），
  读取时 `"effect"` 与 `"adjustment"` 都接受；
- `trackTypeLabel` 中文名改为「调整图层」；
- 补 4 个用例：旧串可读、新串可读、仍写出旧串、枚举值恒为 3。

⚠️ **注意**：本次只消除了「名字误导」，**没有实现渲染**。调整图层的真正生效
仍是 B2-6 的范围。在此之前 `validateClipPlacement` 拒绝任何 Clip 落入该类型
轨道，避免用户以为「放了就会有效果」。

**原始问题**：`TrackType::Effect` 只在 4 处出现，**全是序列化/ABI 转换**：

```
capi.cpp:456          数字 3 → Effect
capi.cpp:527          Effect → 数字 3
serialization.cpp:67  → "effect"
serialization.cpp:76  "effect" → Effect
```

**没有任何渲染代码处理它**。建一条「特效轨」放什么都不生效。

**方案**（二选一）：

- **A（推荐）**：改为 `TrackType::Adjustment`（调整图层，见 B2-6），
  保留枚举数值 3 不变，避免破坏已有工程文件；
- **B**：保留 `Effect` 但明确标注「保留未用」，UI 上禁止创建。

**注意**：枚举值 3 **不能改数字**，否则老工程文件会错位。

**交付**：枚举改名 + 反序列化兼容（"effect" 字符串仍能读入）。

**预估**：0.5 天。

---

## 三、B1 特效扩展（你已确认）

> 跳过粒子引擎（你已明确不做）。

### B1-1 ✅ 混合模式 5 → 16

**已实施（2026-10-27）**：新增 11 种（Multiply / Lighten / ColorDodge / ColorBurn /
LinearBurn / LinearDodge / SoftLight / LinearLight / PinLight / Difference / Exclusion），
`kBlendModeCount = 16`。

- **数值契约**：0..4 为历史值永不改变（C ABI 整数入口按值映射），新模式从 5 起追加；
- 公式采用 W3C Compositing Level 1 / Photoshop 标准式；
- SoftLight 完整实现双分支（含 A ≤ 0.25 的多项式 D(A) 与 sqrt 分支）；
- ColorDodge / ColorBurn 有除零保护（B=1 / B=0）；
- 名称与解析双向覆盖 16 种（中英文别名），越界整数拒绝；
- **验证方式**：另写一份独立 Python 实现，与 C++ 逐点比对 4851 个点
  （11 种 × 21×21 网格），全部一致——避免「用自己写的公式验证自己」。

**待完成**：UI 下拉项（`ui/assets/editor.js` 的模式选择器）。

**原始问题**：

**现状**：`blend_modes.cpp` 130 行，`BlendMode` 仅 5 种
（Normal / Screen / Overlay / Darken / HardLight）。

**新增 11 种**（对标 PS/AE）：Multiply、ColorBurn、ColorDodge、LinearBurn、
LinearDodge、SoftLight、LinearLight、PinLight、Difference、Exclusion、Hue/Luminosity。

**技术**：纯公式，`blendChannel()` 加 case 即可。

**交付**：公式与 PS 一致性（用标准色值对拍）+ 单元测试 + UI 下拉项。

**预估**：1 天。

---

### B1-2 ✅ LUT 调色（核心层）

**已实施（2026-10-27）**：`core/include/nairoclip/core/lut.h` + `core/src/lut.cpp`。

- `.cube` 解析（Adobe Cube LUT Specification 1.0）：TITLE / LUT_3D_SIZE /
  DOMAIN_MIN/MAX / 注释 / 空行；
- 尺寸 2..64（覆盖 17³ 与 33³）；三线性插值；
- **红色变化最快**的数据顺序（`((b*size+g)*size+r)*3`）——这是 .cube 最易错处，
  专门用「交换红蓝」的 LUT 测试，identity LUT 对顺序不敏感、测不出来；
- 强度混合 0..1（0 = 原图，1 = 完整 LUT）；
- 数据模型：`Clip.lutPath`（存路径不存数据——33³ 是 10 万+ double，
  塞进每个片段会让工程文件膨胀几十 MB）+ `Clip.lutStrength`；
- 序列化：仅启用时写入；强度越界截断（与 `gainDb` 同策略）。

**格式校验从严**：畸形文件拒绝并给**行号 + 具体原因**（字段数不对 / 非数值 /
数据行数不符 / 尺寸越界 / 重复声明 / 1D LUT 明确拒绝）。解析失败时
不修改调用方已有的 LUT，避免拿到半个表。共 12 个畸形用例。

**开发中发现并修复的真实缺陷**：`lutSample` 最初把输入按「域单位」解释，
导致 DOMAIN 声明为 0..255 的 LUT 把归一化输入 1.0 映射到起点，
**整张图会被压黑**。已改为「输入归一化 0..1 → 线性映射到整个 DOMAIN 区间」，
并保留一个专门的回归用例。

**未完成**（需产品决策/素材）：20 个内置 LUT 预设、C ABI
（`nc_set_clip_lut` 等）、导出/预览管线接入点。

**原始问题**：

**现状**：全库 `LUT` **0 命中**，无任何 LUT 能力。

**方案**：

| 项 | 内容 |
|---|---|
| 格式 | `.cube`（行业标准，纯文本） |
| 解析 | 三线性插值，支持 17³ / 33³ |
| 数据模型 | `Clip` 增加 `lutPath` + `lutStrength`(0~1) |
| 内置预设 | 20 个（电影感 / 日系 / 胶片 / 青橙 / 黑白 / 动漫燃向…） |
| 双路径 | 预览（可 17³）+ 导出（33³） |
| C ABI | `nc_set_clip_lut` / `nc_get_clip_lut` / `nc_lut_preset_count` |

**关键**：`lutStrength` 支持强度混合（0 = 原图，1 = 全量），这是专业软件必备。

**交付**：`.cube` 解析测试（含畸形文件拒绝）+ 预设预览图 + 序列化往返。

**预估**：4 天。

---

### B1-3 ✅ 光效（Bloom + 镜头光晕）

**已实施（2026-10-27）**：`core/include/nairoclip/core/light_effects.h` +
`core/src/light_effects.cpp`。

**Bloom**（`applyBloomToRgba`）：高光提取 → 高斯模糊（复用 `spatial_effects`）→ 加法叠加。
- 高光提取用 **Rec.709 亮度**而非算术平均（纯蓝的算术均值 85，看起来明显比纯绿暗）；
- 权重 `(y - threshold) / (1 - threshold)`：只让真正超过阈值的部分发光；
- **加法**叠加而非 alpha 混合——光应该"加上去"而不是"盖上去"；
- 无高光时直接返回（零改动）。

**镜头光晕**（`applyLensFlareToRgba`）：主光源圆盘 + 沿「光源→中心」延长线的鬼影串。
- 软边用 `cos` 曲线（比线性更接近真实光学）；
- 色温 → 三通道色偏（暖偏橙金 / 冷偏青蓝）；
- 先累加到 double 缓冲再统一截断，避免多次饱和导致亮度失真。

**验证中的两次修正（如实记录）**：
1. 鬼影间距系数原为 0.55，光源在 0.8 时第一个鬼影落在 `16 - 14.9 ≈ 1.1`，
   即画面最边缘 —— 观感上等于没有鬼影。改为 0.35 后前两个落在画面内。
2. 首版 `DarkImageIsUnchanged` 是**假断言**：纯暗图 (20,20,20) 的 luma≈0.078，
   即便参与发光，`(0.078-0.8)/0.2` 仍为负、被 clamp 到 0，结果仍是零改动。
   变异测试（改成"直接模糊整帧"）时它没报警。补了 `MidToneRegionDoesNotGlow`
   用中灰区才真正区分「是否做了高光提取」。

变异验证：改成「跳过提取、恒定权重模糊整帧」→ 上述 2 个用例均报警。

**未完成**：接入 `EffectType`（`Bloom`/`LensFlare` 枚举与参数化）、UI 控制面板。

**现状**：有 `GlowOutline`（依赖抠像 Alpha，目前不可用）、`LightLeak`、`Starburst`。

**新增**：

| 特效 | 原理 | 参数 |
|---|---|---|
| **Bloom** | 高光提取 → 高斯模糊 → 叠加 | 阈值、半径、强度 |
| **镜头光晕** | 光源位置 + 鬼影环 + 光斑 | 位置、大小、色温、可见度 |

**交付**：导出/预览双路径 + 测试（亮部溢出断言）。

**预估**：3 天。

---

### B1-4 ✅ 动态模糊重构（方向性模糊）

**已实施（2026-10-27）**：新增 `core/include/nairoclip/core/motion_blur_directional.h`
+ `core/src/motion_blur_directional.cpp`。

**为什么重构**：原 `MotionBlur` 是「相邻多帧三角加权叠加」。物理上正确，但两个硬伤：
1. **需要帧序列**——单帧渲染（预览拖拽、缩略图、`applyMotionEffectToRgba`）拿不到
   相邻帧，`motion_effects.cpp:327` 只能写「返回 true 不改动」的占位；
2. **与运动方向无关**——水平甩镜和垂直运动得到完全相同的模糊，没有方向感。

**新方案**：单帧内沿运动矢量多点采样。
- 方向来自位移动画（`ClipProperty::PositionX/Y`）求导，用户不必额外调角度；
  也支持直接给 `velocityX/Y`（甩镜等场景）。
- 采样点权重三角分布（中心 1.0 → 端点 0.1，不归零），模拟真实快门积分形状；
- 模糊半长 = `|位移| × (快门角度/360) / 2`，快门角度 0..360 可调；
- `maxLength` 钳制（0..0.5 画幅）防止极快运动糊成一团。

**单位约定（易错点，已在 header 写明）**：`velocityX/Y` 是**本帧内位移**
（归一化画幅），不是「画幅/秒」。因为函数不接收帧率，用帧位移才能自洽。
手上是速度时先用 `velocityFromPositions(..., dt, frameDuration, ...)` 换算。
开发中一度出现 header 写「每秒」而实现按「每帧」——已统一为「每帧」。

**与原实现的关系**：`applyMotionBlurFrames`（多帧叠加）**保留不动**——它在导出管线
帧序列层面仍是物理更准的方案。本模块补充的是单帧路径 + 方向感知，
两者不是替代关系。这一点与文档原表述「重构」有出入，如实记录。

**验证（12 用例）**：
- `ZeroVelocityIsNoOp` / `ZeroShutterIsNoOp` — 静止与快门 0° 零改动
- `HorizontalMotionBlursHorizontally` — 竖条纹 + 水平运动 → 行方差下降
- **`HorizontalMotionDoesNotBlurVertically`** — 水平运动不该让**水平**条纹变糊
  （这条是方向性的守门人，各向同性实现会红）
- `VerticalMotionBlursVertically` — 反向对照
- `LongerShutterBlursMore` / `MaxLengthClampsExtremeMotion`
- `AlphaPreserved` / `Deterministic` / `InvalidParamsRejected`
- `VelocityFromPositionsConvertsUnits` — 10 画幅/秒 × 1/30 秒 = 0.333 画幅/帧
- `NegativeVelocityBlursOtherDirection` — 模糊量对方向是偶函数

**变异验证（两次，第二次发现真问题）**：
1. 「忽略方向恒用水平」→ `VerticalMotionBlursVertically` 报警 ✅
2. 「删除 maxLength 钳制」→ **首版断言未捕获** ❌

第 2 次暴露了假断言：`MaxLengthClampsExtremeMotion` 原本写「方差 < 1.0 才报警」，
而钳制失效时采样点被推到画布边界、互相混合后的方差仍远大于 1.0。
**阈值型断言在极值处不可靠**。改为「同一速度下 maxLength=0.02 vs 0.5 互相比，
短的那次方差必须显著更大」，重跑变异版立即报警（tight=441 vs loose=44）。
还原后 12/12 通过，`grep -c MUTANT` = 0。

**未完成**：`EffectType` 接入（当前 `EffectType::MotionBlur` 仍走多帧路径）、
导出管线里从关键帧自动求速度的接线、UI 快门角度控件。

---

### B1-5 ✅ 震动/摇晃特效（从 depth_ai 抽出）

**已实施（2026-10-27）**：`core/include/nairoclip/core/shake_effect.h` +
`core/src/shake_effect.cpp`，`EffectType::Shake = 36`。

- 4 种轨迹：Horizontal / Vertical / Circular / Random；
- 参数：振幅（占短边比例 0..0.2）、频率（0..20Hz）、种子、衰减时长（0..10s）、相位；
- **确定性**（文档标为「关键」）：全部随机性来自 `seed` 的 murmur3 哈希，
  不用 `rand()`/`random_device`。测试断言同参数同时间点**逐像素一致**
  （单帧 + 连续 30 帧 + 4 种轨迹全覆盖）；
- 衰减包络 `exp(-3t/T)`：t=T 时约剩 5%，效果「抖一下就停」；
- 振幅以**短边**为基准，横竖屏观感一致；
- 亚像素位移直接跳过；边缘镜像填充避免黑边；alpha 原样保留。

**参数为何不进 `Effect.value`**：`Effect` 是单值字段，塞不下四维参数。
按项目既有惯例（`person_effects` / `motion_effects` 同做法），
结构化参数走独立 `ShakeParams`，`Effect.value` 只作强度倍率。

**原始问题**：

**现状**：`depth_ai.cpp:419` 有正弦轨迹（0=静止 / 1=摇摆 / 2=环绕），
但只服务于深度图视差，不是独立特效。

**方案**：抽成通用 `EffectType::Shake`。

| 参数 | 说明 |
|---|---|
| 振幅 | 像素或百分比 |
| 频率 | Hz |
| 相位 | 随机种子（确定性） |
| 衰减 | 冲击后递减（用于超燃剪辑） |

**关键**：**必须确定性**（固定种子），否则三端渲染不一致、导出不可复现。

**交付**：同参数多次渲染结果逐像素一致（断言）。

**预估**：0.5 天。

---

## 四、B2 模板系统（对标 Dula Auto Clip）

> 依据竞品截图分析：其"AI 剪辑"本质是**模板槽位填充**，
> 界面显示"参考镜头 01-09 / 使用 2 处 / 成片 16.4 秒"即模板参数。

### B2-1 ✅ 模板数据结构

**已实施（2026-10-28）**：`core/include/nairoclip/core/edit_template.h` +
`core/src/edit_template.cpp`。

**与文档草案的三处偏差（按实际代码库修正，不是随意改动）**：

| 文档写法 | 实际采用 | 原因 |
|---|---|---|
| `Time duration` | `core::Time`（= `Rational`） | 本库 `Time` 就是 `Rational`，没有独立 Time 结构体 |
| `std::vector<EffectSpec> effects` | `std::vector<TemplateEffect>` | **`EffectSpec` 是静态规格**（min/max/default/name，描述"某类特效的取值范围"），不是特效实例。要表达"这个槽位加 0.7 强度的 ZoomPunch"，必须用 `{EffectType, double}` |
| `double shakeAmplitude` | 同名字段，口径对齐 B1-5 | B1-5 已有 `ShakeParams`，沿用"振幅=画幅短边占比、频率 Hz"，避免两套语义 |

**新增类型**：
- `LayerRole{Main, Foreground2, Foreground3}` + `layerRoleName`（中文）/
  `layerRoleKey`（"main"/"fg2"/"fg3"）/ `layerRoleFromKey`；
- `TemplateEffect{EffectType type; double value}`；
- `TemplateSlot{duration, role, inTransition, transitionDuration, effects,
  shakeAmplitude, shakeFrequency}` + `hasShake()`；
- `EditTemplate{id, name, category, totalDuration, slots, musicHint, coverPath}`
  + `slotsDuration()` / `slotCount()`。

**校验用 `Result` 而非 `bool`**（B0-3 同一原则）：模板来自用户可编辑的 JSON，
失败必须说清"第 3 个槽位时长非法"，不能只给"模板无效"。校验项：
- id/name 非空、id ≤ 64 字符、至少一个槽位；
- 每槽位时长 > 0；摇晃振幅 0~0.2、频率 0~20Hz；
- 非 `Cut` 转场必须有时长且不超过槽位时长；
- 特效强度落在该特效 `effectSpec()` 的合法区间；
- **`totalDuration` 与槽位之和相差 ≤ 1 微秒**（冗余字段的价值在于能被校验）。

**序列化**：时间一律用**微秒整数**（`durationUs` / `totalDurationUs` /
`transitionDurationUs`），避免秒制浮点在 JSON 里的精度漂移。
默认值不写入（`shakeAmplitude == 0` 时省略、空的 `musicHint`/`coverPath` 省略），
保持 diff 稳定（与 B2-10 同策略）。

**内置 10 个模板**（`builtinTemplates()` + `findBuiltinTemplate(id)`）：
`keyframe_shake`（关键帧摇晃，文档点名示例）、`triple_hit_beat`、
`car_showcase`、`car_engine_cut`、`anime_flash`、`anime_impact`、
`travel_open`、`travel_dissolve_chain`、`pip_commentary`、`ending_hold`。
覆盖 卡点/汽车/动漫/旅拍 四个品类，用到 FlashWhite/WhipPan/Zoom/CrossDissolve
以及 ZoomPunch/RgbSplit/Glitch/Brightness 等特效。

**测试**（`tests/test_edit_template.cpp`，19 用例）：
- `BuiltinTemplatesAreValid` / `BuiltinIdsAreUnique` — 10 个内置模板逐个完整校验；
- `TotalDurationEqualsSlotSum` / `RejectsInconsistentTotalDuration`
  — 冗余字段必须真能被校验；
- `RejectsZeroDurationSlot` / `RejectsTransitionLongerThanSlot` /
  `RejectsOutOfRangeShake` / `RejectsNegativeShake` / `RejectsInvalidEffectValue`
  — 各类非法输入，且**报错要指出第几个槽位**；
- `SerializationRoundtrip` / `BuiltinTemplatesRoundtrip` — 全部内置模板
  序列化→反序列化仍合法；
- `FailedParseDoesNotModifyOutput` — 解析失败不污染调用方数据；
- `OmitsDefaultFieldsInJson` — 默认值不写入。

**变异验证**：
1. 「跳过 `totalDuration` 自洽校验」→ `RejectsInconsistentTotalDuration`
   报警。⚠️ 第一次只跑了分片 0 就断言"未捕获"，**判断错误**——该用例恰好在
   分片 1。教训：**变异验证必须跑完全部分片**，否则会把"没跑到"误判成"没抓到"。
2. 「内置模板总时长硬编码成错值」→ `BuiltinTemplatesRoundtrip` 报警 ✅

**环境踩坑（如实记录）**：中途出现 6 个 FFmpeg 用例大面积失败
（`h264_nvenc Undefined constant missing`）。排查确认**与代码无关**：
`/tmp` 被系统清理，导致 `/tmp/media_gen/` 测试夹具丢失。
这些夹具由 `tests/fixtures/generate_media.sh` 在 `nairoclip_tests` 的
POST_BUILD 步骤幂等生成，`touch` 源码触发重编译后自动恢复，重跑即全绿。
**注意**：这类失败看起来像"代码改坏了"，实际是环境漂移，不要浪费时间去查代码。

**未完成**：模板的 C ABI（`nc_template_*`）与 UI 模板选择器——属 B2-4。

---

### B2-2 ✅ 智能填充算法

**已实施（2026-10-28）**：`core/include/nairoclip/core/template_fill.h` +
`core/src/template_fill.cpp`。

**先说能力边界（这是本次最重要的判断）**

文档列了四个打分维度，但其中两个**本库拿不到数据**：

| 维度 | 需要的数据 | 现状 |
|---|---|---|
| 时长够不够槽位 | `MediaAsset::duration` | ✅ 已有 → 硬性过滤 |
| 色调与相邻槽位协调 | 素材平均色 | ⚠️ 需调用方先分析（`AssetFeatures::color`）|
| 主体位置适配 | 主体在画面的 x/y | ❌ **无任何分析能力** |
| 运动方向承接 | 运镜方向 | ❌ **无任何分析能力** |

所以接口设计成 `AssetFeatures` **由调用方提供**，不假装能自己算出来。
不填时对应维度按 **0.5 中性分**处理——关键是所有候选都用同一中性值，
因此**只影响绝对分、不影响相对排序**，不会扭曲选择结果。

为什么不在填充时解码视频取平均色：会引入 FFmpeg 依赖，且 100 个素材要解码
100 次，在 UI 交互路径上不可接受。正确做法是**导入时分析一次并缓存**，
`AssetFeatures` 就是那个缓存载体。

**打分公式**：
```
总分 = 时长余量分 × 1.0
     + 色调协调分 × 2.0
     + 主体位置分 × 1.5
     + 运动承接分 × 1.5
     - 复用惩罚 × 0.8 × 已用次数
```
各分项归一化到 0..1，权重可配（`FillWeights`）。

**几个刻意的设计选择**：

1. **「刚好够」不是满分**。时长余量 = 素材/槽位，比值 1.0 → 0.5 分，
   2.0 以上 → 1.0 分。刚好够意味着没有裁剪余地，用户想微调时长就得重填。
2. **贪心而非全局最优**。槽位之间通过「与前一个已选素材比」耦合，贪心
   已能保证相邻协调，且 O(S×C) 而非指数级——UI 交互必须能即时出结果。
3. **复用不是开关而是惩罚**。素材够用时复用会被扣分自然避免；素材不够时
   宁可复用也不留空。同一套逻辑覆盖两种场景，不需要额外配置。
4. **复用惩罚关掉后会黏在最优素材上**（`ReusePenaltyZeroAllowsStickingToOne`
   固化了这个行为）——这是权重设计的必然结果，写进测试免得日后被当 bug。

**测试**（`tests/test_template_fill.cpp`，15 用例）：
- `FillsEverySlotWhenEnoughMaterial` / `ReusesWhenMaterialIsScarce` — 够/不够两种情形；
- `TooShortAssetIsHardFiltered` — 硬性过滤，且**原因要说明时长不够**；
- `ImageIsNeverFilteredByDuration` — 图片可无限拉长（`duration=0` 也能填 10s 槽位）；
- `PrefersLongerAssetForSlack` / `ReusePenaltyAvoidsUnnecessaryRepeats`
  / `ReusePenaltyZeroAllowsStickingToOne` — 两个权重各自可验证；
- `ColorHarmonyPrefersSimilarToPrevious` — 相邻色调协调；
- `MissingFeaturesAreNeutralNotPenalized` — **缺特征不受系统性歧视**（这条守住了
  「不伪造数据」的承诺）；
- `NoCandidateReportsReason` — 填不上要说清为什么；
- `Deterministic` / `WorksWithBuiltinTemplates`（10 个内置模板 + 充足素材必须全填满）。

**变异验证**：
1. 「去掉时长硬过滤」→ `TooShortAssetIsHardFiltered` 报警 ✅
2. 「去掉复用惩罚」→ `ReusePenaltyAvoidsUnnecessaryRepeats` 报警 ✅

**未完成**：
- 接入 B2-3 模板应用管线（把 `FillResult` 变成真实时间线上的片段）；
- 导入时的素材分析（算 `ColorProfile` 并缓存），这是让色调维度真正生效的前提；
- 主体位置/运动方向需要 B5-3（人脸/目标跟踪）才有数据来源。

---

### B2-3 ✅ 模板应用管线

**已实施（2026-10-28）**：`core/include/nairoclip/core/template_apply.h` +
`core/src/template_apply.cpp`。

**接口**：`applyTemplate(editor, sequenceId, tmpl, assignments, out) → Result`，
配合 `assignmentsFromFill(fill, candidates)` 从 B2-2 的结果直接转换。

**与 B2-2 的分工**：B2-2 只决定「哪个素材进哪个槽位」，不碰编辑器；
本模块负责落盘。分开的好处是填充算法可脱离编辑器单测，应用管线也能接受
用户手工指定的素材映射（用户拖素材覆盖自动填充）。

**排布规则**：
- 槽位**首尾相接**排在一条新建的「模板·主轴」轨道上；
- 前景角色（`Foreground2/3`）放到独立的「模板·前景 N」轨道，时间区间与
  对应槽位对齐——这样才能形成画中画；
- 素材入点从 0 开始；片段时长 = 槽位时长（素材更长则只取前段）。

**撤销**：整个过程包在一条 `beginTransaction`/`endTransaction` 里，
用户点一次套用 = 一条撤销记录。**任何一步失败都会回滚**并返回错误。

**撤销验证方式（这条是关键）**：没有只断言「片段数回到原值」——
特效没删干净、轨道多建了一条、ID 分配器前进过，都算不一致。
测试用 **序列化文本逐字符全等** 作为判据（`saveProjectToNclip` 前后比较），
能抓住所有残留。

**测试**（`tests/test_template_apply.cpp`，16 用例）：
- `UndoRestoresProjectExactly` — 核心验收，序列化文本全等；
- `SingleUndoStepForWholeTemplate` — 套用 4 段模板只产生 **1** 条历史；
- `RedoReappliesTemplate` — 重做后与套用后完全一致；
- `SlotsAreLaidOutBackToBack` — 逐段验证 start/duration 的精确值；
- `ForegroundSlotsGetOwnTrack` / `EffectsAreAdded` / `TransitionsBetweenAdjacentClips`；
- `MissingAssignmentIsSkippedWithWarning` — 未指定素材的槽位跳过并**说明是第几个**；
- `UnknownAssetIsRejected`（且**不留下半成品**）/ `InvalidSlotIndexIsRejected`
  / `DuplicateSlotAssignmentIsRejected` / `InvalidTemplateIsRejected`；
- `WorksWithBuiltinTemplate` — 用真实内置模板跑通并撤销。

**变异验证**：
1. 「不包事务」→ `UndoRestoresProjectExactly`、`SingleUndoStepForWholeTemplate`、
   `WorksWithBuiltinTemplate` 三个同时报警 ✅
2. 「去掉素材预校验」→ `UnknownAssetIsRejected` 报警（后半句
   `CHECK(seq->tracks.empty())` 抓住了「留下半成品轨道」）✅
   这条证明「失败后不留痕」的断言不是摆设。

**已知局限（如实记录）**：
- **摇晃参数未落盘**：`TemplateSlot::shakeAmplitude/Frequency` 在应用时
  只统计进 `shakesApplied`，并在 warnings 里明确提示「片段层无对应字段」。
  原因是 `Clip` 没有摇晃字段，摇晃目前是 `EffectType::Shake` 的运行时参数
  （见 B1-5），需要渲染侧接线才能真正生效。**没有假装应用了**。
- 转场依赖引擎的「相邻片段」判定，失败时记入 warnings 而不中断整个模板
  （硬切情形下「相邻」本来就无意义）。
- 模板应用**不调整已有片段**：如果目标轨道已有内容会直接叠加，
  不做自动避让。这符合「模板通常用在空时间线」的实际用法，但如果用户
  在有内容的时间线套用，可能出现重叠——需要 B2-4 的 UI 做前置提示。

**未完成**：C ABI 与 UI（B2-4）。

---

### B2-4 ✅ 模板 C ABI（UI 未做）

**已实施（2026-10-28）**：`capi.h` / `capi.cpp` 新增 4 个函数。

```c
int32_t nc_template_list(char** out_json, char** out_error);
int32_t nc_template_slot_count(const char* template_id);          // 新增
int32_t nc_template_apply(NcEditor*, const char* template_id,
                          const uint64_t* material_ids, int32_t count,
                          char** out_summary, char** out_error);
int32_t nc_template_apply_slots(NcEditor*, const char* template_id,
                                const int32_t* slot_indices,
                                const uint64_t* material_ids,
                                int32_t count, char** out_summary, char** out_error);
```

**比原方案多两个函数，理由**：
- `nc_template_slot_count`：UI 要在套用**之前**告诉用户「这个模板需要 5 段
  素材」。让平台层去解析 `nc_template_list` 的 JSON 数组也能做到，
  但那是把简单查询变成 JSON 解析负担（Kotlin/Swift 侧都要写一遍）。
- `nc_template_apply_slots`：原方案只有自动填充。但用户很可能想手动指定
  「第 2 段用这个镜头」。没有这个入口，平台层只能自己调一堆
  `nc_insert_clip`，绕过模板的排布/转场/事务逻辑。

**行为**：
- `nc_template_apply` 自动填充（走 B2-2 打分），摘要里包含插入数/转场数/
  特效数/轨道数与全部 warnings；
- `nc_template_apply_slots` 手工映射，未指定的槽位跳过并在摘要里说明；
- 两者都**整次调用一条撤销记录**；
- 失败原因同时通过 `out_error`（需 `nc_free_string`）与 `nc_last_error()`
  两条路径给出，与 B0-3/B0-4 的约定一致。

**测试**（`tests/test_capi_template.cpp`，12 用例）：
- `ListReturnsJsonWithAllTemplates` / `SlotCountMatchesTemplate`；
- `ApplyFillsAndReturnsSummary` / `ApplySlotsWithExplicitMapping`；
- `ApplyIsUndoableInOneStep` — 用 `nc_project_save` 取文本比对，
  套用→撤销后**逐字符相同**；
- `UnknownTemplateGivesReason` / `NullArgsGivesReason` /
  `UnknownMaterialIdsAreRejected` / `ApplySlotsRejectsNegativeIndex`
  — 各类失败都必须有可读原因；
- `OutputStringsAreHeapAllocated` / `NullOutParamsAccepted`
  — 所有权与「可 NULL」约定（B0-4 的落地验证）；
- `LastErrorSetOnTemplateFailure` — 新接口也要覆盖 last_error。

**变异验证**：「不写 `out_summary`」→ `ApplyFillsAndReturnsSummary` 报警 ✅

**开发中自己发现并修正的问题**：
1. 头文件注释里写了 `nc_template_preview(tmpl_id, ...)`，但**这个函数我并没有
   实现**——典型的"注释里画饼"。已改为实际存在的 `nc_template_slot_count`。
   虚假引用比没注释更糟：平台层会照着写代码然后链接失败。
2. `setDetail` 的一句错误文案写成了病句
   （「给定的素材都存在于工程中之外的素材不存在」），已改为
   「给定的素材 ID 都不在工程素材表中」。

**未完成**：UI 模板选择页（`docs/UI_BACKLOG.md` P-06）——
需要设计稿与前端，属独立工作项。C ABI 已就绪，UI 可直接对接。

---

### B2-5 ✅ 轨道重排序

**已实施（2026-10-27）**：

- `TimelineEditor::moveTrack(seq, track, newIndex)` + `MoveTrackCommand`；
- 校验：`newIndex ∈ [0, tracks.size()-1]`；锁定轨道拒绝；`-1`/越界均拒绝并给中文原因；
- 跨类型重排允许（顺序只影响渲染层级）；
- 连续拖动同一轨道会合并为一条历史（`canMergeWith`/`mergeFrom`），
  避免逐像素拖动塞满撤销栈；
- 撤销/重做往返，重排后 `invariantsHold()` 仍成立。

**已知局限**：移动到原位置仍会产生一条空历史记录。要抑制它需要
`Command` 层增加 no-op 钩子，影响面大于收益，暂不处理（已在测试中注明）。

**原始问题**：

**现状**：`editor.h` / `commands.h` / `capi.h` **无任何** `moveTrack` /
`reorderTrack` / `swapTrack`。轨道只能增删，**不能改上下顺序**。

**为什么重要**：视频轨的顺序决定**遮挡关系**（上层盖下层）。
不能重排 = 用户建错顺序后只能删了重建。

**方案**：

```cpp
// Sequence 增加（或直接改 tracks 顺序）
Result moveTrackTo(SequenceId seq, TrackId track, int newIndex);
```

配套 `MoveTrackCommand`（apply/revert 记录原索引与新索引）。

**校验**：

- `newIndex` 在 `[0, tracks.size()-1]` 范围内
- 源轨与目标轨都未锁定
- 跨类型重排允许（视频轨可以移到音频轨上方，顺序只影响渲染层级）

**交付**：命令可撤销；重排后 `invariantsHold()` 仍成立；补测试。

**预估**：1 天。

---

### B2-6 ✅ 调整图层（Adjustment Layer）

**已实施（2026-10-27）**：

- 数据模型：`Track.adjustmentEffects`（`EffectStack`）+ `adjustmentLutPath` +
  `adjustmentLutStrength`；`Row` 级 `hasAdjustment()` 判定；
- 效果挂在**轨道**而非片段上——调整图层没有片段，它作用于「下方所有轨道」；
- 计划编译：`RenderTrack` 携带同名字段；
- 渲染集成点：`finalizeFrame()` 内、**水印之前**依次应用。
  - 放水印之前：水印属于最终叠加物，不该被调整图层的调色改变；
  - 放 `finalizeFrame` 内：所有输出帧路径天然统一，不会漏；
- 只收集「有实际效果」的调整图层（effects 或 LUT 非空）——空轨道不进入循环。

**验收证据（关键）**：

| 断言 | 证明了什么 |
|---|---|
| `AllFrameDigestsMatchPreFeatureBaseline` | 30 帧指纹逐个等于**引入本功能之前**的实测值 |
| `EmptyAdjustmentTrackChangesNothing` | 空调整图层零影响（防止"当成黑层盖上"这类错误实现） |
| `EffectsApplyToLowerTracks` | 效果确实作用到下方轨道 |
| `GrayscaleActuallyRemovesColor` | 不只是"变了"，而是"变成 R=G=B" |
| `HiddenAdjustmentIsSkipped` | 隐藏的调整图层不生效 |

开发中实测：引入本功能后，30 帧与 `303cec4` 记录的基线**逐帧完全一致**。

**能力边界**（如实记录）：
- 生效区间 = **整片**（没有"调整片段"的概念，暂不支持只在某段时间生效）；
- 多调整图层的 LUT 只应用第一个（效果栈则全部依次应用）。

**原始评估与基线设施**：

> **2026-10-27 更新**：像素基线**已建成并验证有效**（见下方「基线设施」）。
> 前置条件已满足，下一步可以直接实施本项。

**基线设施（已完成）**：`tests/pixel_baseline.h` + `tests/test_render_baseline.cpp`

- `fnv1a64` 帧指纹 + `diffDigests()`（报告首个差异帧，而非刷屏）；
- `PatternReader`：确定性图案读取器，**seek 语义与真实 FFmpeg 一致**
  （seek 后读到的就是目标时间的帧）；
- 8 条断言，其中三条是「地基」而非功能：
  - `FramesActuallyDifferAcrossTime`——30 帧必须有 ≥2 种指纹。
    这条是写基线时踩坑后补的：最初的 mock 读取器在 `seek()` 里把计数器
    清零，导致导出器每帧都读到第 0 帧，30 帧指纹完全相同，
    「两次导出一致」「加滤镜后不一致」全都假通过；
  - `SpecificKnownRegionIsStable`——钉死首帧的具体像素值，
    整帧指纹只能说「变了」，这条能说「变成了什么」；
  - `FilterChangesOutputPixels`——确认基线对真实改动敏感。
- **有效性已验证**：向 `export_executor.cpp` 注入「少拷贝一个像素」的变异，
  30/30 帧全部检出差异；还原后恢复一致。

**原始评估结论（保留）**：本项要改 `export_executor.cpp` 合成主链路（887 行），
验收标准是「无调整图层时输出与改动前逐像素一致」。没有基线就用"编译通过 +
看起来对"冒充验证，一旦错位就是整条导出链路静默损坏。

**原先认为需要补的前置工作**：
1. 用 `tests/fixtures` 的固定素材导出一段短片，存为参考帧序列；
2. 加一个「逐像素比对」的测试工具（允许编码噪声容差，如 PSNR > 50dB）；
3. 在这个基线上先跑通一次，确认它能在**故意改坏**时报警。

**为什么暂停**：本项要求改动 `export_executor.cpp` 的合成主链路（887 行、
含多层混合/蒙版/转场），且验收标准是「**无调整图层时输出与改动前逐像素一致**」。
要证明这一点，必须先有一条可信的**像素级回归基线**（参考帧 + 逐帧比对），
而项目目前没有这项设施——现有测试只断言"导出成功""尺寸正确"这类结构性质。
在没有基线的情况下改主链路，等于用"编译通过 + 看起来对"冒充验证，
一旦错位就是整条导出链路静默损坏（用户拿到花屏成片才会发现）。

**补测前置工作**（建议下一步先做，约 0.5 天）：
1. 用 `tests/fixtures` 的固定素材导出一段短片，存为参考帧序列；
2. 加一个「逐像素比对」的测试工具（允许编码噪声容差，如 PSNR > 50dB）；
3. 在这个基线上先跑通一次，确认它能在**故意改坏**时报警。

有了基线再动 B2-6，才能证明"既有行为未被破坏"。

**原始问题**：

**现状**：`TrackType::Effect` 是空壳（见 B0-8），无「调整图层」概念。
全库 `adjustment` / `调整图层` **0 命中**。

**什么是调整图层**：

```
一条轨道，本身没有画面内容，
但它上面的特效/调色会作用于【下方所有轨道】。
```

典型用途：给整个片子加统一 LUT、暗角、胶片颗粒，而不必逐片段设置。

**方案**：

| 项 | 内容 |
|---|---|
| 枚举 | `TrackType::Adjustment = 3`（复用原 Effect 数值） |
| 数据 | 每条调整图层有自己的 `std::vector<EffectSpec>` 与 LUT 引用 |
| 渲染 | 合成时：先把下方所有轨道合成为中间图 → 应用调整图层特效 → 输出 |
| 时间 | 调整图层的生效区间 = 其名下的「调整片段」覆盖的时间（无片段 = 全片生效？需定） |
| UI | 时间轴上显示为特殊样式（斜纹底/齿轮图标） |

**渲染集成点**（关键）：

```
export_executor.cpp 的合成循环
  → 当前实现：逐轨叠加
  → 改为：按轨道顺序，遇到 Adjustment 轨时，
          把已合成的下层图应用其特效，再继续往上叠
```

**风险**：这会改动导出主链路（887 行），必须补回归测试
（无调整图层时输出必须与改动前**逐像素一致**）。

**交付**：

- 单条调整图层 + 单个 LUT 生效，影响下方轨道
- 无调整图层时，输出与改动前逐像素一致（回归断言）
- 可撤销（增删调整图层走命令）

**预估**：2 天。

---

### B2-7 ✅ 轨道成组（核心层）

**已实施（2026-10-27）**：

- `TrackGroupId` + `Sequence::trackGroups` + `Sequence::groupOf()`；
- 三个可撤销命令：`CreateTrackGroupCommand` / `RemoveTrackGroupCommand` /
  `SetTrackGroupFlagCommand`（静音/隐藏/锁定联动）；
- **一条轨道只能属于一个组**：违反时拒绝并说明「已属于其它轨道组，请先解组」。
  理由：两个组同时管一条轨会让「A 组静音 / B 组取消静音」无从裁决；
- 组内联动**只产生一条撤销记录**：逐条设置会让用户按一次撤销只还原一条，
  观感是「撤销坏了」；
- 撤销按成员**各自原值**还原（成员原本状态不一致时不会一刀切）；
- `Sequence::invariantsHold()` 纳入组校验：组非空、成员存在、无重复归组；
- 序列化：仅非空时写入；载入时**整组丢弃**引用幽灵轨道的组（保留会让联动永远失败）。

**未完成**：跨类型联动**移动**（整组上下挪）、C ABI 的
`nc_create_track_group` / `nc_ungroup_tracks` / `nc_track_group_list`、UI 折叠与高亮。

**原始问题**：

**现状**：全库 `group` / `分组` / `成组` / `link` 在 `track.h`、`sequence.h`
**0 命中**。轨道之间完全独立。

**你的需求**：音频轨、视频轨、字幕轨可以任意组合成一组，
组内联动操作（一起移动 / 一起删除 / 一起显示隐藏）。

**方案**：

```cpp
struct TrackGroup {
    TrackGroupId id;
    std::string name;              // 如「主画面组」
    std::vector<TrackId> tracks;   // 允许跨类型
    bool collapsed = false;        // UI 折叠
};

struct Sequence {
    // ... 现有字段
    std::vector<TrackGroup> trackGroups;
};
```

**联动语义**（必须明确，否则实现会歧义）：

| 操作 | 组内行为 |
|---|---|
| 移动轨道上下位置 | 整组一起移动（保持组内相对顺序） |
| 删除轨道 | **不联动**（删单条轨不该删整组）→ 必须显式「删除整组」 |
| 静音/隐藏 | 联动（一次切所有成员） |
| 锁定 | 联动 |
| 选中 | 点任一成员 → 整组高亮（可选，建议做成设置项） |

**注意**：一条轨道**只能属于一个组**（避免嵌套歧义）。

**C ABI**：

```c
int32_t nc_create_track_group(NcEditor*, int32_t sequence_index,
                              const int32_t* track_indexes, int32_t count,
                              const char* name, uint64_t* out_group_id, char** out_error);
int32_t nc_ungroup_tracks(NcEditor*, int32_t sequence_index, uint64_t group_id, char** out_error);
int32_t nc_track_group_list(NcEditor*, int32_t sequence_index, char** out_json, char** out_error);
```

**交付**：成组/解组可撤销；组内静音联动；一条轨道不能加入两个组（拒绝）。

**预估**：2 天。

---

### B2-8 ✅ 片段跨轨拖动的类型校验（核心层 + C ABI）

**B0-7 已做的事**：`validateClipPlacement` 会拒绝不相容的落点。
**但那不够**——它要求一个完整 `Clip`，且会因重叠/锁定等原因失败，
**无法区分「类型不兼容」与「位置被占」**。UI 想在拖动过程中显示禁止态，
需要一个不依赖 Clip、只看类型的查询。

**发现的核心缺口**：`trackAcceptsClipType` 和 `PlacementCompat` 原本藏在
`commands.cpp` 的**匿名 namespace** 里，外部根本调不到。

**已实施（2026-10-28）**：
1. 把 `PlacementCompat` / `trackAcceptsClipType` / `trackTypeLabel` /
   `mediaTypeLabel` 提升为公开 API（声明进 `commands.h`）；
2. 新增 2 个 C ABI：
```c
int32_t nc_track_accepts_media(int32_t track_type, int32_t media_type);
int32_t nc_track_placement_reason(int32_t track_type, int32_t media_type,
                                  char** out_reason, char** out_error);
```
`nc_track_accepts_media` 返回 1/0/-1（可放/不兼容/参数非法）；
`nc_track_placement_reason` 额外给出**中文原因**供 UI tooltip 显示。

**相容表**（沿用 B0-7 的结论，含那处有意偏离）：

| 轨道 \ 素材 | 视频 | 音频 | 图片 | 未知 |
|---|---|---|---|---|
| Video | ✅ | ❌ 黑屏 | ✅ | ⚠️ 放行 |
| Audio | ✅ 取声音 | ✅ | ❌ 读不出 PCM | ⚠️ 放行 |
| Subtitle | ❌ | ❌ | ❌ | ⚠️ 放行 |
| Adjustment | ❌ | ❌ | ❌ | ⚠️ 放行 |

**测试**（`tests/test_track_placement_query.cpp`，12 用例）：
- 相容表逐格验证（`VideoTrackAcceptsVideoAndImage` /
  `AudioTrackAcceptsAudioAndVideo` / `SubtitleAndAdjustmentRejectEverything` /
  `UnknownMediaIsTolerated`）；
- `CapiAcceptsMatchesCoreTable` — C ABI 的整数映射与核心层一致；
- `CapiRejectsInvalidEnums` — 越界枚举返回 -1 并留下原因；
- `ReasonExplainsWhyRejected` / `ReasonForAllowedIsNull` /
  `ReasonForUnknownExplainsTolerance` — 原因文本的三种情形；
- **`ConsistentWithActualInsert`** — 最关键的一条：查询说「不可放」时
  真的插入也必须失败。否则 UI 显示禁止但实际能放（或反之），
  用户会彻底失去对拖放反馈的信任。

**变异验证**：
1. 「音频轨接受一切」→ `AudioTrackAcceptsAudioAndVideo`、
   `CapiAcceptsMatchesCoreTable`、`ConsistentWithActualInsert` 三个报警 ✅
2. 「C ABI 的 media_type 映射把 Image/Audio 交换」→ 3 个用例、10 个检查点报警 ✅
   （这条专门验证「整数映射不能错位」——C ABI 最容易出的错）

**开发中的一次失误（如实记录）**：用脚本移动代码块时，第二次 `replace`
的锚点没匹配上导致 `AssertionError`，但**第一次删除已经落盘**——
把 `trackTypeLabel`/`mediaTypeLabel` 的实现删掉了，只留下一条孤立的注释。
编译期报 `undefined reference` 才发现。教训：**多步文件改写脚本要在
每步之间做可回滚的备份**，或者用一次性的整体重写而不是多次 `replace`。

**未完成**：UI 层（`editor.js`）接入——需要前端在 `dragstart`/`dragover`
时调 `nc_track_accepts_media` 并切换 CSS 类。C ABI 已就绪。

---

### B2-9 ⬜ 素材区拖动排序 + 拖到时间轴

**现状**：素材区只能点击导入，**无拖动、无排序、无分组**。

**方案**：

1. 素材网格支持长按拖动排序（`asset.order` 字段或独立顺序数组）
2. 素材可**直接拖到时间轴**（落到目标轨道 + 目标时间）
3. 拖到时间轴时预览「落点幽灵」（半透明占位）

**注意**：素材顺序是否需要持久化？建议**持久化**（用户整理过的顺序不应丢失）。

**交付**：排序后序列化往返一致；拖到时间轴生成正确片段。

**预估**：1.5 天。

---

### B2-10 ✅ 轨道高度可调 + 折叠（核心层）

**已实施（2026-10-27）**：

- `Track` 增加 `heightPx`（默认 48，范围 24~200）与 `collapsed`；
- `TimelineEditor::setTrackHeight(...)` + `SetTrackHeightCommand`（可撤销）；
- 越界**拒绝**而非静默截断——静默截断会让用户「拖了半天突然跳回去」；
- 序列化：仅非默认值写入（保持 diff 稳定）；读取时缺省用默认值，
  **越界值截断**而不是拒绝（高度写坏不该让整份工程打不开，与 `gainDb` 同策略）；
- `Track::invariantsHold()` 纳入高度合法性。

**待完成**：UI 层拖拽调高/双击折叠的交互与 `nc_track_*` C ABI 暴露。
本次只交付核心层数据模型与持久化。

**原始问题**：

**现状**：`editor.js` 中 `ROW_H` 是常量，所有轨道等高，不可调、不可折叠。

**问题**：音频轨需要更高才能看清波形；轨道多了滚动很长。

**方案**：

```cpp
struct Track {
    // ...
    int heightPx = 48;      // 可调（24~200）
    bool collapsed = false; // 折叠（仅显示标题栏）
};
```

UI：拖轨道下边缘调整；双击标题栏折叠。

**注意**：`heightPx` 属于**视图状态**，是否序列化？
建议**序列化**（用户调好的布局应保留），但要考虑不同屏幕尺寸的适配
（存相对值或上限截断）。

**交付**：高度调整后序列化往返；折叠状态保留。

**预估**：0.5 天。

---

### B2-11 ⬜ 素材区双指缩放（列数切换）

**现状**：时间轴支持双指捏合（`editor.js:1014`），但**素材网格区不支持**。

**方案**：素材网格双指捏合 → 切换 2/3/4 列。

**交付**：捏合手势识别 + 列数切换 + 记住偏好。

**预估**：0.5 天。

---

## 五、B3 卡点快剪（动漫超燃品类）

> 判定依据：该品类 16 个要素中 13 项项目已具备（节拍检测、34 转场、
> 35 特效、速度曲线、Flash/RgbSplit/ZoomPunch 全有）。

### B3-1 ⬜ 卡点自动切分引擎

**现状**：`beat_detect.cpp` 已能出 BPM 与节拍时间点；
`applyBeatSync` 存在于 `auto_edit.h`，但需核实是否按重音强度分级。

**方案**：

1. 节拍分级（强拍 / 弱拍）——需要音频能量包络；
2. 强拍切镜头，弱拍不动；
3. 鼓点密集段落自动缩短镜头时长；
4. 静音段自动放长（呼吸）。

**交付**：给定音乐 + 素材，输出时间线；断言切点落在节拍容差内（±30ms）。

---

### B3-2 ✅ 抽帧/冻帧接入导出

**核实结论（先纠正文档的过时描述）**：`FreezeFrame` **命令存在**（`nc_freeze_frame`），
但它此前**没有显式语义**——实现是把 `speed` 设成 `1/1000000` 来"骗取"几乎不动
的效果，因为 `Clip::valid()` 要求 `speed.isPositive()`，写不了 `speed = 0`。

**已实施（2026-10-27）**：

- 新增 `Clip::freeze`（显式语义）+ `RenderClip::freeze`；
- `Clip::sourceTimeAt()` 与导出器的 `mapSourceTime()` 都优先判 `freeze`，
  返回恒定的 `sourceIn`；
- `nc_freeze_frame` 改用 `freeze = true`（`speed` 保持 1，避免 `sourceOut` 越界）；
- 序列化：仅启用时写 `"freeze": true`；
- 三端一致：导出、预览、C ABI 走同一判定（`sourceTimeAt` 是内联实现）。

**为什么要改掉旧实现**（不是洁癖）：`speed = 1e-6` 在 1 秒定格下源时间只漂
1 微秒、10 秒也只漂 10 微秒，看似无害；但它的正确性**依赖取样方式的巧合**。
一旦速度下限被放宽、或 reader 改成"按毫秒取整再换算帧号"，定格就会静默失效，
而测试仍会通过（因为旧测试只断言 `sn > 0 && sd >= sn`）。

**交付验证**（文档要求"冻帧段像素完全一致"）：

| 断言 | 内容 |
|---|---|
| `FrozenSegmentHasIdenticalPixels` | 30 帧逐帧比对，全部等于首帧 |
| `EmptyFreezeLongSegmentStaysFrozen` | 10 秒定格（300 帧）仍完全静止 |
| `NonFrozenClipDoesVaryAcrossFrames` | **反向对照**：不冻帧的片段必须逐帧变化 |
| `FreezeUsesSourceIn` | 不同 `sourceIn` 的定格片段像素不同 |
| `FreezeOverridesReverseAndCurve` | 定格优先级高于倒放/曲线 |

变异验证：删掉 `sourceTimeAt` 与 `mapSourceTime` 里的 `freeze` 判断 →
4 个用例立即报警（含两个导出路径的）。

---

### B3-3 ⬜ 冲击组合预设

把以下组合打包为"超燃"预设：

```
强拍 → 硬切 + FlashWhite(0.15s)
爆发点 → 速度曲线（0.3x → 2x 骤变）
冲击帧 → RgbSplit + ZoomPunch
持续段 → Glitch 低频 + Scanlines
整体 → LUT（高对比青橙）
```

**交付**：一键应用 + 可调强度滑块。

---

### B3-4 ⬜ 燃向字幕样式

**现状**：字幕系统有样式（字体/描边/位置），缺"燃向"预设。

**新增预设**：粗描边 + 斜体 + 弹入动画 + 逐字出现。

**交付**：预设 + 预览。

---

## 六、B4 参考视频分析（风格迁移）

> **口径澄清**：做的是"同类型风格"，不是"审美判断"。
> 见 §八「明确不做」的说明。

### B4-1 ⬜ 读参考视频 → FrameFeature

**现状**：`FrameFeature` 结构已有（时间戳 / avgRGB / histogramDiff），
`detectShots()` 已有，但**没有"从视频文件读"的入口**。

**方案**：新增 `nc_reference_analyze_video`。

```
FFmpegReader 解码 → 每 N 帧算 avgRGB + 直方图差异
                 → detectShots() → analyzeRhythm()/analyzeColor()
                 → 输出 style_json
```

**交付**：给定参考视频，输出 JSON 含 shotCount / median / p25 / p75 / 色调。

---

### B4-2 ⬜ 一键仿剪

`nc_reference_auto_edit(style_json, material_ids) → summary`

内部：`suggestCuts` → `applyAutoEdit` → 事务化。

**交付**：应用后可撤销；输出的镜头时长分布与参考的偏差 < 20%。

---

### B4-3 ⬜ 色调自动匹配（LUT 联动）

**关键**：把 `ColorProfile`（avgR/G/B/饱和度/冷暖）转成近似 LUT，
让成片色调贴近参考。**这是"像那个视频"的核心观感来源**。

**交付**：给定参考色调，生成的 LUT 应用到测试图后，平均色差 ΔE < 阈值。

---

## 七、B5 AI 能力补齐（⛔ 阻塞）

### B5-1 ⛔ 人像抠像模型

**阻塞原因**：`models/matting_basic/` 与 `matting_hq/` 均为空；
已实测 2 个公开 ONNX 导出（MediaPipe / MODNet）输出**恒 0**。

**解锁路径**（按可靠性排序）：

1. 用 `optimum` 从官方 PyTorch 权重自行导出（最可靠，需 torch 环境）；
2. 找带 sigmoid 后处理的正确导出；
3. MediaPipe 官方 `.tflite` + TFLite 运行时（引入新依赖）。

**解锁后受益**：AI 换背景、美颜、瘦身、发光轮廓、前景分层模板。

### B5-2 ⛔ TTS 实现

有 `tts_zh` 模型（116M），无 `tts_ai.cpp`。需先协议核查。

### B5-3 ⬜ 人脸/目标跟踪（OpenCV）

`nc_set_camera_tracking` 已有数据结构，但追踪点需外部传入。
OpenCV 有 CSRT/KCF，可接入。**不依赖 AI 模型**，可先做。

---

## 八、明确不做

以下需求经评估**不做**，并说明原因，避免反复讨论：

| 需求 | 原因 |
|---|---|
| **AI 判断"高级/审美"** | 无客观函数可优化；需生成式模型或云 API（违规） |
| **从成片反推用了什么特效** | 欠定问题（inverse rendering），学术未解 |
| **从成片反推变速曲线** | 无法区分摄影机运动与后期变速 |
| **AI 自己看番剧挑"最燃"片段** | 需视频理解模型判断语义激烈程度，现无且不可端侧 |
| **云端大模型剪辑** | 违反 `COMPLIANCE.md` 第 1 条（剪辑全本地） |
| **粒子引擎增强** | 你已明确不做 |

> **替代方案**：以上需求用「模板 + 打分填充 + 确定性特效」实现同等观感。
> 竞品 Dula 的"AI 剪辑"即此路线（其界面显示"参考镜头 01-09 / 使用 N 处"）。

---

## 九、依赖关系图

```
B0-1 Rational 溢出 ─────┬─→ B3-1 卡点切分（时间精度关键）
B0-2 线程模型 ──────────┼─→ B2-3 模板应用（可能后台执行）
                        └─→ 导出并发
B0-5 序列化兼容 ────────→ B2-1 模板结构 / B2-7 轨道组（都会改 Project）
B0-7 轨道类型校验 ──────→ B2-8 UI 拖放反馈
B0-8 Effect 空壳清理 ───→ B2-6 调整图层（复用枚举值 3）
B1-1 混合模式 ──────────→ P-16 混合面板接引擎
B1-2 LUT ───────────────→ B4-3 色调匹配 / B2-6 调整图层
B1-5 摇晃 ──────────────→ B2-1 模板槽位（摇晃参数）
B2-5 轨道重排序 ────────┬─→ B2-7 轨道成组（组内需保持相对顺序）
B2-6 调整图层 ──────────→ 全局 LUT/暗角（模板前置能力）
B2-7 轨道成组 ──────────→ 多轨联动编辑
B4-1 读视频 ────────────→ B4-2 一键仿剪 ──→ B4-3
B5-1 抠像模型 ──────────→ 前景分层模板 / 美颜 / 瘦身
```

**关键路径**：`B0-1 → B0-2 → B0-7 → B2-5 → B2-7`
（时间精度 → 并发安全 → 类型正确 → 可重排 → 可成组）

---

## 十、建议执行顺序

| 周次 | 内容 | 可验证产出 |
|---|---|---|
| W1 | B0-1、B0-2、B0-5 | 三平台时间精度一致；线程契约；跨版本加载 |
| W2 | B0-7、B0-8、B2-5、B2-10 | 类型校验；Effect 清理；轨道可重排；高度可调 |
| W3 | B1-1、B1-5、B1-2 | 16 混合模式；摇晃特效；LUT + 20 预设 |
| W4 | B2-6、B2-7 | 调整图层生效；轨道成组联动 |
| W5 | B1-3、B1-4、B3-2 | Bloom/光晕；动态模糊；冻帧 |
| W6 | B2-9、B2-11、B2-8 | 素材拖动排序；捏合列数；拖放反馈 |
| W7 | B2-1~B2-4 | 模板系统可用（10 个内置模板） |
| W8 | B3-1、B3-3、B3-4 | 卡点切分；超燃预设；燃向字幕 |
| W9 | B4-1、B4-2、B4-3 | 参考视频 → 同风格成片 |

> **调整说明（2026-10-27）**：原计划 W2 直接做特效，现将轨道系统提前到 W2/W4，
> 因为调整图层（B2-6）是 LUT 全局应用与模板系统的前置能力；
> 轨道成组（B2-7）是专业编辑体验的基础。

**每周末产出可实测的构建**，你看效果再决定下周是否调整。
