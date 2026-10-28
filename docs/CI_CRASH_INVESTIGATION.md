# CI 崩溃排查：Capi.WatermarkRealExportEndToEnd

状态：**未结**。已确定崩溃的全部外部特征与触发边界，但尚未定位到具体写入者。
更新：2026-10-28

## 一、现象

CI（x64 / Ubuntu / glibc）上原生 Release 构建跑全量测试时稳定崩溃：

    malloc(): invalid size (unsorted)
    exit=134 (SIGABRT)

崩溃点固定为 `Capi.WatermarkRealExportEndToEnd`（注册索引 267）。
本地 aarch64 上不崩溃。

## 二、已确证的事实

### 2.1 崩溃只在「原生内存布局」下复现

| 构建/工具 | 结果 |
|---|---|
| 原生 Release（主 job） | exit=134，稳定复现 |
| ASAN（Release + ORT，同配置） | 1444 用例跑完，AddressSanitizer 无报错 |
| valgrind memcheck（-O0） | 跑完，Invalid read/write 无 |
| UBSan + -ftrivial-auto-var-init=pattern | 该用例**通过**（另 3 个 ExportRunner 用例失败） |

三个 sanitizer 都会改变内存布局（redzone / 自定义分配器 / 0xAA 填充），
问题被掩盖而不是消失。因此继续加 sanitizer 没有意义。

### 2.2 gdb 栈：发现点不是原因

    #6  malloc_printerr   "malloc(): invalid size (unsorted)"
    #7  _int_malloc       bytes=2048
    #9  operator new(2048)
    #10 makeBlockOrder(width, height, config)      <- 发现点
    #11 detectInvisibleWatermark
    #12 nc_watermark_detect
    #13 nc_test_Capi_WatermarkRealExportEndToEnd

`makeBlockOrder` 分配 `std::vector<Block>`，`Block{int x; int y}` 共 8 字节，
2048 / 8 = 256 个块。这是 `detectInvisibleWatermark` 内部第一次堆分配
（之前 `effectiveWatermarkConfig` 只有纯值计算，不分配）。
所以 glibc 是在复用坏 chunk 时才报错，写入者在更早处。

### 2.3 堆在 nc_export_run 返回后仍是好的

在用例里 `nc_export_run` 之后插强制启用的 HeapProbe
（128 次错开尺寸的 malloc + memset + free）：

- 输出字节数 26203 -> 26237，增量恰好等于 `[诊断] 导出后堆检查通过\n`（33+1）
- 该行确实被打印，heapProbe 通过

即：破坏不在导出路径内部。

### 2.4 崩溃发生在 reader.open 完成之前

在 `reader.open` / `readVideoFrame` / `nc_watermark_detect` 之后各插一次 heapProbe：

- 一条都没打印（字节数只再 +40，来自不执行的代码）
- `reader.open` 的最后两个动作是 `av_frame_alloc()` / `av_packet_alloc()`

即崩溃在 `reader.open` 内部收敛。但注意：`reader.open` 只是
「下一个做堆分配的地方」，真正写坏堆的可能仍是它之前那段
（导出返回 -> writer 析构 -> reset()）。

### 2.5 触发边界（二分结果）

| 诊断 | 范围 | 结果 |
|---|---|---|
| NC_TEST_MAX=200 | 前 200 个 | 过 |
| NC_TEST_MAX=266 | 到崩溃用例前一个 | 过 |
| SKIP=203 MAX=267 | 整个 Capi 段不含该用例 | 过 |
| SKIP=267 MAX=268 | **只跑这一个用例** | 崩 |
| SKIP=50 | 跳过前 50 个，仍含该用例 | 崩 |
| 单独靶 nairoclip_capi_only_tests | 只链 test_capi.cpp | 过 |

即：**与前面用例无关，该用例自身触发**；但换构建/链接就消失。

### 2.6 用例内部各阶段的对照

| 阶段开关 | 执行内容 | 结果 |
|---|---|---|
| NC_WM_STAGE=export | 只 nc_export_run | 过 |
| NC_WM_STAGE=detect | 导出 + 解码 + 检测（**与完整版工作完全相同**） | 过 |
| 未设（完整） | 同上 | 崩 |

两者执行路径一致，唯一差别是多一次 `getenv("NC_WM_STAGE")` 调用。
**多一次函数调用改变栈布局就换结果** —— 未定义行为的典型特征。


## 二·补、最新进展（2026-10-28 晚）

### 导出路径已排除

在 `runExport` 内部插了 6 个堆检查点（`NC_EXPORT_HEAP_PROBE=1`）：

    writer 创建后/未 open -> writer.open 之后 -> executeExport 之后 ->
    finish 之前 -> finish 之后 -> runExport 返回前

**全部通过。** 这一轮的探针已覆盖 16B..62KB 共 122 档
（含 glibc 实报过的 1048 / 2048），不是之前那种「只到 3KB」的窄探针。
所以「导出写坏堆」可以排除 —— 之前几轮对 writer / audioFifo / 编码器
的怀疑都不成立。

### 破坏点收敛到 readVideoFrame

诊断行原文（不再用字节数反推）显示：

    用例入口 -> nc_export_run 之前 -> [runExport 六点全过] ->
    nc_export_run 之后 -> 解码前 -> reader.open   都打印了
    下一条 readVideoFrame                          没有

同时 gdb 栈：`#9 nc_test_Capi_WatermarkRealExportEndToEnd` 直接调
`malloc(919)`。919 == `16 + 43*21`，正是 HeapProbe 的某档尺寸
→ 是探针自己在 malloc 时踩到坏块，而不是业务代码在解引用。

结论：破坏发生在 `reader.open` 返回之后、`readVideoFrame` 返回之后。

### 方法论修正

1. **探针尺寸必须覆盖所有 bin。** 旧探针最大 3KB，坏 chunk 在更大档位
   根本不会被复用，「通过」是假阴性。这次白白多跑了三轮。
2. **不要用输出字节数反推诊断进度。** 中文多字节 + 同一检查点可能被
   多次调用，我算错过两次。直接把 `grep 诊断` 的原文打到日志 / annotation。
3. **检查点要放在循环体外也要放一份。** `heapOk("readVideoFrame")` 写在
   `while` 体内，`readVideoFrame` 返回 false 时会整段跳过，
   一度让我误以为「没打印 = 崩在 readVideoFrame 内部」，
   实际上也可能崩在循环之后的 `reader.close()`。

## 三、已修的真实缺陷（不一定是本崩溃的根因）

排查过程中确认并修复的独立缺陷，均有独立价值：

1. **outData 只给 1 个指针，但 AAC 输出是 FLTP（平面）**
   `media_ffmpeg_writer.cpp`。平面格式需要每声道一个指针，
   `outData[1]` 是栈上未初始化值。ASAN 实抓到
   `SEGV WRITE libswresample`。单声道时越界读不到，故长期未暴露。
2. **gcd64 对 INT64_MIN 取负**（`rational.cpp:12`）
   UBSan 实抓到 `negation of -9223372036854775808`。
   该函数是全库 Rational 约分的唯一入口。
3. **nc_export_run 未把 include_audio 同步给 executor**
   `ExportExecutorOptions::outputAudio` 默认 true，
   导致 include_audio=0 时仍解码并混合整条音轨，最后被 writer 静默丢弃。
4. **reader.open 未检查 avcodec_alloc_context3 / avcodec_parameters_to_context**
   （`media_ffmpeg.cpp:143/164`）。失败后继续用 nullptr / 字段全空的
   context 是 UB，崩不崩取决于布局。
5. **平面 FIFO 的 per-channel 消费**（`writeAudioBlock` 的 erase）
   平面布局下按 `consumedFrames * channels` 从队首 erase，
   会削掉 ch0 开头却留下 ch1 旧数据，破坏「每声道帧数相同」不变式。

## 四、下一步（按性价比排序）

1. **MALLOC_CHECK_=3 下跑 gdb**（已推送 3961184）
   `MALLOC_CHECK_=3` 让 glibc 每次 malloc/free 都复核 chunk，
   能在写坏的瞬间 abort。若复现，栈顶就是**真正的写入者**而不是后续分配者。
2. 若仍未复现：把 heapProbe 粒度从「整个 runExport 之后」
   下移到 `~FFmpegMediaWriter` / `reset()` 内部逐项之间。
3. 若确认与 tcache 有关：用 `glibc.malloc.tcache_count=0` 关掉 tcache 再跑，
   排除 chunk 复用掩盖。

## 五、方法论教训

1. **二分前必须算准目标用例的注册索引。**
   第一次设 `NC_TEST_SKIP=1100`，直接跳过了索引 267 的用例，
   跑出的「绿」是假阴性。
2. **诊断 job 必须与主 job 覆盖同一份代码。**
   ASAN/valgrind 早期都没装 ONNX Runtime，AI 路径走降级 stub，
   它们的「全绿」推不出主 job 干净。现已对齐（都装 ORT、都是 Release）。
3. **不要在已确认不执行的代码路径上继续投入。**
   该用例 `include_audio=0`，音频路径根本不执行；
   前两轮针对 audioFifo 的修复虽然是真 bug，但与本次崩溃无关。
4. **轮询 CI 不要用长 sleep。** 单次 `sleep 290` 会吃掉回合预算，
   导致工具被取消（表现为 tool_cancelled_wall_clock），
   与 CI 失败无关，但很容易误判。
