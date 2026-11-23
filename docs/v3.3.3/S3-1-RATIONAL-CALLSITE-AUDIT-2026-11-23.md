# S3-1 Rational 时间模型调用链审查（2026-11-23）

- 仓库：`73kyyy/NairoClip`
- 分支：`work/v3.3.3-s5-native-editor-foundation`
- 审查基线：`22084cf3909c65b376046d96baf3270ab69ea845`
- 范围：只读检查 Rational/Time、Sequence/Clip、导出计划与导出执行的调用关系；未修改运行时代码。

## 1. 结论

Rational 的底层算术已经具备自实现 128 位中间值、饱和返回和线程局部溢出标志；但当前调用链尚未把“溢出诊断”转化为时间轴/导出边界的强制拒绝策略。现有测试证明若干算术边界和常规帧率换算，不足以证明工程时间值在所有生产调用链中均安全。

**S3-1 暂不能验收为完整通过，仍不能解除 S5-0B 前置门。**

## 2. 已确认的实现事实

### 2.1 Rational API

文件：`core/include/nairoclip/core/rational.h`

- `Rational` 表示分子/分母；约定 `den > 0`、最简形式、零为 `0/1`。
- 溢出采用饱和返回，并通过 `rationalLastOpOverflowed()` / `rationalResetOverflowFlag()` 查询和清除线程局部标志。
- API 注释说明调用方（例如导出校验）可以据此拒绝执行。

文件：`core/src/rational.cpp`

- 算术中间值使用自实现二补码 128 位结构，不依赖编译器 `__int128`。
- 超出 int64 表示范围时饱和并置标志。
- 标志是 sticky 状态：发生溢出后保持为 true，直到显式 reset；普通成功运算不会自动清除此前的标志。
- 除零和非法分母路径返回零值；当前 API 没有独立错误结果类型，调用方必须明确这些退化语义是否适用于其业务。

## 3. 已确认的调用链缺口

### 3.1 时间转换接口没有暴露溢出失败

文件：`core/src/time.cpp`

- `timeAtFrame()` 通过 `Rational::make(frame) / fps.value` 计算时间。
- `frameAtTime()` 通过 `floorToInt(t * fps.value)` 计算帧号。
- `toMicroseconds()` 通过 `roundToInt(t * 1,000,000)` 转换为微秒。
- 以上函数均直接返回数值，没有在内部清理旧标志、检查本次计算是否溢出，或向调用方返回明确错误状态。

风险：如果上游时间值超出可表示范围，饱和值可能被当作普通帧号/微秒继续传递。由于溢出标志为线程局部 sticky 标志，调用方若不在明确边界 reset 并检查，也无法可靠区分本次转换是否发生溢出。

### 3.2 Clip/Sequence 时间累计会传播饱和值

文件：`core/include/nairoclip/core/clip.h`

- `Clip::end()` 直接返回 `start + duration`。
- `Clip::sourceOut()` 直接返回 `sourceIn + duration * speed`。
- `Clip::contains()` 依赖 `end()` 比较。

文件：`core/src/sequence.cpp`

- `Sequence::duration()` 遍历所有片段，反复调用 `c.end()` 并选取最大值。
- 该过程不检查 Rational 溢出标志，也没有错误返回通道。

风险：异常或损坏工程中的极端时间值可能使 end/sourceOut/sequence duration 饱和；后续逻辑仍可能把结果当作有效时间边界。

### 3.3 导出计划验证没有拒绝 Rational 溢出

文件：`core/src/export_plan.cpp`

- `compileExportPlan()` 计算 `sequence.duration()`、片段 `end()`、转场 `start + duration` 等时间表达式。
- `exportPlanValid()` 检查宽高、帧率正值、总时长非负、片段区间和速度等结构约束。
- `exportPlanValid()` 当前没有调用 `rationalResetOverflowFlag()` / `rationalLastOpOverflowed()`，也没有验证编译计划构造阶段是否发生过溢出。

风险：导出计划可能由饱和时间值构造，之后仅凭正负和区间比较仍通过部分结构检查。需要在明确的计划编译/校验边界建立错误传播，不能依赖全局线程局部标志的偶然状态。

### 3.4 导出执行混合了精确有理数与浮点时间路径

文件：`core/src/export_executor.cpp`

- 主视频遍历使用 `timeAtFrame(fps, frameIndex)`，以 Rational 生成时间戳。
- 片段速度曲线的 `mapSourceTime()` 路径将时间转成 double，执行固定步数积分，再转回微秒 Rational。
- 调整图层帧索引另一路径使用 `frame.timestamp.toDouble() * fps.value.toDouble() + 0.5`。
- 音频进度也使用 double 比值。

判断：浮点用于插值、进度或数值积分可以是有意的局部近似，但必须明确其精度边界，并确保最终帧边界、音视频时间戳及导出时长仍由同一时间基准约束。当前证据不足以宣称所有视频/音频/效果帧索引路径完全统一。

## 4. 当前测试覆盖边界

文件：`tests/test_rational.cpp`

- 覆盖规范化、常规算术、比较、取整、部分 int64 极值、饱和与溢出标志、29.97fps 精确运算等。
- 这些测试验证 Rational 底层算术的重要边界，但不验证工程级错误传播。

文件：`tests/test_time.cpp`

- 当前覆盖 30fps、30000/1001 帧率、常规微秒转换、时间格式化。
- 当前文件没有覆盖极端时间值下 `frameAtTime()`、`timeAtFrame()`、`toMicroseconds()` 的溢出处理策略，也没有断言调用方能拒绝饱和值。

文件：`tests/test_export_plan.cpp`

- 覆盖普通工程的计划构造、片段和转场等场景。
- 尚未证明时间算术溢出会导致导出计划被拒绝。

## 5. 风险分级与建议

### P1：导出计划构造与验证之间缺少溢出错误传播

建议在不改变现有兼容 API 的前提下，设计显式的 checked 时间运算/计划编译结果；在单次编译边界清除并捕获溢出状态，发现溢出即返回结构化错误，不允许饱和值进入可执行计划。不要仅在 `exportPlanValid()` 末尾查询一个可能来自更早操作的 sticky 标志。

### P1：时间转换函数的失败语义未定义

为帧号、微秒转换明确边界行为：返回状态/expected 结果，或新增 checked 版本供生产关键路径使用。旧函数是否保留兼容包装需单独评估，不能静默改变现有调用方语义。

### P2：音视频和效果帧索引的时间基准一致性证据不足

对主视频帧时间戳、调整图层效果帧索引、速度曲线采样、音频块时间戳建立统一基准与误差预算；为 23.976/29.97/59.94、长时长、非整数倍采样率和变速场景增加端到端测试。

### P2：异常/损坏工程的时间边界测试不足

增加片段 end/sourceOut 溢出、Sequence::duration 溢出、转场 end 溢出、导出计划拒绝、微秒转换边界、帧号边界和 sticky 标志污染隔离测试。

## 6. 下一步专项验收

1. 先确定 checked 时间 API 的兼容方案与错误模型。
2. 为 `compileExportPlan()` 增加可验证的错误传播设计，覆盖片段、复合片段、转场和总时长。
3. 为 `timeAtFrame/frameAtTime/toMicroseconds` 增加边界语义和回归测试。
4. 建立长时长/变速/非整数帧率的视频时间戳与音频块时间戳一致性测试。
5. 在对应审批和文件所有权允许后实施代码变更，再运行 Host、ASan、UBSan、Android NDK 和 Kotlin 验证。
6. 以测试证据和实际代码行为复核后，才能重新评估 S3-1 是否通过。

## 7. 审查边界

本文件是只读审查结论和后续设计任务，不代表上述风险已经修复。未修改 Rational、Time、Sequence、Clip、ExportPlan、ExportExecutor 或 Manifest/Activity。既有 A-12 与其他审批门、文件所有权和 S5-0B 依赖仍然有效。
