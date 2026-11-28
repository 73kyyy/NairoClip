# S1-3 / S3-1 前置门证据复核（2026-11-23）

## 范围

仓库：73kyyy/NairoClip
分支：work/v3.3.3-s5-native-editor-foundation
复核目的：判断 S5-0B 是否具备依赖矩阵要求的 S1-3 工程保存/恢复与 S3-1 Rational 时间模型前置证据。
本报告只做源码和 CI 证据复核；不解除审批门、不修改运行时行为。

## 结论摘要

| 前置项 | 源码/测试证据 | 当前结论 | 对 S5-0B 的影响 |
|---|---|---|---|
| S1-3 保存/恢复 | NCLIP 字段往返、ID、错误加载不污染、内存快照环形覆盖测试存在 | 部分覆盖；持久化重启链路未证实 | 阻塞 |
| S3-1 Rational | rational.cpp 自实现 128 位中间运算；test_rational.cpp 覆盖基本算术、比较、溢出和取整边界 | 源码/测试覆盖存在；需以当前 HEAD CI 最终结果为准，仍缺完整验收证据 | 阻塞 |
| A-12 审批 | 依赖矩阵仍列为审批门 | 未解除 | 阻塞 |
| S5-0A | 原生 UI 只读盘点已提交 | 已完成只读盘点，不替代 S5-0B 前置条件 | 不解除其他门 |

## 1. S1-3 关键缺口：当前仅有内存恢复存储实现

### 1.1 现有实现

- core/include/nairoclip/core/recovery.h 定义抽象 RecoveryStore，注释明确要求由 Android/iOS/桌面平台层实现。
- 当前仓库树中与 recovery 相关的实现只有 recovery.h、recovery.cpp 和 tests/test_recovery.cpp。
- 唯一具体实现 InMemoryRecoveryStore 使用 std::map<size_t, string> 保存快照。
- AutoSaveManager 的 writeOrder_、nextSlot_、脏状态和时间戳均是进程内成员；进程退出后不会自动恢复。
- recoverLatest() 依赖当前 manager 实例内的 writeOrder_。新进程重建 manager 后，若没有持久化索引/序列信息，就无法按真实写入顺序恢复。
- 当前 tests/test_s1_3_persistence.cpp 的往返测试调用 nc_project_save / nc_project_load 并在内存字符串之间重载；自动保存用例使用 InMemoryRecoveryStore。这证明序列化和内存状态机行为，不等价于文件落盘、进程重启后恢复。

### 1.2 已有测试能证明什么

- RoundTripIsFieldIdentical：丰富工程的序列化文本加载后再次保存逐字节一致。
- RepeatedCyclesStayStable：多轮内存加载/保存无漂移。
- AllocatorRestoredAfterLoad：载入后新对象 ID 不与已有对象冲突。
- FailedLoadDoesNotPollute：错误载入不污染当前内存工程。
- RecoverLatestReturnsNewestAfterRingWrap：同一进程内、同一内存 store 中环形槽位覆盖时能选最新快照。
- SnapshotIntegratesWithSerialization：内存快照内容能被 NCLIP 解析。

### 1.3 尚未证明的 S1-3 验收项

1. Android 文件系统 RecoveryStore 的原子写入（临时文件 + fsync/rename 或等效保证）。
2. 进程重启后发现恢复快照、读取索引并选出最新有效快照。
3. 快照元数据与 payload 一致性、截断/损坏文件拒绝和回退到上一份有效快照。
4. 正常保存/关闭后清理恢复状态的真实文件行为。
5. Android 存储空间不足、权限/IO 失败、进程中断写入的错误处理。
6. 真实工程文件落盘、关闭/杀进程、重新启动并逐字段恢复的集成测试。
7. Project::id 保留的独立明确断言（现有测试覆盖 allocator 不冲突，但本次复核未找到足以证明工程 ID 在磁盘重开后保留的设备/文件集成证据）。

因此，S1-3 不应标记为完整通过。

## 2. S3-1 Rational 复核

已核对：
- core/src/rational.cpp：统一自实现二补码 128 位中间量，避免 MSVC 缺少 __int128 时退化为 int64 运算。
- core/include/nairoclip/core/rational.h：定义 den > 0、最简分数、0/1 的类型不变量及饱和溢出查询接口。
- tests/test_rational.cpp：包含规范化、基本运算、比较、取整、乘法中间精度、负分母极值、加减乘溢出饱和、29.97fps 精确运算、超大分母取整及负半值取整测试。
- S5 Validation run 45099622968（盘点文档提交 19864594）已全部通过：Kotlin 编译/JVM 单元测试、Host CMake/CTest、Android NDK arm64 与 JNI 编译。
- 最近一次完整 CI run 45097377387（源码提交 d72666d4）已全部通过：build-and-test、ASan（含 S1 生命周期定向回归与全量 ASan 测试）、UBSan。其后 f7be63cf、19864594 与本报告提交均为文档变更，没有修改源码。
- 文档提交 19864594 触发的 CI run 45099622884 在本次复核时仍处于构建阶段；由于它不包含源码变化，当前可用的最近完整源码 CI 证据仍为 45097377387。不得把此证据扩大解释为设备或完整 S1-3 验收。

本轮确认 Rational 代码与专项测试存在，且对应源码提交 d72666d4 的 Host/CI/ASan/UBSan 自动化工作流通过；最新文档 HEAD 的 Android Kotlin/Host/NDK 验证也通过。但这仍不等于 S3-1 全部验收通过：还需核对完整时间模型调用方边界、溢出标志消费策略、跨平台一致性及导出/时间轴集成证据。

## 3. 门禁处理

- S5-0B 的依赖矩阵明确要求 S1-3 与 S3-1。
- A-12 仍是 S1 批次审批门，不能因“继续开发”自动视为解除。
- 不因 S5-0A 只读盘点完成而跳过 S5-0B 的依赖冻结。
- 不切换 Launcher Activity，不改 AndroidManifest，不删除 WebView，不合并 PR #32。

## 4. 下一步允许的工作

1. 等待当前 HEAD CI 完成，记录各 job 的实际结论。
2. 补齐 S1-3 的 Android 文件级 RecoveryStore 设计与进程重启验收计划；在相应审批/文件所有权门解除前，不擅自改动受保护文件。
3. 完成 S3-1 专项边界审查和 CI 证据对账。
4. 只有当 S1-3、S3-1 与相关审批均有真实证据解除后，才进入 S5-0B 接口冻结及后续宿主导航工作。

最终判断：当前 S5-0A 盘点可作为已完成的只读交付；S5-0B 仍被 S1-3 持久化恢复证据、S3-1 最终验收证据和 A-12 审批门阻塞。