# NairoClip B2 实施任务规划

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-B2-IMPLEMENTATION-TASK-PLAN |
| 阶段 | C 阶段 · 批次 B2（9 组重复 op 收敛） |
| 依据 | `docs/NC-C-COMMAND-AUDIT.md` §D-C2、`docs/NC-C-COMMAND-IMPLEMENTATION-PLAN.md` §3/§12、`docs/NC-B1-FOLLOWUP-REPORT.md` |
| 审计基准 | `main` @ `28252751f6d04377514395ee6d1c8ec4271cc591`（本轮实测） |
| 本轮性质 | **只制定规划，不执行整改** |
| 状态 | 待人工批准 |

---

## 1. B2 的准确范围

### 1.1 范围定义（不得扩大、不得与 B3 混淆）

原始 C 阶段计划 §12 的批次划分：

| 批次 | 内容 | 状态 |
|---|---|---|
| B1 | 三个绕过项接入统一 Command | ✅ 已完成 |
| **B2** | **9 组重复 op 收敛（别名表 + 单一 commandType，行为不变）** | **本规划** |
| B3 | 71 个门面 op 接入命令信封 | 未授权 |
| B4 | 事务与 Task 全面迁移 | 未授权 |
| B5 | 全链路与兼容收口 | 未授权 |

**B2 = 只做重复 op 的收敛。**
- 目标：同一业务操作**只有一个审计身份**（commandType），短名与全名都路由到它；
- **不在 B2 内**把写操作迁移到命令信封（那是 B3）；
- **不在 B2 内**改动 `nc_*` 的业务语义与 C ABI 签名。

### 1.2 一句话目标

> 让「短名 op」与「规范 `nc_*` op」从「两段各自独立的业务代码」，
> 变成「一个业务实现 + 一层可审计的别名适配」。

---

## 2. 涉及的 9 组操作与真实实现状态（本轮重新审计）

审计方法：解析 `mobile/native/android/nc_jni.cpp` 中每个 `if (op == "...")` 分支，
提取**入参键**与**Ok 载荷**，逐组比对。**不依据历史报告推断。**

### 2.1 总表

| # | 短名 op | 规范 op | 入参是否一致 | 载荷是否一致 | 归类 |
|---|---|---|---|---|---|
| 1 | `opacity` | `nc_set_clip_opacity` | ❌ `value` vs `opacity` | ❌ `""` vs `"{}"` | 有差异 |
| 2 | `add_track` | `nc_add_track` | ✅ | ✅ | 完全等价 |
| 3 | `blend` | `nc_set_clip_blend_mode` | ✅ `mode` | ❌ `""` vs `"{}"` | 仅载荷差异 |
| 4 | `effect_add` | `nc_add_effect` | ✅ `type,value` | ✅ | 完全等价 |
| 5 | `keyframe` | `nc_insert_keyframe` | ❌ 见 §2.2 | ❌ `""` vs `"{}"` | 有差异（含默认值） |
| 6 | `remove_keyframe` | `nc_remove_keyframe` | ❌ 见 §2.2 | ✅ 都是 `""` | 仅入参差异 |
| 7 | `adjust_effect_add` | `nc_adjustment_add_effect` | ✅ | ❌ `effect_id` vs `value` | 仅载荷键名 |
| 8 | `adjust_effect_remove` | `nc_adjustment_remove_effect` | ✅ | ✅ | 完全等价 |
| 9 | `adjust_lut` | `nc_adjustment_set_lut` | ✅ | ✅ | 完全等价 |

**统计**：完全等价 4 组；存在参数或载荷差异 5 组。

### 2.2 三组关键差异（必须逐字保留旧调用方行为）

**① `opacity` vs `nc_set_clip_opacity`**

```cpp
// 短名：读 value，成功返回 ""
const int32_t rc = nc_set_clip_opacity(ed, ti, cid, JNum(a, "value", 1.0));
// 全名：读 opacity，成功返回 {}
const double opacity = JNum(a, "opacity", 1.0);
```
→ 收敛后必须**同时接受** `value` 与 `opacity`；返回值需按调用方兼容策略处理。

**② `keyframe` vs `nc_insert_keyframe`**

```cpp
// 短名：prop ← (prop | property)；at_us ← (at_us | t*1e6)；值读 v，默认 1.0
// 全名：property；time_us；值读 value，默认 0.0
```
→ 差异不只是键名，**默认值也不同**（1.0 vs 0.0）。
收敛必须保留两套默认值语义，否则「不传值的调用方」行为会变。

**③ `adjust_effect_add` vs `nc_adjustment_add_effect`**

同一 C ABI、同一入参，只是返回 JSON 的键名不同（`effect_id` vs `value`）。

### 2.3 调用方现状（谁在用短名 / 全名）

| 调用方 | 用法 | 文件 |
|---|---|---|
| `l3-panel.js` | 短名（`effect_add` / `blend` / `keyframe` / `opacity` 等） | `mobile/assets/l3-panel.js` |
| `editor.js` | 短名（`canvas` / `track_flag` / `split` …） | `mobile/assets/editor.js` |
| `nc-api.js` | **全名**（由 `tools/gen_ncapi.py` 从 `capi.h` 生成） | `mobile/assets/nc-api.js` |
| `engine_static.js` | 少量短名 | `mobile/assets/engine_static.js` |

→ **两套调用方都真实存在**，因此 B2 不能简单删掉任一入口。

---

## 3. 目标设计

### 3.1 分层

```
JS (短名或全名)
   ↓  NCEngine.call(op, args)
JNI DispatchEdit
   ↓  ① 别名归一化（新增，纯数据驱动）
core/op_alias  ← 单一事实源（可被 core 单测覆盖）
   ↓  ② 参数归一化（把短名参数键映射到规范键，保留默认值策略）
JNI 规范分支（每组只保留 1 份业务实现）
   ↓
nc_* C ABI（不变）
```

### 3.2 新增模块：`core/op_alias`

| 文件 | 内容 |
|---|---|
| `core/include/nairoclip/core/op_alias.h` | `OpAliasSpec` / `findOpAlias` / `canonicalOpName` / `commandTypeForOp` / `normalizeOpArgs` |
| `core/src/op_alias.cpp` | 9 组别名表 + 参数归一化实现 |

**为什么放在 core 而不是 JNI**：

1. JNI 不在 CI 的 `build-and-test` 编译范围内（只有 Android 交叉编译会编），
   放 JNI 里就**无法在阻断性 CI 中被单测覆盖**；
2. 别名表 + commandType 是**跨平台契约**（iOS/Swift、桌面也会用到），
   不属于 Android 专属；
3. 放 core 后可用普通 C++ 单测断言「同一业务只有一个 commandType」。

### 3.3 数据结构

```cpp
struct OpArgAlias {
    const char* alias;      // 旧键，如 "value"
    const char* canonical;  // 规范键，如 "opacity"
};

struct OpAliasSpec {
    const char* opName;        // 短名，如 "opacity"
    const char* canonicalOp;   // 规范 op，如 "nc_set_clip_opacity"
    const char* commandType;   // 唯一业务身份，如 "clip.opacity.set"
    const OpArgAlias* args;    // 参数键映射（可空）
    std::size_t argCount;
    /// 短名的成功载荷（保留旧返回形状）；规范名为 "{}"
    const char* legacyOkPayload;
};
```

### 3.4 commandType 唯一性约束

9 组各对应**一个** commandType，与 B1 已确定的命名体系一致：

| 短名 | 规范 op | commandType |
|---|---|---|
| `opacity` | `nc_set_clip_opacity` | `clip.opacity.set` |
| `add_track` | `nc_add_track` | `track.add` |
| `blend` | `nc_set_clip_blend_mode` | `clip.blend.set` |
| `effect_add` | `nc_add_effect` | `effect.add` |
| `keyframe` | `nc_insert_keyframe` | `keyframe.insert` |
| `remove_keyframe` | `nc_remove_keyframe` | `keyframe.remove` |
| `adjust_effect_add` | `nc_adjustment_add_effect` | `track.adjustment.effect.add` |
| `adjust_effect_remove` | `nc_adjustment_remove_effect` | `track.adjustment.effect.remove` |
| `adjust_lut` | `nc_adjustment_set_lut` | `track.adjustment.lut.set` |

**硬约束**：`commandTypeForOp("opacity") == commandTypeForOp("nc_set_clip_opacity")`，
且全表 commandType **两两不重复**。

---

## 4. 源码修改顺序与批次

### 4.1 批次划分

| 批次 | 内容 | 依赖 | 规模 |
|---|---|---|---|
| **B2-0** | 基线核验 + 本规划评审 | — | — |
| **B2-1** | 新增 `core/op_alias`（别名表 + 参数归一化 + commandType），纯新增，不改 JNI | B2-0 | S |
| **B2-2** | 4 组**完全等价**收敛：JNI 删除重复分支，短名路由到规范分支 | B2-1 | S |
| **B2-3** | 5 组**有差异**收敛：参数键 + 默认值 + 载荷适配 | B2-1 | M |
| **B2-4** | 一致性守卫 + 文档 + 全量回归 | B2-2/3 | M |

### 4.2 每批的具体动作

**B2-1（纯新增，零行为变化）**

1. 新增 `core/include/nairoclip/core/op_alias.h`、`core/src/op_alias.cpp`；
2. `core/CMakeLists.txt` 注册；
3. 新增 `tests/test_op_alias.cpp`；
4. **不接线到 JNI**（此批结束后运行时行为 0 变化，便于单独评审）。

**B2-2（4 组完全等价）**

对 `add_track` / `effect_add` / `adjust_effect_remove` / `adjust_lut`：

1. 在 `DispatchEdit` 入口处加一行归一化：
   `const std::string op = canonicalOpName(rawOp);`
2. **物理删除**这 4 个短名分支（不是保留两份）；
3. 短名调用方经归一化后落入规范分支，行为与载荷逐字节不变。

**B2-3（5 组有差异）**

对 `opacity` / `blend` / `keyframe` / `remove_keyframe` / `adjust_effect_add`：

1. 规范分支的入参读取改为「先读规范键，再回落旧键」：
   `JNum(a, "opacity", JNum(a, "value", 1.0))`；
2. **默认值按调用来源区分**：`keyframe` 的 `v` 默认 1.0 与 `value` 默认 0.0
   必须分别保留（在别名 spec 里记录 `legacyDefaultValue`）；
3. 成功载荷：短名返回旧形状（`""` 或 `{"effect_id":N}`），规范名返回 `"{}"`；
   形状由 `legacyOkPayload` 字段驱动，**不允许**在两个分支各写一份；
4. 删除短名分支。

**B2-4（守卫 + 回归）**

1. 新增 `tools/check_op_alias.py`（阻断 CI）：
   - JNI 中「短名」的 `if (op == "...")` 分支数必须为 **0**（只允许出现在别名表）；
   - 每个 `canonicalOp` 必须有且仅有一个 JNI 分支；
   - commandType 全表无重复；
   - 别名表覆盖 9/9 组，无遗漏。
2. 更新 `docs/NC-C-COMMAND-IMPLEMENTATION-PLAN.md` 的 B2 状态；
3. 全量回归（本地 + CI）。

---

## 5. 每批必须新增/修改的测试

| 批次 | 测试 | 判定 |
|---|---|---|
| B2-1 | `tests/test_op_alias.cpp`：<br>· 9 组 `canonicalOpName` 正确<br>· 短名与全名 `commandType` **相同**<br>· 全表 commandType **唯一**<br>· 未知 op 原样返回<br>· 参数归一化：`value→opacity`、`prop→property`、`t→time_us`、`v→value`、`at_us→time_us` | 全部通过 |
| B2-2 | `tests/test_op_alias.cpp` 增加「4 组等价组」断言：归一化前后参数对象**逐字段相等** | 通过 |
| B2-3 | 增加「5 组差异组」断言：<br>· 旧键与规范键都能取到值<br>· 两套默认值各自生效（`v`=1.0 / `value`=0.0）<br>· 短名载荷形状不变 | 通过 |
| B2-4 | `python3 tools/check_op_alias.py`（阻断 CI 步骤）；<br>Android 交叉编译确认 JNI 仍可编译 | CI success |

> **JNI 侧无法在 Ubuntu CI 单测**（需 NDK/jni.h），因此逻辑全部下沉到 core，
> JNI 只做「调用 + 传参」。这是本设计的核心取舍，不是规避测试。

---

## 6. 构建与验收要求（每批）

| 检查 | 要求 |
|---|---|
| `build-and-test` | ✅ success（含全部新增单测） |
| `ASAN 诊断` | ✅ success（非阻断，但必须通过） |
| `UB 诊断` | ✅ success（非阻断，但必须通过） |
| `Valgrind Memcheck` | ✅ success；16 分片全绿；`executed == total`；0 错误 0 泄漏 |
| `Android APK` | ✅ success（含冒烟：badging / 签名 / ABI / 原生库） |
| `Android Cross-Compile` | ✅ success（三 ABI） |
| 前端静态检查 | `check_mobile_frontend.py` / `check_wiring.py` 通过 |

---

## 7. 风险、兼容与回滚

| ID | 风险 | 等级 | 缓解 | 回滚 |
|---|---|---|---|---|
| RB-1 | 删除短名分支后旧 JS 调用失败 | 高 | 归一化在入口统一处理；保留 9 组回归用例 | 单批 revert |
| RB-2 | `keyframe` 默认值 1.0 与 0.0 混用导致动画行为变化 | 高 | 别名 spec 显式记录 legacy 默认值；专项用例断言 | 回退 B2-3 |
| RB-3 | 载荷形状变化破坏前端解析 | 中 | `legacyOkPayload` 驱动；逐组对比用例 | 回退 B2-3 |
| RB-4 | 别名表与 JNI 分支不一致 | 中 | `check_op_alias.py` 阻断 CI | — |
| RB-5 | 误把 B3 的「接入信封」混进 B2 | 高 | 明确禁止（§9）；B2 不碰 `CommandDispatcher` | — |
| RB-6 | 别名表被当成第二套业务逻辑 | 中 | 别名表**只描述映射与参数键**，不含任何校验/副作用 | — |

回滚单元 = 单个批次；每批一个独立 PR，可独立 revert。

---

## 8. 验收标准（B2 整体）

1. 9 组别名 **100%** 收敛，短名在 JNI 中**无独立业务分支**（工具校验）。
2. 每组 `commandType` 唯一，短名与全名返回**同一个** commandType。
3. 旧 JS/Kotlin 调用**行为不变**（载荷形状、默认值、错误码一致）。
4. `check_op_alias.py` 已进阻断 CI。
5. 本地 + CI 全绿；Valgrind 1764 用例 0 错误。
6. Android APK 构建 + 冒烟 success。

---

## 9. 明确禁止（B2 边界）

**禁止修改的文件/模块**：

- `core/src/contract/*`（信封、dispatcher、事件）—— 属 B3；
- `core/src/commands.cpp` 中 71 个门面命令的实现；
- `nc_*` C ABI 签名（`capi.h` 不得删改既有声明）；
- `core/src/preview.cpp` / `frame_fit.cpp` / `xform.cpp`（渲染路径）；
- `frame_fit` 的任何行为（仍属独立缺陷单）；
- `.github/workflows/ci.yml` 的 job 结构（只允许新增一个检查步骤）。

**禁止提前开展**：

- B3（71 个门面 op 接入信封）、B4（事务/Task）、B5、D 阶段；
- GPU 任何生产代码（G0.5 及以后）；
- 删除任何旧 op 名（必须保留到调用方全部迁移）。

---

## 10. 停止点与人工验收条件

| 批次 | 停止点 | 人工验收条件 |
|---|---|---|
| B2-1 | 提交 `core/op_alias`（纯新增）后停止 | 评审别名表 9/9、commandType 唯一性、单测覆盖；确认未接线到 JNI |
| B2-2 | 4 组等价收敛后停止 | 评审「短名分支已物理删除」的 diff；确认载荷逐字节不变 |
| B2-3 | 5 组差异收敛后停止 | 逐组评审参数键与默认值；确认 `v`/`value` 默认值分别保留 |
| B2-4 | 守卫 + 文档后停止 | 确认 `check_op_alias.py` 进阻断 CI；全量回归绿 |

**任何一批未获人工批准，不得进入下一批。**

---

## 11. 与 GPU 工作流的关系

B2 与 GPU G0/G0.5 **可并行**，但：

- 二者**不得同时修改** `mobile/native/android/nc_jni.cpp`
  （B2 要删短名分支；GPU 后期可能加 Surface/纹理入口）；
- 文件所有权规则：**B2 期间 `nc_jni.cpp` 归 B2 独占**；
  若 GPU 需改动，必须等 B2 合并后再申请。

---

## 12. 前置条件

1. B1 最终验收通过（已交付 `docs/NC-B1-FINAL-ACCEPTANCE-REPORT.md`）；
2. 本规划获人工批准；
3. main 基线重新核验（不得沿用 `28252751` 作为执行时基线）。
