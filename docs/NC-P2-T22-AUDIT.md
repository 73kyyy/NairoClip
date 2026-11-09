# T2.2 TaskRegistry 接入审计报告

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-P2-T22-AUDIT |
| 审计对象 | `nc_ai_task_*` 全家族（C ABI / JNI / Kotlin / JS）+ `frame_fit` 分支 |
| 基线 | `main` @ `6832d0fb` |
| 授权范围 | 仅执行 A（T2.2）；本轮**只读审计**，未改动任何源码 |
| 审计方式 | 静态检索 + 调用链追踪 + 现有测试覆盖核对 |

> **结论先行**：`nc_ai_task_*` 目前是一套**没有执行者的状态机**。
> UI 能"开始"任务，但仓库内**不存在任何代码**会上报进度、完成或失败；
> `cancel` 只改一个枚举值，没有任何工作会被真正停止。
> 因此「接入 TaskRegistry」不是加 ID 包装，而是**补一条真实的任务生命周期**。

---

## 1. 现状结构

### 1.1 三层各自有什么

| 层 | 内容 | 证据 |
|---|---|---|
| C++ 核心 | `AiTaskRunner`（单任务槽状态机）+ `AiTask` 结构 | `core/include/nairoclip/core/ai_task.h`、`core/src/ai_task.cpp` |
| C ABI | 8 个函数：`status` / `begin` / `progress` / `succeed` / `fail` / `cancel` / `reset` / `failure_text` | `capi.h:818-842`、`capi.cpp:6277-6375` |
| JNI | **7 个** op 分支（缺 `failure_text`） | `nc_jni.cpp:1610-1690` |
| JS | `nc-api.js` 暴露 **8 个**（含 `failure_text`），`l3-panel.js` 只调用 `begin` | `nc-api.js:267-284`、`l3-panel.js:766-775` |
| Kotlin | 无专用方法，统一走 `call(fn, argsJson)` 字符串通道 | `NativeEngineBridge.kt` |

### 1.2 `AiTask` 字段

```cpp
struct AiTask {
    AiFeatureType feature;   AiTaskState state;
    double progress;         std::string stageNote;
    std::string failureReason; bool retryable;
    std::string resultPayload; std::uint64_t targetId;
};
```

**没有 `taskId`**（全文件检索 `taskId`/`task_id` 命中 0），也没有创建时间、超时、取消令牌、结果大小上限。

---

## 2. 逐项审计（8 个 op）

| # | op | 真实执行路径 | 生命周期 | 取消能力 | 进度来源 | 结果结构 | 风险 |
|---|---|---|---|---|---|---|---|
| 1 | `nc_ai_task_begin` | JS `l3-panel.js:769` → JNI `:1610` → `nc_ai_task_begin` → `AiTaskRunner::begin` → `state=Running` | 只到 Running，**无后继** | — | 初始 0，`stageNote="准备中"` | 无 | **UI 提交后永久停在 Running** |
| 2 | `nc_ai_task_progress` | 有 C ABI 与 JNI，**仓库内无任何调用方** | 需 Running | — | 外部传入，单调不减 | 无 | 通道存在但无人驱动 |
| 3 | `nc_ai_task_succeed` | 同上，无调用方 | 需 Running | — | — | 直接存 `resultPayload` 字符串 | 无结构校验、无大小上限 |
| 4 | `nc_ai_task_fail` | 同上，无调用方 | 需 Running | — | — | 写 `failureReason`，空原因回退通用文案 | 通道存在但无人驱动 |
| 5 | `nc_ai_task_cancel` | JNI `:1645` → `AiTaskRunner::cancel` → `state=Cancelled` | 需 Running | **名义取消**：只改枚举，无工作可停、无令牌可轮询 | — | 无 | **虚构取消能力**（本次审计重点） |
| 6 | `nc_ai_task_reset` | JNI `:1650` → `reset()` → `task_ = AiTask{}` | 任何状态 | — | — | 清空 | 复位会丢弃结果，无归档 |
| 7 | `nc_ai_task_status` | JNI `:1655` 组装 JSON 返回（含 7 个出参） | 任何状态 | — | 读 `progress` | 返回字符串 | 有读取方，但读到的是"永远 0 的进度" |
| 8 | `nc_ai_task_failure_text` | **无 JNI 分支** | — | — | — | — | JS 调用必得「未开放的操作」 |

### 2.1 关于第 8 项

`nc-api.js:284` 暴露了 `nc_ai_task_failure_text`，但 `nc_jni.cpp` 中检索该名字命中 **0 次**。
→ 前端一旦调用就是错误返回。属于**前后端契约缺口**，不是"未实现功能"，是**断链**。

### 2.2 关于 `nc_ref_analyze`

op 清单中曾被归入 Task 类，实测为**同步调用**：`nc_ref_analyze` 立即返回 profile JSON（`nc_jni.cpp:1274-1288`），不经过 AI 任务状态机。
→ **更正归类：同步分析调用，不是异步任务**。不纳入本次 TaskRegistry 接入范围。

---

## 3. 真实执行链证据（关键）

全仓检索 `aiTask.` 与 `nc_ai_task_` 的调用点：

| 位置 | 性质 |
|---|---|
| `capi.cpp` 8 个实现 | 定义 |
| `nc_jni.cpp` 7 个分支 | 转发 |
| `nc-api.js` 8 个包装 | 前端 API 表 |
| `l3-panel.js:769` | **唯一的真实调用方**，只调 `begin` |
| `tests/test_ai_task.cpp` | 14 个用例，直接测 `AiTaskRunner` 与 C ABI |

**没有任何一处调用 `progress` / `succeed` / `fail`。**

`l3-panel.js` 的提示语本身就承认了这一点：

```js
toast(`已提交「${act.label}」，等待平台层推理`);
```

「等待平台层推理」——而平台层（Kotlin/Android）**从未实现**这个推理执行器。检索 Kotlin 侧 `AiTask` 命中 0。

---

## 4. 与 P1 TaskRegistry 的关系

P1 新增的 `contract::TaskRegistry`（`core/src/contract/task.cpp`）具备：taskId 生成、五态、进度钳制、取消标记、快照。
**除测试外无任何使用点**——与 `AiTaskRunner` 完全隔离，两套并行且都半空。

这正是任务书禁止的「双事实源」苗头：一旦有人同时给两套写状态，就会不一致。

---

## 5. `frame_fit` 专项核查（按要求，不删除）

### 5.1 证据

`nc_jni.cpp:653-656`：

```cpp
if (op == "frame_fit") {
    *out = Ok("");      // 直接返回成功
    return true;
}
```

**未调用任何 C ABI**。而真实接口存在且完整：

- `int32_t nc_set_frame_fit(NcEditor*, int32_t mode)` — `capi.h:1359`
- `int32_t nc_get_frame_fit(const NcEditor*)` — `capi.h:1362`

### 5.2 判定

| 维度 | 结论 |
|---|---|
| 是否死分支 | **否**——JS 可调用到，并且会拿到 `ok:true` |
| 行为 | **假成功**：用户切「适应/拉伸/填充」后画面不变，桥却回成功 |
| 危害等级 | 高：属于任务书第十六章第 5 条「只实现 UI 没有连接真实引擎」 |
| 处置 | **不删除**。按授权仅记录，实施方案中列为待修项（P2 或单独缺陷单） |

> 明确标记：`frame_fit` 的**真实调用关系已查清**（JS → JNI no-op），不存在"证据不足"的情况；但它是否曾被设计为纯前端状态，需要产品确认——在确认前**保留分支不动**，只修行为。

---

## 6. 风险清单

| ID | 风险 | 等级 | 影响 |
|---|---|---|---|
| RT-1 | UI 提交 AI 任务后**永久卡在 Running**，无进度、无失败、无结束 | **P0** | 违反文档 30.3「禁止进度条卡住」 |
| RT-2 | `cancel` 返回成功但**没有任何工作被停止** | **P0** | 虚构能力，用户以为已取消 |
| RT-3 | 无 taskId，跨层日志/事件无法关联 | P1 | 无法排查线上问题 |
| RT-4 | 结果载荷无结构、无大小上限 | P1 | 内存风险 + 消费端无法解析 |
| RT-5 | `failure_text` 前后端断链 | P1 | 失败时 UI 拿不到统一文案 |
| RT-6 | `AiTaskRunner` 与 `TaskRegistry` 双状态源 | P1 | 后续必然不一致 |
| RT-7 | 单任务槽：切工程/切换编辑器即丢失任务 | P2 | 多工程场景状态错乱 |
| RT-8 | `frame_fit` 假成功 | P1 | 用户操作无效果却显示成功 |

---

## 7. 实施方案（待验收后执行，本轮不改代码）

### 7.1 设计原则

1. **单一事实源**：`TaskRegistry` 成为唯一任务账本；`AiTaskRunner` 降级为**兼容适配器**，内部读写 Registry，不再自持状态。
2. **不虚构能力**：取消 = 设置 `cancelRequested` 供执行器轮询；若没有执行器在跑，任务状态必须如实为 `Cancelled`，且**报告"未执行"而不是"已终止工作"**。
3. **不卡死**：引入**心跳超时**。平台层必须在 `deadlineMs` 内上报进度，否则看门狗把任务判为 `Failed(NC-EXEC-TASK_TIMEOUT)`。看门狗由显式调用驱动（`nc_task_tick(now_ms)`），不引入后台线程。
4. **结果可管理**：结果带类型标签、大小上限（1 MB）、可读取后可释放。

### 7.2 任务模型扩展

| 字段 | 说明 |
|---|---|
| `taskId` | `"task-<seq>"`，跨层唯一 |
| `kind` | `ai.matting` / `ai.superres` / `export` / `proxy` … |
| `state` | queued / running / succeeded / failed / cancelled |
| `progress` | 0..1，单调不减 |
| `deadlineMs` | 心跳超时阈值（默认 15s，可配） |
| `lastHeartbeatMs` | 最近一次进度/心跳时间 |
| `cancelRequested` | 执行器轮询此标志 |
| `result.type` / `result.payload` | 结构化管理 |

### 7.3 跨层改动清单（预估）

| 层 | 改动 |
|---|---|
| C++ | `contract::TaskRegistry` 扩展 taskId/kind/deadline/result；新增 `nc_task_*` 通用 ABI；`AiTaskRunner` 改为注册表适配器 |
| C ABI | 新增 `nc_task_begin/progress/heartbeat/complete/fail/cancel/query/result_get/result_release/tick`；**保留** `nc_ai_task_*` 作为适配层（不破坏现有调用） |
| JNI | 新增 **typed** 方法（`nativeTaskBegin` 等），不往 `op ==` 链条里加分支；旧 7 个 op 保留并标注弃用 |
| Kotlin | `NCEngine` 增加 `taskBegin/taskProgress/taskCancel/taskQuery`；补齐 `failure_text` 通道 |
| JS | `nc-api.js` 增加新表；`l3-panel.js` 改为"提交后轮询 taskQuery"，不再无限等待 |
| 测试 | 生命周期 / 取消语义 / 心跳超时 / 结果上限 / 跨层往返 / 旧 ABI 兼容 |

### 7.4 明确不做的事

- 不伪造任何 AI 推理结果（无模型时如实失败）。
- 不把 `frame_fit` 的修复混进本次（它是独立缺陷，需产品确认语义）。
- 不删除 `nc_ai_task_*` 任何函数。

---

## 8. 验收标准（对应任务书 T2.2 八项要求）

| 要求 | 本方案的落点 | 可验证方式 |
|---|---|---|
| taskId 统一管理 | Registry 生成、全链路透传 | 单测 + C ABI 往返 |
| 任务创建与状态管理 | 五态 + 合法迁移表 | 状态机单测 |
| 进度查询 | `nc_task_query` 返回快照 | 集成测试 |
| 任务取消 | `cancelRequested` 轮询 + 如实状态 | 取消语义单测 |
| 完成/失败通知 | `task.progress` 事件 | 事件断言 |
| 任务结果管理 | 类型 + 上限 + 读取/释放 | 结果单测 |
| 资源安全释放 | 句柄式结果 + 显式 release | 泄漏检测 |
| 跨层贯通 | Kotlin/JNI/C ABI/C++ 四层 | 端到端用例 |

---

## 9. 待批决策

1. **心跳超时阈值**：15 秒是否合适？（AI 推理可能长时间无进度，需产品给值）
2. **`nc_ai_task_*` 处置**：保留为适配层（推荐）还是标记弃用并限期移除？
3. **`frame_fit` 语义**：实现真调用（接 `nc_set_frame_fit`）还是先返回明确"未实现"错误？
4. **是否允许在本次一并修复 `failure_text` 断链**（工作量很小，但超出 T2.2 字面范围）。
