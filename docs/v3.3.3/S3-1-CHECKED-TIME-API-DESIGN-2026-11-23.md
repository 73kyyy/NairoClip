# S3-1 Checked 时间运算与导出错误传播设计提案（2026-11-23）

- 仓库：`73kyyy/NairoClip`
- 分支：`work/v3.3.3-s5-native-editor-foundation`
- 依据：`S3-1-RATIONAL-CALLSITE-AUDIT-2026-11-23.md`
- 性质：设计提案；未修改运行时代码，不改变现有 API。

## 1. 设计目标

1. 生产关键路径不得把饱和结果误当成合法时间。
2. 错误必须属于本次运算/本次导出计划，而不是依赖线程局部 sticky 标志的历史状态。
3. 现有 40+ 调用方继续编译，避免一次性破坏兼容 API。
4. 导出计划只在所有时间运算与结构验证均成功时才可执行。
5. 错误具备稳定机器可读类型和可定位上下文，便于 UI 显示、日志和测试。

## 2. 推荐 API 形态

### 2.1 结果类型

建议在 Rational 基础层增加轻量结果类型：

```cpp
enum class RationalError {
    None,
    Overflow,
    DivisionByZero,
    InvalidDenominator
};

struct CheckedRational {
    Rational value{};
    RationalError error = RationalError::None;
    bool ok() const { return error == RationalError::None; }
};
```

实际落地时需确认 C++ 标准和现有错误类型约定。若项目已有统一 Result/Error 类型，应复用现有类型，不再引入平行体系。

### 2.2 Checked 运算函数

新增独立 checked 函数，不改变现有运算符语义：

- `checkedRationalMake(num, den)`
- `checkedAdd(a, b)`
- `checkedSub(a, b)`
- `checkedMul(a, b)`
- `checkedDiv(a, b)`
- `checkedFloorToInt(r)`
- `checkedRoundToInt(r)`

要求：
- 每次调用独立返回本次错误，不读取或依赖之前的 thread-local 标志。
- 溢出不得只返回饱和值并标记成功。
- 分母为 0、除数为 0 与 int64 溢出分别报告。
- 结果成功时仍保持 `den > 0`、最简分数、零为 `0/1`。
- 现有 `operator+/-/*//`、`floorToInt`、`roundToInt` 暂时保持兼容；内部可逐步复用 checked 实现，但兼容包装的历史语义需通过现有测试锁定。

## 3. 时间转换 API

建议增加 checked 时间转换入口：

- `checkedTimeAtFrame(fps, frame)`
- `checkedFrameAtTime(fps, time)`
- `checkedToMicroseconds(time)`

每个函数返回明确成功/失败，不把 INT64_MAX/MIN 饱和值作为正常帧号或微秒数。

输入验证：
- 帧率必须为正有理数。
- 帧索引按现有语义处理负数；如果产品不允许负时间，应在业务边界显式拒绝，而不是隐式更改数学 API。
- 时间转帧的 floor 规则必须保持向负无穷取整。
- 微秒舍入规则必须明确为当前 roundToInt 的“0.5 向上”语义，并覆盖负值边界。
- 对转换结果超出 int64 的情况返回 Overflow。

旧 API 继续保留兼容入口；导出和工程校验等关键路径改用 checked 版本。

## 4. 导出计划错误传播

建议新增 `compileExportPlanChecked(project, sequence)`，返回：

- 成功：完整 ExportPlan。
- 失败：结构化错误列表；每条包含错误码、trackId/clipId/transitionId（可用时）、字段路径和简短说明。

检查必须在计划编译期间完成，覆盖：
- `Clip::end()`：start + duration。
- `Clip::sourceOut()`：sourceIn + duration × speed。
- 速度曲线积分与源时间映射边界。
- 复合片段展开后的嵌套序列时长和偏移累计。
- 转场 start/end。
- 序列总时长。
- 时间转换为帧号、微秒及音视频输出时间戳。
- 音频淡入淡出时间边界。

原则：
- 任一关键时间运算失败，整个计划编译失败；不能用部分饱和结果继续生成可执行计划。
- 结构校验与时间算术校验分开报告，避免把算术错误混成普通区间错误。
- 旧 `compileExportPlan()` 可保留兼容包装，但生产导出入口必须调用 checked 版本；若兼容包装遇到错误，应采用项目统一的失败表达，不能悄悄返回“看起来可用”的计划。
- `exportPlanValid()` 保持纯结构校验或升级为明确接收 checked compile 结果；不能依赖 thread-local 标志来推断计划是否安全。

## 5. 线程局部溢出标志的定位

`rationalLastOpOverflowed()` / `rationalResetOverflowFlag()` 可以继续作为诊断、旧调用方兼容与调试辅助，但不应成为新的安全边界。

若保留：
- 文档必须明确它是线程局部 sticky 诊断状态。
- 调用方若用它诊断单段旧 API，必须在操作前 reset、操作后立即读取，且禁止在两者之间执行无关 Rational 运算。
- 新增 checked API 不应设置/清除或依赖该标志，除非明确规定双轨兼容行为并测试。
- 不得在并发任务之间共享“本次导出”标志状态。

## 6. 测试矩阵

### Rational 单元测试
- 每种算术运算的正向、负向 int64 边界。
- 乘法中间值超出 int64、最终约分后仍在界内的情况。
- 分母为 0、除数为 0、负分母、INT64_MIN 归一化。
- checked 调用不受此前 sticky 标志污染。
- 连续 checked 成功/失败调用相互隔离。
- 旧 API 行为兼容回归。

### Time 单元测试
- 24/25/30/30000÷1001/60000÷1001 帧率。
- 极大帧号与极端时间的 timeAtFrame/frameAtTime/toMicroseconds。
- 负时间 floor 与负数舍入规则。
- 微秒边界前后 1 单位。
- 非法帧率、饱和路径拒绝。

### ExportPlan 集成测试
- clip end/sourceOut 溢出。
- 多片段累计导致 sequence duration 溢出。
- 嵌套/复合序列偏移溢出。
- transition end 溢出。
- 速度曲线映射和长时长变速边界。
- 导出计划在任何 checked 时间错误时均不可执行。
- 错误包含对应对象 ID 和字段路径。
- 正常工程导出计划与既有基线逐字段一致。

### A/V 一致性测试
- 23.976/29.97/59.94 fps。
- 长时长、非整数帧率、速度变化、倒放、定格。
- 视频帧 PTS、音频块 PTS、微秒时间戳与计划时长之间的容差。
- 首帧、末帧、转场边界、片段交界处无重复/遗漏帧。

## 7. 迁移顺序

1. 先确认统一 Result/Error 类型、C++ 标准与 API 命名。
2. 新增 checked Rational API 和单元测试，不删除旧 API。
3. 新增 checked Time 转换和测试。
4. 新增 checked ExportPlan 编译路径，保留兼容包装。
5. 将真正执行导出的入口迁移到 checked 计划。
6. 迁移预览/时间轴/音频等生产关键调用点。
7. 运行 Host、ASan、UBSan、Android NDK 交叉编译与完整回归。
8. 用调用点清单证明生产关键路径不再将饱和值当作有效时间后，才重新评估 S3-1。

## 8. 尚待确认事项

- 项目是否已有统一 Expected/Result 类型及其命名约定。
- C++ 标准是否允许直接使用 `std::expected`；不可假设当前工具链均支持。
- 旧 API 在遇到溢出时应保持饱和返回，还是在兼容层断言/记录错误；不能未经调用方审计改变。
- `compileExportPlan()` 现有调用点数量及其是否均可迁移到 checked 入口。
- 预览与导出是否共享同一计划编译路径，需从真实调用点继续核实。

## 9. 审批边界

本提案不是实现，不改变 S3-1 状态，也不解除 S5-0B 依赖。实施前仍需按项目文件所有权、审批矩阵和依赖 DAG 确认可修改范围。未获授权前不修改受保护源文件。
