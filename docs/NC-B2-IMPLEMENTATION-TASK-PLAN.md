# NairoClip B2 实施任务规划

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-B2-IMPLEMENTATION-TASK-PLAN |
| **文档版本** | **V1.2（第二轮技术复审后修订）** |
| 阶段 | C 阶段 · 批次 B2（9 组重复 op 收敛） |
| 依据 | `docs/NC-C-COMMAND-AUDIT.md` §D-C2、`docs/NC-C-COMMAND-IMPLEMENTATION-PLAN.md` §3/§12、`docs/NC-B1-FOLLOWUP-REPORT.md` |
| 审计基准 | `main` @ `2ce8f32d7899f2b445a11271b1a64249546366b5`（本轮实测） |
| 本轮性质 | **只修订本文档，不执行整改** |
| 修订摘要 | 见 §0 |

---

## 0. V1.2 修订摘要（对应第二轮复审 7 条意见）

| # | 复审意见 | 本版处置 | 落点 |
|---|---|---|---|
| 1 | `OpCallContext.spec` 规定规范名为 nullptr 是矛盾 | 改为：**短名与规范名都解析到同一份 `OpAliasSpec`**，用 `viaAlias` 区分入口；**只有未知 op 才没有 spec** | §3.2 |
| 2 | keyframe 时间优先级不得插入 `time_us`；舍入方式需核实 | **逐字重读源码**：短名链为 `at_us → t`（**无 `time_us`**）；规范链为 `time_us`（**无 `at_us`/`t`**）。舍入为 `static_cast<int64_t>` = **截断**（V1.1 误写 `llround`，已纠正） | §2.3 / §3.4 |
| 3 | `Int/Double/MicrosFromSeconds` 共用 `double` 默认值不安全 | 改为**类型化默认值** `ArgDefault{kind, i64/f64}` + 同 kind 强制；给出 Int 溢出、Double NaN/Inf、微秒溢出规则 | §3.3 |
| 4 | 需要真实 nc_dispatch 测试链路契约 | 新增 §3.6 集成测试契约：JS 样本 → JSON → nc_dispatch → **真实 C ABI** → 响应；明确 Editor 生命周期、链接方式、动态 ID、失败路径、mock 标注 | §3.6 / §5 |
| 5 | `rawOp` 暴露边界未定义 | 分三层定义：内部 `OpCallContext` / 日志诊断 / 前端 JSON；**前端响应结构不得改变** | §3.7 |
| 6 | 兼容性与行为收紧混在一起 | 拆成两节：§3.8 合法输入兼容性（必须逐字一致）与 §3.9 非法类型收紧（**独立变更**，需真实样本与专项回归） | §3.8 / §3.9 |
| 7 | `nc_dispatch` 改动边界 | 明确**只接管 9 组目标 op**；其余 op 保持原路径/参数/错误码/载荷，不因抽取而改变 | §3.10 / §4.2 |

**V1.1 已纠正的错误（本轮主动披露）**

| 位置 | V1.1 错误 | 正确（源码实测） |
|---|---|---|
| §3.4.1 优先级表 | 短名链写成 `at_us > time_us > t` | 短名链**只有** `at_us → t`，源码中**不存在** `time_us` |
| §3.4.6 舍入 | 写成 `llround` | 源码是 `static_cast<int64_t>(x)`，即**向零截断** |
| §3.4.1 通用 fallback | 假设所有组都有「旧键↔规范键」互回落 | 仅 `prop←property`、`at_us→t` 两处存在回落；`opacity`/`blend` 等**各读各的键，无互回落** |

---

## 1. B2 的准确范围（不变）

| 批次 | 内容 | 状态 |
|---|---|---|
| B1 | 三个绕过项接入统一 Command | ✅ 已完成 |
| **B2** | **9 组重复 op 收敛（别名表 + 单一 commandType，行为不变）** | 本文档 |
| B3 | 71 个门面 op 接入命令信封 | 本次不授权 |
| B4 | 事务与 Task 全面迁移 | 未授权 |
| B5 | 全链路收口 | 未授权 |

**B2 只做重复 op 的收敛**：一个业务实现 + 一层保留调用来源的别名适配；
不迁移到命令信封；不改 `nc_*` 语义与签名；不改 `core/src/contract/*`。

---

## 2. 9 组操作的逐字源码核对（本轮重做）

> 方法：直接读取 `mobile/native/android/nc_jni.cpp` 对应分支的**实际语句**，
> 逐参数记录「键名、读取函数、默认值、回落条件、单位换算」。
> 行号为本轮实测，仅作定位参考。

### 2.1 读取函数语义（决定"非法输入"行为）

| 函数 | 实现要点 | 缺失 | `null` | 非数值 | `NaN`/`Inf` |
|---|---|---|---|---|---|
| `JNum(json,key,def)` | `strtod`，`end==start` 时返回 `def` | 返回 def | `strtod("null")` 失败 → **def** | 失败 → **def**（静默） | `strtod("nan")` 成功 → **返回 NaN**（危险） |
| `JInt(json,key,def)` | `static_cast<int64_t>(JNum(...))` | 同 JNum | 同 JNum | 同 JNum | `int64(NaN)` 为**未定义行为** |
| `JStrField(json,key)` | 找 `"` 后取到下一个 `"`，处理 `\n/\t/\` 转义 | 返回 `""` | `null` 无引号 → **`""`** | — | — |

> 这条表是 §3.9「非法类型」与 §3.3「类型安全默认值」的事实基础。

### 2.2 九组读取链逐字对照

| # | 短名 op | 短名读取链（源码逐字） | 规范 op | 规范读取链（源码逐字） |
|---|---|---|---|---|
| 1 | `opacity` | `value`（JNum，def **1.0**） | `nc_set_clip_opacity` | `opacity`（JNum，def **1.0**） |
| 2 | `add_track` | `name`（JStr）；`type`（JInt，def 0） | `nc_add_track` | 同左 |
| 3 | `blend` | `mode`（JInt，def 0） | `nc_set_clip_blend_mode` | 同左 |
| 4 | `effect_add` | `type`（JInt，def 0）；`value`（JNum，def **0.5**） | `nc_add_effect` | 同左 |
| 5 | `keyframe` | 见 §2.3 | `nc_insert_keyframe` | 见 §2.3 |
| 6 | `remove_keyframe` | 见 §2.3 | `nc_remove_keyframe` | 见 §2.3 |
| 7 | `adjust_effect_add` | `track_index`（JInt，0）；`type`（JInt，0）；`value`（JNum，def **1.0**） | `nc_adjustment_add_effect` | 同左 |
| 8 | `adjust_effect_remove` | `track_index`（JInt，0）；`effect_id`（JInt→uint64，0） | `nc_adjustment_remove_effect` | 同左 |
| 9 | `adjust_lut` | `track_index`（JInt，0）；`path`（JStr，空→`nullptr`）；`strength`（JNum，def **1.0**） | `nc_adjustment_set_lut` | 同左 |

**注意**：第 1/3/7/8/9 组**各自只读自己那一套键，源码中没有任何跨键回落**。
不得为了实现"统一"而新增互回落——那是行为变化。

### 2.3 `keyframe` / `remove_keyframe` 的精确读取顺序（复审意见 2 的核心）

**短名 `keyframe`（源码 832–844 行）**

```
1) prop  = JInt(a, "prop",  JInt(a, "property", 0))
        → 先读 "prop"；缺失则读 "property"；再缺则 0
2) atUs  = JInt(a, "at_us", -1)
        → 先读 "at_us"，默认 -1
3) if (atUs < 0)  atUs = (int64)(JNum(a, "t", 0.0) * 1e6)
        → 只有 at_us 缺失或**显式 < 0** 时才读 "t"（单位秒）
        → 换算 = 乘 1e6 后 static_cast<int64_t>（向零截断）
        → "t" 缺失时 JNum 返回 0.0 → atUs = 0
4) v     = JNum(a, "v", 1.0)
```

**规范名 `nc_insert_keyframe`（源码 539–552 行）**

```
1) property = JInt(a, "property", 0)
2) time_us  = JInt(a, "time_us", 0)
3) value    = JNum(a, "value", 0.0)
```

**短名 `remove_keyframe`（源码 614–621 行）**

```
1) prop = JInt(a, "prop", JInt(a, "property", 0))
2) atUs = JInt(a, "at_us", -1); 若 < 0 → (int64)(JNum(a,"t",0.0) * 1e6)
```

**规范名 `nc_remove_keyframe`（源码 1890–1897 行）**

```
1) property = JInt(a, "property", 0)
2) time_us  = JInt(a, "time_us", 0)
```

**必须保持的四点（不得"优化"）**

1. 短名链**没有** `time_us`；规范链**没有** `at_us` / `t`；
2. `at_us < 0` 触发 `t` 回落 —— 这意味着**显式传负值**也会走 `t`（既有行为，需保留）；
3. `t` 的换算用 **截断**（`static_cast<int64_t>`），不是四舍五入；
4. `v` 默认 **1.0**，`value` 默认 **0.0** —— 两套默认值。

### 2.4 关于舍入方式的差分验证要求（复审意见 2）

V1.1 把舍入写成 `llround`，与源码不符。本版规定：

| 项 | 规定 |
|---|---|
| 换算实现 | `static_cast<std::int64_t>(t * 1e6)`（**向零截断**） |
| 差分验证 | B2-3 必须先写「新实现 vs 旧实现」差分测试，覆盖下述向量，**全部一致**才允许合并 |
| 区分性向量 | `t=0.0000019` → 截断 **1** / 四舍五入 2<br>`t=1.9999999` → 截断 **1999999** / 四舍五入 2000000<br>`t=-0.0000019` → 截断 **-1** / 四舍五入 -2<br>`t=-1.9999999` → 截断 **-1999999** / 四舍五入 -2000000 |
| 精度向量 | `t=1/3`≈0.3333333 → 333333；`t=0.5` → 500000 |
| 边界向量 | `t=0`；`t=0.0000009`→0；`t=2^31` 秒（溢出检查） |
| 负数向量 | `t=-0.5` → -500000；`t=-1.9999999` → -1999999 |

> 若差分结果与上述任一不符 → **停止并报告**，不得改测试迁就实现。

---

## 3. 目标设计

### 3.1 分层

```
JS (短名或全名)
   ↓  NCEngine.call(op, argsJson)
nc_jni.cpp                ← 只做 jstring ↔ std::string 编组
   ↓
nc_dispatch.{h,cpp}       ← 【新增】平台中立：分发 + 参数读取 + 响应成形
   ↓
core/op_alias.{h,cpp}     ← 【新增】别名规格表（纯数据 + 纯函数）
   ↓
nc_* C ABI（不变）
```

### 3.2 `OpCallContext` 与 `spec` 归属（复审意见 1）

**V1.1 的矛盾**：既规定"规范名调用时 `spec == nullptr`"，
又要求规范名也要用 `canonicalDefault` / `canonicalOk` —— 无 spec 就无从取默认值与载荷。

**V1.2 修正**：

```cpp
struct OpCallContext {
    std::string rawOp;         // 调用方原始 op（短名或规范名），不可丢失
    std::string canonicalOp;   // 规范 op
    std::string commandType;   // 唯一业务身份
    bool viaAlias = false;     // true = 本次调用来自短名
    const OpAliasSpec* spec = nullptr;  // **非空**：短名与规范名都指向同一份 spec
};

/// 解析结果：只有未知 op 才返回"无 spec"。
enum class ResolveStatus { Known, UnknownOp };

struct OpResolveResult {
    ResolveStatus status;
    OpCallContext ctx;   // status == Known 时有效
};

OpResolveResult resolveOp(const std::string& rawOp);
```

**规格表结构（9 条 spec，每条被两个名字命中）**

```cpp
struct OpAliasSpec {
    const char* canonicalOp;   // 规范名，如 "nc_set_clip_opacity"
    const char* legacyOp;      // 短名，如 "opacity"（可 nullptr = 无短名）
    const char* commandType;   // 唯一业务身份
    const OpArgSpec* args;
    std::size_t argCount;
    OkSpec canonicalOk;
    OkSpec legacyOk;
};
```

`resolveOp()` 用**两级查找**：先匹配 `canonicalOp`（`viaAlias=false`），
再匹配 `legacyOp`（`viaAlias=true`）；两者都命中同一条 `spec`。

**单元测试（必须有）**

| 用例 | 断言 |
|---|---|
| `OpAlias.SpecNonNullForBothNames` | 9 组 × 2 个名字共 18 次解析，`spec != nullptr` |
| `OpAlias.SameSpecForPair` | `resolveOp(短名).spec == resolveOp(规范名).spec`（指针相等） |
| `OpAlias.ViaAliasFlag` | 短名 → `viaAlias=true`；规范名 → `false` |
| `OpAlias.UnknownOpHasNoSpec` | `resolveOp("nope")` → `status=UnknownOp`、`spec==nullptr` |
| `OpAlias.CommandTypeUnique` | 9 条 spec 的 `commandType` 两两不同 |

### 3.3 `OpArgSpec`：类型安全默认值（复审意见 3）

```cpp
enum class ArgKind { Int32, Int64, Double, MicrosFromSeconds };

/// 类型化默认值：kind 必须与参数 kind 一致，禁止隐式转换。
struct ArgDefault {
    bool present = false;
    ArgKind kind = ArgKind::Double;
    std::int64_t i64 = 0;   // 仅 Int32 / Int64 使用
    double f64 = 0.0;       // 仅 Double / MicrosFromSeconds 使用（后者单位为**秒**）

    static ArgDefault none();
    static ArgDefault int32(std::int32_t v);
    static ArgDefault int64(std::int64_t v);
    static ArgDefault real(double v);              // 内部 assert isfinite
    static ArgDefault microsFromSeconds(double s); // 内部 assert isfinite
};

struct OpArgSpec {
    const char* canonicalKey;
    const char* legacyKey;   // 可 nullptr
    const char* altKey;      // 可 nullptr（当前仅短名 keyframe/remove 的 "t"）
    ArgKind kind;
    ArgDefault canonicalDefault;
    ArgDefault legacyDefault;
};
```

**规则（必须逐条落进实现与测试）**

| 规则 | 说明 |
|---|---|
| 默认值 kind == 参数 kind | 构造规格表时断言；另有单测 `OpArg.DefaultKindMatchesArgKind` 遍历 9 组全部参数 |
| Int32 溢出 | 取值超出 `INT32_MIN..INT32_MAX` → `NC_ERROR_INVALID_ARGUMENT`（不得截断） |
| Int64 溢出 | 超出 `INT64_MIN..INT64_MAX` → 同上 |
| Double NaN / ±Inf | **拒绝**（`NC_ERROR_INVALID_ARGUMENT`），不得进入 C ABI |
| MicrosFromSeconds 溢出 | `|s| * 1e6 > INT64_MAX` → 拒绝；换算后仍须 `isfinite` 检查 |
| 禁止隐式转换 | 提供 `int32Value()` / `int64Value()` / `doubleValue()` 显式取值，内部 `assert(kind == ...)`；规格表中**不得**出现 `double` 字段承载整型默认值 |
| 语义等价 | `Int32` 参数的默认值必须用 `ArgDefault::int32()` 构造，**不得**写 `ArgDefault::real(0.0)` |

> 本版把"默认值表达"与"运行时非法输入处理"**分开**：
> 前者是规格表类型安全（本批必做），后者见 §3.9（独立变更）。

### 3.4 参数读取：按入口分别登记，不做跨键回落（复审意见 2）

**总原则**：`OpArgSpec` 的 `legacyKey` / `altKey` **只在该参数于源码短名链中真实出现过**时才填。

| # | 参数（规范键） | 短名链（源码） | 规范链（源码） | 说明 |
|---|---|---|---|---|
| 1 | `value`（opacity 组） | `value` def 1.0 | `opacity` def 1.0 | 两组键**不同**；各自只读自己的 |
| 2 | `name`,`type` | 同 | 同 | 无差异 |
| 3 | `mode` | 同 | 同 | 无差异 |
| 4 | `type`,`value` | 同（`value` def 0.5） | 同 | 无差异 |
| 5 | `property` | `prop` → `property` → 0 | `property` → 0 | **短名有回落** |
| 5 | `time_us` | `at_us`(def -1) → 若<0 读 `t`(秒) | `time_us`(JInt def 0) | **两套完全不同的键** |
| 5 | `value` | `v` def **1.0** | `value` def **0.0** | 两套默认值 |
| 6 | 同 5（无 `value`） | — | — | — |
| 7/8/9 | 同 | 同 | 同 | 无差异 |

**关于 `opacity` 组的重要澄清**：源码中短名只读 `value`、规范名只读 `opacity`。
若要"同时接受两个键"，那是**新增能力**，必须单独标注为行为变化；
本版**不采用**，保持各自只读自己的键。

**冲突输入处理（仅针对源码本来就有回落的参数）**

| 场景 | 处理 |
|---|---|
| 短名 `keyframe` 同时传 `prop` 与 `property` | `prop` 优先（源码内层 fallback 顺序），冲突仅记录诊断 |
| 短名同时传 `at_us>=0` 与 `t` | `at_us` 优先，`t` 忽略 |
| 短名 `at_us<0` 且传 `t` | 用 `t`（源码既有行为） |
| 短名 `at_us<0` 且未传 `t` | `atUs = 0` |
| 规范名同时传 `property` 与 `prop` | **只读 `property`**（源码无回落），`prop` 被忽略 |
| 规范名同时传 `time_us` 与 `at_us` | **只读 `time_us`**，`at_us` 被忽略 |

### 3.5 响应载荷：结构化描述

```cpp
enum class OkShape { EmptyObject, EmptyString, IdObject };
struct OkSpec { OkShape shape = OkShape::EmptyObject; const char* idKey = nullptr; };
```

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

失败路径统一走既有 `Err(...)`，错误码不因入口不同而变。

### 3.6 集成测试契约（复审意见 4）

#### 3.6.1 目标链路

```
真实 JS 调用样本（从 l3-panel.js / nc-api.js 提取的 (op, argv 字符串)）
   ↓  JSON 解析（与 nc_jni 相同的字符串，不重新构造）
   ↓  nc_dispatch(op, argv) → OpCallContext → 参数求值
   ↓  **真实 C ABI**（nc_*，不 mock）
   ↓  返回值 + 载荷成形
   ↓  断言 JSON 响应字符串
```

#### 3.6.2 真实 C ABI 的落地方式

| 项 | 做法 |
|---|---|
| 被测 C ABI | 直接链接 `libnairoclip_core.a`（其中含 `capi.cpp`），**不实现任何 `nc_*` 的 mock** |
| Editor 生命周期 | 每个用例 `nc_editor_create()` → 操作 → `nc_editor_destroy()`；用例结束断言无泄漏（ASAN 覆盖） |
| 测试链接方式 | `tests/CMakeLists.txt` 把 `nc_dispatch.cpp` 与 `test_nc_dispatch_compat.cpp` 一起编进 `nairoclip_tests`（与既有测试同二进制） |
| 动态 ID 验证 | `add_track` / `effect_add` / `adjust_effect_add` 返回的 ID 必须 **> 0 且与后续查询一致**（如 `nc_track_count` / `nc_effect_count` 增加） |
| 失败路径 | 构造真实失败（如对不存在的 clip 操作）→ 断言 `Err` 且 **错误码与旧实现一致** |
| 错误码 | 逐组比对「旧分支代码」与「新 dispatch」在同一输入下的 `rc` 值 |

#### 3.6.3 必须标注 mock 的部分

| 部分 | 是否 mock | 说明 |
|---|---|---|
| `nc_*` C ABI | ❌ 不 mock | 真实链接 |
| `std::string`/JSON 解析 | ❌ 不 mock | 真实执行 |
| `jstring` 编组（`GetStringUTFChars` / `NewStringUTF`） | ✅ **无法覆盖** | 需 JNI 运行时；不写成"已测试" |
| JS 运行时（`window.NCEngine`） | ✅ **不执行** | 只取**真实样本字符串**，不跑 JS 引擎 |

> 因此本测试的准确名称是「**nc_dispatch 与真实 C ABI 的集成测试**」，
> **不是**「端到端 JS→JNI 测试」。文档与报告必须用前者措辞。

#### 3.6.4 真机补充测试（B2-4 交付，非自动化）

| 项 | 用例 |
|---|---|
| 空字符串 | `name=""`（add_track）、`path=""`（adjust_lut，应清除 LUT） |
| 中文字符串 | `name="中文轨道"`、`path` 含中文路径 |
| 错误输入 | 缺参数、类型错误、越界值 → 断言错误码与界面提示 |
| JNI 字符串生命周期 | 连续 1000 次调用无崩溃/无句柄泄漏（`GetStringUTFChars` 必须 `ReleaseStringUTFChars`） |

### 3.7 `rawOp` 暴露边界（复审意见 5）

| 层 | 是否包含 `rawOp` | 形式 |
|---|---|---|
| **内部 `OpCallContext`** | ✅ 必须有 | 字段 `rawOp`，`resolveOp` 填写，全程不覆盖 |
| **日志 / 诊断接口** | ✅ 必须有 | 错误消息前缀 `[op=<rawOp>]`；诊断回调携带 `rawOp`、`canonicalOp`、`viaAlias` |
| **前端 JSON 响应** | ❌ **禁止** | `Ok(...)` / `Err(...)` 的 JSON **结构不变**；不得新增 `op` / `rawOp` 字段 |

> 理由：前端多处按固定结构解析（`{ok, code, message, result}`），
> 新增字段会让部分调用方的严格解析失败。**保留调用来源 ≠ 改变响应协议。**
> 若将来需要前端可见的来源信息，必须走独立的诊断通道（不在 B2 范围）。

### 3.8 合法输入兼容性（复审意见 6 之一）

**定义**：合法输入 = 当前 `JNum`/`JInt`/`JStrField` 能正常取到值的输入
（含缺失键与 `null`，因为源码把它们当"未提供"）。

**要求**：对 9 组 × 两个名字的全部合法输入，**新旧实现逐字节一致**：

| 维度 | 一致性要求 |
|---|---|
| 传给 C ABI 的参数值 | 逐位相等（含浮点） |
| 返回的 `rc` | 相等 |
| 成功载荷字符串 | 逐字节相等 |
| 失败消息 | 逐字节相等 |

**证明方式**：B2-2/B2-3 必须包含「旧实现 vs 新实现」差分测试——
把**旧分支逻辑原样保留**在测试里作为 oracle，与新 dispatch 对比。
（测试里的 oracle 是**测试代码**，不是生产代码；B2-4 随旧分支删除而移除。）

### 3.9 非法类型行为收紧（复审意见 6 之二，**独立变更**）

**这是与"兼容性"分开的独立行为变化**，必须单独评审、单独提交。

| 输入形态 | 旧行为 | 新行为（收紧后） |
|---|---|---|
| `"value": "abc"`（字符串给数值键） | 静默用默认值 | `NC_ERROR_INVALID_ARGUMENT` |
| `"value": {}` / `[]` | 静默用默认值 | 同上 |
| `"value": true` | 静默用默认值 | 同上 |
| `"value": NaN`（畸形 JSON） | `strtod` 得 NaN → 传给 C ABI（**UB 风险**） | `NC_ERROR_INVALID_ARGUMENT` |
| `"type": 1e300`（Int32 溢出） | `int64→int32` 截断（实现定义） | `NC_ERROR_INVALID_ARGUMENT` |

**启用前置条件（缺一不可）**：

1. 产出「**真实前端调用样本清单**」：扫描 `l3-panel.js` / `editor.js` /
   `nc-api.js` / `engine_static.js` 对 9 组 op 的**全部**调用点，列出每个参数的实际字面量类型；
2. 证明**现存调用全部为合法类型**（否则停下来报告，不得放宽规则）；
3. 专项回归测试覆盖上表 5 种形态；
4. 由人工单独批准后才可启用；**默认在 B2-2/B2-3 保持旧行为**。

**若样本清单无法证明合法** → 本项**不启用**，仅记录为后续技术债。

### 3.10 `nc_dispatch` 的改动边界（复审意见 7）

| 项 | 边界 |
|---|---|
| 接管范围 | **仅 9 组目标 op**（18 个名字） |
| 其余 op（约 100 个） | **完全走原路径**：`DispatchEdit` 中保持原有 `if (op == ...)` 分支、原参数读取、原错误码、原载荷 |
| 抽取方式 | 在 `DispatchEdit` **入口**做一次 `resolveOp`；命中 9 组才走新路径，否则落到原分支链 |
| 禁止 | 不得借抽取重构其他 op；不得统一其他 op 的错误码；不得改动其载荷 |

**代码形态（示意）**

```cpp
bool DispatchEdit(NcEditor* ed, const std::string& rawOp, const std::string& a,
                  std::string* out) {
    const OpResolveResult r = resolveOp(rawOp);
    if (r.status == ResolveStatus::Known) {
        return dispatchB2(ed, r.ctx, a, out);   // 仅 9 组
    }
    // ↓ 以下为其余 op 的原有分支链，**一行不动**
    const int32_t ti = ...;
    if (op == "split") { ... }
    ...
}
```

---

## 4. 源码修改顺序与批次

| 批次 | 内容 | 依赖 | 规模 |
|---|---|---|---|
| **B2-0** | 基线核验 + 本文档人工批准 | — | — |
| **B2-1** | 新增 `core/op_alias`（纯新增，**不接线**） | B2-0 | S |
| **B2-2** | 4 组等价收敛 + 抽出 `nc_dispatch`（仅接管 9 组） | B2-1 | M |
| **B2-3** | 5 组差异收敛（参数/默认值/载荷）+ 差分验证 | B2-2 | M |
| **B2-3b** | （可选）非法类型收紧，**单独提交、单独批准** | B2-3 + 样本清单 | S |
| **B2-4** | 守卫工具 + 全量回归 + 文档 | B2-3(/3b) | M |

### 4.1 各批动作要点

**B2-1**：新增 `op_alias.h/.cpp` + `tests/test_op_alias.cpp`；**不接线**，运行时零变化。

**B2-2**：
1. 新增 `nc_dispatch.h/.cpp`（平台中立）；
2. `DispatchEdit` 入口加 `resolveOp`，命中 9 组走新路径；
3. **物理删除** `add_track` / `effect_add` / `adjust_effect_remove` / `adjust_lut` 四个短名分支；
4. 其余 op 分支**一行不动**。

**B2-3**：
1. 5 组按 §3.4 读取链实现；
2. `keyframe`/`remove_keyframe` 的时间链严格按 §2.3；
3. 差分测试（旧逻辑作 oracle）全覆盖；
4. 删除 5 个短名分支。

**B2-3b**（可选）：按 §3.9 实施类型收紧，独立 PR。

**B2-4**：`tools/check_op_alias.py` 进阻断 CI + 真机手测清单 + 文档更新。

---

## 5. 测试要求

| 层 | 文件 | 覆盖 | CI |
|---|---|---|---|
| ① 规格与解析 | `tests/test_op_alias.cpp` | spec 非空（18 次）、同 spec、viaAlias、未知 op、commandType 唯一、默认值 kind 一致 | `build-and-test`（阻断） |
| ② 分发 + **真实 C ABI** | `tests/test_nc_dispatch_compat.cpp` | §3.6 全链路；差分 oracle；动态 ID；失败路径；错误码 | `build-and-test`（阻断） |
| ③ JNI 编组 | 无单测 | `jstring` 生命周期 | Cross-Compile + APK + **真机手测** |

### 5.1 明确不覆盖（不得声称已测）

- `nc_jni.cpp` 的 JNI 编组本身；
- JS 运行时执行（只取样本字符串）；
- 真机交互（靠手测清单）。

---

## 6. 构建与验收要求（每批）

| 检查 | 要求 | 备注 |
|---|---|---|
| `build-and-test` | ✅ success | 含 `check_op_alias.py` 与新增单测 |
| `ASAN 诊断` | ✅ success | **该 job 为 `continue-on-error`，非阻断** |
| `UB 诊断` | ✅ success | 同上 |
| `Valgrind Memcheck` | ✅ success；16 分片全绿；`executed == total`；0 错误 | — |
| `Android APK` | ✅ success（含冒烟） | — |
| `Android Cross-Compile` | ✅ success（三 ABI） | 证明 JNI 可编译 |
| 前端静态检查 | `check_mobile_frontend.py` / `check_wiring.py` 通过 | — |

---

## 7. 风险、兼容与回滚

| ID | 风险 | 等级 | 缓解 | 回滚 |
|---|---|---|---|---|
| RB-1 | 删短名分支后旧 JS 失败 | 高 | 入口统一归一化；9 组真实样本回归；真机手测 | 单批 revert |
| RB-2 | `keyframe` 默认值 1.0/0.0 混用 | 高 | 两套默认值分别声明；三种情况各一条用例 | 回退 B2-3 |
| RB-3 | **时间换算舍入被改**（截断→四舍五入） | **高** | §2.4 差分向量；不一致即停 | 回退 B2-3 |
| RB-4 | 误加跨键回落（如给短名加 `time_us`） | **高** | §3.4 表逐字对照；差分测试覆盖"只传 `time_us` 的短名调用"应仍走默认 | 回退 B2-3 |
| RB-5 | 载荷形状变化 | 中 | `OkSpec` + 逐字节比对 | 回退 B2-3 |
| RB-6 | 抽取 `nc_dispatch` 波及非目标 op | **高** | §3.10 边界；对非目标 op 做"行为不变"抽查（至少 5 个） | 回退 B2-2 |
| RB-7 | 类型收紧破坏现存调用 | 中 | §3.9 前置条件；独立 PR；默认不启用 | 不启用即可 |
| RB-8 | 别名表与 JNI 分支不一致 | 中 | `check_op_alias.py` 阻断 CI | — |
| RB-9 | `rawOp` 丢失 | 中 | 结构体必填 + 单测 + 工具断言 | — |

---

## 8. 验收标准（B2 整体）

1. 9 组别名 100% 收敛；短名在 JNI 中无独立业务分支（工具校验）。
2. 每组的短名与规范名**解析到同一份 `OpAliasSpec`**（指针相等断言）。
3. `commandType` 唯一（9 条两两不同）。
4. `rawOp` 在 `OpCallContext` 与诊断路径完整保留；**前端 JSON 结构不变**。
5. **合法输入**新旧逐字节一致（差分测试，含缺失与 `null`）。
6. `keyframe`/`remove_keyframe` 时间链按 §2.3；**差分向量全通过**（截断语义）。
7. `v`=1.0 / `value`=0.0 两套默认值分别生效。
8. 载荷三种形状正确，`effect_id` 动态值正确。
9. `nc_dispatch` 只接管 9 组；非目标 op 行为不变（抽查通过）。
10. `check_op_alias.py` 进阻断 CI；本地 + CI 全绿；Valgrind 0 错误；APK 冒烟 success。
11. （若启用 B2-3b）类型收紧有真实样本清单与专项回归，且经单独批准。

---

## 9. 明确禁止

- 修改 `core/src/contract/*`（属 B3）；
- 修改 71 个门面命令实现；
- 修改 `nc_*` C ABI 签名；
- 修改 `preview.cpp` / `frame_fit.cpp` / `xform.cpp`；
- 修改 `frame_fit` 任何行为；
- 借 `nc_dispatch` 抽取重构非目标 op；
- 在前端 JSON 响应中新增字段；
- 删除任何旧 op 名；
- 提前进入 B3/B4/B5/D；启动 GPU 生产代码（G0.5+）；
- 新增编解码依赖或修改编解码实现。

---

## 10. 停止点与人工验收

| 批次 | 停止点 | 验收条件 |
|---|---|---|
| B2-1 | 提交 `core/op_alias` 后停止 | spec 非空/同 spec/命令类型唯一/默认值 kind 一致 全部通过；确认未接线 |
| B2-2 | 4 组等价收敛 + `nc_dispatch` 后停止 | 确认非目标 op 分支未变（diff 审查 + 抽查）；载荷逐字节一致 |
| B2-3 | 5 组差异收敛后停止 | 逐组核对读取链；差分向量全过；确认无跨键回落 |
| B2-3b | 类型收紧（若启用） | 真实样本清单 + 专项回归 + 单独批准 |
| B2-4 | 守卫 + 文档后停止 | `check_op_alias.py` 进阻断 CI；真机手测签核；全量回归绿 |

**每批独立 PR、独立停止点；未获人工批准不得进入下一批。**

---

## 11. 与 GPU 工作流的关系

- 可并行，但**不得同时修改** `mobile/native/android/nc_jni.cpp`；
- **B2 期间 `nc_jni.cpp` 归 B2 独占**；
- GPU 若需改动须等 B2 合并后申请交接。

---

## 12. 前置条件

1. B1 最终验收通过；
2. 本文档 V1.2 获人工复审批准；
3. 执行前重新核验 main HEAD（不得沿用 `2ce8f32d` 作为执行基线）。
