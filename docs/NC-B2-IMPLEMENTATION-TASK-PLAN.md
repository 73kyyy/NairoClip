# NairoClip B2 实施任务规划

| 项 | 值 |
|---|---|
| 文档编号 | NC-B2-IMPLEMENTATION-TASK-PLAN |
| **文档版本** | **V1.4（全量技术复审 · 定稿候选）** |
| 阶段 | C 阶段 · 批次 B2（9 组 / 18 个重复 op 参数映射收敛） |
| 审计基准 | `main` @ `bd2e862c13e0e7ba7b22f29bfa9add1c688d5d6f`（本轮实测） |
| 文档性质 | 实施规划。**批准本文档 ≠ 授权任何代码实施**（见 §9.3） |
| 修订记录 | 见 §0.1 |

---

## 目录

| 章节 | 标题 |
|---|---|
| §0 | 文档控制 |
| └ §0.1 | 版本历史 |
| └ §0.2 | V1.3 → V1.4 修订清单 |
| └ §0.3 | 术语与约定 |
| └ §0.4 | 权威定义索引（单一事实源） |
| §1 | 范围与边界 |
| └ §1.1 | 批次定位 |
| └ §1.2 | 必须保留的边界（15 条，不得退化） |
| └ §1.3 | 本轮明确不做 |
| §2 | 源码事实基线（逐字核对） |
| └ §2.1 | 读取函数语义 |
| └ §2.2 | 数值转换路径与精度 |
| └ §2.3 | 九组参数映射总表（表 T-01） |
| └ §2.4 | G5 / G6 精确读取链 |
| └ §2.5 | 返回值与副作用（表 T-02） |
| └ §2.6 | G9 的 LUT 清除链路（表 T-03，R7 依据） |
| └ §2.7 | 待核实项汇总 |
| §3 | 数据结构设计 |
| └ §3.1 | 参数来源规格（R1 核心） |
| └ §3.2 | 九组规格表（表 T-04） |
| └ §3.3 | A 层：规格表静态类型安全（**B2 必做**） |
| └ §3.4 | 参数读取算法（权威定义） |
| └ §3.5 | `OpCallContext` 与 spec 生命周期（表 T-06） |
| └ §3.6 | 响应载荷（表 T-05） |
| └ §3.7 | `rawOp` 暴露边界（E-12） |
| §4 | 数值安全 |
| └ §4.1 | int64 / double 边界（R3 / R4 依据） |
| └ §4.2 | 微秒换算五步检查（表 T-07） |
| └ §4.3 | 边界测试表（表 T-08，R4 依据） |
| └ §4.4 | 运行时防护归属（E-10） |
| §5 | 兼容性与行为变更 |
| └ §5.1 | 合法输入兼容性（E-09） |
| └ §5.2 | 非法输入（**B 层，默认不实施**） |
| └ §5.3 | 两层分离（E-10） |
| §6 | 测试设计 |
| └ §6.1 | 三层结构与覆盖边界 |
| └ §6.2 | 层 ① 用例清单（表 T-09，`test_op_alias.cpp`） |
| └ §6.3 | 层 ② 用例清单（`test_nc_dispatch_compat.cpp`） |
| └ §6.4 | 层 ③ 前置：JNI 真机测试清单（表 T-11） |
| └ §6.5 | 明确不覆盖（不得声称已测） |
| §7 | 批次计划 |
| └ §7.1 | 依赖图 |
| └ §7.2 | 各批详表（每批 14 字段） |
| └ §7.3 | 守卫工具检查项（`check_op_alias.py`） |
| §8 | 唯一验收矩阵（表 T-12） |
| §9 | 停止点与人工审批 |
| └ §9.1 | 最终停止点表（表 T-13） |
| └ §9.2 | 通用规则 |
| └ §9.3 | 文档批准 ≠ 实施授权 |
| §10 | 风险与回滚（表 T-14） |
| §11 | B1 遗留事项（逐条引用原文，不凭记忆） |
| └ §11.1 | 来源与原文 |
| └ §11.2 | 当前状态与是否阻断 B2 |
| └ §11.3 | B1 §2.5 事实表述修正（引用，不改 B1 报告） |
| └ §11.4 | B1 与 B2 的关系 |
| §12 | 未解决问题清单（表 T-15） |
| └ §12.1 | 文档内部问题 |
| └ §12.2 | 源码事实待核实 |
| └ §12.3 | 环境无法执行 |
| └ §12.4 | B1 遗留 |
| └ §12.5 | B2 范围外风险（仅记录，不处理） |
| §13 | 主动技术审查结果（任务书九·20 项） |
| §14 | 附录 |
| └ §14.1 | 关键常量 |
| └ §14.2 | 引用文件 |
| └ §14.3 | 文档一致性自检 |

---

## 0. 文档控制

### 0.1 版本历史

| 版本 | 主要变化 |
|---|---|
| V1.0 | 初稿 |
| V1.1 | 落实首轮 7 条评审意见 |
| V1.2 | 落实第二轮 7 条评审意见（**已纠正 V1.1 两处事实错误**：短名链误插 `time_us`；舍入误写 `llround`） |
| V1.3 | 落实第二轮整改 11 项（拆静态/运行时两层、`OpArgSpec` 扩展、微秒边界五步、统一验收矩阵） |
| **V1.4** | **全量复审**：数据结构重构为「每来源独立规格」、数值边界重算并删除错误案例、LUT 全链补全、动态 ID 改读回验证、文档一致性与编号体系整理。修订清单见 §0.2 |

### 0.2 V1.3 → V1.4 修订清单

| # | 原问题 | 修改位置 | 修改内容 | 依据 | 是否彻底解决 |
|---|---|---|---|---|---|
| R1 | `OpArgSpec` 三来源共用 `kind`/`unit`/`conversion`，无法表达 `at_us`(µs) 与 `t`(s) 单位不同 | §3.1 / §3.2 | 重构为 `ArgSourceSpec`：**每个来源独立** `key`/`kind`/`unit`/`conversion`/`defaultValue`/`fallback` | 源码 `nc_jni.cpp:832-844` | ✅ |
| R2 | 正文用文字补充参数语义，结构体表达不了 | §3.1 / §3.2 | 9 组参数全部落成可映射到结构体的表格，无"靠正文解释"的字段 | 同上 | ✅ |
| R3 | 误把 `t=2^31` 秒列为溢出案例 | §4.1 | **删除**；实测 `2^31 s × 1e6 = 2.147e15 µs ≪ 2^63`，不溢出 | 本轮数值计算 | ✅ |
| R4 | 边界只用十进制字符串，未给 IEEE-754 语义 | §4.3 | 改用 `std::nextafter` 构造相邻可表示值，逐个标注 double 实际值 | 本轮计算 | ✅ |
| R5 | 未明确 `2^53+1` 的精度语义 | §2.2 / §5.1 | 明确 `9007199254740993 →(double) 9007199254740992`，**禁止改整数解析** | 本轮计算 | ✅ |
| R6 | 动态 ID 只要求"大于 0" | §6.3 | 改为**读回验证**（`nc_effect_get`/`nc_adjustment_effect_get` 按 ID 匹配 type+value）；并记录"失败也消耗 ID" | 源码 `capi.cpp:2601-2605`、`editor.cpp:51` | ✅ |
| R7 | LUT 清除只到"赋空串"，未覆盖 undo/redo 与 strength/effects 保留 | §2.6 / §6.4 | 补 9 步链路 + undo/redo + strength 覆盖语义 + effects 保留断言 | 源码 `commands.cpp:447-481` | ✅ |
| R8 | 验收矩阵与正文有遗漏/不一一对应 | §8 | 22 项唯一矩阵，每项标注批次/命令/通过标准/失败标准/阻断性/证据 | 本轮 | ✅ |
| R9 | 批次表字段不全 | §7.2 | 每批 14 个字段齐全（目标/范围/禁止/依赖/文件/生产改动/测试/命令/证据/失败判定/回滚/PR/审批/能否进下一批） | 任务书五 | ✅ |
| R10 | B1 遗留事项描述凭记忆 | §11 | 逐条引用 B1 报告原文行号；修正 §2.5 事实表述 | `NC-B1-FINAL-ACCEPTANCE-REPORT.md:328-331`、`NC-B1-FOLLOWUP-REPORT.md:322-324` | ✅ |
| R11 | 章节/表格/交叉引用不统一 | 全文 | 统一编号（表 T-xx、章节 §x.y）、建立 §0.3 权威定义索引 | 本轮 | ✅ |
| R12 | 存在"适当处理""必要时"等模糊表述 | 全文 | 全部替换为可判定的规则 | 本轮 | ✅ |

### 0.3 术语与约定

| 术语 | 定义 |
|---|---|
| **短名 op** | 形如 `opacity` / `keyframe` 的历史 op 名（`legacyOp`） |
| **规范 op** | 形如 `nc_set_clip_opacity` 的 `nc_*` 名（`canonicalOp`） |
| **业务组** | 一组实现同一业务的两个 op（短名 + 规范名），共 9 组 / 18 个名字 |
| **来源（source）** | 一个参数的取值出处：`canonical` / `legacy` / `alternate` 三种 |
| **合法输入** | `JNum`/`JInt`/`JStrField` 能给出**明确定义**结果的输入（见 §5.1） |
| **UB 输入** | 旧实现中行为未定义（`static_cast<int64_t>` 越界/NaN/Inf）的输入（见 §5.2.2） |
| **A 层** | 规格表**静态**类型安全（B2 必做） |
| **B 层** | 运行时非法输入防护（**B2-3b**，默认不实施） |
| **阻断项** | 未通过则禁止进入下一批/禁止合并 |

### 0.4 权威定义索引（单一事实源）

| 主题 | 唯一权威位置 | 其余章节处理方式 |
|---|---|---|
| 参数来源结构 | §3.1 | 引用 |
| 九组参数映射 | 表 T-01（§2.3）、表 T-04（§3.2） | 引用 |
| 参数读取算法 | §3.4 | 引用 |
| 数值边界与换算 | §4.1 / §4.2 / 表 T-08 | 引用 |
| 合法/非法输入分界 | §5.1 / §5.2 | 引用 |
| 验收项与标准 | §8（唯一矩阵） | 其他章节不重复列标准 |
| 批次计划 | §7.2 | 引用 |
| 停止与审批 | §9 | 引用 |
| B1 遗留 | §11 | 引用 |

> **规则**：任何规则只在上表指定位置定义一次；其他章节只引用，不重述。

---

## 1. 范围与边界

### 1.1 批次定位

| 批次 | 内容 | 归属 |
|---|---|---|
| B1 | 三个绕过项接入统一 Command | 已完成（有条件通过，见 §11） |
| **B2** | **9 组 / 18 个重复 op 参数映射收敛** | **本文档** |
| B3 | 71 个 facade op 接入命令信封 | 未授权 |
| B4 | 事务 / Task 全面迁移 | 未授权 |
| B5 | 全链路收口 | 未授权 |
| GPU G0–G6 | 独立并行工作流 | 不在 B2 范围 |

### 1.2 必须保留的边界（15 条，不得退化）

| # | 边界 |
|---|---|
| E-01 | B2 仅处理 9 组 / 18 个名字 |
| E-02 | 短名与规范名解析到**同一份** `OpAliasSpec`（同地址） |
| E-03 | keyframe 短名链**只有** `at_us → t`，**不存在** `time_us` |
| E-04 | 规范名链**只有** `time_us`，**不存在** `at_us` / `t` |
| E-05 | `at_us < 0` 时回落到 `t`（含显式负值） |
| E-06 | 秒→微秒用 `static_cast<int64_t>`，**向零截断** |
| E-07 | `v` 默认 **1.0**，`value` 默认 **0.0** |
| E-08 | 不新增源码不存在的跨键回落 |
| E-09 | 合法输入新旧行为**逐字节兼容** |
| E-10 | 非法输入收紧必须**独立审批**（B2-3b） |
| E-11 | 真实 C ABI 集成测试**不得 mock C ABI** |
| E-12 | 前端 JSON 响应**不新增字段** |
| E-13 | 其余约 100 个 op 保持**原路径/原参数/原错误码/原载荷** |
| E-14 | 不进入 B3 / B4 / B5 |
| E-15 | B2 期间 `nc_jni.cpp` 归 B2 独占；GPU 工作流不得同时修改 |

### 1.3 本轮明确不做

不修改生产源码、测试源码、CI、构建配置、C ABI、JNI、Kotlin、C++ 实现、GPU 文档、B1 报告；
不创建或执行 B2-1~B2-4 的实施任务；不合并代码 PR；不自动推进下一批。

---

## 2. 源码事实基线（逐字核对）

> 全部结论来自 `mobile/native/android/nc_jni.cpp` 与 `core/src/*.cpp` 的**实际语句**，
> 不以历史文档推断。行号为本轮实测定位参考。

### 2.1 读取函数语义

```cpp
bool FindKey(json, key, &pos);        // 找 "key" 后第一个 ':'；失败返回 false
double JNum(json, key, def) {
    if (!FindKey(...)) return def;
    跳过 ' ' / '\t'; strtod(json+p, &end);
    return (end == start) ? def : v;   // 解析失败 → def
}
int64_t JInt(json, key, def) {
    return static_cast<int64_t>(JNum(json, key, (double)def));
}
std::string JStrField(json, key);      // 取引号内文本；处理 \n \t \ 转义
```

| 输入形态 | `JNum` 结果 | 性质 |
|---|---|---|
| 键缺失 | `def` | 明确定义 |
| `null` | `strtod("null")` 失败 → `def` | 明确定义（**等同缺失**） |
| 非数值文本（`"abc"` / `{}` / `[]` / `true`） | `def` | 明确定义（静默回落） |
| 数值文本 | `strtod` 解析值 | 明确定义 |
| `nan` / `inf` 字面（畸形 JSON） | `strtod` 可解析出 NaN/Inf | 明确定义（但下游 `int64` 转换 **UB**，见 §5.2.2） |

> **[待核实 V-01]** `FindKey` 为子串搜索，理论上可能命中字符串值内部的 `"key"` 片段。
> 现有 9 组键名未发现实际触发场景；B2-3 差分测试将构造用例覆盖。
> **阻塞条件**：若构造用例触发，则暂停 B2-3 并单独报告，不得擅自改 `FindKey`。

### 2.2 数值转换路径与精度

**旧路径（不得更改）**

```
JSON 数字文本  --strtod-->  double  --static_cast<int64_t>-->  int64_t
```

| 事实 | 数值 |
|---|---|
| double 可精确表示的最大整数 | `2^53 = 9007199254740992` |
| 文本 `9007199254740993` 经 `strtod` | `9007199254740992.0`（**精度损失 1**） |
| 文本 `9223372036854775807`（INT64_MAX）经 `strtod` | `9223372036854775808.0`（= `2^63`，**越界**） |
| `(double)INT64_MAX` | `9223372036854775808.0`（向上舍入为 `2^63`） |

**硬约束**：新实现**不得**改用 `strtoll` 或其它整数解析。
原因：旧路径经 `double`，>2^53 的整数会被舍入；改整数解析会**改变合法输入的返回值**（违反 E-09）。

### 2.3 九组参数映射总表（表 T-01）

> 本表是 §3.2 规格表的**源码依据**；两表必须逐格对应。

| 组 | 短名 op | 规范 op | 参数（语义名） | 短名读取链 | 规范读取链 |
|---|---|---|---|---|---|
| G1 | `opacity` | `nc_set_clip_opacity` | 不透明度 | `value`（JNum, def **1.0**） | `opacity`（JNum, def **1.0**） |
| G2 | `add_track` | `nc_add_track` | 名称 / 类型 | `name`（JStr, def ""）；`type`（JInt, def 0） | 同左 |
| G3 | `blend` | `nc_set_clip_blend_mode` | 混合模式 | `mode`（JInt, def 0） | 同左 |
| G4 | `effect_add` | `nc_add_effect` | 类型 / 强度 | `type`（JInt, def 0）；`value`（JNum, def **0.5**） | 同左 |
| G5 | `keyframe` | `nc_insert_keyframe` | 属性 / 时间 / 值 | 见 §2.4 | 见 §2.4 |
| G6 | `remove_keyframe` | `nc_remove_keyframe` | 属性 / 时间 | 见 §2.4 | 见 §2.4 |
| G7 | `adjust_effect_add` | `nc_adjustment_add_effect` | 轨道 / 类型 / 强度 | `track_index`(0)；`type`(0)；`value`（JNum, def **1.0**） | 同左 |
| G8 | `adjust_effect_remove` | `nc_adjustment_remove_effect` | 轨道 / 效果 ID | `track_index`(0)；`effect_id`（JInt→uint64, def 0） | 同左 |
| G9 | `adjust_lut` | `nc_adjustment_set_lut` | 轨道 / 路径 / 强度 | `track_index`(0)；`path`（JStr，空→`nullptr`）；`strength`（JNum, def **1.0**） | 同左 |

**关键事实**：G1/G3/G4/G7/G8/G9 **各自只读自己那一套键，源码中无任何跨键回落**（E-08）。

### 2.4 G5 / G6 精确读取链

**短名 `keyframe`（`nc_jni.cpp:832-844`）**

```
1) prop  = JInt(a,"prop", JInt(a,"property", 0))     → prop → property → 0
2) atUs  = JInt(a,"at_us", -1)                        → 默认 -1
3) if (atUs < 0) atUs = (int64)(JNum(a,"t",0.0) * 1e6)  → 秒→微秒，向零截断
4) v     = JNum(a,"v", 1.0)
```

**规范名 `nc_insert_keyframe`（`:539-552`）**

```
1) property = JInt(a,"property", 0)
2) time_us  = JInt(a,"time_us", 0)
3) value    = JNum(a,"value", 0.0)
```

**短名 `remove_keyframe`（`:614-621`）**：同 G5 的 1)–3)（无 `v`）
**规范名 `nc_remove_keyframe`（`:1890-1897`）**：`property` 0；`time_us` JInt 0

### 2.5 返回值与副作用（表 T-02）

| 组 | C ABI 返回类型 | 成功返回值 | 失败返回值 | 副作用 |
|---|---|---|---|---|
| G1 `nc_set_clip_opacity` | `int32_t` | `NC_OK` | 错误码 | 改片段不透明度（可撤销） |
| G2 `nc_add_track` | `uint64_t` | 新 `track_id` | `0` | 新增轨道（可撤销）；**失败也消耗一个 ID** |
| G3 `nc_set_clip_blend_mode` | `int32_t` | `NC_OK` | 错误码 | 改混合模式（可撤销） |
| G4 `nc_add_effect` | `uint64_t` | 新 `effect_id` | `0` | 片段特效栈追加（可撤销）；**失败也消耗 ID** |
| G5 `nc_insert_keyframe` | `int32_t` | `NC_OK` | 错误码 | 插入关键帧（可撤销） |
| G6 `nc_remove_keyframe` | `int32_t` | `NC_OK` | 错误码 | 删除关键帧（可撤销） |
| G7 `nc_adjustment_add_effect` | `uint64_t` | 新 `effect_id` | `0` | 调整层效果栈追加（可撤销）；**失败也消耗 ID** |
| G8 `nc_adjustment_remove_effect` | `int32_t` | `NC_OK` | 错误码 | 删除调整层效果（可撤销） |
| G9 `nc_adjustment_set_lut` | `int32_t` | `NC_OK` | 错误码 | **整栈替换** effects + 写 lutPath + **写 lutStrength**（可撤销） |

**ID 消耗语义（R6 依据）**：`nc_add_track`（`editor.cpp:51`）、`nc_add_effect`（`capi.cpp:2602`）、
`nc_adjustment_add_effect`（`capi.cpp:2728`）都在**执行前**调用 `project.ids.next()`。
因此：

- **不得**用「ID > 0」作为成功判据（失败返回 0，但 ID 已被消耗）；
- **不得**用「ID 连续递增」作为成功判据（失败会留下 ID 空洞）；
- **必须**用读回验证（§6.3.3）。

### 2.6 G9 的 LUT 清除链路（表 T-03，R7 依据）

| 步 | 位置 | 行为 |
|---|---|---|
| ① | `nc_jni.cpp` `adjust_lut` | `path = JStrField(a,"path")`；`lp.empty() ? nullptr : lp.c_str()` |
| ② | `nc_adjustment_set_lut`（`capi.cpp:2764`） | `lutPath = (path != nullptr) ? path : ""` → **nullptr 变空串**；并校验 `strength ∈ [0,1]` |
| ③ | `TimelineEditor::setAdjustment`（`editor.cpp:104`） | 构造 `SetAdjustmentCommand(seq, trackId, effects, lutPath, strength)` |
| ④ | `SetAdjustmentCommand::apply`（`commands.cpp:447`） | 校验序列/轨道/`TrackType::Adjustment`/strength；快照旧值；`adjustmentEffects=effects_`、`adjustmentLutPath=lutPath_`、`adjustmentLutStrength=lutStrength_` |
| ⑤ | 渲染/导出/查询 | `hasLut = !track.adjustmentLutPath.empty()`（`capi.cpp:2828`、`export_executor.cpp:229`）→ 空串 = 无 LUT |
| ⑥ | `SetAdjustmentCommand::revert`（`commands.cpp:474`） | 从快照恢复 **effects + lutPath + lutStrength** 三者 → undo 可完整还原 |
| ⑦ | `nc_adjustment_get`（`capi.h:1226`） | 可读 `out_effect_count` / `out_has_lut` / `out_lut_strength` / `out_lut_path` |

**三条必须写明的语义**

| 语义 | 事实 |
|---|---|
| 空路径是否清除 LUT | **是**（①→⑤ 七步链已闭合） |
| **strength 是否保留** | **否**。`setAdjustment` 会写入新传入的 `strength`（JNI 默认 1.0）→ 清除 LUT 时 strength 被**覆盖**，不保留旧值 |
| **effects 是否保留** | JNI 传的是 `track->adjustmentEffects` 当前值 → 效果栈内容不变；但它是**整栈替换**，测试须断言未变 |

### 2.7 待核实项汇总

| ID | 内容 | 需要的证据 | 阻塞条件 |
|---|---|---|---|
| V-01 | `FindKey` 子串搜索是否可能命中字符串值内部键名 | B2-3 构造用例实测 | 若命中 → 暂停 B2-3，单独报告 |
| V-02 | 真机 JNI Modified UTF-8 对 `\u0000` 与 4 字节字符的实际行为 | B2-4 真机测试 | 若异常 → 记录为独立缺陷，不在 B2 修 |

---

## 3. 数据结构设计

### 3.1 参数来源规格（R1 核心）

> **设计目标**：每个参数来源拥有**独立的**键名、类型、单位、转换规则、默认值、回落条件。
> 不得出现"结构体表达不了、只能靠正文补充解释"的字段。

```cpp
/// 最终传给 C ABI 的类型
enum class ArgKind { Int32, Int64, Double, String };   // String = JStrField 来源

/// 来源键的输入单位
enum class ArgUnit { None, Microseconds, Seconds };

/// 单位转换方式（从 ArgUnit 换算到 ArgKind）
enum class UnitConversion {
    None,
    SecondsToMicrosTruncTowardZero,   // static_cast<int64_t>(seconds * 1e6)
};

/// 读到值后的回落条件
enum class FallbackCondition {
    Never,              // 不回落到下一来源
    Missing,            // 仅"键缺失"时回落（G5/G6 的 prop → property 用）
    MissingOrNegative,  // 缺失或值 < 0 时回落（G5/G6 的 at_us → t 用）
};

/// 类型化默认值：kind 必须与所属 ArgSourceSpec.kind 一致
struct ArgDefault {
    bool present = false;
    ArgKind kind = ArgKind::Double;
    std::int64_t i64 = 0;      // 仅 Int32 / Int64
    double f64 = 0.0;          // 仅 Double
    const char* str = nullptr; // 仅 String（指向字符串字面量）

    static ArgDefault none();
    static ArgDefault int32(std::int32_t v);
    static ArgDefault int64(std::int64_t v);
    static ArgDefault real(double v);
    static ArgDefault emptyString();   // str = ""（静态字面量）
};

/// **单个参数来源**的完整规格
struct ArgSourceSpec {
    const char* key;                 // nullptr = 该来源不存在
    ArgKind kind;                    // 最终类型
    ArgUnit unit;                    // key 的输入单位
    UnitConversion conversion;       // 单位换算方式
    ArgDefault defaultValue;         // key 缺失/不可解析时的默认
    FallbackCondition fallback;      // 读到值后是否回落
};

/// 一个语义参数的三个来源
struct OpArgSpec {
    const char* semanticName;   // 文档/日志用语义名，如 "time"
    ArgSourceSpec canonical;    // 规范名 op 使用
    ArgSourceSpec legacy;       // 短名 op 首选
    ArgSourceSpec alternate;    // 短名 op 回落（可无）
};
```

**表达力校验**（对照 §2.4 的 keyframe 时间参数）：

| 需求 | 由哪个字段表达 |
|---|---|
| `at_us` 单位微秒 | `legacy.unit = Microseconds` |
| `t` 单位秒 | `alternate.unit = Seconds` |
| `t` 需 ×1e6 并截断 | `alternate.conversion = SecondsToMicrosTruncTowardZero` |
| `at_us` 默认 -1 | `legacy.defaultValue = int64(-1)` |
| `t` 默认 0.0 秒 | `alternate.defaultValue = real(0.0)` |
| `at_us < 0` 才回落 | `legacy.fallback = MissingOrNegative` |
| 规范名只读 `time_us` | `canonical.key = "time_us"`，且 `canonical.fallback = Never` |
| 短名不读 `time_us` | `legacy.key = "at_us"`（`legacy` 无 `time_us`） |

> 上表 8 项**全部**由结构体字段直接表达，**无一项依赖正文补充解释**。

### 3.2 九组规格表（表 T-04）

> 与表 T-01 逐格对应。`—` 表示该来源不存在（`key = nullptr`）。

| 组 | 语义参数 | canonical（key/kind/unit/conv/default/fallback） | legacy（同 6 项） | alternate（同 6 项） |
|---|---|---|---|---|
| G1 | opacity | `opacity` / Double / None / None / **1.0** / Never | `value` / Double / None / None / **1.0** / Never | — |
| G2 | name | `name` / **String** / None / None / **""** / Never | `name` / 同上 | — |
| G2 | type | `type` / Int32 / None / None / **0** / Never | `type` / 同上 | — |
| G3 | mode | `mode` / Int32 / None / None / **0** / Never | `mode` / 同上 | — |
| G4 | type | `type` / Int32 / None / None / **0** / Never | `type` / 同上 | — |
| G4 | value | `value` / Double / None / None / **0.5** / Never | `value` / 同上 | — |
| G5 | property | `property` / Int32 / None / None / **0** / Never | `prop` / Int32 / None / None / **0** / **Missing** | `property` / Int32 / None / None / **0** / Never |
| G5 | time | `time_us` / Int64 / Microseconds / None / **0** / Never | `at_us` / Int64 / Microseconds / None / **-1** / **MissingOrNegative** | `t` / Int64 / **Seconds** / **SecondsToMicrosTruncTowardZero** / **0.0** / Never |
| G5 | value | `value` / Double / None / None / **0.0** / Never | `v` / Double / None / None / **1.0** / Never | — |
| G6 | property | 同 G5 property | 同 G5 property | 同 G5 property |
| G6 | time | 同 G5 time | 同 G5 time | 同 G5 time |
| G7 | track_index | `track_index` / Int32 / None / None / **0** / Never | 同左 | — |
| G7 | type | `type` / Int32 / None / None / **0** / Never | 同左 | — |
| G7 | value | `value` / Double / None / None / **1.0** / Never | 同左 | — |
| G8 | track_index | `track_index` / Int32 / None / None / **0** / Never | 同左 | — |
| G8 | effect_id | `effect_id` / Int64→uint64 / None / None / **0** / Never | 同左 | — |
| G9 | track_index | `track_index` / Int32 / None / None / **0** / Never | 同左 | — |
| G9 | path | `path` / **String** / None / None / **""** / Never | 同左 | — |
| G9 | strength | `strength` / Double / None / None / **1.0** / Never | 同左 | — |

**关于 G5 `property` 的特殊说明**：源码短名链为 `JInt("prop", JInt("property",0))`，
即 `prop` 缺失时回落到 `property`。这是**缺失回落**而非**负值回落**，
因此：

- `legacy.key = "prop"`、`legacy.fallback = Never`
- `alternate.key = "property"`、`alternate.fallback = Never`

而源码的实际行为是"**`prop` 缺失才读 `property`**"（不回落到负值）。
因此该参数使用 `legacy.fallback = Missing`（定义见 §3.1），
与 G5/G6 时间参数的 `MissingOrNegative` 是**两种不同语义**：
前者仅缺失回落，后者缺失或负值回落。

> V1.3 只有 `MissingOrNegative`，无法区分二者；V1.4 在 §3.1 增加 `Missing` 以精确表达。

### 3.3 A 层：规格表静态类型安全（**B2 必做**）

> 本节**只**约束编译期/构造期，不涉及运行时行为（运行时见 §4.4 / §5.2）。

| 静态约束 | 落实方式 |
|---|---|
| 整型默认值用整型存储 | `ArgDefault::int32/int64` 只写 `i64`，`f64` 保持 0 |
| 浮点默认值用浮点存储 | `ArgDefault::real` 只写 `f64`，`i64` 保持 0 |
| 默认值 kind == 来源 kind | 规格表构造期 `assert`；单测遍历全部来源断言 |
| String 默认值 | 必须 `str != nullptr`；`i64`/`f64` 保持 0；仅允许指向字符串字面量 |
| 禁止隐式窄化 | 取值只用 `int32Value()` / `int64Value()` / `doubleValue()`，内部 `assert(kind==…)` |
| 不可变 | 规格数组 `const`，无运行时写入 |
| 单位与转换一致 | `unit == Seconds` 必须配 `conversion == SecondsToMicrosTruncTowardZero`；`unit == None/Microseconds` 必须配 `conversion == None`（单测断言） |

### 3.4 参数读取算法（权威定义）

```
Value resolveArg(const OpArgSpec& arg, const Args& a, bool viaAlias)
{
    if (!viaAlias) {
        return readSource(arg.canonical, a);        // 规范名：只读 canonical，不回落
    }
    Value v = readSource(arg.legacy, a);
    if (shouldFallback(arg.legacy.fallback, v)) {
        v = readSource(arg.alternate, a);           // 回落来源自带 unit/conversion/default
    }
    return v;
}

bool shouldFallback(FallbackCondition c, Value v) {
    switch (c) {
        case Never:             return false;
        case Missing:           return v.wasMissing;      // 仅"键缺失"
        case MissingOrNegative: return v.wasMissing || v.asInt64() < 0;
    }
}

Value readSource(const ArgSourceSpec& s, const Args& a) {
    if (s.key == nullptr) return Missing;
    if (!hasKey(a, s.key)) return s.defaultValue;         // 缺失 → 默认
    // 类型处理：B2-2/B2-3 沿用旧语义（不可解析 → 默认）；
    //           B2-3b 若获批则改为显式拒绝（§5.2）
    Value raw = parse(s.key, a);                          // 旧：strtod / JStrField
    if (raw.unparsable) return s.defaultValue;
    return applyConversion(raw, s.unit, s.conversion, s.kind);
}
```

**四条硬约束**

| # | 约束 |
|---|---|
| A-1 | `nc_dispatch` 中**不得**出现 `if (op == "keyframe")` 之类特例分支；全部由规格驱动 |
| A-2 | 不得为 `fallback == Never` 且 `alternate.key == nullptr` 的参数新增回落（E-08） |
| A-3 | 规范名**只读** `canonical`，**不得**回落 `legacy` / `alternate`（E-04） |
| A-4 | 短名读取顺序固定为 `legacy → (条件满足时) alternate`，不得插入第三个键（E-03） |

### 3.5 `OpCallContext` 与 spec 生命周期（表 T-06）

```cpp
struct OpCallContext {
    std::string rawOp;        // 原始 op，不可丢失
    std::string canonicalOp;
    std::string commandType;
    bool viaAlias = false;
    const OpAliasSpec* spec = nullptr;   // 短名与规范名都非空；仅未知 op 为空
};
enum class ResolveStatus { Known, UnknownOp };
struct OpResolveResult { ResolveStatus status; OpCallContext ctx; };
OpResolveResult resolveOp(const std::string& rawOp);
```

| 生命周期要求 | 内容 |
|---|---|
| 存储期 | 9 条 `OpAliasSpec` 为**函数内 `static const` 数组**或文件级 `constexpr` 数组 → **静态存储期** |
| 不可变 | 全部字段 `const`，运行时不得写入 |
| 禁止临时 | 返回指针**只能**指向该静态数组元素；不得指向局部变量、`std::string`、临时构造对象 |
| 同组同址 | 短名与规范名返回**同一元素地址** |
| 键字符串 | `key` 指向**字符串字面量**（静态存储期），不得指向 `std::string::c_str()` |
| 调用方约束 | `nc_dispatch` 仅在单次调用内使用，**不得缓存**跨调用 |
| 并发 | 静态不可变 → **多线程只读安全**；`OpCallContext` 本身为每次调用的栈上值，不共享 |

### 3.6 响应载荷（表 T-05）

```cpp
enum class OkShape { EmptyObject, EmptyString, IdObject };
struct OkSpec { OkShape shape = OkShape::EmptyObject; const char* idKey = nullptr; };
```

| 组 | canonical OkSpec | legacy OkSpec |
|---|---|---|
| G1 opacity | `EmptyObject` | `EmptyString` |
| G2 add_track | `IdObject{"track_id"}` | `IdObject{"track_id"}` |
| G3 blend | `EmptyObject` | `EmptyString` |
| G4 effect_add | `IdObject{"value"}` | `IdObject{"value"}` |
| G5 keyframe | `EmptyObject` | `EmptyString` |
| G6 remove_keyframe | `EmptyString` | `EmptyString` |
| G7 adjust_effect_add | `IdObject{"value"}` | **`IdObject{"effect_id"}`** |
| G8 adjust_effect_remove | `EmptyString` | `EmptyString` |
| G9 adjust_lut | `EmptyString` | `EmptyString` |

失败路径统一 `Err(...)`；**错误码不因入口不同而改变**。

### 3.7 `rawOp` 暴露边界（E-12）

| 层 | 含 `rawOp` | 形式 |
|---|---|---|
| 内部 `OpCallContext` | ✅ 必填字段 | `rawOp` |
| 日志 / 诊断 | ✅ 必须有 | 错误消息前缀 `[op=<rawOp>]`；诊断回调携带 `rawOp` / `canonicalOp` / `viaAlias` |
| **前端 JSON 响应** | ❌ **禁止** | `{ok, code, message, result}` 结构**不变**，不新增任何字段 |

---

## 4. 数值安全

### 4.1 int64 / double 边界（R3 / R4 依据）

**事实基础**

| 事实 | 数值 |
|---|---|
| `2^63` | `9223372036854775808`，double 可**精确表示** |
| `-2^63` | `-9223372036854775808`，double 可**精确表示** |
| `(double)INT64_MAX` | `9223372036854775808.0`（= `2^63`，**向上舍入**） |
| 安全区间 | `-2^63 <= x < 2^63`（**左闭右开**） |

**为何 `abs(x) <= INT64_MAX` 不成立**

`(double)INT64_MAX == 2^63`，因此 `x <= (double)INT64_MAX` 会**放行 `x == 2^63`**，
而 `static_cast<int64_t>(2^63)` 是 **UB**。

**微秒换算的正确上界（本轮实测）**

| 项 | 值 |
|---|---|
| 理论上界 `2^63 / 1e6` | `9223372036854.775391` 秒 |
| **最后一个可用 double** | **`9223372036854.773`** → `us = 9.223372036854774e18 < 2^63` ✅ |
| 下一个 double | `9223372036854.775` → `us = 9.223372036854776e18 == 2^63` ❌ 越界 |
| **最后一个可用负 double** | **`-9223372036854.775`** → `us == -2^63` ✅（`-2^63` 合法） |
| 下一个负 double | `-9223372036854.777` → `us < -2^63` ❌ 越界 |

> **注意负侧不对称**：`-2^63` 恰好可用（等于 `INT64_MIN`），`+2^63` 不可用。

**`2^31` 秒不是溢出案例（R3 更正）**

`2^31 s × 1e6 = 2,147,483,648,000,000 µs = 2.147e15`，远小于 `2^63 ≈ 9.22e18`。
**V1.3 将其列为溢出案例是错误，V1.4 已删除。**

### 4.2 微秒换算五步检查（表 T-07）

```
输入：seconds（double）
1) 有限性：     std::isfinite(seconds) 为假 → 拒绝
2) 换算：       us = seconds * 1e6；再查 std::isfinite(us)（乘法可能溢出为 ±Inf）
3) 可表示范围： -9223372036854775808.0 <= us < 9223372036854775808.0
4) 向零截断：   result = static_cast<std::int64_t>(us)
5) 后置断言：   result ∈ [INT64_MIN, INT64_MAX]（防御性，理论上恒真）
```

> **归属**：以上 5 步是**运行时防护**，属 **B2-3b**（§5.2）。B2-2/B2-3 **不启用**。

### 4.3 边界测试表（表 T-08，R4 依据）

> 全部数值为**本轮实测的 IEEE-754 double 实际值**，不是十进制近似。
> 测试须用 `std::nextafter` 构造相邻可表示值，不得只写字面量。

| # | 输入 `seconds` | double 实际值 | `us = s×1e6` 实际值 | 期望 |
|---|---|---|---|---|
| B-01 | `0.0` | `0.0` | `0.0` | `0` |
| B-02 | `0.5` | `0.5` | `500000.0` | `500000` |
| B-03 | `-0.5` | `-0.5` | `-500000.0` | `-500000` |
| B-04 | `1.0/3.0` | `0.333…` | `333333.33…` | `333333`（截断） |
| B-05 | `1.9999999` | 同 | `1999999.9` | `1999999`（**与四舍五入可区分**） |
| B-06 | `-1.9999999` | 同 | `-1999999.9` | `-1999999`（向零） |
| B-07 | `0.0000019` | 同 | `1.9` | `1`（**可区分**） |
| B-08 | `-0.0000019` | 同 | `-1.9` | `-1`（向零） |
| B-09 | **`9223372036854.773`** | 同 | `9.223372036854774e18` | **合法**，`9223372036854773760` |
| B-10 | `nextafter(9223372036854.773, +inf)` = **`9223372036854.775`** | 同 | `9.223372036854776e18`（== `2^63`） | **拒绝**（B2-3b） |
| B-11 | **`-9223372036854.775`** | 同 | `-9.223372036854776e18`（== `-2^63`） | **合法**，`INT64_MIN` |
| B-12 | `nextafter(-9223372036854.775, -inf)` = **`-9223372036854.777`** | 同 | `-9.223372036854778e18` | **拒绝**（B2-3b） |
| B-13 | `NaN` | `nan` | `nan` | **拒绝**（B2-3b） |
| B-14 | `+Inf` | `inf` | `inf` | **拒绝**（B2-3b） |
| B-15 | `-Inf` | `-inf` | `-inf` | **拒绝**（B2-3b） |
| B-16 | `1e308` | `1e308` | `inf`（乘法溢出） | **拒绝**（B2-3b，第 2 步） |
| B-17 | `2^31 = 2147483648.0` | 同 | `2.147483648e15` | **合法**（**不是溢出**，R3） |

**B2-2/B2-3 的适用范围**：B-01 ~ B-09、B-11、B-17 属**合法输入**，必须与新实现一致；
B-10、B-12 ~ B-16 属 **UB / 非法输入**，仅 B2-3b 处理。

### 4.4 运行时防护归属（E-10）

| 项 | 归属 | 默认状态 |
|---|---|---|
| 静态类型安全（§3.3） | B2-1 | **必做** |
| 越界 / NaN / Inf / 非法类型拒绝（§4.2） | **B2-3b** | **不实施** |
| 旧实现对上述输入的行为 | 保持 | 含 UB（§5.2.2） |

---

## 5. 兼容性与行为变更

### 5.1 合法输入兼容性（E-09）

**合法输入定义**：`JNum`/`JInt`/`JStrField` 能给出**明确定义**结果的输入 ——
含键缺失、`null`、非数值文本（回落默认值）、以及所有在 `int64` 范围内可表示的数值。

**要求**：对 9 组 × 2 名字的全部合法输入，新旧实现**逐字节一致**：
① 传给 C ABI 的参数值 ② `rc` ③ 成功载荷 ④ 失败消息。

**必须保留的数值语义（R5）**

| 输入文本 | `strtod` 得 double | 最终 int64 | 新实现必须一致 |
|---|---|---|---|
| `9007199254740992` | `9007199254740992.0` | `9007199254740992` | ✅ |
| `9007199254740993` | **`9007199254740992.0`**（精度损失） | `9007199254740992` | ✅ **不得**改成 `9007199254740993` |
| `1234567890123456789` | `1234567890123456768.0` | `1234567890123456768` | ✅ |

> **禁止**改用 `strtoll`/整数解析 —— 那会把 `9007199254740993` 解析成 `9007199254740993`，
> 破坏 E-09。

**证明方式**：B2-2/B2-3 内含「旧逻辑 oracle vs 新 dispatch」差分测试；
oracle **只存在于测试代码**，B2-4 随旧分支删除一并移除（E-11 的反面：oracle 不得进生产）。

### 5.2 非法输入（**B 层，默认不实施**）

#### 5.2.1 范围

| 类别 | 具体输入 | 归属 |
|---|---|---|
| 越界整数 | Int32 / Int64 超范围 | B2-3b |
| 非有限浮点 | `NaN` / `+Inf` / `-Inf` | B2-3b |
| 非法 JSON 类型 | 字符串 / 对象 / 数组 / 布尔给数值键 | B2-3b |
| 微秒换算溢出 | §4.2 步骤 1–3 拒绝者 | B2-3b |

#### 5.2.2 旧实现中属 **UB** 的输入（不得作为兼容基准）

| 输入 | 旧行为 | 处置 |
|---|---|---|
| 超 `int64` 的有限 double（正/负） | `static_cast<int64_t>` **UB** | 列入非法输入专项测试；**不要求**新旧一致 |
| `NaN` | `int64(NaN)` **UB** | 同上 |
| `+Inf` / `-Inf` | `int64(±Inf)` **UB** | 同上 |

> **原则**：UB 不是"行为"，不能当契约；也不得因"旧实现碰巧返回某值"就写成兼容要求。

#### 5.2.3 启用前置条件（缺一不可）

1. 产出「真实前端调用样本清单」：扫描 `l3-panel.js` / `editor.js` / `nc-api.js` /
   `engine_static.js` 对 18 个名字的**全部**调用点，逐参数记录字面量类型；
2. 证明现存调用**全部为合法类型**（否则停下报告，不放宽规则）；
3. 专项回归覆盖 §4.3 的 B-10/B-12~B-16 与 §5.2.1 全部形态；
4. **独立 PR + 单独人工批准**；B2-3 获批**不自动**包含 B2-3b。

### 5.3 两层分离（E-10）

| 层 | 内容 | 批次 | 是否改变运行时行为 |
|---|---|---|---|
| **A 层** | 规格表静态类型安全 | B2-1 | **否**（纯新增模块，未接线） |
| **B 层** | 运行时非法输入拒绝 | **B2-3b** | **是**（独立变更，默认不实施） |

> A 层完成**不构成**启用 B 层的理由。

---

## 6. 测试设计

### 6.1 三层结构与覆盖边界

| 层 | 文件 | 覆盖 | 执行环境 |
|---|---|---|---|
| ① 规格与解析 | `tests/test_op_alias.cpp` | 规格表、解析、生命周期、默认值、读取算法 | Ubuntu CI（阻断） |
| ② 分发 + **真实 C ABI** | `tests/test_nc_dispatch_compat.cpp` | 全链路、差分 oracle、动态 ID、LUT、失败路径 | Ubuntu CI（阻断） |
| ③ JNI 编组 | 无单测 | 字符串编组、异常路径、编码 | Android 真机（人工验收） |

> ① ② 通过**不能**替代 ③；③ 通过**不能**替代 ①②。三类证据不得混同（见 §8）。

### 6.2 层 ① 用例清单（表 T-09，`test_op_alias.cpp`）

| ID | 用例 | 断言 |
|---|---|---|
| T01-01 | `SpecNonNullForBothNames` | 9 组 × 2 名字 = 18 次解析，`spec != nullptr` |
| T01-02 | `PairSharesAddress` | 短名与规范名 `spec` 指针**相等** |
| T01-03 | `SpecAddressStableAcrossCalls` | 连续 3 次解析同组，地址不变 |
| T01-04 | `SpecAddressStableAfterOtherCalls` | 解析其他组后再解析本组，地址不变 |
| T01-05 | `KeyStringsAreStatic` | `key` 指针跨调用不变 |
| T01-06 | `UnknownOpHasNoSpec` | `resolveOp("nope")` → `UnknownOp`、`spec==nullptr` |
| T01-07 | `ViaAliasFlag` | 短名 `true`；规范名 `false` |
| T01-08 | `CommandTypeUnique` | 9 条 `commandType` 两两不同 |
| T01-09 | `DefaultKindMatchesSourceKind` | 遍历全部来源，`default.kind == source.kind` |
| T01-10 | `UnitConversionConsistency` | `unit==Seconds` ⇔ `conversion==SecondsToMicrosTruncTowardZero` |
| T01-11 | `NoUnexpectedFallback` | 除 G5/G6 的 property/time 外，其余来源 `fallback==Never && alternate.key==nullptr` |
| T01-12 | `LegacyTimeChain` | `at_us=1000`→1000；只传 `t=0.5`→500000；都不传→0；`at_us=-5`+`t=0.25`→250000 |
| T01-13 | `CanonicalTimeChain` | `time_us=1000`→1000；**只传 `t=0.5`→0**；**只传 `at_us=1000`→0** |
| T01-14 | `LegacyPropertyChain` | 只传 `property=3`→3；只传 `prop=2`→2；都传 `prop=2,property=3`→**2** |
| T01-15 | `CanonicalPropertyChain` | 只传 `prop=2`→**0**（规范链不读 `prop`） |
| T01-16 | `AltDefaultApplied` | `at_us` 缺失 → 触发回落 → `t` 缺失 → `0` |
| T01-17 | `NoCrossKeyFallback` | 短名传 `time_us` 被忽略；规范名传 `at_us` 被忽略 |

### 6.3 层 ② 用例清单（`test_nc_dispatch_compat.cpp`）

#### 6.3.1 链路与隔离

| ID | 用例 | 断言 |
|---|---|---|
| T02-01 | `RealCAbiLinked` | 测试二进制链接 `libnairoclip_core.a`；无任何 `nc_*` mock 符号 |
| T02-02 | `EditorIsolation` | 每个用例独立 `nc_editor_create` → 操作 → `nc_editor_destroy` |
| T02-03 | `EditorReleasedOnSuccess` | 成功路径释放（RAII `EditorGuard`） |
| T02-04 | `EditorReleasedOnFailure` | 失败路径同样释放（ASAN 覆盖泄漏） |
| T02-05 | `NoCrossCaseStatePollution` | 相邻用例间 `nc_track_count` / `nc_contract_revision` 从基线重新开始 |

#### 6.3.2 差分与载荷

| ID | 用例 | 断言 |
|---|---|---|
| T02-06 | `DiffG1`~`DiffG9`（9 个） | 旧逻辑 oracle vs 新 dispatch：参数值、`rc`、载荷**逐字节一致** |
| T02-07 | `LegacyPayloadShape` | G1/G3/G5 短名 `""`；G2/G4/G7 `IdObject`；G6/G8/G9 `""` |
| T02-08 | `CanonicalPayloadShape` | G1/G3/G5 `"{}"`；G2/G4/G7 `IdObject`；G6/G8/G9 `""` |
| T02-09 | `G7PayloadKeyDiffers` | 短名键 `effect_id`，规范名键 `value` |
| T02-10 | `FailureCodesUnchanged` | 失败路径 `rc` 与旧实现相同；响应为 `Err` 结构 |

#### 6.3.3 动态 ID 读回验证（R6 核心）

| ID | 用例 | 断言 |
|---|---|---|
| T02-11 | `AddTrackIdReadback` | `nc_add_track` 返回 `id>0`；`nc_track_get` 能读到该 `id`；`nc_track_count` **+1** |
| T02-12 | `AddEffectIdReadback` | `nc_add_effect` 返回 `eid`；`nc_effect_get(count-1, &out_id, &type, &value, …)` 的 `out_id==eid` 且 `type/value` 与入参一致 |
| T02-13 | `AdjustEffectIdReadback` | `nc_adjustment_add_effect` 返回 `eid`；`nc_adjustment_effect_get(count-1, &out_id, …)` 的 `out_id==eid` |
| T02-14 | `IdConsumedOnFailure` | 构造失败调用 → 返回 0；**下一次成功调用的 ID 跳过失败消耗的那个**（证明 ID 已消耗，不能用 ID>0 判成功） |

> **为什么必须读回**：`ids.next()` 在操作**之前**执行（§2.5），失败也消耗 ID。
> 因此"ID > 0"或"ID 连续"都不能证明成功；只有读回并匹配 `type/value` 才能证明
> 该 ID 指向**本次调用真实创建的对象**。

#### 6.3.4 LUT 清除（R7 核心，表 T-10）

| ID | 步骤 | 断言 |
|---|---|---|
| T02-15 | 建立非空 LUT | `nc_adjustment_set_lut(ti, "/tmp/a.cube", 0.8)` → `NC_OK`；`nc_adjustment_get` 的 `out_has_lut==1`、`out_lut_path=="/tmp/a.cube"`、`out_lut_strength==0.8` |
| T02-16 | 空路径清除 | `adjust_lut` 传 `path=""`（JNI 层转 `nullptr`）→ `NC_OK`；`out_has_lut==0`、`out_lut_path==""` |
| T02-17 | strength 被覆盖 | 上一步未传 `strength` → 默认 **1.0** → `out_lut_strength==1.0`（**不保留旧值 0.8**） |
| T02-18 | effects 未被清除 | 清除前后 `out_effect_count` 相同；逐条 `nc_adjustment_effect_get` 的 `id/type/value` 不变 |
| T02-19 | 清除后重新设置 | 再设 `"/tmp/b.cube", 0.5` → `out_has_lut==1`、path/strength 正确 |
| T02-20 | undo 恢复 LUT | `nc_undo` → `out_has_lut==1`、path 回到上一步值、strength 回到上一步值 |
| T02-21 | redo 再清除 | `nc_redo` → `out_has_lut==0` |
| T02-22 | C ABI 直接传 nullptr | 直接调 `nc_adjustment_set_lut(ed, ti, nullptr, 1.0)` → 与空串同效 |

> **A/B 拆分（必须遵守）**：
> - T02-15~T02-21 是**平台无关层**验证（`nc_dispatch` + 真实 C ABI，覆盖 JSON 空串 → 内部 `nullptr` 的转换）；
> - T02-22 是**纯 C ABI 层**验证（直接传 `nullptr`）。
> - JNI 层的 `jstring → std::string → 空串判定` 属层 ③（§6.4 表 T-11），
>   **不得**用 ② 的结果宣称 ③ 已通过。

### 6.4 层 ③ 前置：JNI 真机测试清单（表 T-11）

| ID | 检查项 | 说明 |
|---|---|---|
| T03-01 | `GetStringUTFChars` / `ReleaseStringUTFChars` 配对 | 每条路径（含早退）都释放 |
| T03-02 | **异常路径释放** | C ABI 返回失败时同样释放，不得 `return` 漏掉 |
| T03-03 | 空字符串 | `name=""`（G2）、`path=""`（G9） |
| T03-04 | 中文字符串 | `name="中文轨道"`、`path` 含中文 |
| T03-05 | 4 字节 UTF-8（emoji） | 如 `name="轨道🎬"` |
| T03-06 | `\u0000` 字符 | Modified UTF-8 下编码为 `C0 80`；观察是否被截断 |
| T03-07 | `NewStringUTF` 返回失败 | 返回 `nullptr` 时不泄漏已分配 `std::string` |
| T03-08 | 连续 1000 次调用 | 无崩溃、无句柄泄漏；`dumpsys meminfo` 采样无持续增长 |

**编码警告（必须遵守）**

> JNI 使用 **Modified UTF-8**（`U+0000` 编码为两字节 `C0 80`；补充平面字符以代理对编码），
> 与**标准 UTF-8 不等价**。
> 因此「中文字符串测试通过」**不等于** JNI 编码正确性验证。
> 必须额外覆盖 T03-05 / T03-06。
> 若这些用例异常，记录为**独立缺陷**（不在 B2 内修）。

### 6.5 明确不覆盖（不得声称已测）

| 部分 | 状态 | 由谁覆盖 |
|---|---|---|
| `nc_jni.cpp` 的 JNI 编组 | ❌ 无单测 | Cross-Compile + APK 构建 + 表 T-11 真机 |
| JS 运行时执行 | ❌ 不执行 | 只取样本字符串 |
| GPU 相关路径 | ❌ 不在 B2 | — |

---

## 7. 批次计划

### 7.1 依赖图

```
B2-0（基线核验 + 本文档批准）
  └─→ B2-1（core/op_alias 纯新增，不接线）
        └─→ B2-2（4 组等价收敛 + 抽出 nc_dispatch）
              └─→ B2-3（5 组差异收敛 + 差分验证）
                    ├─→ B2-3b（非法输入收紧，独立 PR，默认不实施）
                    └─→ B2-4（守卫 + 全量回归 + 真机 + 文档）
```

**规则**：任一箭头未通过验收，不得进入下游；B2-3b 与 B2-4 **无先后依赖**（可并行），
但 B2-4 若包含 B2-3b 的验收项，须等其完成或明确标注"未执行"。

### 7.2 各批详表（每批 14 字段）

#### B2-0 基线核验

| 字段 | 内容 |
|---|---|
| 1 目标 | 确认执行基线、文档批准、环境可用 |
| 2 修改范围 | **无代码改动** |
| 3 禁止 | 不得在此批开始任何代码修改 |
| 4 前置依赖 | 本文档 V1.4 获人工批准 |
| 5 允许修改文件 | 无 |
| 6 生产代码改动 | 无 |
| 7 测试范围 | 无 |
| 8 验收命令 | `git rev-parse HEAD`；`git status --porcelain` |
| 9 验收证据 | 当前 HEAD 完整 SHA + 工作区干净输出 |
| 10 失败判定 | HEAD 与文档审计基准不一致且未记录差异 → 失败 |
| 11 回滚 | 无需 |
| 12 独立 PR | 否（无代码） |
| 13 人工审批 | 是 |
| 14 能否进下一批 | 仅当 8/9 齐备且审批通过 |

#### B2-1 `core/op_alias`（纯新增）

| 字段 | 内容 |
|---|---|
| 1 目标 | 建立 9 组规格表 + 解析 + 读取算法（**不接线**） |
| 2 修改范围 | 新增 `core/include/nairoclip/core/op_alias.h`、`core/src/op_alias.cpp`；`core/CMakeLists.txt` 注册；新增 `tests/test_op_alias.cpp` |
| 3 禁止 | 不得修改 `nc_jni.cpp`；不得修改任何既有 op 分支；不得启用 B 层 |
| 4 前置依赖 | B2-0 |
| 5 允许修改文件 | 上述 4 个（3 新增 + 1 注册） |
| 6 生产代码改动 | 仅新增模块，**运行时行为 0 变化**（无调用点） |
| 7 测试范围 | 层 ①（表 T-09，T01-01~T01-17） |
| 8 验收命令 | `cmake --build build && ./build/tests/nairoclip_tests` |
| 9 验收证据 | 测试报告（T01 全通过）+ 新增文件清单 |
| 10 失败判定 | 任一 T01 用例失败；或发现已接线到 JNI |
| 11 回滚 | 删除新增文件 + 撤销 CMake 注册 |
| 12 独立 PR | **是** |
| 13 人工审批 | **是**（评审规格表 9/9、`FallbackCondition` 三种语义、地址稳定性） |
| 14 能否进下一批 | 仅当 9 齐备且审批通过 |

#### B2-2 4 组等价收敛 + `nc_dispatch`

| 字段 | 内容 |
|---|---|
| 1 目标 | G2/G4/G8/G9 收敛；抽出平台中立 `nc_dispatch`；只接管 9 组 |
| 2 修改范围 | 新增 `mobile/native/android/nc_dispatch.{h,cpp}`；改 `nc_jni.cpp` 的 `DispatchEdit` **入口**；删 4 个短名分支；新增 `tests/test_nc_dispatch_compat.cpp`；`tests/CMakeLists.txt` |
| 3 禁止 | 不得改其余 ~100 个 op 分支；不得改 C ABI；不得改载荷形状 |
| 4 前置依赖 | B2-1 |
| 5 允许修改文件 | `nc_dispatch.h/.cpp`（新）、`nc_jni.cpp`（仅入口 + 删 4 分支）、`test_nc_dispatch_compat.cpp`（新）、`tests/CMakeLists.txt` |
| 6 生产代码改动 | 4 组短名分支删除；`DispatchEdit` 入口增加 `resolveOp` + 分流 |
| 7 测试范围 | T02-01~T02-05（隔离/释放）、T02-06 中 G2/G4/G8/G9、T02-07/08、T02-10、T02-11~T02-13 |
| 8 验收命令 | `./build/tests/nairoclip_tests`；`python3 tools/check_wiring.py .` |
| 9 验收证据 | 测试报告 + **非目标 op 未变抽查 ≥5 个**（`git diff` 逐行审查记录） |
| 10 失败判定 | 非目标 op 任一行为变化；载荷非逐字节一致 |
| 11 回滚 | revert 本批 PR |
| 12 独立 PR | **是** |
| 13 人工审批 | **是**（重点看 `nc_jni.cpp` diff 是否只动了入口与 4 个分支） |
| 14 能否进下一批 | 仅当 9/10 齐备且审批通过 |

#### B2-3 5 组差异收敛

| 字段 | 内容 |
|---|---|
| 1 目标 | G1/G3/G5/G6/G7 收敛（含 keyframe 时间链、两套默认值、载荷键差异） |
| 2 修改范围 | `nc_dispatch.cpp` 补 5 组；`nc_jni.cpp` 删 5 个短名分支；`test_nc_dispatch_compat.cpp` 补用例 |
| 3 禁止 | 不得新增跨键回落（E-08）；不得改舍入方式（E-06）；不得启用 B 层（E-10） |
| 4 前置依赖 | B2-2 |
| 5 允许修改文件 | `nc_dispatch.cpp`、`nc_jni.cpp`（仅删 5 分支）、测试文件 |
| 6 生产代码改动 | 5 组短名分支删除；5 组参数按表 T-04 读取 |
| 7 测试范围 | T02-06 中 G1/G3/G5/G6/G7；T02-09（G7 键差异）；T01-12~T01-17 对应的集成验证；表 T-08 的 B-01~B-09/B-11/B-17 |
| 8 验收命令 | `./build/tests/nairoclip_tests` |
| 9 验收证据 | **五组合法输入差分全部通过**；时间链/默认值/载荷/动态 ID 四类断言记录 |
| 10 失败判定 | 任一差分不一致；任一舍入向量不符（§2.4 表）；出现跨键回落 |
| 11 回滚 | revert 本批 PR |
| 12 独立 PR | **是** |
| 13 人工审批 | **是**（逐组核对表 T-04 与源码） |
| 14 能否进下一批 | 仅当 9/10 齐备且审批通过 |

#### B2-3b 非法输入收紧（**默认不实施**）

| 字段 | 内容 |
|---|---|
| 1 目标 | 运行时拒绝越界 / NaN / Inf / 非法类型 / 微秒溢出 |
| 2 修改范围 | `nc_dispatch.cpp` 的参数读取分支；`test_nc_dispatch_compat.cpp` 增严格模式用例 |
| 3 禁止 | **不得**因 B2-3 获批而自动执行；不得在无样本清单时启用 |
| 4 前置依赖 | B2-3 + §5.2.3 四项前置条件 |
| 5 允许修改文件 | `nc_dispatch.cpp`、测试文件 |
| 6 生产代码改动 | **改变非法输入行为**（合法输入不变） |
| 7 测试范围 | 表 T-08 的 B-10/B-12~B-16；§5.2.1 全部形态 |
| 8 验收命令 | `./build/tests/nairoclip_tests` |
| 9 验收证据 | 真实前端调用样本清单 + 专项回归报告 + 单独批准记录 |
| 10 失败判定 | 任一现存前端调用被判为非法类型（说明样本未清） |
| 11 回滚 | 不合并即回滚 |
| 12 独立 PR | **是（独立于 B2-3）** |
| 13 人工审批 | **是（单独批准）** |
| 14 能否进下一批 | 不阻塞 B2-4；未批准则 B2-4 对应项标"未执行" |

#### B2-4 守卫 + 全量回归 + 真机 + 文档

| 字段 | 内容 |
|---|---|
| 1 目标 | 建立守卫工具、完成全量验收、真机验证、文档收口 |
| 2 修改范围 | 新增 `tools/check_op_alias.py`；`ci.yml` 增加一个检查步骤；更新本文档 B2 状态 |
| 3 禁止 | 不得改 `ci.yml` 的 job 结构；不得删除任何既有检查；不得改 `continue-on-error` 设置 |
| 4 前置依赖 | B2-3（B2-3b 可选） |
| 5 允许修改文件 | `tools/check_op_alias.py`（新）、`.github/workflows/ci.yml`（仅新增 1 step）、本文档 |
| 6 生产代码改动 | 无 |
| 7 测试范围 | 全量回归 + 守卫工具 + Valgrind + APK + 真机（表 T-11） |
| 8 验收命令 | `python3 tools/check_op_alias.py .`；CI 全流程；`adb` 真机手测 |
| 9 验收证据 | 见 §8 矩阵中标注 B2-4 的各项 |
| 10 失败判定 | 守卫工具报错；任一阻断项未过 |
| 11 回滚 | revert 本批 PR |
| 12 独立 PR | **是** |
| 13 人工审批 | **是（最终验收）** |
| 14 能否进下一批 | 不适用（B2 终点） |

### 7.3 守卫工具检查项（`check_op_alias.py`）

| 检查 | 判定 |
|---|---|
| 9 个短名在 `nc_jni.cpp` 中的 `if (op == "...")` 分支数 | 必须为 **0** |
| 每个 `canonicalOp` 的 JNI 分支数 | 必须为 **1** |
| `nc_jni.cpp` 是否调用 `resolveOp(` | 必须存在 |
| 9 条 `commandType` 是否两两不同 | 必须不同 |
| 别名表覆盖组数 | 必须为 **9** |

---

## 8. 唯一验收矩阵（表 T-12）

> 本表是**唯一**验收标准定义处。其他章节只引用，不重述。
> **阻断** = 未通过则禁止合并/禁止进入下一批；**诊断** = 失败须说明但不等同阻断；**人工** = 需人签核。

| # | 验收项 | 批次 | 执行命令/工作流 | 通过标准 | 失败标准 | 阻断性 | 证据 |
|---|---|---|---|---|---|---|---|
| A-01 | `test_op_alias`（表 T-09，17 用例） | B2-1 | `./build/tests/nairoclip_tests` | 17/17 通过 | 任一失败 | **阻断** | 测试报告 |
| A-02 | `test_nc_dispatch_compat`（§6.3，T02-01~T02-22） | B2-2 / B2-3 | 同上 | 全部通过 | 任一失败 | **阻断** | 测试报告 + 三类失败分类计数 |
| A-03 | `check_op_alias.py` | B2-4 | `python3 tools/check_op_alias.py .` | 退出码 0 | 非 0 | **阻断** | CI 日志 |
| A-04 | `build-and-test` 工作流 | 每批 | `ci.yml` | 该 job success | 非 success | **阻断** | CI run URL |
| A-05 | canonical/legacy 差分（T02-06） | B2-2 / B2-3 | 测试二进制 | 参数/`rc`/载荷逐字节一致 | 任一不一致 | **阻断** | 差分结果 |
| A-06 | 默认值测试（T01-12~T01-16） | B2-1 / B2-3 | 测试二进制 | 两套默认值各自生效 | 任一不符 | **阻断** | 测试报告 |
| A-07 | 参数优先级测试（T01-12~T01-15） | B2-1 | 测试二进制 | 按表 T-04 生效 | 任一不符 | **阻断** | 测试报告 |
| A-08 | keyframe 秒/微秒转换（§2.4 区分向量） | B2-3 | 测试二进制 | 4 组区分向量全中 | 任一不符 | **阻断** | 测试报告 |
| A-09 | int64 边界（表 T-08 B-01~B-09/B-11/B-17） | B2-3 | 测试二进制 | 合法值全部正确 | 任一不符 | **阻断** | 测试报告 |
| A-10 | NaN/Inf 拒绝（B-10/B-12~B-16） | **B2-3b** | 测试二进制 | 全部拒绝 | 任一放行 | **阻断（仅 3b）** | 测试报告 |
| A-11 | C ABI 集成（T02-06~T02-10） | B2-2 / B2-3 | 测试二进制 | 全通过 | 任一失败 | **阻断** | 测试报告 |
| A-12 | Editor 隔离与释放（T02-02~T02-05） | B2-2 | 测试二进制 + ASAN | 无泄漏、无污染 | 泄漏/污染 | **阻断** | ASAN 报告 |
| A-13 | 动态 ID 读回（T02-11~T02-14） | B2-2 | 测试二进制 | ID 与对象匹配 | 不匹配 | **阻断** | 测试报告 |
| A-14 | LUT 清除（T02-15~T02-22） | B2-2 | 测试二进制 | 8 项全通过 | 任一失败 | **阻断** | 测试报告 |
| A-15 | undo/redo（T02-20/T02-21） | B2-2 | 测试二进制 | 状态正确还原 | 不还原 | **阻断** | 测试报告 |
| A-16 | **ASAN** | 每批 | `ci.yml` `asan-diagnose` | success | 失败须说明 | **诊断**（`continue-on-error`） | 报告 |
| A-17 | **UBSAN** | 每批 | `ci.yml` `ub-diagnose` | success | 失败须说明 | **诊断**（`continue-on-error`） | 报告 |
| A-18 | **Valgrind Memcheck** | B2-4 | `valgrind.yml` | run success；16 分片 `executed==total`；0 错误 | 任一不符 | **阻断** | 汇总日志 |
| A-19 | **Android APK 构建 + 冒烟** | B2-4 | `android-apk.yml` | `Build APK` + smoke success | 非 success | **阻断** | 构建日志 + 冒烟结果 |
| A-20 | **JNI 真机测试**（表 T-11，8 项） | B2-4 | `adb` 手测 | 8 项逐条通过 | 任一失败 | **人工验收** | 手测记录签核 |
| A-21 | 中文/特殊字符（T03-04~T03-06） | B2-4 | 真机 | 行为符合预期或记录缺陷 | — | **人工验收** | 手测记录 |
| A-22 | 回归 + 文档/脚本一致性 | B2-4 | 全量测试 + `check_mobile_frontend.py` / `check_wiring.py` | 全通过 | 任一失败 | **阻断** | 测试报告 |

**覆盖一致性检查**：本表 A-01~A-22 与正文 §6 用例、§7 批次**一一对应**；
无"正文要求测试但矩阵缺项"，也无"矩阵要求测试但无批次/环境"。

**环境不可执行项**：当前无 GPU 硬件与持续真机接入 → A-20/A-21 标为**人工验收**，
必须在 B2-4 时**实际执行**或明确标注"未执行"，**不得虚构通过**。

---

## 9. 停止点与人工审批

### 9.1 最终停止点表（表 T-13）

| 批次 | 停止条件 | 人工审批要点 | 未通过时 |
|---|---|---|---|
| B2-0 | HEAD 与工作区证据齐备 | 确认基线、确认本文档批准 | 不进入 B2-1 |
| B2-1 | A-01 通过且**未接线** | 规格表 9/9、`FallbackCondition` 三种语义、地址稳定性 | 不进入 B2-2 |
| B2-2 | A-02（G2/G4/G8/G9 部分）+ A-04 + A-05 + A-11~A-15 通过；非目标 op 抽查 ≥5 | `nc_jni.cpp` diff 仅动入口与 4 分支 | 不进入 B2-3 |
| B2-3 | A-05~A-09 + A-11 通过；无新增跨键回落 | 逐组核对表 T-04 与源码 | 不进入 B2-3b / B2-4 |
| B2-3b | **单独批准** + A-10 + 样本清单 | 现存调用是否全合法 | 不合并；B2-4 对应项标"未执行" |
| B2-4 | A-03 + A-18 + A-19 + A-20 + A-21 + A-22 通过 | 四类证据齐全（诊断/内存/构建/真机） | 不宣称 B2 完成 |

### 9.2 通用规则

1. 每批**独立 PR**、**独立停止点**、**独立回滚点**、**独立人工验收**；
2. 任一批失败 → **立即停止**，不自动进入下一批；
3. **不得自动合并**任何 PR；
4. **不得自动创建**下一批的实施任务。

### 9.3 文档批准 ≠ 实施授权

> **V1.4 获得人工技术复审通过，仅代表"文档可以交给工程团队执行"，
> 不构成 B2-1 或任何批次的执行授权。**
> 是否启动 B2-1 必须由项目负责人**单独决定**。

---

## 10. 风险与回滚（表 T-14）

| ID | 风险 | 等级 | 缓解 | 回滚 |
|---|---|---|---|---|
| RB-01 | 删短名分支后旧 JS 调用失败 | 高 | 入口统一归一化；真实样本回归；真机手测 | revert 单批 |
| RB-02 | G5 两套默认值（`v`=1.0 / `value`=0.0）混用 | 高 | 表 T-04 分别声明；T01-12~T01-13 | revert B2-3 |
| RB-03 | 时间换算被改成四舍五入 | 高 | §2.4 四组区分向量 | revert B2-3 |
| RB-04 | 误加跨键回落（如给短名加 `time_us`） | 高 | E-08 + T01-13/T01-17 | revert B2-3 |
| RB-05 | 载荷形状变化破坏前端 | 中 | 表 T-05 + T02-07/08 逐字节 | revert B2-3 |
| RB-06 | `nc_dispatch` 波及非目标 op | 高 | E-13 + 抽查 ≥5 | revert B2-2 |
| RB-07 | 类型收紧破坏现存调用 | 中 | §5.2.3 前置 + 独立 PR + 默认不启用 | 不启用 |
| RB-08 | 别名表与 JNI 分支不一致 | 中 | A-03 守卫 | — |
| RB-09 | `rawOp` 丢失 / 前端 JSON 被改 | 中 | E-12 + 结构体必填 + 单测 | — |
| RB-10 | `spec` 指针悬空 | 高 | §3.5 静态存储 + T01-03~T01-05 | revert B2-1 |
| RB-11 | `JInt` 改整数解析导致大整数行为变化 | 高 | §2.2 / §5.1 禁止 + 2^53+1 用例 | revert B2-3 |
| RB-12 | LUT 清除时 `strength` 被覆盖未被察觉 | 中 | T02-17 显式断言 | — |
| RB-13 | 用"ID > 0"误判成功 | 中 | T02-14 显式断言 ID 消耗 | — |
| RB-14 | 把 ASAN/Valgrind/APK/真机混为一种证据 | 中 | 表 T-12 分类 | — |
| RB-15 | 测试 oracle 泄漏进生产代码 | 中 | §5.1 规定 oracle 仅存测试；B2-4 随旧分支删除 | — |

---

## 11. B1 遗留事项（逐条引用原文，不凭记忆）

### 11.1 来源与原文

**来源 A**：`docs/NC-B1-FINAL-ACCEPTANCE-REPORT.md` §4.3「遗留技术债」（第 328–335 行）

| ID | 原文描述（摘） | 影响 | 归属 |
|---|---|---|---|
| TD-1 | `frame_fit` JNI 分支仍 return `Ok` 而不调 C ABI | 切换适配模式画面不变却显示成功 | 独立缺陷单 `docs/DEFECT-frame_fit-noop.md` |
| TD-2 | 剩余 **71** 个门面 Command 未接入命令信封（只有 3 个走 `nc_apply_command`） | 旧接口无 commandId/expectedRevision | B3 |
| TD-3 | 9 组重复 op 未收敛 | 同一操作两个审计身份 | **B2** |
| TD-4 | ASAN / UBSAN 为 `continue-on-error` | 非阻断 | 需单独授权 |

**来源 B**：`docs/NC-B1-FOLLOWUP-REPORT.md` §8「未完成事项」（第 322–324 行）

| ID | 原文描述（摘） |
|---|---|
| F-1 | GPU Alpha 一致性验证 —— 仓库无 GPU 渲染后端 |
| F-2 | GPU 硬件真机矩阵 —— 无自托管 GPU Runner / 无真机接入 |
| F-3 | Valgrind 硬件相关路径 —— memcheck 只覆盖 CPU 路径 |

### 11.2 当前状态与是否阻断 B2

| ID | 当前状态（本轮实测） | 是否阻断 B2 | 处置 |
|---|---|---|---|
| TD-1 | `nc_jni.cpp` 中 `frame_fit` 分支仍 `return Ok("")` 不调 C ABI（本轮复核确认） | **否**（9 组中不含 `frame_fit`） | 不修；若 B2 期间发现影响 9 组任一 op → 停下申请例外批准 |
| TD-2 | 仅 3 个命令走 `nc_apply_command` | 否（属 B3） | 不处理 |
| TD-3 | 9 组仍各有短名 + 规范名两个分支（本轮复核确认） | — | **B2 处理对象** |
| TD-4 | `ci.yml` 中 `asan-diagnose` / `ub-diagnose` 仍 `continue-on-error: true`（本轮复核） | **否**，但影响证据分级 | 表 T-12 标为「诊断」而非「阻断」 |
| F-1~F-3 | 仓库仍无 GPU 后端 | 否 | 不处理 |

### 11.3 B1 §2.5 事实表述修正（引用，不改 B1 报告）

**B1 报告原文**（§2.5）：`build-and-test` 内含 6 个静态检查步骤…全部为 `success`，**无 `skipped`**。

**本轮实测**（run 42815746913，main @ `28252751`）：该 job 共 **27 步**，其中：

| 步骤类型 | 步号 | 状态 |
|---|---|---|
| **静态检查步骤（6 个）** | 7 C++ 头文件结构 / 8 条件编译一致性 / 9 前端静态 / 10 Valgrind 汇总工具自检 / 11 C ABI 调用点自检 / 12 C ABI 调用点 | **全部 `success`，无跳过** ✅ |
| **缓存命中跳过的下载步骤（1 个）** | 6 `下载 ONNX Runtime` | **`skipped`**（`if: cache-hit != 'true'`，缓存命中，属设计行为） |
| 其余步骤 | 1–5、13–24、47–49 | 全部 `success` |

**准确表述应为**：

> 「6 个静态检查步骤全部 `success` 且无跳过；
> 另有 1 个**非检查**步骤（`下载 ONNX Runtime`）因缓存命中而 `skipped`，属预期行为。」

**处置**：本轮**只允许改本文档**，故：

1. **不修改 B1 报告**；
2. 建议后续单独提交一次 B1 报告勘误；
3. 该表述错误**不影响** B1 实质结论，也**不影响** B2 启动。

### 11.4 B1 与 B2 的关系

| 问题 | 结论 |
|---|---|
| B1「有条件通过」是否满足 B2 启动 | **满足**（B2 不依赖 GPU / Alpha / 真机 / `frame_fit`） |
| 允许带入 B2 的遗留 | TD-1、TD-2、TD-4、F-1~F-3（均与 B2 无交集） |
| 必须先解决的 | **无** |
| 需人工例外批准才能带入 | TD-1（仅当其影响 9 组中任一 op 时） |
| B2 内处理 | TD-3 |

---

## 12. 未解决问题清单（表 T-15）

### 12.1 文档内部问题

| ID | 问题 | 状态 |
|---|---|---|
| D-01 | V1.3 中 `t=2^31` 秒被误列为溢出案例 | ✅ 已修复（§4.1 明确不溢出） |
| D-02 | V1.3 `OpArgSpec` 三来源共用 kind/unit/conversion | ✅ 已重构（§3.1） |
| D-03 | V1.3 `FallbackCondition` 无法区分"缺失回落"与"负值回落" | ✅ 已修复（§3.1 增加 `Missing`） |
| D-04 | V1.3 验收矩阵与正文不完全对应 | ✅ 已统一（表 T-12） |
| D-05 | V1.3 批次表字段不全 | ✅ 已补全（§7.2，每批 14 字段） |
| D-06 | V1.3 存在"适当处理"类模糊表述 | ✅ 已全部替换为可判定规则 |

### 12.2 源码事实待核实

| ID | 内容 | 阻塞条件 |
|---|---|---|
| V-01 | `FindKey` 子串搜索是否命中字符串值内部键名 | B2-3 构造用例实测；若命中则暂停并报告 |
| V-02 | 真机 JNI Modified UTF-8 对 `\u0000` / 4 字节字符行为 | B2-4 真机测试；若异常记为独立缺陷 |

### 12.3 环境无法执行

| ID | 项 | 说明 |
|---|---|---|
| E-01 | 真机 JNI 测试（A-20/A-21） | 当前无持续真机接入；B2-4 时须实际执行或标"未执行" |
| E-02 | GPU 路径验证 | 仓库无 GPU 后端（B1 F-1），与 B2 无关 |

### 12.4 B1 遗留

见 §11（TD-1~TD-4、F-1~F-3）。**均不阻断 B2**。

### 12.5 B2 范围外风险（仅记录，不处理）

| ID | 风险 | 归属 |
|---|---|---|
| X-01 | TD-1 `frame_fit` 假成功 | 独立缺陷单 |
| X-02 | TD-2 71 个 facade op 无信封 | B3 |
| X-03 | TD-4 ASAN/UBSAN 非阻断 | 需单独授权 |
| X-04 | `FindKey` 子串搜索健壮性（V-01 若成立） | 独立缺陷 |
| X-05 | `ids.next()` 在失败时消耗 ID（§2.5） | 既有行为，B2 不改；已在测试中显式断言 |

---

## 13. 主动技术审查结果（任务书九·20 项）

| # | 检查点 | 结论 |
|---|---|---|
| 1 | canonical/legacy/alternate 冲突处理顺序 | ✅ 已定义（§3.4：按 `viaAlias` 选首选来源；`fallback` 决定是否回落） |
| 2 | canonical 显式提供但类型错误时是否回退 legacy | ✅ **不回退**（E-04：规范名只读 `canonical`；类型错误在 B2-2/3 回落默认值，B2-3b 拒绝） |
| 3 | 显式 `null` 与缺失是否等价 | ✅ **等价**（§2.1：`strtod` 解析 `null` 失败 → 默认值） |
| 4 | 空字符串是否等价于缺失 | ✅ **数值键等价**（`JNum` 对 `""` 解析失败 → 默认）；**字符串键不等价**（`JStrField` 返回 `""`，G2 `name` / G9 `path` 的 `""` 是**有效值**，非缺失） |
| 5 | 默认值是否改变合法旧输入语义 | ✅ 不改变（表 T-04 的默认值逐项取自源码） |
| 6 | 类型转换是否存在隐式窄化 | ⚠️ 旧实现存在（`int64→int32`、`double→int64`）；B2 **保留**该行为（E-09）；A 层禁止**规格表**内部隐式窄化（§3.3） |
| 7 | signed/unsigned 转换风险 | ✅ 已识别：G8 `effect_id` 为 `JInt`（int64）→ `static_cast<uint64_t>`（源码 `nc_jni.cpp:1086`）；负值会变成巨大 uint64 → 属**非法输入**，B2-3b 处理 |
| 8 | uint64 ID 与 int64 JSON 中转边界 | ✅ 已识别：`IdObject` 成形用 `std::to_string(uint64)`，不经 JSON 数字；**避免** double 精度问题 |
| 9 | JSON 数字精度是否影响 ID 或时间戳 | ✅ ID 走字符串拼接（不受影响）；时间戳走 `JInt`→double（>2^53 有精度损失，§2.2 已记录并要求保留） |
| 10 | C ABI 返回值内存所有权 | ✅ 9 组均返回标量（`int32_t`/`uint64_t`），**无 `char*` 出参**，无所有权问题 |
| 11 | 错误路径是否留下部分修改 | ⚠️ 已识别：`ids.next()` 失败仍消耗 ID（§2.5）；**工程数据不变**（`commit` 失败不入栈）。B2 不改该行为，测试显式断言 |
| 12 | command 执行失败是否污染项目状态 | ✅ 不污染（`UndoStack::push` 失败不入栈、不改 revision）；`SetAdjustmentCommand::apply` 先校验后写入 |
| 13 | alias 与 canonical 是否真正走相同规范化路径 | ✅ 设计上同一 `resolveOp` → 同一 `OpAliasSpec` → 同一 `dispatchB2`；T01-02 断言同地址 |
| 14 | 参数规范化是否发生在副作用之前 | ✅ 是（`resolveArg` 在调用 `nc_*` 之前完成；`nc_*` 内部才产生副作用） |
| 15 | 测试是否验证实际状态而非仅返回码 | ✅ 表 T-12 A-13/A-14/A-15 要求读回验证（`nc_effect_get` / `nc_adjustment_get` / undo 后状态） |
| 16 | 并发调用是否共享可变参数状态 | ✅ 不共享（`OpCallContext` 为栈上值；规格表 `const` 只读；`nc_editor` 本就单线程约定） |
| 17 | 测试 oracle 与生产实现是否耦合 | ✅ 已隔离：oracle 仅在测试代码；B2-4 随旧分支删除移除（§5.1、RB-15） |
| 18 | 构建/测试/APK/JNI 验收的环境依赖 | ✅ 已标注：A-04/A-16/A-17/A-18/A-19 在 CI；A-20/A-21 需真机（E-01） |
| 19 | CI `continue-on-error` 是否造成假通过 | ✅ 已标注：A-16/A-17 为**诊断**非阻断；run 级结论可能因诊断作业而显示 `cancelled`（B1 已知），因此**不以 run 级结论代替作业级结论** |
| 20 | 测试覆盖范围与 B2 目标是否匹配 | ✅ 表 T-12 与 §6 用例、§7 批次一一对应 |

**审查中新增并已纳入的问题**（不在原 9 项内）：

| 新增 | 内容 | 落点 |
|---|---|---|
| N-01 | `FallbackCondition` 需区分"缺失回落"与"负值回落" | §3.1（增加 `Missing`） |
| N-02 | G5 `property` 的回落是"缺失"而非"负值" | §3.2 特殊说明 |
| N-03 | 失败调用也消耗 ID → 不能用 ID>0 判成功 | §2.5、T02-14、RB-13 |
| N-04 | LUT 清除时 `strength` 被覆盖（不保留旧值） | §2.6、T02-17、RB-12 |
| N-05 | 空字符串对**字符串键**是有效值，不等价缺失 | §13 第 4 项 |
| N-06 | G8 `effect_id` 负值 → 巨大 uint64 | §13 第 7 项 |
| N-07 | `continue-on-error` 导致 run 级 `cancelled`，不得以 run 级代替作业级 | §13 第 19 项 |

---

## 14. 附录

### 14.1 关键常量

| 名称 | 值 |
|---|---|
| `INT64_MAX` | `9223372036854775807` |
| `INT64_MIN` | `-9223372036854775808` |
| `2^63`（double 精确） | `9223372036854775808.0` |
| `-2^63`（double 精确） | `-9223372036854775808.0` |
| `2^53`（double 精确整数上限） | `9007199254740992` |
| 安全转换区间 | `-2^63 <= x < 2^63` |
| 微秒换算最大可用秒 | `9223372036854.773`（正）/ `-9223372036854.775`（负） |

### 14.2 引用文件

| 文件 | 用途 |
|---|---|
| `mobile/native/android/nc_jni.cpp` | 9 组分支、`JNum`/`JInt`/`JStrField`/`FindKey` |
| `core/include/nairoclip/core/capi.h` | C ABI 签名 |
| `core/src/capi.cpp` | C ABI 实现、ID 分配、LUT 查询 |
| `core/src/editor.cpp` | `addTrack` 等门面 |
| `core/src/commands.cpp` | `SetAdjustmentCommand` apply/revert |
| `docs/NC-B1-FINAL-ACCEPTANCE-REPORT.md` | TD-1~TD-4、§2.5 事实 |
| `docs/NC-B1-FOLLOWUP-REPORT.md` | F-1~F-3 |
| `docs/NC-C-COMMAND-AUDIT.md` / `NC-C-COMMAND-IMPLEMENTATION-PLAN.md` | 9 组识别、批次划分依据 |

### 14.3 文档一致性自检

| 检查 | 结果 |
|---|---|
| 章节编号连续（§0–§14） | ✅ |
| 表格编号唯一（T-03~T-17） | ✅ |
| 交叉引用可解析 | ✅ |
| 关键规则单一定义（§0.4 索引） | ✅ |
| 无"适当处理/必要时/尽可能"等模糊表述 | ✅ |
| 正文与表 T-12 一一对应 | ✅ |
| 表 T-01 与表 T-04 逐格对应 | ✅ |
| 未修改其他文件 | ✅（见交付报告 Git 证据） |

---

**文档结束。本文档为 B2 实施规划，不构成任何代码实施授权。**
