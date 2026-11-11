# NairoClip B2 实施任务规划

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-B2-IMPLEMENTATION-TASK-PLAN |
| **文档版本** | **V1.3（第二轮复审整改后修订）** |
| 阶段 | C 阶段 · 批次 B2（9 组重复 op 收敛） |
| 依据 | `docs/NC-C-COMMAND-AUDIT.md` §D-C2、`docs/NC-C-COMMAND-IMPLEMENTATION-PLAN.md` §3/§12、`docs/NC-B1-FOLLOWUP-REPORT.md`、`docs/NC-B1-FINAL-ACCEPTANCE-REPORT.md` |
| 审计基准 | `main` @ `7b80b5d86cc4aab3e4457182b62c26fd7da2a4ee`（本轮实测，与任务书给出的一致） |
| 本轮性质 | **只修订本文档，不执行整改，不启动任何批次** |
| 修订对照 | 见 §0.2 |

---

## 0. V1.3 修订说明

### 0.1 修订总原则（逐条遵守）

1. 保留 V1.2 已正确修复的七项复审意见；
2. 不重新引入 V1.1 已纠正的错误；
3. 不改变现有源码行为来迁就新设计；
4. 不为统一参数处理而新增源码中不存在的键名回落；
5. 合法输入兼容性与非法输入收紧**严格分离**；
6. 类型安全规格表**不得**成为提前启用运行时收紧的理由；
7. 每条新增规则都有对应实现约束与测试要求；
8. 无法从源码确认的内容标记 **【待核实】**，不自行假定。

### 0.2 V1.2 → V1.3 修订对照

| # | 复审问题 | V1.3 处置 | 落点 |
|---|---|---|---|
| 1 | §3.3 与 §3.9 行为矛盾（阻断） | 拆成两层：**A. 规格表静态类型安全（B2 必做）** 与 **B. 运行时非法输入处理（B2-3b，默认不实施）**；明确旧实现部分非法输入是 UB，**不得把 UB 当兼容基准** | §3.3 / §3.9 |
| 2 | `OpArgSpec` 无法表达 keyframe 时间行为（阻断） | 新增 `altDefault` / `fallback` / `inputUnit` / `conversion` 四类字段，规格表可**完整描述**源码行为，不在 `nc_dispatch` 里硬编码 keyframe 特例 | §3.4 |
| 3 | 微秒转换边界不严谨 | 给出五步检查（有限性→换算→范围→截断→测试），并论证为何 `abs(x) <= INT64_MAX` **不成立**；同时明确这些检查属 B2-3b | §3.5 |
| 4 | `JInt` 风险说明不完整 | 补 ±超范围有限 double / NaN / ±Inf 五类；确认旧路径为 `JNum→double→static_cast<int64_t>`，**新实现不得改为整数解析**（会改变 >2^53 输入） | §2.1 / §3.8 |
| 5 | 真实 C ABI 集成测试需完善 | 补：独立 Editor、成功/失败均释放、动态 ID 对应、三类失败区分、oracle 仅存于测试、多键分别验证、失败错误码 | §3.10 |
| 6 | 空 LUT 行为需核实 | **已从源码五步确认**清除链；但仍要求端到端集成测试，并列出 4 步验证 | §3.10.5 |
| 7 | `spec` 指针生命周期 | 明确静态存储、不可变、禁返回局部/临时、同组两名字同地址；加生命周期与稳定性测试 | §3.2.1 |
| 8 | JNI 字符串生命周期 | B2-4 真机清单扩充 7 项；明确 **Modified UTF-8 ≠ 普通 UTF-8**，不得混同 | §5.3 |
| 9 | §12 的 B1 前置条件 | 明确 B1「有条件通过」是否满足 B2 启动、可带入/须先解决/需例外批准的分类；§2.5 skipped 错误单独勘误 | §12 |
| 10 | §6 与 §9 验收矩阵不统一 | 合并为**单一验收矩阵**（§11），逐项标注批次/阻断性质/证据；ASAN/Valgrind/APK/真机**不混为一种证据** | §11 |
| 11 | B2-3/3b/4 停止点不全 | 三批各自的 PR/停止/回滚/人工验收条件写全 | §10 |

---

## 1. B2 的准确范围（不变）

| 批次 | 内容 | 状态 |
|---|---|---|
| B1 | 三个绕过项接入统一 Command | ✅ 有条件通过（见 §12） |
| **B2** | **9 组重复 op 收敛（9 组 / 18 个名字）** | 本文档 |
| B3 | 71 个门面 op 接入命令信封 | 本次不授权 |
| B4 | 事务与 Task 全面迁移 | 未授权 |
| B5 | 全链路收口 | 未授权 |

B2 只做重复 op 收敛：**一个业务实现 + 一层保留调用来源的别名适配**。
不迁移命令信封；不改 `nc_*` 语义与签名；不改 `core/src/contract/*`。

---

## 2. 逐字源码核对（V1.2 结论保留 + V1.3 补强）

### 2.1 读取函数语义与风险（V1.3 扩充，对应问题 4）

```cpp
bool FindKey(json, key, &pos);   // 找 "key" 后第一个 ':'；找不到返回 false
double JNum(json, key, def) {
    if (!FindKey(...)) return def;
    跳过空格; strtod(...);
    return (end == start) ? def : v;      // 解析失败 → def
}
int64_t JInt(json, key, def) {
    return static_cast<int64_t>(JNum(json, key, (double)def));   // ← 经 double 中转
}
std::string JStrField(json, key);         // 取引号内文本，处理 \n \t \ 转义
```

**旧 `JInt` 的真实转换路径（不得更改）**

```
JSON 文本  --strtod-->  double  --static_cast<int64_t>-->  int64_t
```

**风险清单（对应复审问题 4）**

| 输入形态 | 旧行为 | 性质 |
|---|---|---|
| 缺键 | 返回 `def` | 明确定义 |
| `null` | `strtod("null")` 失败 → 返回 `def` | 明确定义（等同缺失） |
| 非数值文本（`"abc"` / `{}` / `[]` / `true`） | 返回 `def` | 明确定义（静默） |
| 超出 `int64` 的**正**有限 double（如 `1e300`） | `static_cast<int64_t>` → **未定义行为** | **UB，不得作为兼容基准** |
| 超出 `int64` 的**负**有限 double（如 `-1e300`） | 同上 → **UB** | **UB** |
| `NaN` | `int64(NaN)` → **UB** | **UB** |
| `+Inf` / `-Inf` | `int64(±Inf)` → **UB** | **UB** |
| 大整数文本（> 2^53，如 `9007199254740993`） | 经 `double` 舍入为 `9007199254740992`，再转 int64 | 明确定义，但**有精度损失** |

**两条硬约束**

1. **新实现不得改用 `strtoll`/整数解析**。旧路径经 `double`，因此 >2^53 的整数会被舍入；
   改成整数解析会**改变合法输入的结果**，破坏兼容性。
2. **UB 类输入不得纳入"合法输入兼容性"**（见 §3.9）。旧实现在这些输入上行为不可定义，
   不能要求新旧逐字节一致。

> 【待核实】`FindKey` 采用子串搜索，理论上可能命中字符串值内部的 `"key"` 片段。
> 现有 9 组用到的键名（`value`/`opacity`/`mode`/`prop`/`property`/`at_us`/`t`/`v`/
> `time_us`/`name`/`type`/`track_index`/`effect_id`/`path`/`strength`）
> 未发现实际触发场景；B2-3 差分测试将覆盖"键名出现在字符串值内"的构造用例。
> 若发现真实触发，**停止并报告**，不擅自改 `FindKey`。

### 2.2 九组读取链逐字对照（与 V1.2 一致）

| # | 短名 op | 短名读取链 | 规范 op | 规范读取链 |
|---|---|---|---|---|
| 1 | `opacity` | `value`（JNum，def **1.0**） | `nc_set_clip_opacity` | `opacity`（JNum，def **1.0**） |
| 2 | `add_track` | `name`（JStr）；`type`（JInt，0） | `nc_add_track` | 同左 |
| 3 | `blend` | `mode`（JInt，0） | `nc_set_clip_blend_mode` | 同左 |
| 4 | `effect_add` | `type`（JInt，0）；`value`（JNum，def **0.5**） | `nc_add_effect` | 同左 |
| 5 | `keyframe` | 见 §2.3 | `nc_insert_keyframe` | 见 §2.3 |
| 6 | `remove_keyframe` | 见 §2.3 | `nc_remove_keyframe` | 见 §2.3 |
| 7 | `adjust_effect_add` | `track_index`(0)；`type`(0)；`value`（JNum，def **1.0**） | `nc_adjustment_add_effect` | 同左 |
| 8 | `adjust_effect_remove` | `track_index`(0)；`effect_id`(JInt→uint64，0) | `nc_adjustment_remove_effect` | 同左 |
| 9 | `adjust_lut` | `track_index`(0)；`path`(JStr，空→`nullptr`)；`strength`（JNum，def **1.0**） | `nc_adjustment_set_lut` | 同左 |

**第 1/3/7/8/9 组各自只读自己那一套键，源码中无任何跨键回落。**

### 2.3 `keyframe` / `remove_keyframe` 精确读取顺序（不变）

**短名 `keyframe`**

```
1) prop = JInt(a,"prop", JInt(a,"property", 0))   → prop → property → 0
2) atUs = JInt(a,"at_us", -1)                      → 默认 -1
3) if (atUs < 0) atUs = (int64)(JNum(a,"t",0.0) * 1e6)   → 秒 → 微秒，向零截断
4) v = JNum(a,"v", 1.0)
```

**规范名 `nc_insert_keyframe`**

```
1) property = JInt(a,"property", 0)
2) time_us  = JInt(a,"time_us", 0)
3) value    = JNum(a,"value", 0.0)
```

**短名 `remove_keyframe`**：同 keyframe 的 1)–3)（无 `v`）
**规范名 `nc_remove_keyframe`**：`property` 0；`time_us` JInt 0

**四点不得"优化"**：① 短名链无 `time_us`；② 规范链无 `at_us`/`t`；
③ `at_us < 0` 触发 `t` 回落（含显式负值）；④ 换算为**向零截断**。

### 2.4 舍入差分验证要求（不变）

| 项 | 规定 |
|---|---|
| 换算实现 | `static_cast<std::int64_t>(t * 1e6)`（向零截断） |
| 差分要求 | 新实现 vs 旧实现，全部向量一致才允许合并 |
| 区分向量 | `t=0.0000019`→**1**（四舍五入得 2）<br>`t=1.9999999`→**1999999**（得 2000000）<br>`t=-0.0000019`→**-1**（得 -2）<br>`t=-1.9999999`→**-1999999**（得 -2000000） |
| 精度向量 | `t=1/3`→333333；`t=0.5`→500000 |
| 边界向量 | `t=0`→0；`t=0.0000009`→0；`t=2^31` 秒（见 §3.5 溢出） |
| 负数向量 | `t=-0.5`→-500000 |

> 与上述任一不符 → **停止并报告**，不得改测试迁就实现。

---

## 3. 目标设计

### 3.1 分层（不变）

```
JS (短名或全名) → nc_jni.cpp（仅编组） → nc_dispatch（分发/参数/响应）
  → core/op_alias（规格表，纯数据+纯函数） → nc_* C ABI（不变）
```

### 3.2 `OpCallContext` 与 spec 归属（V1.2 设计保留 + 生命周期明确）

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

#### 3.2.1 指针生命周期与稳定性（V1.3 新增，对应问题 7）

| 要求 | 内容 |
|---|---|
| 存储期 | 9 条 `OpAliasSpec` 定义在 `op_alias.cpp` 的**函数内 `static const` 数组**（或文件级 `constexpr` 数组），**静态存储期** |
| 不可变 | 全部字段 `const`；运行时不得写入 |
| 禁止返回临时 | `resolveOp` 返回的指针**只能**指向上述静态数组元素，**不得**指向局部变量、`std::string`、临时构造对象 |
| 同组同址 | 同一组的短名与规范名必须返回**同一个元素地址**（`&specs[i]`） |
| 参数键字符串 | `canonicalKey` / `legacyKey` / `altKey` 指向**字符串字面量**（静态存储期），不得指向 `std::string` 的 `c_str()` |
| 调用方约束 | `nc_dispatch` 只在单次调用内使用该指针，**不得缓存**跨调用 |

**测试要求（必须有）**

| 用例 | 断言 |
|---|---|
| `OpAlias.SpecAddressStableAcrossCalls` | 连续 3 次 `resolveOp("opacity")` 返回同一地址 |
| `OpAlias.PairSharesAddress` | 短名与规范名地址相等 |
| `OpAlias.SpecAddressStableAfterOtherCalls` | 解析其他组后再解析本组，地址不变（防静态容器扩容） |
| `OpAlias.KeyStringsAreStatic` | 指针在多次调用间不变 |

### 3.3 规格表：静态类型安全（**B2 必做**，对应问题 1 的 A 层）

> **本节的约束全部是"编译期/构造期"的静态约束，不涉及运行时行为。**

```cpp
enum class ArgKind { Int32, Int64, Double, MicrosFromSeconds };

/// 类型化默认值：整型默认值必须用整型字段，浮点默认值必须用浮点字段。
struct ArgDefault {
    bool present = false;
    ArgKind kind = ArgKind::Double;
    std::int64_t i64 = 0;    // 仅 Int32 / Int64 使用
    double f64 = 0.0;        // 仅 Double / MicrosFromSeconds 使用（后者单位=秒）

    static ArgDefault none();
    static ArgDefault int32(std::int32_t v);
    static ArgDefault int64(std::int64_t v);
    static ArgDefault real(double v);
    static ArgDefault seconds(double v);
};
```

| 静态约束 | 落实方式 |
|---|---|
| 整型默认值用整型存储 | `ArgDefault::int32/int64` 写 `i64`；`f64` 保持 0 |
| 浮点默认值用浮点存储 | `ArgDefault::real/seconds` 写 `f64`；`i64` 保持 0 |
| 默认值 kind == 参数 kind | 规格表构造时 `assert`；单测 `OpArg.DefaultKindMatchesArgKind` 遍历 9 组全部参数 |
| 禁止隐式转换 | 取值只用 `int32Value()` / `int64Value()` / `doubleValue()`，内部 `assert(kind==...)`；规格表**不得**用 `real(0.0)` 表达整型默认值 |
| 不可变 | 规格数组 `const`，无运行时写入 |

**注意（与问题 1 的 B 层分界）**：以上全部是**静态约束**。
本节**不规定**运行时如何拒绝越界/NaN/Inf —— 那些在 §3.9 / B2-3b。

### 3.4 `OpArgSpec` 完整表达能力（V1.3 重设计，对应问题 2）

V1.2 的 `{canonicalKey, legacyKey, altKey, kind, canonicalDefault, legacyDefault}`
**无法表达** keyframe 短名时间参数的完整行为（`at_us` 默认 -1、`<0` 才回落、`t` 单位秒、
×1e6、向零截断）。V1.3 重新设计：

```cpp
/// 参数值单位
enum class ArgUnit { None, Microseconds, Seconds };

/// 单位转换方式
enum class UnitConversion {
    None,
    SecondsToMicrosTruncTowardZero,   // static_cast<int64_t>(seconds * 1e6)
};

/// 回落触发条件（作用于"首选键读到的值"，缺失时该值等于首选默认值）
enum class FallbackCondition {
    None,               // 不回落
    MissingOrNegative,  // 值 < 0 时回落（缺失时值 = legacyDefault，通常为 -1，因此也会回落）
};

struct OpArgSpec {
    const char* canonicalKey;        // 规范名使用的键
    const char* legacyKey;           // 短名**首选**键（可 nullptr）
    const char* altKey;              // 短名**回落**键（可 nullptr）

    ArgKind kind;                    // 最终传给 C ABI 的类型
    ArgUnit unit;                    // 首选键/回落键的输入单位
    UnitConversion conversion;       // 单位转换方式

    ArgDefault canonicalDefault;     // 规范名缺省
    ArgDefault legacyDefault;        // 短名首选键缺省
    ArgDefault altDefault;           // **回落键缺省**（V1.3 新增）
    FallbackCondition fallback;      // **回落触发条件**（V1.3 新增）
};
```

**`keyframe` / `remove_keyframe` 的时间参数（完整表达源码行为）**

| 字段 | 值 |
|---|---|
| `canonicalKey` | `"time_us"` |
| `legacyKey` | `"at_us"` |
| `altKey` | `"t"` |
| `kind` | `Int64` |
| `unit` | `Microseconds`（`t` 单独为 `Seconds`，见下） |
| `conversion` | `SecondsToMicrosTruncTowardZero`（仅作用于 `altKey`） |
| `canonicalDefault` | `int64(0)` |
| `legacyDefault` | `int64(-1)` |
| `altDefault` | `seconds(0.0)` |
| `fallback` | `MissingOrNegative` |

**读取算法（由 spec 驱动，不在 `nc_dispatch` 里写特例）**

```
resolve(entry, args):
  if entry.viaAlias:
      v = readKey(legacyKey, legacyDefault)                 # 缺失 → legacyDefault
      if fallback == MissingOrNegative and v < 0:
          a = readKey(altKey, altDefault)                   # 缺失 → altDefault
          v = convert(a, conversion)                        # 秒 → 微秒，向零截断
      return v
  else:
      return readKey(canonicalKey, canonicalDefault)        # 规范名不回落、不读 altKey
```

**硬约束**

| # | 约束 |
|---|---|
| 1 | 规格表必须能**完整描述**源码行为，`nc_dispatch` 中**不得**出现 `if (op == "keyframe")` 之类特例分支 |
| 2 | **不得**为 `opacity` / `blend` / `add_track` / `effect_add` / `adjust_*` 新增任何 fallback（源码无回落 → `fallback=None`、`altKey=nullptr`、`altDefault=none()`） |
| 3 | **不得**改变短名与规范名的既有读取顺序（见 §2.2 / §2.3） |
| 4 | `unit` 与 `conversion` 必须与源码一致：`at_us`/`time_us` 单位微秒、`t` 单位秒 |
| 5 | `altDefault` 必须是 `seconds(0.0)`（`t` 缺失时源码得 `JNum("t",0.0)=0.0`） |

**测试要求**

| 用例 | 断言 |
|---|---|
| `OpArg.KeyframeSpecDescribesSource` | 上表 10 个字段逐一等于源码行为 |
| `OpArg.NoFallbackForOtherGroups` | 其余 8 组 `fallback==None && altKey==nullptr` |
| `OpArg.LegacyTimeChain` | 只传 `at_us=1000`→1000；只传 `t=0.5`→500000；都不传→0；`at_us=-5` 且 `t=0.25`→250000 |
| `OpArg.CanonicalTimeChain` | 只传 `time_us=1000`→1000；**只传 `t=0.5` → 0**（规范链不读 `t`）；只传 `at_us=1000` → 0 |
| `OpArg.AltDefaultApplied` | `at_us=-1`（缺省值触发回落）且无 `t` → 0 |
| `OpArg.NoCrossKeyFallback` | 短名传 `time_us` 被忽略；规范名传 `at_us` 被忽略 |

### 3.5 微秒转换边界（V1.3 新增，对应问题 3）

> **定位**：本节定义**检查算法**。是否在 B2-2/B2-3 启用，见 §3.9 —— 属 B2-3b，默认不启用。

#### 3.5.1 为什么 `abs(x) <= INT64_MAX` 不成立

| 事实 | 说明 |
|---|---|
| `INT64_MAX = 2^63 - 1 = 9223372036854775807` | 而 `double` 只能精确表示 ≤ 2^53 的整数 |
| `(double)INT64_MAX` 实际等于 `2^63 = 9223372036854775808.0` | 转换时**向上舍入**，比真实上限大 1 |
| 因此 `x <= (double)INT64_MAX` 会放行 `x == 2^63` | 而 `static_cast<int64_t>(2^63)` 是 **UB** |
| `-2^63` 在 double 中**可精确表示** | 负边界反而可以精确比较 |

**结论**：必须用 `[-2^63, 2^63)` 的**半开区间**比较，不能写成 `abs(x) <= INT64_MAX`。

#### 3.5.2 五步检查算法

```
输入：seconds（double）
1) 有限性：      isfinite(seconds) 为假 → 拒绝
2) 换算：        us = seconds * 1e6
                 （换算后再次 isfinite(us)，因为乘法可能溢出为 ±Inf）
3) 可表示范围：  -9223372036854775808.0 <= us < 9223372036854775808.0
                 （左闭右开；两个端点均可被 double 精确表示）
4) 向零截断：    result = static_cast<std::int64_t>(us)
5) 后置断言：    result 在 [INT64_MIN, INT64_MAX]（防御性，理论上恒真）
```

#### 3.5.3 边界测试（B2-3b 实施时必须有）

| 输入 seconds | us = s×1e6 | 期望 |
|---|---|---|
| `0.0` | 0 | 0 |
| `9.2e12`（≈ INT64_MAX µs） | 9.2e18 | 接近上限，合法 |
| `9.3e12` | 9.3e18 > 2^63 | **拒绝** |
| `-9.2e12` | -9.2e18 | 合法（> -2^63） |
| `-9.3e12` | < -2^63 | **拒绝** |
| `1e308` | +Inf | **拒绝**（步骤 2） |
| `NaN` | NaN | **拒绝**（步骤 1） |
| `+Inf` / `-Inf` | — | **拒绝**（步骤 1） |
| `1.9999999` | 1999999.9 | **1999999**（截断） |
| `-1.9999999` | -1999999.9 | **-1999999**（向零） |

### 3.6 响应载荷（不变）

| 组 | 规范 OkSpec | 短名 OkSpec |
|---|---|---|
| opacity | EmptyObject | EmptyString |
| add_track | IdObject{track_id} | IdObject{track_id} |
| blend | EmptyObject | EmptyString |
| effect_add | IdObject{value} | IdObject{value} |
| keyframe | EmptyObject | EmptyString |
| remove_keyframe | EmptyString | EmptyString |
| adjust_effect_add | IdObject{value} | **IdObject{effect_id}** |
| adjust_effect_remove | EmptyString | EmptyString |
| adjust_lut | EmptyString | EmptyString |

### 3.7 `rawOp` 暴露边界（不变）

| 层 | 含 `rawOp` | 形式 |
|---|---|---|
| 内部 `OpCallContext` | ✅ 必填 | 字段 `rawOp` |
| 日志/诊断 | ✅ 必须有 | `[op=<rawOp>]` 前缀 + 诊断回调携带 `rawOp`/`canonicalOp`/`viaAlias` |
| **前端 JSON 响应** | ❌ **禁止** | `{ok, code, message, result}` 结构**不变**，不新增字段 |

### 3.8 合法输入兼容性（问题 5/6 之一）

**合法输入定义**：`JNum`/`JInt`/`JStrField` 能**明确定义**返回值的输入 ——
含缺失键、`null`、非数值文本（回落默认值）、以及所有在 `int64` 范围内的数值。

**要求**：对 9 组 × 2 个名字的全部合法输入，新旧实现**逐字节一致**：
参数值、`rc`、成功载荷、失败消息。

**特别保留旧数值语义（问题 4）**：

| 输入 | 旧结果 | 新实现必须一致 |
|---|---|---|
| `time_us = 9007199254740993`（2^53+1） | 经 double → `9007199254740992` | ✅ 相同（**不得**改用整数解析） |
| `time_us = 9223372036854775807`（INT64_MAX） | 经 double → `9223372036854775808.0` → cast **UB** | ⚠️ **UB 类，不纳入兼容性**（见 §3.9） |

**证明方式**：B2-2/B2-3 内含「旧逻辑 oracle vs 新 dispatch」差分测试；
oracle **只存在于测试代码**，B2-4 随旧分支删除一并移除。

### 3.9 非法输入处理（问题 1 的 B 层，**默认不实施**）

#### 3.9.1 范围

| 类别 | 具体输入 | 归属 |
|---|---|---|
| 越界整数 | Int32/Int64 超范围 | B2-3b |
| 非有限浮点 | `NaN` / `+Inf` / `-Inf` | B2-3b |
| 非法 JSON 类型 | 字符串/对象/数组/布尔给数值键 | B2-3b |
| 微秒转换溢出 | §3.5 步骤 1–3 拒绝的情形 | B2-3b |

#### 3.9.2 旧实现中属于 **UB** 的输入（不得作为兼容基准）

| 输入 | 旧行为 | 处置 |
|---|---|---|
| 超 `int64` 的有限 double（正/负） | `static_cast<int64_t>` **UB** | 单独列入「非法输入专项测试」；**不要求**新旧一致 |
| `NaN` | `int64(NaN)` **UB** | 同上 |
| `+Inf` / `-Inf` | `int64(±Inf)` **UB** | 同上 |

> **明确原则**：UB 不是"行为"，不能当作兼容性基准；
> 也不能因为"旧实现碰巧返回某个值"就把它写成契约。

#### 3.9.3 启用前置条件（缺一不可）

1. 产出「真实前端调用样本清单」：扫描 `l3-panel.js` / `editor.js` / `nc-api.js` /
   `engine_static.js` 对 18 个名字的**全部**调用点，逐参数记录字面量类型；
2. 证明现存调用**全部为合法类型**（否则停下报告，不放宽规则）；
3. 专项回归覆盖 §3.5.3 与 §3.9.1 全部形态；
4. **独立 PR + 单独人工批准**；B2-3 获批**不自动**包含 B2-3b。

**默认状态：不实施。** B2-2/B2-3 保持旧实现行为（含其对非法输入的静默回落）。

### 3.10 集成测试契约（问题 5，V1.3 扩充）

#### 3.10.1 链路

```
真实 JS 调用样本（从 l3-panel.js / nc-api.js 提取的 (op, argv) 原文）
 → JSON 解析 → nc_dispatch → **真实 C ABI（不 mock）** → 响应成形 → JSON 断言
```

#### 3.10.2 真实 C ABI 落地

| 项 | 做法 |
|---|---|
| 链接 | 直接链接 `libnairoclip_core.a`（含 `capi.cpp`）；**不得**实现任何 `nc_*` mock |
| **Editor 隔离（新增）** | **每个用例使用独立 `NcEditor`**（`nc_editor_create` → 操作 → `nc_editor_destroy`）；若确需共享，必须给出"无状态污染"的证明（如操作前后 `nc_track_count` / `nc_contract_revision` 可解释） |
| **释放（新增）** | **成功与失败路径都必须 `nc_editor_destroy`**；用 RAII 包装（`struct EditorGuard`）避免早退泄漏；ASAN 覆盖泄漏 |
| 动态 ID | `add_track`→`track_id`；`effect_add`→`value`；`adjust_effect_add`→`effect_id`；**分别验证**：ID > 0 且与后续查询一致（`nc_track_count` 增加 / `nc_effect_count` 增加 / `nc_adjustment_effect_get` 可读到同 ID） |
| 失败路径 | 构造真实失败（不存在的 clip / 非调整轨 / strength 越界）→ 断言 `rc` **与旧实现相同**，且响应结构为 `Err` 形态 |

#### 3.10.3 三类失败必须区分（新增）

| 失败类型 | 判定方式 |
|---|---|
| C ABI 失败 | `nc_*` 返回非 `NC_OK` |
| 响应成形失败 | C ABI 成功但 `OkSpec` 成形异常（如 `IdObject` 但 ID 为 0） |
| JSON 断言失败 | 生成的 JSON 字符串与期望不符 |

测试报告必须**分类计数**，不得合并为"测试失败"。

#### 3.10.4 明确不覆盖（不得声称已测）

| 部分 | 状态 |
|---|---|
| `jstring` 编组（`GetStringUTFChars` / `NewStringUTF`） | ❌ 未覆盖（需 JNI 运行时）→ 由 Cross-Compile + APK + §5.3 真机清单覆盖 |
| JS 运行时执行 | ❌ 不执行（只取样本字符串） |

> 本测试的准确名称是「**nc_dispatch 与真实 C ABI 的集成测试**」，
> **不是**「端到端 JS→JNI 测试」。

#### 3.10.5 空 LUT 清除行为（问题 6，**已核实**）

**源码五步确认链**（本轮实测）：

```
① JNI adjust_lut： path = JStrField(a,"path")；空 → 传 nullptr
② nc_adjustment_set_lut： lutPath = (path != nullptr) ? path : ""    ← nullptr 变空串
③ TimelineEditor::setAdjustment(seq, trackId, track->adjustmentEffects, "", strength)
④ SetAdjustmentCommand::apply： track->adjustmentLutPath = lutPath_   ← 赋空串
⑤ 渲染/导出/查询： hasLut = !track.adjustmentLutPath.empty()          ← 空串 = 无 LUT
```

**结论**：空路径确实清除 LUT。**但**仍需端到端测试证明四件事：

| # | 必须验证 |
|---|---|
| 1 | 空字符串 → `nullptr`（JNI 层转换） |
| 2 | C ABI 收到 `nullptr` 后 `lutPath` 为空串 |
| 3 | 已有 LUT **被实际清除**（`nc_adjustment_get` 的 `out_lut_path` 为空） |
| 4 | 返回码与响应载荷正确（`NC_OK` + 短名 `""` / 规范名 `""`） |

**测试步骤**：先 `nc_adjustment_set_lut(track, "/tmp/x.cube", 0.8)` 建立 LUT →
再传 `path=""` → 断言 `nc_adjustment_get` 的 `out_lut_path` 为空 → 断言 `rc==NC_OK`。

**注意**：`nc_adjustment_set_lut` 会**整栈替换** `adjustmentEffects`（JNI 传的是
`track->adjustmentEffects` 现值），因此测试须同时断言**效果栈未被意外改动**。

---

## 4. 源码修改顺序与批次（V1.3 补 B2-3b）

| 批次 | 内容 | 依赖 | 阻断性质 |
|---|---|---|---|
| **B2-0** | 基线核验 + 本文档人工批准 | — | — |
| **B2-1** | 新增 `core/op_alias`（纯新增，不接线） | B2-0 | — |
| **B2-2** | 4 组等价收敛 + 抽出 `nc_dispatch`（仅接管 9 组） | B2-1 | — |
| **B2-3** | 5 组差异收敛 + 差分验证 | B2-2 | — |
| **B2-3b** | 非法输入收紧（**独立 PR、默认不实施**） | B2-3 + 样本清单 + 批准 | — |
| **B2-4** | 守卫工具 + 全量回归 + 真机清单 + 文档 | B2-3(/3b) | — |

### 4.1 边界（问题 7 保留 + 强化）

`nc_dispatch` **只接管 9 组（18 个名字）**；其余约 100 个 op 的
`if (op == ...)` 分支、参数读取、错误码、载荷 **一行不动**。

```cpp
bool DispatchEdit(NcEditor* ed, const std::string& rawOp, const std::string& a,
                  std::string* out) {
    const OpResolveResult r = resolveOp(rawOp);
    if (r.status == ResolveStatus::Known) return dispatchB2(ed, r.ctx, a, out);
    // ↓ 其余 op 原有分支链，一行不动
    const int32_t ti = ...;
    if (op == "split") { ... }
}
```

---

## 5. 测试要求

### 5.1 三层结构

| 层 | 文件 | 覆盖 | CI |
|---|---|---|---|
| ① 规格与解析 | `tests/test_op_alias.cpp` | spec 非空/同址/生命周期、commandType 唯一、默认值 kind 一致、keyframe 规格完整、无多余 fallback | `build-and-test`（阻断） |
| ② 分发 + 真实 C ABI | `tests/test_nc_dispatch_compat.cpp` | §3.10 全链路、差分 oracle、动态 ID、三类失败、空 LUT 清除 | `build-and-test`（阻断） |
| ③ JNI 编组 | 无单测 | 见 §5.3 真机清单 | Cross-Compile + APK + 真机 |

### 5.2 明确不覆盖

`nc_jni.cpp` 的 JNI 编组、JS 运行时执行、真机交互（靠手测）。

### 5.3 JNI 字符串生命周期真机清单（问题 8，B2-4 交付）

| # | 检查项 | 说明 |
|---|---|---|
| 1 | `GetStringUTFChars` 成功后必须 `ReleaseStringUTFChars` | 每条路径（含异常/早退）都要释放 |
| 2 | **异常路径释放** | C ABI 返回失败时同样要释放，不得 `return` 早退漏掉 |
| 3 | 空字符串 | `name=""`（add_track）、`path=""`（adjust_lut） |
| 4 | 中文字符串 | `name="中文轨道"`、`path` 含中文 |
| 5 | `NewStringUTF` 返回失败的资源处理 | 返回 `nullptr` 时不得泄漏已分配的 `std::string` |
| 6 | 连续调用内存增长观察 | `dumpsys meminfo` 或 `adb shell` 采样，1000 次后无持续增长 |
| 7 | 连续 1000 次调用无崩溃、无句柄泄漏 | 覆盖 18 个名字各若干次 |

**编码警告（必须写明）**：

> JNI 使用 **Modified UTF-8**（`\u0000` 编码为两字节 `C0 80`；补充平面字符以代理对
> 形式编码）。它与普通 UTF-8 **不等价**。
> 因此「中文字符串测试通过」**不等于** JNI 编码正确性验证；
> 必须额外覆盖：含 `\u0000` 的字符串、4 字节 UTF-8 字符（如 emoji）。
> 若这些用例行为异常，记录为**独立缺陷**，不在 B2 内修。

---

## 6. 合法输入兼容性验收（与 §11 矩阵对应）

对 9 组 × 2 名字：参数值、`rc`、成功载荷、失败消息**逐字节一致**；
差分 oracle 全部通过；`time_us` 大整数（>2^53）精度行为一致（§3.8）。

---

## 7. 风险与回滚

| ID | 风险 | 等级 | 缓解 | 回滚 |
|---|---|---|---|---|
| RB-1 | 删短名分支后旧 JS 失败 | 高 | 入口统一归一化；真实样本回归；真机手测 | 单批 revert |
| RB-2 | `keyframe` 默认值 1.0/0.0 混用 | 高 | 两套默认值分别声明；三种情况各一条用例 | 回退 B2-3 |
| RB-3 | 时间换算舍入被改（截断→四舍五入） | 高 | §2.4 差分向量；不一致即停 | 回退 B2-3 |
| RB-4 | 误加跨键回落 | 高 | §3.4 约束 2/3 + `OpArg.NoCrossKeyFallback` | 回退 B2-3 |
| RB-5 | 载荷形状变化 | 中 | `OkSpec` + 逐字节比对 | 回退 B2-3 |
| RB-6 | 抽取 `nc_dispatch` 波及非目标 op | 高 | §4.1 边界；对非目标 op 抽查 ≥5 个 | 回退 B2-2 |
| RB-7 | 类型收紧破坏现存调用 | 中 | §3.9.3 前置条件；独立 PR；默认不启用 | 不启用即可 |
| RB-8 | 别名表与 JNI 分支不一致 | 中 | `check_op_alias.py` 阻断 CI | — |
| RB-9 | `rawOp` 丢失 / 前端 JSON 被改 | 中 | 结构体必填 + 单测 + 工具断言 | — |
| RB-10 | `spec` 指针悬空（指向临时） | **高** | §3.2.1 静态存储 + 地址稳定性测试 | 回退 B2-1 |
| RB-11 | `JInt` 改整数解析导致大整数行为变化 | **高** | §3.8 明确禁止 + 2^53+1 用例 | 回退 B2-3 |

---

## 8. 明确禁止

- 修改 `core/src/contract/*`（B3）；修改 71 个门面命令实现；
- 修改 `nc_*` C ABI 签名；修改 `preview.cpp`/`frame_fit.cpp`/`xform.cpp`；
- 修改 `frame_fit` 任何行为；
- 借 `nc_dispatch` 抽取重构非目标 op；
- 在前端 JSON 响应新增字段；
- 删除任何旧 op 名；
- **把 `JInt` 改为整数解析**；
- **为无回落的组新增 fallback**；
- **把 UB 输入当作兼容性基准**；
- 提前进入 B3/B4/B5/D；启动 GPU 生产代码；
- 新增编解码依赖或修改编解码实现。

---

## 9. 与 GPU 工作流的关系（不变）

可并行，但**不得同时修改** `mobile/native/android/nc_jni.cpp`；
**B2 期间 `nc_jni.cpp` 归 B2 独占**；GPU 需改动须等 B2 合并后申请交接。

---

## 10. 停止点与人工验收（问题 11，V1.3 补全）

| 批次 | 独立 PR | 停止点 | 人工验收条件 | 回滚点 |
|---|---|---|---|---|
| **B2-1** | 是 | 提交 `core/op_alias` 后停止 | spec 非空×18、同址、生命周期稳定、commandType 唯一、默认值 kind 一致、keyframe 规格 10 字段正确；**确认未接线** | 删除新增文件 |
| **B2-2** | 是 | 4 组等价收敛 + `nc_dispatch` 后停止 | 非目标 op 分支未变（diff 审查 + 抽查 ≥5）；载荷逐字节一致；`rawOp` 保留 | revert 本批 |
| **B2-3** | 是 | 5 组差异收敛后停止 | ① 五组**合法输入差分测试全部通过** ② `keyframe`/`remove_keyframe` 时间链正确（含 `at_us<0` 回落、截断） ③ 两套默认值（`v`=1.0 / `value`=0.0）正确 ④ 响应载荷与动态 ID 正确 ⑤ 无新增跨键回落 | revert 本批 |
| **B2-3b** | 是（**独立**） | 类型收紧实施后停止 | 独立评审 + **单独批准**；真实样本清单；专项回归；**不因 B2-3 获批而自动执行；默认不实施** | 不合并即回滚 |
| **B2-4** | 是 | 全部证据齐全后停止 | ① `check_op_alias.py` 进阻断 CI ② 全量回归 ③ Valgrind ④ APK 构建 + 冒烟 ⑤ JNI 真机清单（§5.3）⑥ 文档更新 | revert 本批 |

**每批未获人工批准，不得进入下一批。**

---

## 11. 统一验收矩阵（问题 10，合并原 §6 与 §9）

| 验收项 | 执行批次 | 阻断性质 | 必须提交的证据 |
|---|---|---|---|
| `OpAlias` 单元测试（`test_op_alias.cpp`） | B2-1 | **阻断** | 测试报告（用例数/通过数） |
| `nc_dispatch` + **真实 C ABI** 集成测试 | B2-2 / B2-3 | **阻断** | 测试报告 + 三类失败分类计数 |
| 合法输入差分测试（oracle 对比） | B2-2 / B2-3 | **阻断** | 差分结果（逐组逐参数） |
| `check_op_alias.py` | B2-4 | **阻断 CI** | CI 日志（该步骤 success） |
| **ASAN** | 每批 | **非阻断诊断**（`continue-on-error`） | 报告；失败须说明 |
| **UBSAN** | 每批 | **非阻断诊断**（`continue-on-error`） | 报告；失败须说明 |
| **Valgrind Memcheck** | B2-4 | **阻断**（独立工作流，16 分片 `executed==total`、0 错误） | 汇总日志 |
| **Android APK 构建 + 冒烟** | B2-4 | **阻断** | 构建日志 + 冒烟步骤结果 |
| **JNI 真机测试** | B2-4 | **人工验收**（非自动化） | 手测记录（§5.3 七项逐条签核） |
| Android Cross-Compile（三 ABI） | 每批 | 阻断 | CI 日志 |

> **四类证据不得混同**：ASAN/UBSAN（诊断）、Valgrind（内存检测）、
> APK（构建产物）、真机（运行行为）。任一项通过**不能**替代其他项。

---

## 12. B1 前置条件与引用关系（问题 9）

### 12.1 B1 当前状态（引用，不重新判定）

依据 `docs/NC-B1-FINAL-ACCEPTANCE-REPORT.md`：

| 项 | 状态 |
|---|---|
| 三项源码整改 + 契约能力 + CI/Valgrind/APK | ✅ 通过 |
| **B1 整体结论** | **有条件通过**（**不是**"全部完成"） |
| GPU Alpha 一致性 | ❌ 未验证 |
| GPU 硬件真机矩阵 | ❌ 未建立 |
| GPU 路径 Valgrind | ❌ 未验证 |
| `frame_fit` JNI 返回 `Ok` 但未真正执行 C ABI（TD-1） | ❌ 未修 |
| 剩余 71 个 Command 未迁移（TD-2） | 属 B3 |
| 9 组重复 op 未收敛（TD-3） | 属 B2（本批） |
| ASAN/UBSAN 为 `continue-on-error`（TD-4） | 未改 |
| B1 报告 §2.5「无 skipped」表述错误 | 待勘误（见 §12.4） |

### 12.2 「有条件通过」是否满足 B2 启动条件

**满足。** 理由：B2 只涉及 JNI 别名收敛与参数读取，**不依赖** GPU、Alpha、
真机矩阵或 `frame_fit`。B1 的通过项（三项整改、Revision/Event/Undo、
画布/Alpha CPU 路径、CI/Valgrind/APK）已覆盖 B2 所需的全部前置能力。

### 12.3 遗留问题分类

| 类别 | 事项 | 处置 |
|---|---|---|
| **允许带入 B2** | GPU Alpha 未验证、GPU 真机矩阵、GPU 路径 Valgrind、ASAN/UBSAN 非阻断、看门狗锁竞争等技术债 | 与 B2 无交集，不阻塞 |
| **必须先解决** | 无 | — |
| **需人工例外批准才能带入** | TD-1（`frame_fit` 假成功）—— 若 B2 期间发现其影响 9 组中任一 op，须先获批 | 本批**不修**，仅记录 |
| **B2 范围内处理** | TD-3（9 组收敛） | 本批 |

### 12.4 B1 报告 §2.5 skipped 事实性错误（单独处理）

**事实**：B1 报告 §2.5 写「`build-and-test` … 全部为 `success`，无 `skipped`」；
实测该 job 共 27 步，其中第 6 步 `下载 ONNX Runtime` 为 `skipped`（ORT 缓存命中，
`if: cache-hit != 'true'` 的设计行为）；6 个静态检查步骤（7–12）确实全部 `success`。

**处置**：本轮**只允许改本文档**，因此：

1. **不修改 B1 报告**；
2. 建议后续单独提交一次 B1 报告勘误（改为「6 个静态检查全部 success 且无跳过；
   另有 1 个非检查步骤因缓存命中而跳过，属预期」）；
3. 该错误**不影响** B1 的实质结论，也**不影响** B2 启动。

---

## 13. 前置条件

1. B1 有条件通过（见 §12），且本批不依赖其未验证项；
2. 本文档 V1.3 获人工技术复审批准；
3. **V1.3 批准 ≠ 授权 B2-1**：是否启动 B2-1 须**单独决定**；
4. 执行前重新核验 main HEAD（不得沿用 `7b80b5d8` 作为执行基线）。
