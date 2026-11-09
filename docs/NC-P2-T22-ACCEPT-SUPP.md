# T2.2 最终验收补充报告

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-P2-T22-ACCEPT-SUPP |
| 阶段 | A（T2.2）验收补充 |
| 最终 main | `4cddc9a82a09f4726e6972753629905a41aa8f18` |
| 结论 | 三项缺陷已修复并验证；main 全链路绿色 |

---

## 一、取消语义核验

### 1.1 发现的问题（真缺陷）

原实现 `core/src/contract/task.cpp :: TaskRegistry::cancel`：

```cpp
it->second.state = TaskState::Cancelled;   // 直接宣告已取消
it->second.cancelRequested = true;
```

只要调用取消，状态立刻变成 `Cancelled` —— 但底层执行器可能仍在运行。
这是**虚构取消能力**，也正是验收要求第一项指出的问题。

### 1.2 修复

| 位置 | 改动 |
|---|---|
| `contract/task.h` | 新增状态 `CancelRequested`；新增 `acknowledgeCancel()` |
| `contract/task.cpp :: cancel` | 只置 `cancelRequested=true` + `CancelRequested`；重复调用幂等返回 true |
| `contract/task.cpp :: acknowledgeCancel` | **仅** `CancelRequested` 可迁移到 `Cancelled`；未请求直接 ack 返回 false（防伪造） |
| `contract/task.cpp :: tick` | 取消中任务同样纳入超时；执行器不确认 → `Failed(NC-SERVICE-CANCEL_TIMEOUT)` |
| `ai_task.h/.cpp` | `AiTaskRunner::cancel()` = 登记请求；新增 `confirmCancel()`；`beginEx` 在未确认前拒绝重开；`task()` 视图在未确认前**仍是 Running** |

C ABI：`nc_task_cancel`（改语义）、`nc_task_cancel_ack`（新增）、`nc_ai_task_cancel_ack`（新增）。
JNI/Kotlin/JS：`nativeTaskCancelAck` → `taskCancelAck`。

### 1.3 测试用例

| 用例 | 验证点 |
|---|---|
| `TaskRegistry2.CancelRequestsButDoesNotDeclareStopped` | 请求后是 `CancelRequested`，不是 `Cancelled` |
| `TaskRegistry2.ExecutorAckMovesToCancelled` | 确认后才 `Cancelled` |
| `TaskRegistry2.AckWithoutRequestRejected` | 未请求就确认被拒绝 |
| `TaskRegistry2.RepeatedCancelIsIdempotent` | 重复取消幂等 |
| `TaskRegistry2.CancelRejectedOnTerminalTask` | 终态不可取消 |
| `TaskRegistry2.UnacknowledgedCancelFailsOnTimeout` | 执行器不响应 → `CANCEL_TIMEOUT` |
| `ContractTask.CancelSemantics`（更新） | 旧断言「cancel 即 Cancelled」按新契约改写，**未放宽** |
| `AiTask.SingleActiveTaskConstraint`（更新） | 未确认前不得重开任务 |
| `AiTask.CapiCancelAndEngineInfo`（更新） | 取消后 state 仍为 1(Running)，ack 后才 4(Cancelled) |

### 1.4 能力边界（如实声明）

**当前仓库内没有 AI 推理执行器。** 因此：

- `cancel()` 的实际能力 = **登记请求**；
- `acknowledgeCancel()` 是给未来执行器用的确认入口；
- 没有执行器时，任务会在心跳超时后判 `Failed(CANCEL_TIMEOUT)`，**不会假装取消成功**。

---

## 二、看门狗独立性核验

### 2.1 发现的问题（真缺陷）

原实现只在这两处推进 tick：

- `NativeEngineBridge.kt :: state()` —— UI 每次刷新
- `NativeEngineBridge.kt :: taskTick()` —— 需外部主动调用

**UI 停止刷新（后台/页面暂停/主线程阻塞）时，超时检测完全停止。**
因此原报告称"超时机制完整"不成立。

### 2.2 修复

`NativeEngineBridge.kt` 新增独立单线程调度器：

```kotlin
private val taskWatchdog = Executors.newSingleThreadScheduledExecutor { ... daemon ... }

init {
    editor = nativeCreate()
    taskWatchdog.scheduleWithFixedDelay({
        synchronized(this) { if (editor != 0L) nativeTaskTick(editor, System.currentTimeMillis()) }
    }, 1000L, 1000L, MILLISECONDS)
}
```

`release()` 中 `shutdownNow()`，线程为 daemon，不阻碍进程退出。

### 2.3 剩余风险（必须记录）

| 风险 | 说明 | 影响 |
|---|---|---|
| 引擎锁竞争 | `NcEditor` 非线程安全，看门狗与 UI 共用对象锁 | UI 线程若长时间持有锁，tick 会延后（延后量 = 该次引擎调用耗时） |
| 进程被杀死 | 看门狗是进程内线程 | 应用被杀后无任何超时检测（属正常语义） |
| 核心层无时钟 | `tick` 完全依赖调用方传 `now_ms` | 若平台层停止调用，仍不检测 |

**结论**：看门狗已与「UI 刷新」解耦，但**未**做到「与进程内所有阻塞完全无关」。
彻底方案需要核心层自带看门狗线程（违反当前零线程设计）或独立进程，列入后续阶段评估。

### 2.4 心跳与总超时独立性

`TaskRegistry::tick` 中两条判定互不干扰，总超时优先：

```cpp
if (totalTimeoutMs > 0 && now - startedAt > totalTimeoutMs)      → TASK_TIMEOUT
else if (now - lastHeartbeat > heartbeatTimeoutMs)                → TASK_HEARTBEAT_TIMEOUT
```

测试：`TotalTimeoutIndependentOfHeartbeat`（心跳很勤但总耗时越界，必须报总超时而非心跳超时）。

---

## 三、结果与资源生命周期核验

### 3.1 发现的问题（真缺陷）

`TaskRegistry::remove` 原先无条件 `erase` —— **可以删掉正在运行的任务**，
之后执行器的进度/结果无处可写，UI 也丢失"仍在运行"的事实。

### 3.2 修复

| 位置 | 改动 |
|---|---|
| `contract/task.cpp :: remove` | `Running` / `CancelRequested` 一律拒绝删除 |
| `contract/task.cpp :: clearResult`（新增） | 只清 `resultType`/`resultPayload` 并 `shrink_to_fit`，保留任务记录 |
| `contract/task.cpp :: completeWithResult` | >1MB 直接判 `Failed(RESULT_TOO_LARGE)`，不写入 |
| `capi.cpp :: nc_task_clear_result` | 暴露释放入口 |

### 3.3 所有权

| 对象 | 所有者 | 释放时机 |
|---|---|---|
| `TaskInfo` | `TaskRegistry`（`std::map` 值语义） | `remove()` 后析构 |
| `resultPayload` | 同上 | `clearResult()` 或 `remove()` |
| `error` / `stageNote` | 同上 | 同上 |
| C ABI 返回的 `char*` | 调用方 | `nc_free_string` |

**无裸指针、无跨层借用**，异常退出时由 `std::map` 析构回收。

### 3.4 测试用例

| 用例 | 验证点 |
|---|---|
| `RemoveRefusesActiveTasks` | 运行中/取消中拒删，终态可删 |
| `ClearResultReleasesPayload` | 4096 字节载荷被清空，记录保留 |
| `RepeatedTerminalTransitionsRejected` | 重复完成/完成后失败/完成后取消全部拒绝 |
| `OversizedResultFailsTask` | >1MB 判失败且不写入 |
| `InterleavedQueryDeleteTransitions` | 乱序查询/删除/状态迁移不互相破坏 |

> 说明：核心层**不是线程安全的**（`editor.h` 头注释明确「同一实例不得多线程并发」）。
> 因此「并发」验证的是**乱序调用一致性**，不是真并发；真并发需平台层串行化，已在看门狗章节记录。

---

## 四、main 最终验证（实测，非分支引用）

**HEAD**：`4cddc9a82a09f4726e6972753629905a41aa8f18`

| 检查 | 结果 | 运行 ID |
|---|---|---|
| `build-and-test` | ✅ success | 42625849823 |
| `ASAN 诊断` | ✅ success | 42625849823 |
| `UB 诊断`（UBSAN） | ✅ success | 42625849823 |
| `Android APK` | ✅ success | 42625849831 |
| APK 冒烟（badging + 签名 + ABI + 原生库） | ✅ success | 42625849831 |
| `Android Cross-Compile`（armeabi-v7a / arm64-v8a / x86_64） | ✅ success | 42625849808 |
| `valgrind 诊断` | ⏳ 运行中（`continue-on-error`，不阻断） | 42625849823 |

---

## 五、提交记录

| 提交 | 内容 |
|---|---|
| `9c18f714` | 两阶段取消 + 安全删除 + 独立看门狗 + 测试 |
| `4cddc9a8` | PR #12 合并入 main |

前序：`2f93e16f` / `a2b7ef1d` / `8ff80afd` / `3109ec37` / `3031e8db` / `d6e9312a`。

---

## 六、剩余风险汇总

| ID | 风险 | 等级 | 处置 |
|---|---|---|---|
| AR-1 | 看门狗受引擎锁影响可能延后 | 中 | 已记录；彻底方案需核心线程，列入后续 |
| AR-2 | 无真实 AI 执行器，取消只能登记 | 中 | 已如实声明，未虚构 |
| AR-3 | 核心层非线程安全，「并发」仅验证乱序一致性 | 中 | 平台层串行化，已记录 |
| AR-4 | `frame_fit` 假成功未修 | P1 | 缺陷单 `docs/DEFECT-frame_fit-noop.md`，待单独授权 |
| AR-5 | `l3-panel.js` 仍显示「等待平台层推理」 | 低 | 无执行器时属如实描述 |

---

## 七、T2.2 范围声明

- 未修改 76 个 Command；
- 未实施 B（C ABI 结构体版本化）与 D（句柄生命周期）；
- 未修改 `frame_fit` 业务实现；
- 未降低任何测试标准，未删除检查项。
