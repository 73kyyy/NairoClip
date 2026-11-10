# NairoClip B1 最终验收报告

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-B1-FINAL-ACCEPTANCE |
| 验收对象 | B1 主体 + B1 补充验收（三项绕过整改、expectedRevision、Event、Alpha） |
| 验收基准 | `main` @ `28252751f6d04377514395ee6d1c8ec4271cc591`（本轮实测，非历史引用） |
| 仓库 | `github.com/73kyyy/NairoClip` |
| 工作区 | 干净；本轮只读审计，未改动任何生产源码 |
| 结论 | **有条件通过** —— 允许进入 B2 规划与实施；GPU Alpha 相关项明确未验证 |

---

## 0. 验收方法与边界

- 所有结论均基于**当前 main 的真实源码**与**GitHub Actions 实际运行记录**，
  不以历史报告作为证明（历史报告只在「与源码是否一致」这一项被核对）。
- 逐文件核对：本地工作副本与 main @ `28252751` 的 11 个关键文件
  SHA-256 **逐一相同**（见 §1.2），因此本地审计结论对应当前 HEAD。
- 本轮**未修改**任何生产源码、CI、测试或构建配置。

---

## 1. 基线核验

### 1.1 仓库状态（实时查询）

| 项 | 值 |
|---|---|
| 仓库 | `73kyyy/NairoClip` |
| 默认分支 | `main` |
| **当前 HEAD** | **`28252751f6d04377514395ee6d1c8ec4271cc591`** |
| 最近推送 | `2026-11-10T12:41:17Z` |

> 说明：任务书给出的历史基准 `28252751` 与实测一致；但本报告仍按「先重新获取」
> 执行，未默认仓库停在该提交。

### 1.2 本地副本与远端一致性（逐文件 SHA-256）

| 文件 | 结果 |
|---|---|
| `core/src/editor.cpp` | ✅ 一致（36,591 B） |
| `core/src/capi.cpp` | ✅ 一致（383,650 B） |
| `core/src/canvas.cpp` | ✅ 一致（8,740 B） |
| `core/src/frame_fit.cpp` | ✅ 一致（22,879 B） |
| `core/src/contract/dispatch.cpp` | ✅ 一致（6,683 B） |
| `tests/test_framework.h` | ✅ 一致（11,925 B） |
| `tools/vg_summary.py` | ✅ 一致（6,191 B） |
| `.github/workflows/valgrind.yml` | ✅ 一致（7,502 B） |
| `.github/workflows/ci.yml` | ✅ 一致（35,920 B） |
| `tests/test_b1_supplement.cpp` | ✅ 一致（16,769 B） |
| `docs/NC-B1-FOLLOWUP-REPORT.md` | ✅ 一致（19,632 B） |

---

## 2. 逐项核实（任务 A 的 8 项）

### 2.1 ① B1 主体与补充验收是否完整

**结论：完整。**

| 交付物 | 位置 | 状态 |
|---|---|---|
| B1 主体验收 | `docs/NC-C-B1-ACCEPTANCE-REPORT.md` | 存在，含 S1–S8 补充章节 |
| B1 后续报告 | `docs/NC-B1-FOLLOWUP-REPORT.md` | 存在（19,632 B） |
| 三个整改命令 | `core/include/nairoclip/core/commands.h` | `RenameProjectCommand` / `SetCanvasCommand` / `SetCanvasBackgroundCommand` **各 1 处定义** |
| 命令实现 | `core/src/commands.cpp` | 三个 `::apply` / `::revert` / `::isNoOp` 齐备 |

证据命令：

```
grep -c "class RenameProjectCommand|class SetCanvasCommand|class SetCanvasBackgroundCommand" \
     core/include/nairoclip/core/commands.h     → 3
```

---

### 2.2 ② 三个绕过 Command 的操作是否已整改

**结论：已整改，无残留直接写入。**

对三个 C ABI 函数体逐一扫描「是否仍有工程字段直接赋值」：

| C ABI 函数 | 直接写 `project.name` / `main->width` / `main->height` / `main->backgroundRgba` | 结论 |
|---|---|---|
| `nc_set_project_name` | **0 处** | ✅ 已转发 `renameProject()` |
| `nc_set_project_canvas` | **0 处** | ✅ 已转发 `setCanvasSize()` |
| `nc_set_background_color` | **0 处** | ✅ 已转发 `setCanvasBackground()` |

当前实现（`core/src/capi.cpp`）：

```cpp
return applyResultKeepingLastError(editor->editor->renameProject(text, 0));
return applyResultKeepingLastError(editor->editor->setCanvasSize(
    main->id, width, height, CanvasAdaptMode::Preserve));
return applyResultKeepingLastError(editor->editor->setCanvasBackground(
    main->id, CanvasBackground::fromRgba32(rgba)));
```

三个门面方法内部均经 `commitChecked()` → `UndoStack::push`（`core/src/editor.cpp:755 / 764 / 797`）。

---

### 2.3 ③ Revision / expectedRevision / Conflict / Undo / Event 的实现与边界

#### 实现

| 能力 | 实现位置 | 关键行为 |
|---|---|---|
| Revision 唯一来源 | `TimelineEditor::editRevision()` → `UndoStack::editRevision()` | **无第二计数器** |
| expectedRevision | `core/src/editor.cpp :: commitChecked` | 非 0 时比对当前 revision，不等即返回 `ErrorCode::Conflict` |
| Conflict 错误码 | `core/include/nairoclip/core/types.h`（`ErrorCode::Conflict`）+ `capi.h:68`（`NC_ERROR_REVISION_CONFLICT = 9`） | C ABI 层可辨识 |
| Undo/Redo | `UndoStack::undo / redo` | 与既有 71 个门面操作共用同一栈 |
| Event | `core/src/editor.cpp :: commit → publishTimelineChanged` | 唯一事件源 |

#### 边界（必须如实记录）

| 边界 | 事实 |
|---|---|
| `expectedRevision = 0` | **不校验**。这是 P1 契约既定语义；三个**旧** C ABI 入口没有 revision 参数，因此恒传 0。需要并发控制的调用方必须走 `_ex` 变体。 |
| 事件发布条件 | 三条件同时满足才发：① `UndoStack::push` 返回 Ok ② revision 前后不同 ③ 已接线 `EventBus` |
| 事件覆盖范围 | `TimelineEditor::commit()` 是唯一提交点 → **所有**经门面提交的命令都会发事件（含旧的 71 个门面操作）。这是 B1 补充验收引入的行为变化。 |
| `CommandDispatcher` | 已**移除**自身 `bus_.publish`，改为 `setCorrelationId()`；`lastEvent_` 仅用于查询，不重复发布 |
| no-op | `isNoOp` 拦截 → 不入栈、revision 不变、**不发事件** |
| 失败 | `apply` 失败不入栈、不改 revision、不发事件 |

#### 测试证据（`tests/test_b1_supplement.cpp`）

| 用例 | 覆盖 |
|---|---|
| `B1Rev.MatchingRevisionAccepted` | revision 匹配 → 成功且递增 |
| `B1Rev.MismatchedRevisionRejectedWithoutSideEffect` | 不匹配 → Conflict；名称/revision/栈深/事件数全不变 |
| `B1Rev.ZeroMeansNoCheck` | 0 → 跳过校验 |
| `B1Rev.CanvasAndBackgroundHonorRevision` | 画布/背景同样生效 |
| `B1Rev.CapiExRejectsConflict` | C ABI 返回 `NC_ERROR_REVISION_CONFLICT`，数据未变 |
| `B1Rev.CapiLegacyStillWorksWithoutRevision` | 旧接口语义不变 |
| `B1Event.SuccessPublishesExactlyOnce` | 成功恰好 1 条 |
| `B1Event.FailurePublishesNothing` / `NoOpPublishesNothing` / `RevisionConflictPublishesNothing` | 三种情况均 0 条 |
| `B1Event.DispatcherPathPublishesExactlyOnceNoDuplicate` | dispatcher 路径不重复 |
| `B1Event.CorrelationIdPropagated` / `EachCommandGetsDistinctEventId` | 关联 ID 与唯一性 |

---

### 2.4 ④ 画布比例 / 尺寸 / 适配 / 背景 RGBA / Alpha

| 项 | 实现 | 证据 |
|---|---|---|
| 比例预设 | 10 项（1:1 / 4:5 / 3:4 / 9:16 / 2:3 / 16:9 / 4:3 / 3:2 / 21:9 / 2.39:1） | `core/src/canvas.cpp :: canvasPresets()`，`grep -c '{"'` = 10 |
| 宽高权威 | `Sequence::width/height` 唯一；比例由宽高用 `Rational` 计算，**不另存** | `canvasAspectRatio()` |
| 尺寸校验 | 四类：正数 / 16..16384 / 像素总量 ≤ 8192² / 乘法溢出 | `validateCanvasSize()` |
| 比例锁定 | `solveHeightForWidth` / `solveWidthForHeight`，取整后 `exact` 如实返回 | 用例 `RatioLockSolveExact` / `RatioLockSolveWithRounding` |
| 适配模式 | Preserve / Fit / Fill，只改 `ClipTransform.scale` | `canvasAdaptScale()` |
| 背景 RGBA | straight alpha、`0xRRGGBBAA` | `CanvasBackground` / `flattenOnto()` |
| **Alpha 真实合成** | `blendStraightOver()` 整数实现，替换 **4 处**写入点 | `core/src/frame_fit.cpp:171 / 241 / 416 / 528` |

Alpha 测试：

| 用例 | 覆盖 |
|---|---|
| `B1AlphaMatrix.RequiredAlphaLevelsMatchReference` | α ∈ {0,1,64,128,192,255} 与**独立浮点参考**比对 |
| `B1AlphaMatrix.EndpointsAreExact` | α=0 纯背景 / α=255 纯前景 |
| `B1AlphaMatrix.MultipleLayersCompositeInOrder` | 多层叠加顺序 |
| `B1AlphaMatrix.SemiTransparentBackgroundPropagates` | 半透明背景 outA |
| `B1Blend.RenderedFrameBlendsForegroundOverBackground` | 渲染级真混合 |
| `B1Blend.OpaqueSourceOverwritesExactly` | α=255 逐字节等于源（**零回归证据**） |

---

### 2.5 ⑤ CI 是否真正成功，是否存在被跳过或允许失败的关键检查

**结论：run 级 success 属实；但必须披露——ASAN 与 UBSAN 是 `continue-on-error: true`，不是阻断项。**

| Job | `continue-on-error` | 语义 | 最终 HEAD 实测 |
|---|---|---|---|
| `build-and-test` | **无（阻断）** | 编译 + 1764 用例 + 静态检查 | ✅ success |
| `asan-diagnose` | `true` | 非阻断诊断 | ✅ success |
| `ub-diagnose` | `true` | 非阻断诊断 | ✅ success |

运行证据（**main @ `28252751`**）：

| 工作流 | run ID | run 级 | 作业结果 |
|---|---|---|---|
| `CI` | **42815746913** | ✅ **success** | build-and-test ✅ / ASAN ✅ / UB ✅ |

链接：https://github.com/73kyyy/NairoClip/actions/runs/42815746913

**是否存在"被跳过"的检查**：

- `build-and-test` 内含 6 个静态检查步骤（头文件结构、条件编译、前端静态、
  Valgrind 汇总工具自检、C ABI 自检、C ABI 调用点），全部为 `success`，
  无 `skipped`。
- `valgrind` 已从 `ci.yml` 移出（见 §2.6），因此主 CI 内**不再有** valgrind 步骤 ——
  这是**迁移**不是跳过。

**披露项**：ASAN/UBSAN 虽实际通过，但设计上不阻断合并。
若要求"关键检查阻断"，需另行授权把它们改为阻断（本报告不擅自改 CI）。

---

### 2.6 ⑥ Valgrind 是否完整执行 1764 用例 / 16 分片全覆蓋 / XML 汇总可信

**结论：全部成立，但该结果是 `02cfbdf1` 上的运行；与最终 HEAD 的差异已核实为纯文档（见 §3）。**

| 项 | 实测 |
|---|---|
| run | **42805407989**（`Valgrind Memcheck`），run 级 ✅ **success** |
| 分片 | **16 / 16 全部 success** |
| 全量用例总数 | **1764** |
| 实际执行合计 | **1764**（`executed == total` 校验通过） |
| 失败用例 | **0** |
| 内存错误条目 | **0** |
| 泄漏分类 | definitely=0 indirectly=0 possibly=0 still_reachable=0 |

分片耗时区间 0:36 ~ 52:56（最重分片 52:56），1 个 job 已不可能在旧 60 分钟限制内完成。

**XML 汇总可信性**：

| 检查 | 证据 |
|---|---|
| 汇总不读文本日志 | `tools/vg_summary.py` 解析 `vg-shard-*.xml`；文本日志因 `--xml=yes` 为空（已踩坑并修正） |
| 覆盖度机器校验 | `coverage` 子命令：缺分片 / `executed != total` / 有用例失败 → 一律 exit 1 |
| 截断 XML 不当 0 | 自检用例 `test_truncated_xml_is_not_silent_zero` |
| 工具本身被阻断 CI 覆盖 | `ci.yml` 的 `build-and-test` 内含「Valgrind 汇总工具自检」步骤 → 实测 success |
| 自检规模 | `tools/test_vg_summary.py` **10 个用例** |

---

### 2.7 ⑦ Android APK 构建与冒烟测试

| 项 | 证据 |
|---|---|
| run | **42814098289**（`Android APK`，main 上 workflow_dispatch） |
| run 级 | ✅ **success** |
| `Build APK` job | ✅ success |
| **APK 冒烟步骤** | ✅ **success**（badging + 签名 + ABI + 原生库断言） |

> 说明：该 run 的 `head_sha` 为 `02cfbdf1`。因 `28252751` 相对它**只改了 docs/ 一个文件**
> （见 §3），APK 输入完全相同，故结果适用。若要求严格同 SHA，可在最终 HEAD 上重新
> dispatch（约 25 分钟），本报告不擅自扩大范围。

---

### 2.8 ⑧ `docs/NC-B1-FOLLOWUP-REPORT.md` 结论是否与源码/工作流/运行记录一致

逐条抽查：

| 报告结论 | 核实结果 |
|---|---|
| 「valgrind 已拆出独立工作流」 | ✅ `.github/workflows/valgrind.yml` 存在；`ci.yml` 仅 3 个 job |
| 「16 分片」 | ✅ `strategy.matrix.shard` = `[0..15]`，`NC_VALGRIND_SHARDS: 16` |
| 「1764 用例 / 0 错误」 | ✅ run 42805407989 汇总日志一致 |
| 「`--xml=yes` 会让文本日志为空」 | ✅ 代码中保留该注释与解析 XML 的实现 |
| 「GPU 后端不存在」 | ✅ 见 §4，检索命中 0 |
| 「`IRenderBackend` 无派生类」 | ✅ 全仓 `: public IRenderBackend` 命中 0 |
| 「`tests/test_main.cpp` 不支持 argv，但支持环境变量分片」 | ✅ `test_framework.h::runAll()` 读 `NC_TEST_SHARDS/SHARD/SKIP/MAX/INCLUDE/EXCLUDE` |

**未发现报告与源码/运行记录不一致之处。**

---

## 3. Valgrind 版本对应关系（任务书特别要求）

| 项 | 值 |
|---|---|
| Valgrind 运行所在提交 | `02cfbdf1` |
| 最终交付 HEAD | `28252751` |

### 3.1 两提交差异（GitHub Compare API 实测）

```
compare 02cfbdf1...28252751
  commits: 2
  files:   1
    modified  +50  -4   docs/NC-B1-FOLLOWUP-REPORT.md
```

**唯一差异是一个 Markdown 文档**（+50/−4 行）。
**没有任何** `.cpp` / `.h` / `.kt` / `.js` / `tests/` / `tools/` / `.github/workflows/` 文件变化。

### 3.2 结论

Memcheck 覆盖范围内的**代码、测试、检测脚本三者均未变化**，
因此 `02cfbdf1` 的 Valgrind 结论对 `28252751` 依然适用。

**但本报告不把旧提交的结果冒充最终 HEAD 的结果**，表述为：

> 「Valgrind 完整检测执行于 `02cfbdf1`；`02cfbdf1 → 28252751` 仅修改
> `docs/NC-B1-FOLLOWUP-REPORT.md` 一个文件（+50/−4），不触及 Memcheck
> 覆盖范围内的任何代码、测试或脚本，故结论适用。」

### 3.3 若要求严格同 SHA

可在 `28252751` 上重新 dispatch `Valgrind Memcheck`（约 60 分钟墙钟）。
本报告不擅自执行（属额外资源开销），列为可选动作。

---

## 4. 通过项 / 未验证项 / 遗留技术债

### 4.1 通过项

| # | 项 | 证据 |
|---|---|---|
| 1 | 三个绕过项已整改，无残留直接写入 | §2.2（0 处命中） |
| 2 | 三命令接入 `commitChecked` | §2.2（editor.cpp:755/764/797） |
| 3 | expectedRevision + Conflict 真实生效 | §2.3 + `B1Rev.*` 6 用例 |
| 4 | 事件单源、成功一次、失败/no-op/冲突不发 | §2.3 + `B1Event.*` 7 用例 |
| 5 | Undo/Redo 与既有栈兼容 | `B1Rename.UndoRedoRestoresName` 等 |
| 6 | 画布 10 预设 / 自定义 / 锁定 / 适配 | §2.4 |
| 7 | Alpha straight 合成 + 6 档矩阵 | §2.4 + `B1AlphaMatrix.*` |
| 8 | 主 CI run success（最终 HEAD） | §2.5 run 42815746913 |
| 9 | Valgrind 1764/1764、0 错误、0 泄漏 | §2.6 run 42805407989 |
| 10 | APK 构建 + 冒烟 success | §2.7 run 42814098289 |

### 4.2 未验证项（不得计入"B1 已通过"）

| # | 项 | 原因 | 需要什么 |
|---|---|---|---|
| U-1 | **GPU Alpha 一致性** | 仓库无 GPU 渲染后端（`eglCreate`/`glTexImage`/`glCreateShader`/`glBindFramebuffer` 全 0 命中；`IRenderBackend` 无派生类） | 先实现 GPU Render Core（G0→G1），再用真实硬件验证 |
| U-2 | **GPU 硬件真机矩阵** | 无自托管 GPU Runner、无 Android 真机接入 | 真机设备农场或自托管 Runner |
| U-3 | GPU 路径 Valgrind 覆盖 | memcheck 只覆盖 CPU 路径 | 随 U-1 |
| U-4 | 严格同 SHA 的 Valgrind/APK | 二者运行于 `02cfbdf1`；差异为纯文档 | 可选：在 `28252751` 重新 dispatch |

### 4.3 遗留技术债

| ID | 事项 | 影响 | 归属 |
|---|---|---|---|
| TD-1 | `frame_fit` JNI 分支仍 return `Ok` 而不调 C ABI | 切换适配模式画面不变却显示成功 | 独立缺陷单 `docs/DEFECT-frame_fit-noop.md` |
| TD-2 | 剩余 **71** 个门面 Command 未接入命令信封（只有 3 个走 `nc_apply_command`） | 旧接口无 commandId/expectedRevision | B3 |
| TD-3 | 9 组重复 op 未收敛 | 同一操作两个审计身份 | **B2** |
| TD-4 | ASAN / UBSAN 为 `continue-on-error` | 非阻断 | 需单独授权 |
| TD-5 | 看门狗与引擎对象锁竞争（T2.2 遗留） | tick 可能延后 | 技术债 |
| TD-6 | 进程被杀后任务中断与恢复未实现 | — | 技术债 |
| TD-7 | 核心层非线程安全（`TimelineEditor` 单线程） | 并发需平台层串行化 | 技术债 |
| TD-8 | `valgrind` 作业依赖 16 分片（约 60 分钟） | 资源开销 | 可接受 |

---

## 5. 是否允许进入 B2

**允许，但附两条前置约束：**

1. **GPU Alpha 未验证项不得计入 B1 通过范围**，也不得作为 B2 的验收依据；
2. B2 实施前必须先完成本轮的《B2 实施任务规划》并获人工批准（本轮已交付）。

**B1 判定：有条件通过。**
- 三项源码整改、契约能力、CI/Valgrind/APK 证据充分 → **通过**；
- GPU Alpha 与真机矩阵 → **未验证**，不得表述为"B1 已全部完成"。

---

## 6. 停止声明

本轮仅只读审计与文档交付：

- 未修改生产源码、CI、测试、构建配置；
- 未执行 B2/B3/B4/D；
- 未编写 GPU 生产代码，未启动 G0.5；
- 未合并任何源码变更。

**等待人工审查。**
