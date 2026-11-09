# T2.2 完成报告：AI Task 接入统一 TaskRegistry

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-P2-T22-DONE |
| 阶段 | A（T2.2） |
| 基线 | `main` @ `3031e8db` |
| 状态 | 代码已合并，CI/APK 已验证 |

---

## 1. 逐项接口映射

### 1.1 旧接口 → 新账本（保留兼容）

| 旧 C ABI | 内部实现 | 新账本落点 |
|---|---|---|
| `nc_ai_task_begin` | `AiTaskRunner::begin` → `beginEx`（默认心跳 60s） | `TaskRegistry::createEx` + `start` |
| `nc_ai_task_progress` | `AiTaskRunner::setProgress` → `registry.heartbeat` | `TaskRegistry::heartbeat` |
| `nc_ai_task_succeed` | `AiTaskRunner::succeed` → `completeWithResult` | 结果类型 `ai.<feature>.result` |
| `nc_ai_task_fail` | `AiTaskRunner::fail` → `registry.fail` | 写 `error` + `retryable` |
| `nc_ai_task_cancel` | `AiTaskRunner::cancel` → `registry.cancel` | 置 `cancelRequested` + `Cancelled` |
| `nc_ai_task_reset` | `AiTaskRunner::reset` → `registry.remove` | 释放账本条目 |
| `nc_ai_task_status` | 读 `aiTask.task()`（由 Registry 现算） | 单一事实源 |
| `nc_ai_task_failure_text` | 读 `task().failureReason` | 文案闭环 |

### 1.2 新增通用接口

| C ABI | 用途 |
|---|---|
| `nc_task_begin` | 创建并启动任务（type / 心跳超时 / 总超时 / now_ms） |
| `nc_task_heartbeat` | 真实进度上报（单调不减 + 阶段说明） |
| `nc_task_complete` | 完成并写结果（类型 + 载荷，1MB 上限） |
| `nc_task_fail` | 失败（原因 + 是否可重试） |
| `nc_task_cancel` | 请求取消（置标志） |
| `nc_task_query` | 任务快照 JSON |
| `nc_task_tick` | 推进看门狗，返回超时任务数 |
| `nc_ai_task_begin_ex` | AI 任务带超时配置启动 |

### 1.3 跨层入口

| 层 | 方法 |
|---|---|
| JS | `Engine.taskBegin / taskHeartbeat / taskComplete / taskFail / taskCancel / taskQuery / taskTick` |
| Kotlin | `NCEngine.taskBegin … taskTick`（`@JavascriptInterface @Synchronized`） |
| JNI | `nativeTaskBegin … nativeTaskTick`（7 个 typed 方法） |

---

## 2. 调用链证据

```
JS  Engine.taskBegin(type, targetId, heartbeatMs, totalMs)
 └─ Kotlin NCEngine.taskBegin(…, System.currentTimeMillis())
     └─ JNI Java_com_nairoclip_app_NativeEngineBridge_nativeTaskBegin
         └─ nc_task_begin(editor, type, target_id, hb_ms, total_ms, now_ms, &id, &err)
             └─ TaskRegistry::createEx + start
```

取消路径：

```
JS Engine.taskCancel(taskId)
 └─ Kotlin taskCancel → JNI nativeTaskCancel → nc_task_cancel
     └─ TaskRegistry::cancel  →  state=Cancelled + cancelRequested=true
```

> **执行器契约**：真正的推理/导出执行器必须轮询 `cancelRequested`，
> 并把真实进度经 `nc_task_heartbeat` 回填。
> 若没有执行器，`cancel` 只是账本状态变化——接口注释已明确写死这一点，
> **不得对外宣称「工作已停止」**。

看门狗路径：

```
Kotlin state()（UI 每次刷新）
 └─ nativeTaskTick(now) → nc_task_tick → TaskRegistry::tick
     ├─ 心跳超时（> heartbeatTimeoutMs 无真实进度）→ Failed + timedOut
     └─ 总超时（> totalTimeoutMs）→ Failed + timedOut
```

---

## 3. 生命周期

```
Queued ──start/heartbeat──▶ Running ──complete──▶ Completed
   │                          │
   │                          ├──fail──────────▶ Failed
   │                          ├──cancel────────▶ Cancelled
   │                          └──tick 超时─────▶ Failed(timedOut)
   └──remove（reset）───────────────────────────▶ 释放
```

约束：

- 终态不可回退（`start`/`heartbeat`/`complete`/`fail`/`cancel` 一律拒绝）。
- 进度单调不减，回跳被忽略但不报错。
- 结果写入超过 1 MB → 直接判 Failed（`NC-SERVICE-RESULT_TOO_LARGE`），不写入。
- 心跳与总超时**独立判定**，总超时优先。
- 无时钟启动（旧接口）在第一次 `tick` 时补基准，从该刻开始计时。

---

## 4. 测试结果

新增 `tests/test_task_registry.cpp`，**18 个用例**：

| 分组 | 覆盖 |
|---|---|
| 注册表 | createEx/配置回落、心跳单调与阶段、自动 Running、终态拒绝 |
| 超时 | 心跳超时、总超时独立、无时钟不误判、终态不参与 |
| 结果/取消 | 结果类型+载荷、超限失败、取消标志与状态 |
| C ABI | begin/query/heartbeat/complete 往返、tick 判超时、cancel/未知 ID、`nc_ai_task_begin_ex`、failure_text |

CI 结果：`build-and-test` ✅、`ASAN` ✅、`UB` ✅、`Android APK` ✅（含 badging/签名/ABI 冒烟）。

> 过程中 CI 抓到一个真实缺陷：`TaskCapi.TickFailsStalledTask` 按「已有时钟」写断言，
> 而 `now_ms=0` 启动时首次 `tick` 只建基准。已修正测试并保留该语义注释。

---

## 5. Git 提交记录

| 提交 | 内容 |
|---|---|
| `2f93e16f` | 核心 + C ABI（TaskRegistry 扩展、AiTaskRunner 委托、8 个新函数） |
| `a2b7ef1d` | 修正 tick 测试时序假设 |
| `8ff80afd` | 合并 PR #9 |
| `3109ec37` | JNI/Kotlin/JS 贯通 + failure_text 断链修复 |
| `3031e8db` | 合并 PR #10 |

审计报告：`0f3f2855`（PR #8，`docs/NC-P2-T22-AUDIT.md`）。

---

## 6. 边界与遗留

| 项 | 状态 |
|---|---|
| `frame_fit` 假成功 | **未改**（按决策），已立缺陷单 `docs/DEFECT-frame_fit-noop.md` |
| `l3-panel.js` 提交后仍显示「等待平台层推理」 | 未改：无执行器时属如实描述；待真实推理接入后改为轮询 `taskQuery` |
| 多任务并发 UI | 账本已支持，UI 仍按单活动 AI 任务展示 |
| `nc_ai_task_*` 弃用 | 未弃用，保留为适配层 |
