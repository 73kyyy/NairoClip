# NairoClip B2 实施任务规划

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-B2-IMPLEMENTATION-TASK-PLAN |
| **文档版本** | **V1.1（人工评审后修订）** |
| 阶段 | C 阶段 · 批次 B2（9 组重复 op 收敛） |
| 依据 | `docs/NC-C-COMMAND-AUDIT.md` §D-C2、`docs/NC-C-COMMAND-IMPLEMENTATION-PLAN.md` §3/§12、`docs/NC-B1-FOLLOWUP-REPORT.md` |
| 审计基准 | `main` @ `b3fe63155816c6b7bba4ba178fa70ef0eb5be859`（本轮实测） |
| 本轮性质 | **只修订本文档，不执行整改** |
| 修订摘要 | 见 §0 |

---

## 0. V1.1 修订摘要（对应人工评审 7 条意见）

| # | 评审意见 | 本版处置 | 落点 |
|---|---|---|---|
| 1 | 别名归一化后必须保留原始 op 身份 | 新增 `OpCallContext.rawOp` / `viaAlias`，全链路携带；B3 将作为 `CommandRequest.originOp` 落审计 | §3.2 |
| 2 | `OpAliasSpec` 必须完整表达 keyframe 两套默认值 | 每个参数独立 `canonicalDefault` / `legacyDefault`，不用一个笼统字段 | §3.3 |
| 3 | 明确旧键/新键并存优先级，及缺失、null、错误类型 | 新增 §3.4「取值优先级与异常输入」表 | §3.4 |
| 4 | `legacyOkPayload` 无法表达动态 `effect_id` | 改为 `OkSpec{shape, idKey}` 结构化响应描述 | §3.5 |
| 5 | 需 JNI 实际调用路径的兼容性测试 | 抽出平台中立的 `nc_dispatch`，测试直接编译它；并明确剩余未覆盖部分 | §3.6 / §5 |
| 6 | 保持 B2 仅 9 组边界 | §1 明确写出「不做 B3」，§9 列为禁止项 | §1 / §9 |
| 7 | 保留每批独立 PR / 停止点 / 人工批准 | §4 / §10 原样保留并强化 | §4 / §10 |

---

## 1. B2 的准确范围

### 1.1 范围定义（不得扩大、不得与 B3 混淆）

原始 C 阶段计划 §12 的批次划分：

| 批次 | 内容 | 状态 |
|---|---|---|
| B1 | 三个绕过项接入统一 Command | ✅ 已完成 |
| **B2** | **9 组重复 op 收敛（别名表 + 单一 commandType，行为不变）** | **本文档** |
| B3 | 71 个门面 op 接入命令信封 | **本次不授权** |
| B4 | 事务与 Task 全面迁移 | 未授权 |
| B5 | 全链路与兼容收口 | 未授权 |

**B2 只做重复 op 的收敛：**
- 目标：同一业务操作**只有一个审计身份**（commandType），短名与规范名都路由到它；
- **不把写操作迁移到命令信封**（B3 的活）；
- **不改 `nc_*` 的业务语义与 C ABI 签名**；
- **不改** `core/src/contract/*`（信封/dispatcher/事件）。

### 1.2 一句话目标

> 让「短名 op」与「规范 `nc_*` op」从「两段各自独立的业务代码」，
> 变成「一个业务实现 + 一层**可审计、保留调用来源**的别名适配」。

---

## 2. 涉及的 9 组操作与真实实现状态（本轮重新审计）

审计方法：解析 `mobile/native/android/nc_jni.cpp` 中每个 `if (op == "...")` 分支，
提取**入参键**与 **Ok 载荷**，逐组比对。**不依据历史报告推断。**

### 2.1 总表

| # | 短名 op | 规范 op | 入参是否一致 | 载荷是否一致 | 归类 |
|---|---|---|---|---|---|
| 1 | `opacity` | `nc_set_clip_opacity` | ❌ `value` vs `opacity` | ❌ `""` vs `"{}"` | 有差异 |
| 2 | `add_track` | `nc_add_track` | ✅ | ✅（`{"track_id":N}`） | 完全等价 |
| 3 | `blend` | `nc_set_clip_blend_mode` | ✅ `mode` | ❌ `""` vs `"{}"` | 仅载荷差异 |
| 4 | `effect_add` | `nc_add_effect` | ✅ `type,value` | ✅（`{"value":N}`） | 完全等价 |
| 5 | `keyframe` | `nc_insert_keyframe` | ❌ 见 §2.2 | ❌ `""` vs `"{}"` | 有差异（**含默认值**） |
| 6 | `remove_keyframe` | `nc_remove_keyframe` | ❌ 见 §2.2 | ✅ 都是 `""` | 仅入参差异 |
| 7 | `adjust_effect_add` | `nc_adjustment_add_effect` | ✅ | ❌ `effect_id` vs `value`（**动态值**） | 仅载荷键名 |
| 8 | `adjust_effect_remove` | `nc_adjustment_remove_effect` | ✅ | ✅ 都是 `""` | 完全等价 |
| 9 | `adjust_lut` | `nc_adjustment_set_lut` | ✅ | ✅ 都是 `""` | 完全等价 |

**统计**：完全等价 4 组；存在参数或载荷差异 5 组。

### 2.2 三组关键差异（必须逐字保留旧调用方行为）

**① `opacity` vs `nc_set_clip_opacity`**

```cpp
// 短名：读 value，默认 1.0，成功返回 ""
const int32_t rc = nc_set_clip_opacity(ed, ti, cid, JNum(a, "value", 1.0));
// 全名：读 opacity，默认 1.0，成功返回 "{}"
const double opacity = JNum(a, "opacity", 1.0);
```

**② `keyframe` vs `nc_insert_keyframe`**

```cpp
// 短名：prop ← (prop | property)；at_us ← (at_us | t*1e6)；值读 v，默认 1.0
const int32_t prop = JInt(a, "prop", JInt(a, "property", 0));
int64_t atUs = JInt(a, "at_us", -1);
if (atUs < 0) atUs = JNum(a, "t", 0.0) * 1e6;
nc_insert_keyframe(ed, ti, cid, prop, atUs, JNum(a, "v", 1.0));

// 全名：property；time_us；值读 value，默认 0.0
nc_insert_keyframe(ed, ti, cid, JInt(a, "property", 0), JInt(a, "time_us", 0),
                   JNum(a, "value", 0.0));
```

> **关键**：差异不只是键名 —— **默认值不同**（`v` 默认 **1.0**，`value` 默认 **0.0**）。
> 若用一个统一默认值，不传值的调用方行为会变。

**③ `adjust_effect_add` vs `nc_adjustment_add_effect`**

同一 C ABI、同一入参，只是返回 JSON 的键名不同：
`{"effect_id": N}` vs `{"value": N}` —— **N 是运行时返回的动态 ID**。

### 2.3 调用方现状（谁在用短名 / 全名）

| 调用方 | 用法 | 文件 |
|---|---|---|
| `l3-panel.js` | 短名（`effect_add` / `blend` / `keyframe` / `opacity` 等） | `mobile/assets/l3-panel.js` |
| `editor.js` | 短名（`canvas` / `track_flag` / `split` …） | `mobile/assets/editor.js` |
| `nc-api.js` | **全名**（由 `tools/gen_ncapi.py` 从 `capi.h` 生成） | `mobile/assets/nc-api.js` |
| `engine_static.js` | 少量短名 | `mobile/assets/engine_static.js` |

→ **两套调用方都真实存在**，B2 不能删掉任一入口。

---

## 3. 目标设计

### 3.1 分层（修订：新增平台中立分发层）

```
JS (短名或全名)
   ↓  NCEngine.call(op, argsJson)
nc_jni.cpp                  ← 只做 jstring ↔ std::string 的编组
   ↓
nc_dispatch.{h,cpp}         ← 【新增】平台中立：别名归一化 + 参数归一化 + 调 C ABI + 响应成形
   ↓
core/op_alias.{h,cpp}       ← 【新增】别名表 / commandType / 参数规则（纯数据 + 纯函数）
   ↓
nc_* C ABI（不变）
```

**为什么拆出 `nc_dispatch`**（直接回应评审意见 5）：

- 原方案把归一化放 `core/op_alias`、JNI 自己调用，测试只能覆盖到 `op_alias`，
  **碰不到 JNI 真正的分发与响应成形逻辑**；
- 抽出 `nc_dispatch` 后，它**不依赖 `jni.h`**，可以在 Ubuntu CI 里直接编译并单测；
- `nc_jni.cpp` 退化为纯粹的 JNI 编组（`GetStringUTFChars` / `NewStringUTF`），
  这部分无法单测，但由 **Android Cross-Compile + APK 构建** 保证可编译可链接。

### 3.2 保留原始 op 身份（修订：评审意见 1）

```cpp
struct OpCallContext {
    std::string rawOp;          // 调用方**原始** op 字符串（短名或规范名），不可丢失
    std::string canonicalOp;    // 归一化后的规范 op
    std::string commandType;    // 唯一业务身份
    bool viaAlias = false;      // true = 本次调用来自短名
    const OpAliasSpec* spec = nullptr;  // 命中短名时的规格，否则 nullptr
};
```

约束：

1. `rawOp` **不得**被覆盖或丢弃；`nc_dispatch` 返回结果时一并向调用方暴露
   （诊断/日志用，不进 JSON 载荷，避免改变前端可见结构）；
2. 错误消息统一前缀 `[op=<rawOp>]`，便于定位调用来源；
3. **B3 迁移时**，`CommandRequest` 必须新增 `originOp = rawOp` 字段，
   使审计记录能区分「同一个 commandType 是从短名还是规范名进来的」；
   这条写进 B3 的前置要求，本批不实现；
4. `viaAlias` 决定响应载荷形状（见 §3.5）—— 这是"保留来源"的**可观测**后果。

### 3.3 `OpAliasSpec`：完整表达两套默认值（修订：评审意见 2）

```cpp
/// 单个参数的取值规则。
struct OpArgSpec {
    const char* canonicalKey;    // 规范键，如 "value"
    const char* legacyKey;       // 旧键，如 "v"
    const char* altKey;          // 可选第二旧键（如 "t"，单位秒）
    enum class Kind { Int, Double, MicrosFromSeconds } kind = Kind::Double;

    // --- 两套默认值：按调用入口分别生效 ---
    bool hasCanonicalDefault = false;
    double canonicalDefault = 0.0;   // 规范名调用时的默认
    bool hasLegacyDefault = false;
    double legacyDefault = 0.0;      // 短名调用时的默认
};

struct OpAliasSpec {
    const char* opName;        // 短名
    const char* canonicalOp;   // 规范 op
    const char* commandType;   // 唯一业务身份
    const OpArgSpec* args;
    std::size_t argCount;
    OkSpec canonicalOk;        // 规范名的成功载荷
    OkSpec legacyOk;           // 短名的成功载荷
};
```

**`keyframe` 的完整规格（示例，也是本批最复杂的一组）**

| 参数 | canonicalKey | legacyKey | altKey | 规范名默认 | 短名默认 |
|---|---|---|---|---|---|
| 属性 | `property` | `prop` | — | `0` | `0` |
| 时间 | `time_us`（微秒） | `at_us`（微秒） | `t`（**秒**，×1e6） | `0` | `0`（由 `t` 缺省推出） |
| 值 | `value` | `v` | — | **`0.0`** | **`1.0`** |

> 两套默认值**分别声明**，不允许写成「一个默认值 + 注释」。
> 单元测试必须同时断言：只传 `v` 时取 1.0、只传 `value` 时取 0.0、
> 两者都不传时按入口取各自默认。

### 3.4 取值优先级与异常输入（修订：评审意见 3）

#### 3.4.1 优先级（按调用入口区分首选键）

| 调用入口 | 第 1 优先 | 第 2 优先 | 第 3 优先 | 兜底 |
|---|---|---|---|---|
| 短名（`viaAlias = true`） | `legacyKey` | `canonicalKey` | `altKey` | `legacyDefault` |
| 规范名（`viaAlias = false`） | `canonicalKey` | `legacyKey` | — | `canonicalDefault` |

**为什么这样定**：与现状逐字一致 ——
- 短名分支原本就是 `JInt(a,"prop", JInt(a,"property",0))`（旧键优先）；
- 规范分支只读规范键。
把"首选键 = 入口自身的键"固化后，**两类现有调用方行为都不变**。

#### 3.4.2 冲突（新旧键同时出现）

- **采用首选键**的值；
- 若两者值不相等 → 记录一条诊断（`op=<rawOp> key=<canonicalKey> conflict legacy=<v1> canonical=<v2> used=<v>`），
  **不报错、不改变返回值**（避免破坏仍在双写的调用方）；
- 该诊断只进日志/调试通道，**不进响应 JSON**。

#### 3.4.3 缺失值

- 所有候选键都不存在 → 用该入口的默认值；
- 若该参数 `hasCanonicalDefault == false && hasLegacyDefault == false`（无默认）→
  返回 `NC_ERROR_INVALID_ARGUMENT`，消息点名缺失键。

#### 3.4.4 null

- `null` **等同于「未提供」**：继续尝试下一优先级；全部为 null → 用默认值；
- **不**把 null 当 0，也**不**报错。

#### 3.4.5 错误类型

| 输入 | 处理 |
|---|---|
| 字符串给数值键（`"abc"` / `"123"`） | **拒绝**，`NC_ERROR_INVALID_ARGUMENT`，消息点明键名与期望类型 |
| 对象/数组给标量键 | **拒绝**，同上 |
| 布尔给数值键 | **拒绝**（不隐式转 0/1） |
| 整数给 `Double` 键 | 接受（整转浮无损） |
| 浮点给 `Int` 键 | 接受，按 `trunc` 取整（与 C ABI 一致） |

> **这是本批唯一的行为收紧**：现状 `JNum()` 遇到类型不对会静默回落到默认值，
> 掩盖调用方 bug。收紧后非法类型会显式失败。
> 风险与验证见 §7 RB-3；`B2-3` 必须先用真实前端调用形态回归，
> 若有现存调用传错类型，**停下来报告**而不是放宽。

#### 3.4.6 `Unit` 换算

- `MicrosFromSeconds`：`time_us = round(t * 1e6)`；
- 取整用 `llround`，并在测试中断言 `0.5s → 500000`、`1/3s → 333333`。

### 3.5 响应载荷：结构化描述（修订：评审意见 4）

原方案 `const char* legacyOkPayload` **无法表达动态 ID**（`{"effect_id": N}` 里的 N 是运行时返回值），
现改为：

```cpp
enum class OkShape {
    EmptyObject,   // "{}"
    EmptyString,   // ""
    IdObject,      // {"<idKey>": <uint64 id>}   ← id 来自 C ABI 返回值
};

struct OkSpec {
    OkShape shape = OkShape::EmptyObject;
    const char* idKey = nullptr;   // 仅 IdObject 用
};
```

9 组的响应规格：

| 组 | 规范名 OkSpec | 短名 OkSpec |
|---|---|---|
| `opacity` | `EmptyObject` | `EmptyString` |
| `add_track` | `IdObject{track_id}` | `IdObject{track_id}` |
| `blend` | `EmptyObject` | `EmptyString` |
| `effect_add` | `IdObject{value}` | `IdObject{value}` |
| `keyframe` | `EmptyObject` | `EmptyString` |
| `remove_keyframe` | `EmptyString` | `EmptyString` |
| `adjust_effect_add` | `IdObject{value}` | **`IdObject{effect_id}`** |
| `adjust_effect_remove` | `EmptyString` | `EmptyString` |
| `adjust_lut` | `EmptyString` | `EmptyString` |

失败路径统一走既有 `Err(...)`，不因入口不同而改变错误码。

### 3.6 别名表的职责边界（防止变成第二套业务逻辑）

| 允许 | 禁止 |
|---|---|
| 描述「短名 → 规范名」映射 | 做参数合法性校验以外的业务判断 |
| 描述参数键映射与默认值 | 直接调用 C ABI（必须由 `nc_dispatch` 调） |
| 描述成功载荷形状 | 持有状态、缓存、副作用 |

`op_alias` 是**纯数据 + 纯函数**，无 I/O、无全局状态，可被任意平台复用。

---

## 4. 源码修改顺序与批次

| 批次 | 内容 | 依赖 | 规模 |
|---|---|---|---|
| **B2-0** | 基线核验 + 本文档人工批准 | — | — |
| **B2-1** | 新增 `core/op_alias`（纯新增，**不接线**） | B2-0 | S |
| **B2-2** | 4 组完全等价收敛（含抽出 `nc_dispatch`） | B2-1 | M |
| **B2-3** | 5 组有差异收敛（参数/默认值/载荷适配） | B2-2 | M |
| **B2-4** | 守卫工具 + 全量回归 + 文档 | B2-3 | M |

### 4.1 各批具体动作

**B2-1（纯新增，零行为变化）**

1. 新增 `core/include/nairoclip/core/op_alias.h`、`core/src/op_alias.cpp`；
2. `core/CMakeLists.txt` 注册；
3. 新增 `tests/test_op_alias.cpp`；
4. **不接线到 JNI**（本批结束后运行时行为 0 变化，便于单独评审）。

**B2-2（4 组完全等价 + 抽出 nc_dispatch）**

1. 新增 `mobile/native/android/nc_dispatch.h/.cpp`（平台中立，无 `jni.h`）；
2. `nc_jni.cpp::DispatchEdit` 改为：
   ```cpp
   const OpCallContext ctx = resolveOp(rawOp);      // 归一化
   if (!dispatchToAbi(ed, ctx, argsJson, out)) ...  // 分发
   ```
3. **物理删除** `add_track` / `effect_add` / `adjust_effect_remove` / `adjust_lut`
   四个短名分支；
4. 行为与载荷逐字节不变。

**B2-3（5 组有差异）**

1. 规范分支的入参读取改为「按 §3.4 优先级」读；
2. `keyframe` 的 `v`(1.0) / `value`(0.0) 两套默认值按入口生效；
3. 成功载荷按 `OkSpec` 成形（`effect_id` 动态值走 `IdObject`）；
4. 删除 5 个短名分支；
5. **先跑真实前端调用形态回归**，再合并。

**B2-4（守卫 + 回归）**

1. `tools/check_op_alias.py`（进阻断 CI）：
   - JNI 中 9 个短名的 `if (op == "...")` 分支数必须为 **0**；
   - 每个 `canonicalOp` 在 JNI 中**有且仅有 1 个**分支；
   - `commandType` 全表无重复；
   - 别名表覆盖 9/9；
   - `nc_jni.cpp` 必须包含 `resolveOp(` 调用（防止绕过归一化）。
2. 更新本文档的 B2 状态；
3. 全量回归。

---

## 5. 测试要求（修订：评审意见 5 的落点）

### 5.1 三层测试结构

| 层 | 测试文件 | 覆盖 | 在哪个 CI 跑 |
|---|---|---|---|
| **① 别名与规则** | `tests/test_op_alias.cpp` | 别名表、commandType 唯一性、参数优先级/默认值/异常输入 | `build-and-test`（阻断） |
| **② 分发与响应** | `tests/test_nc_dispatch_compat.cpp` | **直接编译 `nc_dispatch.cpp`**：真实 argv JSON → C ABI 参数 → Ok 载荷形状 | `build-and-test`（阻断） |
| **③ JNI 编组** | 无单测 | `jstring ↔ std::string`、`NewStringUTF` 生命周期 | **Android Cross-Compile + APK 构建**（编译/链接级） |

### 5.2 第 ② 层为什么算「JNI 实际调用路径」

`nc_dispatch.cpp` **就是** `DispatchEdit` 的全部非 JNI 逻辑：

- 它在 `nc_jni.cpp` 里被真实调用；
- 测试编译的是**同一个源文件**（不是复制品），输入是**从 JS 提取的真实 argv**；
- 因此「op 字符串进来 → 归一化 → 读哪些键 → 调哪个 C ABI → 返回什么 JSON」
  这条链路是被真实覆盖的。

### 5.3 第 ③ 层未覆盖部分的如实声明

**不能**在 Ubuntu CI 单测 `nc_jni.cpp`（缺 `jni.h` / JNI 运行时）。
该部分靠：

- `Android Cross-Compile`（三 ABI 编译）；
- `Android APK`（真实打包 + 冒烟）；
- **真机手测清单**（B2-4 交付，非自动化）：短名与全名各调 9 组操作一次。

**不得**把 ①② 的通过说成「JNI 已全部验证」。

### 5.4 各批测试清单

| 批次 | 新增测试 | 判定 |
|---|---|---|
| B2-1 | `test_op_alias.cpp`：9 组 `canonicalOp`、短名/全名 commandType 相同、全表唯一、未知 op 原样返回、`rawOp` 保留 | 全通过 |
| B2-2 | `test_nc_dispatch_compat.cpp`：4 组等价组，归一化前后**参数逐字段相等**、载荷逐字节相等 | 全通过 |
| B2-3 | 增加 5 组差异组：旧键/新键都能取值；`v`=1.0 与 `value`=0.0 分别生效；`effect_id`/`value` 动态载荷正确；null/缺失/错误类型按要求处理 | 全通过 |
| B2-4 | `check_op_alias.py`；真机手测清单执行记录 | CI success + 清单签核 |

---

## 6. 构建与验收要求（每批）

| 检查 | 要求 |
|---|---|
| `build-and-test` | ✅ success（含新增单测 + `check_op_alias.py`） |
| `ASAN 诊断` | ✅ success（**注意：该 job 为 `continue-on-error`，非阻断**） |
| `UB 诊断` | ✅ success（同上） |
| `Valgrind Memcheck` | ✅ success；16 分片全绿；`executed == total`；0 错误 0 泄漏 |
| `Android APK` | ✅ success（含冒烟：badging / 签名 / ABI / 原生库） |
| `Android Cross-Compile` | ✅ success（三 ABI） |
| 前端静态检查 | `check_mobile_frontend.py` / `check_wiring.py` 通过 |

---

## 7. 风险、兼容与回滚

| ID | 风险 | 等级 | 缓解 | 回滚 |
|---|---|---|---|---|
| RB-1 | 删除短名分支后旧 JS 调用失败 | 高 | 归一化在入口统一处理；9 组真实 argv 回归；真机手测 | 单批 revert |
| RB-2 | `keyframe` 默认值 1.0/0.0 混用导致动画行为变化 | 高 | 两套默认值分别声明；专项用例断言三种情况 | 回退 B2-3 |
| RB-3 | **类型收紧**（字符串给数值键由"静默默认"变"报错"）破坏现存调用 | 中 | B2-3 前先跑真实前端形态；发现即停并报告，不放宽规则 | 回退 B2-3 |
| RB-4 | 载荷形状变化破坏前端解析 | 中 | `OkSpec` 结构化描述；逐组对比用例 | 回退 B2-3 |
| RB-5 | 别名表与 JNI 分支不一致 | 中 | `check_op_alias.py` 阻断 CI | — |
| RB-6 | 误把 B3 的「接入信封」混进 B2 | 高 | §9 明令禁止；B2 不碰 `contract/*` | — |
| RB-7 | `nc_dispatch` 抽取引入回归 | 中 | B2-2 首批只搬 4 组等价组，diff 可逐行审 | 回退 B2-2 |
| RB-8 | 原始 op 身份在后续重构中丢失 | 中 | `rawOp` 为 `OpCallContext` 必填字段；`check_op_alias.py` 断言其存在 | — |

---

## 8. 验收标准（B2 整体）

1. 9 组别名 **100%** 收敛，短名在 JNI 中**无独立业务分支**（工具校验）。
2. 每组 `commandType` 唯一；短名与全名返回**同一个** commandType。
3. `OpCallContext.rawOp` 全链路保留（工具 + 单测双重断言）。
4. 「旧键优先/规范键优先」按入口生效，与现状逐字一致。
5. `keyframe` 两套默认值（1.0 / 0.0）分别生效。
6. 响应载荷：`EmptyObject` / `EmptyString` / `IdObject` 三种形状正确，
   `effect_id` 动态值正确。
7. null / 缺失 / 错误类型按 §3.4 处理。
8. 旧 JS/Kotlin 调用行为不变（① ② 两层测试 + 真机手测）。
9. `check_op_alias.py` 已进阻断 CI。
10. 本地 + CI 全绿；Valgrind 全量 0 错误；APK 构建 + 冒烟 success。

---

## 9. 明确禁止（B2 边界）

**禁止修改的文件/模块**：

- `core/src/contract/*`（信封、dispatcher、事件）—— 属 B3；
- `core/src/commands.cpp` 中 71 个门面命令的实现；
- `nc_*` C ABI 签名（`capi.h` 不得删改既有声明）；
- `core/src/preview.cpp` / `frame_fit.cpp` / `xform.cpp`（渲染路径）；
- `frame_fit` 的任何行为（仍属独立缺陷单 `docs/DEFECT-frame_fit-noop.md`）；
- `.github/workflows/ci.yml` 的 job 结构（只允许新增一个检查步骤）。

**禁止提前开展**：

- B3（71 个门面 op 接入信封）、B4（事务/Task）、B5、D 阶段；
- GPU 任何生产代码（G0.5 及以后）；
- 删除任何旧 op 名（必须保留到调用方全部迁移）；
- 新增媒体编解码依赖或修改现有编解码实现。

---

## 10. 停止点与人工验收条件

| 批次 | 停止点 | 人工验收条件 |
|---|---|---|
| B2-1 | 提交 `core/op_alias`（纯新增）后停止 | 评审别名表 9/9、commandType 唯一性、两套默认值表达、单测覆盖；确认**未接线到 JNI** |
| B2-2 | 4 组等价收敛 + `nc_dispatch` 抽取后停止 | 评审「短名分支已物理删除」的 diff；确认载荷逐字节不变；确认 `rawOp` 保留 |
| B2-3 | 5 组差异收敛后停止 | 逐组评审参数优先级与默认值；确认 `v`/`value` 分别生效；确认类型收紧清单与前端回归结果 |
| B2-4 | 守卫 + 文档后停止 | 确认 `check_op_alias.py` 进阻断 CI；真机手测清单签核；全量回归绿 |

**每批一个独立 PR、独立停止点；任何一批未获人工批准，不得进入下一批。**

---

## 11. 与 GPU 工作流的关系

B2 与 GPU G0/G0.5 **可并行**，但：

- 二者**不得同时修改** `mobile/native/android/nc_jni.cpp`；
- 文件所有权：**B2 期间 `nc_jni.cpp` 归 B2 独占**（B2 要删短名分支并抽出 `nc_dispatch`）；
- GPU 若需改动，必须等 B2 合并后再申请交接。

---

## 12. 前置条件

1. B1 最终验收通过（见 `docs/NC-B1-FINAL-ACCEPTANCE-REPORT.md`）；
2. 本文档 V1.1 获人工复审批准；
3. 执行前重新核验 main HEAD（不得沿用 `b3fe6315` 作为执行时基线）。
