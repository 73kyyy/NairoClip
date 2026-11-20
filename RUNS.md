# CI 运行状态

> 本文件由 `.github/workflows/ci-status.yml` 在 runner 上自动生成，提交到 `ci-status` 分支。
> 开发容器不跑脚本、也读不到 Actions API，只能靠这条通道看结论。
> **不要手工编辑**——下一次运行会覆盖。

生成时间（UTC）：`2026-11-20T14:43:27Z`　触发：`workflow_run`

## 各工作流最近一次

| | 工作流 | 结论 | 提交 | 事件 | 运行 |
|---|---|---|---|---|---|
| ❌ | CI | `failure` | `e0d311b` | `push` | [#484](https://github.com/73kyyy/NairoClip/actions/runs/44754634628) |
| ⏳ | CI Status | `running` | `e0d311b` | `workflow_run` | [#871](https://github.com/73kyyy/NairoClip/actions/runs/44756153429) |
| ✅ | Report | `success` | `e0d311b` | `workflow_run` | [#430](https://github.com/73kyyy/NairoClip/actions/runs/44756117079) |

## 失败细节

### CI · run #484 · `e0d311b`

链接：https://github.com/73kyyy/NairoClip/actions/runs/44754634628

**失败点：**

- `build-and-test` → `运行全量测试`（failure）

<details><summary>失败日志尾部（截断）</summary>

```
build-and-test	运行全量测试	﻿2026-11-20T14:41:24.8319517Z ##[group]Run # Actions 的 run 默认是 bash -e：stdbuf 一返回 134 脚本就退出，
build-and-test	运行全量测试	2026-11-20T14:41:24.8337921Z ^[[36;1m# Actions 的 run 默认是 bash -e：stdbuf 一返回 134 脚本就退出，^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8347599Z ^[[36;1m# 下面所有诊断都不会执行（上一轮 annotation 只剩 "Process completed^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8347984Z ^[[36;1m# with exit code 134"，就是这个原因）。必须先关掉 errexit。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8357501Z ^[[36;1mset +e^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8359288Z ^[[36;1m# NC_TEST_CHECK_HEAP=1：每个用例后做一次堆一致性检查，把^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8377520Z ^[[36;1m# "malloc(): invalid size" 这类延迟暴露的破坏钉到真凶用例上。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8387265Z ^[[36;1m# 详见 tests/test_framework.h 里 HeapProbe 的注释。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8397151Z ^[[36;1m# MALLOC_PERTURB_：free 时用该字节填充、malloc 时用其反码填充，^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8397504Z ^[[36;1m#   让「读了已释放内存」立刻表现为异常值而不是侥幸的旧数据。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8416968Z ^[[36;1m# MALLOC_CHECK_=3：glibc 在每次 malloc/free 时复核 chunk header，^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8418340Z ^[[36;1m#   发现不一致直接 abort 并打印，比等到下一次 malloc 才报更早。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8426919Z ^[[36;1m# 这三个环境变量都不改变分配器实现（仍是 glibc），^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8446723Z ^[[36;1m# 所以能复现 ASAN/valgrind（自带的分配器）看不到的问题。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8448485Z ^[[36;1m# NC_TEST_EXCLUDE：按**名字**排除已知的崩溃用例。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8448805Z ^[[36;1m#^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8456470Z ^[[36;1m# Capi.WatermarkRealExportEndToEnd 长期在 CI 的 x64/glibc 下以^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8456903Z ^[[36;1m# "malloc(): invalid size (unsorted)" / SIGABRT 结束，^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8476510Z ^[[36;1m# 而 ASAN 与 valgrind（各自带分配器）全量跑完 0 报错。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8486231Z ^[[36;1m# 这是"只在本环境暴露"的堆破坏，与本次 LUT 改动无关。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8488101Z ^[[36;1m#^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8496135Z ^[[36;1m# 之前用 NC_TEST_SKIP/MAX 按**位置**避开它，但位置会随着^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8496468Z ^[[36;1m# 新增测试整体漂移（每批测试都要同步改那个魔数 267），^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8516058Z ^[[36;1m# 忘改就会把崩溃用例又跑起来。按名字排除与顺序无关。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8517832Z ^[[36;1m#^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8535802Z ^[[36;1m# 注意：这是**临时隔离**，不是修好。该用例本身仍需单独排查^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8545687Z ^[[36;1m# （CI 里已有 ONLY_267_* 等专项诊断步骤保留着）。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8546096Z ^[[36;1mMALLOC_PERTURB_=165 MALLOC_CHECK_=3 NC_TEST_CHECK_HEAP=1 \^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8555877Z ^[[36;1m  NC_EXPORT_HEAP_PROBE=1 NC_MEDIA_HEAP_PROBE=1 \^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8565693Z ^[[36;1m  NC_TEST_EXCLUDE=Capi.WatermarkRealExportEndToEnd \^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8575470Z ^[[36;1m  stdbuf -oL -eL ./build/tests/nairoclip_tests \^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8585268Z ^[[36;1m  > /tmp/test_output.txt 2>&1^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8595132Z ^[[36;1mrc=$?^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8595456Z ^[[36;1mecho "exit_code=$rc (137=SIGKILL 134=SIGABRT 139=SIGSEGV)"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8604921Z ^[[36;1mecho "output_bytes=$(stat -c%s /tmp/test_output.txt)"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8615134Z ^[[36;1mecho "---------------- 末尾 40 行 ----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8624881Z ^[[36;1mtail -40 /tmp/test_output.txt^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8634687Z ^[[36;1mecho "---------------- 崩溃点定位 ----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8636553Z ^[[36;1mgrep -E '^\[ RUN  \]' /tmp/test_output.txt | tail -3^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8654652Z ^[[36;1mecho "---------------- 诊断行（堆检查点，原文）----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8664425Z ^[[36;1mgrep -n '诊断' /tmp/test_output.txt | tail -40 || echo "无诊断行"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8664821Z ^[[36;1mecho "---------------- 汇总行 ----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8674596Z ^[[36;1mgrep -E '^=====' /tmp/test_output.txt || echo "无汇总行"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8684451Z ^[[36;1mecho "---------------- 行数 / 前 30 行 ----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8694238Z ^[[36;1mwc -l /tmp/test_output.txt^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8695493Z ^[[36;1mhead -30 /tmp/test_output.txt^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8704262Z ^[[36;1mecho "---------------- abort/assert 关键字 ----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8714496Z ^[[36;1mgrep -n -E 'assert|Assertion|terminate|bad_alloc|invalid size|corrupt|malloc|free\(\)' \^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8724020Z ^[[36;1m  /tmp/test_output.txt | head -20 || echo "无"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8733853Z ^[[36;1m# 断言失败与崩溃不同：进程继续跑完，[FAIL] 行留在中段，^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8743669Z ^[[36;1m# 被 tail -40 截断，下面的「最后一个 RUN 之后」也取不到。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8754879Z ^[[36;1m# 这条必须单独抓：它同时给出失败用例名与断言行号，一次定位。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8755236Z ^[[36;1mecho "---------------- 失败断言（[FAIL] 行）----------------"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8763702Z ^[[36;1mgrep -n '\[FAIL\]' /tmp/test_output.txt | head -10 || echo "无 [FAIL] 行"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8783482Z ^[[36;1m# 走 annotation 通道：public 仓库的日志/artifact 需要认证，^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8793085Z ^[[36;1m# 但 check-run annotation 可匿名读取，是唯一能带出细节的出口。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8803041Z ^[[36;1mecho "::error title=TEST_EXIT::exit=$rc bytes=$(stat -c%s /tmp/test_output.txt)"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8812986Z ^[[36;1mFAILS=$(grep -c '\[FAIL\]' /tmp/test_output.txt 2>/dev/null || echo 0)^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8813499Z ^[[36;1mFAILDETAIL=$(grep -E '\[FAIL\]' /tmp/test_output.txt | tr '\n' '~' | cut -c1-1800)^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8823088Z ^[[36;1mecho "::error title=FAIL_COUNT::${FAILS:-0}"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8832997Z ^[[36;1mecho "::error title=FAIL_DETAIL::${FAILDETAIL:-none}"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8835047Z ^[[36;1mLAST=$(grep -E '^\[ RUN  \]' /tmp/test_output.txt | tail -1)^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8842837Z ^[[36;1mSUM=$(grep -E '^=====' /tmp/test_output.txt | tail -1)^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8852707Z ^[[36;1mecho "::error title=LAST_RUN::${LAST:-none}"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8854387Z ^[[36;1mecho "::error title=SUMMARY::${SUM:-none}"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8854714Z ^[[36;1mTAIL=$(tail -12 /tmp/test_output.txt | tr '\n' '~' | cut -c1-1500)^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8855037Z ^[[36;1mecho "::error title=TAIL::${TAIL:-none}"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8855311Z ^[[36;1m# 诊断行单独走一条 annotation，避免被 TAIL 截断^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8855570Z ^[[36;1m# 取「最后一个 RUN 行之后」的全部内容：即该用例自己的输出。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8855815Z ^[[36;1m# 不用 awk（YAML 里的引号转义容易出错，上一版就是这么写坏的），^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8856071Z ^[[36;1m# 改用 tac + awk 找最后一个 RUN 的行号，再 tail 截取。^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8856426Z ^[[36;1mLASTLINE=$(grep -n '^\[ RUN  \]' /tmp/test_output.txt | tail -1 | cut -d: -f1)^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8856773Z ^[[36;1mecho "崩溃用例起始行=$LASTLINE"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8857008Z ^[[36;1mif [ -n "$LASTLINE" ]; then^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8857292Z ^[[36;1m  tail -n +"$LASTLINE" /tmp/test_output.txt > /tmp/case_out.txt^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8857581Z ^[[36;1melse^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8857789Z ^[[36;1m  cp /tmp/test_output.txt /tmp/case_out.txt^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8858027Z ^[[36;1mfi^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8858209Z ^[[36;1mwc -c /tmp/case_out.txt^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8858439Z ^[[36;1m# 切成多段，每段 2400 字节，各走一条 annotation^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8858668Z ^[[36;1mrm -f /tmp/diagpart_*^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8858960Z ^[[36;1msplit -b 2400 /tmp/case_out.txt /tmp/diagpart_ 2>/dev/null || true^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8859374Z ^[[36;1mi=0^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8859572Z ^[[36;1mfor f in /tmp/diagpart_*; do^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8859803Z ^[[36;1m  [ -f "$f" ] || continue^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8860093Z ^[[36;1m  echo "::error title=CASE_P$i::$(tr '\n' '~' < $f | cut -c1-2300)"^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8860380Z ^[[36;1m  i=$((i+1))^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8860575Z ^[[36;1m  [ "$i" -ge 6 ] && break^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8860933Z ^[[36;1mdone^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8861103Z ^[[36;1mexit $rc^[[0m
build-and-test	运行全量测试	2026-11-20T14:41:24.8904262Z shell: /usr/bin/bash -e {0}
build-and-test	运行全量测试	2026-11-20T14:41:24.8904694Z ##[endgroup]
build-and-test	运行全量测试	2026-11-20T14:42:53.1613736Z exit_code=1 (137=SIGKILL 134=SIGABRT 139=SIGSEGV)
build-and-test	运行全量测试	2026-11-20T14:42:53.1644244Z output_bytes=386387
build-and-test	运行全量测试	2026-11-20T14:42:53.1644888Z ---------------- 末尾 40 行 ----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1665605Z [  OK  ] ShakeExport.LargerAmplitudeChangesMore
build-and-test	运行全量测试	2026-11-20T14:42:53.1665959Z [ RUN  ] ClipLutExport.LutChangesOutputPixels
build-and-test	运行全量测试	2026-11-20T14:42:53.1666237Z [  OK  ] ClipLutExport.LutChangesOutputPixels
build-and-test	运行全量测试	2026-11-20T14:42:53.1666504Z [ RUN  ] ClipLutExport.ZeroStrengthIsNoOp
build-and-test	运行全量测试	2026-11-20T14:42:53.1666750Z [  OK  ] ClipLutExport.ZeroStrengthIsNoOp
build-and-test	运行全量测试	2026-11-20T14:42:53.1667282Z [ RUN  ] ClipLutExport.LutIsDeterministic
build-and-test	运行全量测试	2026-11-20T14:42:53.1667521Z [  OK  ] ClipLutExport.LutIsDeterministic
build-and-test	运行全量测试	2026-11-20T14:42:53.1667819Z [ RUN  ] ClipLutExport.HalfStrengthDiffersFromFullAndFromNone
build-and-test	运行全量测试	2026-11-20T14:42:53.1668193Z [  OK  ] ClipLutExport.HalfStrengthDiffersFromFullAndFromNone
build-and-test	运行全量测试	2026-11-20T14:42:53.1668533Z [ RUN  ] ClipLutExport.MissingFileDoesNotCrashAndIsWarned
build-and-test	运行全量测试	2026-11-20T14:42:53.1669011Z [  OK  ] ClipLutExport.MissingFileDoesNotCrashAndIsWarned
build-and-test	运行全量测试	2026-11-20T14:42:53.1669619Z [ RUN  ] CapiLut.SetAndGetRoundtrip
build-and-test	运行全量测试	2026-11-20T14:42:53.1669972Z [  OK  ] CapiLut.SetAndGetRoundtrip
build-and-test	运行全量测试	2026-11-20T14:42:53.1670354Z [ RUN  ] CapiLut.DefaultsToEmptyAndFullStrength
build-and-test	运行全量测试	2026-11-20T14:42:53.1670784Z [  OK  ] CapiLut.DefaultsToEmptyAndFullStrength
build-and-test	运行全量测试	2026-11-20T14:42:53.1671180Z [ RUN  ] CapiLut.NullPathClearsLut
build-and-test	运行全量测试	2026-11-20T14:42:53.1671542Z [  OK  ] CapiLut.NullPathClearsLut
build-and-test	运行全量测试	2026-11-20T14:42:53.1671902Z [ RUN  ] CapiLut.RejectsStrengthOutOfRange
build-and-test	运行全量测试	2026-11-20T14:42:53.1672296Z [  OK  ] CapiLut.RejectsStrengthOutOfRange
build-and-test	运行全量测试	2026-11-20T14:42:53.1672568Z [ RUN  ] CapiLut.RejectsNaNAndInfinity
build-and-test	运行全量测试	2026-11-20T14:42:53.1672807Z [  OK  ] CapiLut.RejectsNaNAndInfinity
build-and-test	运行全量测试	2026-11-20T14:42:53.1673047Z [ RUN  ] CapiLut.BadTrackOrClipIsReported
build-and-test	运行全量测试	2026-11-20T14:42:53.1673307Z [  OK  ] CapiLut.BadTrackOrClipIsReported
build-and-test	运行全量测试	2026-11-20T14:42:53.1673570Z [ RUN  ] CapiLut.NullEditorIsRejected
build-and-test	运行全量测试	2026-11-20T14:42:53.1673805Z [  OK  ] CapiLut.NullEditorIsRejected
build-and-test	运行全量测试	2026-11-20T14:42:53.1674028Z [ RUN  ] CapiLut.IsUndoable
build-and-test	运行全量测试	2026-11-20T14:42:53.1674236Z [  OK  ] CapiLut.IsUndoable
build-and-test	运行全量测试	2026-11-20T14:42:53.1674466Z [ RUN  ] CapiLut.LockedTrackRejectsLutChange
build-and-test	运行全量测试	2026-11-20T14:42:53.1674734Z [  OK  ] CapiLut.LockedTrackRejectsLutChange
build-and-test	运行全量测试	2026-11-20T14:42:53.1674996Z [ RUN  ] CapiLut.ValidateAcceptsRealCube
build-and-test	运行全量测试	2026-11-20T14:42:53.1675238Z [  OK  ] CapiLut.ValidateAcceptsRealCube
build-and-test	运行全量测试	2026-11-20T14:42:53.1675481Z [ RUN  ] CapiLut.ValidateExplainsFailure
build-and-test	运行全量测试	2026-11-20T14:42:53.1675724Z [  OK  ] CapiLut.ValidateExplainsFailure
build-and-test	运行全量测试	2026-11-20T14:42:53.1675963Z [ RUN  ] CapiLut.ValidateRejects1DLut
build-and-test	运行全量测试	2026-11-20T14:42:53.1676207Z [  OK  ] CapiLut.ValidateRejects1DLut
build-and-test	运行全量测试	2026-11-20T14:42:53.1676463Z [ RUN  ] CapiLut.BuiltinPresetsAreRealAndHonest
build-and-test	运行全量测试	2026-11-20T14:42:53.1676744Z [  OK  ] CapiLut.BuiltinPresetsAreRealAndHonest
build-and-test	运行全量测试	2026-11-20T14:42:53.1676912Z 
build-and-test	运行全量测试	2026-11-20T14:42:53.1677137Z ===== 执行 1805/1806 cases | 1 failed | 1 checks failed =====
build-and-test	运行全量测试	2026-11-20T14:42:53.1677464Z [SHARD_STATS] shard=0 shards=1 executed=1805 total=1806 failed=1
build-and-test	运行全量测试	2026-11-20T14:42:53.1677816Z ---------------- 崩溃点定位 ----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1694629Z [ RUN  ] CapiLut.ValidateExplainsFailure
build-and-test	运行全量测试	2026-11-20T14:42:53.1695043Z [ RUN  ] CapiLut.ValidateRejects1DLut
build-and-test	运行全量测试	2026-11-20T14:42:53.1695445Z [ RUN  ] CapiLut.BuiltinPresetsAreRealAndHonest
build-and-test	运行全量测试	2026-11-20T14:42:53.1699407Z ---------------- 诊断行（堆检查点，原文）----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1728249Z 6662:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1728796Z 6663:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1729407Z 6664:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1729746Z 6665:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1730041Z 6666:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1730359Z 6667:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1730886Z 6668:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1731170Z 6669:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1731468Z 6670:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1731788Z 6671:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1732101Z 6672:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1732480Z 6673:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1732943Z 6674:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1733297Z 6675:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1733817Z 6676:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1734266Z 6677:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1734907Z 6678:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1735208Z 6679:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1735503Z 6680:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1735790Z 6681:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1736073Z 6682:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1736380Z 6683:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1736691Z 6684:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1736972Z 6685:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1737264Z 6686:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1737587Z 6687:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1737896Z 6688:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1738180Z 6689:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1738475Z 6690:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1738786Z 6691:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1739227Z 6692:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1739642Z 6693:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1739980Z 6694:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1740278Z 6695:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1740578Z 6696:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1740860Z 6697:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1741166Z 6839:[诊断] readVideoFrame: receive_frame 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1741485Z 6840:[诊断] readVideoFrame: sws_getCachedContext 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1741777Z 6841:[诊断] readVideoFrame: sws_scale 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1742050Z 6842:[诊断] readVideoFrame: av_frame_unref 之后 后堆 OK
build-and-test	运行全量测试	2026-11-20T14:42:53.1742359Z ---------------- 汇总行 ----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1745475Z ===== 执行 1805/1806 cases | 1 failed | 1 checks failed =====
build-and-test	运行全量测试	2026-11-20T14:42:53.1747491Z ---------------- 行数 / 前 30 行 ----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1773332Z 7611 /tmp/test_output.txt
build-and-test	运行全量测试	2026-11-20T14:42:53.1796571Z [ RUN  ] Rational.Normalize
build-and-test	运行全量测试	2026-11-20T14:42:53.1796977Z [  OK  ] Rational.Normalize
build-and-test	运行全量测试	2026-11-20T14:42:53.1797300Z [ RUN  ] Rational.Arithmetic
build-and-test	运行全量测试	2026-11-20T14:42:53.1797621Z [  OK  ] Rational.Arithmetic
build-and-test	运行全量测试	2026-11-20T14:42:53.1797953Z [ RUN  ] Rational.CompareAndRound
build-and-test	运行全量测试	2026-11-20T14:42:53.1798304Z [  OK  ] Rational.CompareAndRound
build-and-test	运行全量测试	2026-11-20T14:42:53.1798688Z [ RUN  ] Rational.MultiplicationExactlyFitsIn128
build-and-test	运行全量测试	2026-11-20T14:42:53.1799305Z [  OK  ] Rational.MultiplicationExactlyFitsIn128
build-and-test	运行全量测试	2026-11-20T14:42:53.1799795Z [ RUN  ] Rational.LargeButInRangeMultiplicationIsExact
build-and-test	运行全量测试	2026-11-20T14:42:53.1800279Z [  OK  ] Rational.LargeButInRangeMultiplicationIsExact
build-and-test	运行全量测试	2026-11-20T14:42:53.1800583Z [ RUN  ] Rational.MultiplicationUsesCrossReduction
build-and-test	运行全量测试	2026-11-20T14:42:53.1801041Z [  OK  ] Rational.MultiplicationUsesCrossReduction
build-and-test	运行全量测试	2026-11-20T14:42:53.1801342Z [ RUN  ] Rational.ComparisonHandlesLargeValues
build-and-test	运行全量测试	2026-11-20T14:42:53.1801637Z [  OK  ] Rational.ComparisonHandlesLargeValues
build-and-test	运行全量测试	2026-11-20T14:42:53.1802264Z [ RUN  ] Rational.NegativeDenominatorNormalizationHandlesMin
build-and-test	运行全量测试	2026-11-20T14:42:53.1803073Z [  OK  ] Rational.NegativeDenominatorNormalizationHandlesMin
build-and-test	运行全量测试	2026-11-20T14:42:53.1803730Z [ RUN  ] Rational.RoundToIntDoesNotOverflow
build-and-test	运行全量测试	2026-11-20T14:42:53.1804133Z [  OK  ] Rational.RoundToIntDoesNotOverflow
build-and-test	运行全量测试	2026-11-20T14:42:53.1804546Z [ RUN  ] Rational.AdditionSaturatesInsteadOfUb
build-and-test	运行全量测试	2026-11-20T14:42:53.1804962Z [  OK  ] Rational.AdditionSaturatesInsteadOfUb
build-and-test	运行全量测试	2026-11-20T14:42:53.1805388Z [ RUN  ] Rational.SubtractionSaturatesInsteadOfUb
build-and-test	运行全量测试	2026-11-20T14:42:53.1805829Z [  OK  ] Rational.SubtractionSaturatesInsteadOfUb
build-and-test	运行全量测试	2026-11-20T14:42:53.1806414Z [ RUN  ] Rational.NormalArithmeticDoesNotSetOverflowFlag
build-and-test	运行全量测试	2026-11-20T14:42:53.1806917Z [  OK  ] Rational.NormalArithmeticDoesNotSetOverflowFlag
build-and-test	运行全量测试	2026-11-20T14:42:53.1807379Z [ RUN  ] Rational.FramerateArithmeticStaysExact
build-and-test	运行全量测试	2026-11-20T14:42:53.1808010Z [  OK  ] Rational.FramerateArithmeticStaysExact
build-and-test	运行全量测试	2026-11-20T14:42:53.1808390Z [ RUN  ] Time.FrameConversions
build-and-test	运行全量测试	2026-11-20T14:42:53.1808698Z [  OK  ] Time.FrameConversions
build-and-test	运行全量测试	2026-11-20T14:42:53.1809019Z [ RUN  ] Time.FractionalFrameRate
build-and-test	运行全量测试	2026-11-20T14:42:53.1809532Z [  OK  ] Time.FractionalFrameRate
build-and-test	运行全量测试	2026-11-20T14:42:53.1810044Z ---------------- abort/assert 关键字 ----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1826857Z ---------------- 失败断言（[FAIL] 行）----------------
build-and-test	运行全量测试	2026-11-20T14:42:53.1849267Z 2313:    [FAIL] /home/runner/work/NairoClip/NairoClip/tests/test_audio_denoise.cpp:576  CHECK(20.0 * std::log10((toneOut + 1e-12) / (toneIn + 1e-12)) > -8.0)
build-and-test	运行全量测试	2026-11-20T14:42:53.1878458Z ##[error]exit=1 bytes=386387
build-and-test	运行全量测试	2026-11-20T14:42:53.1930217Z ##[error]1
build-and-test	运行全量测试	2026-11-20T14:42:53.1931738Z ##[error]    [FAIL] /home/runner/work/NairoClip/NairoClip/tests/test_audio_denoise.cpp:576  CHECK(20.0 * std::log10((toneOut + 1e-12) / (toneIn + 1e-12)) > -8.0)~
build-and-test	运行全量测试	2026-11-20T14:42:53.1988764Z ##[error][ RUN  ] CapiLut.BuiltinPresetsAreRealAndHonest
build-and-test	运行全量测试	2026-11-20T14:42:53.1990796Z ##[error]===== 执行 1805/1806 cases | 1 failed | 1 checks failed =====
build-and-test	运行全量测试	2026-11-20T14:42:53.2032042Z ##[error][  OK  ] CapiLut.LockedTrackRejectsLutChange~[ RUN  ] CapiLut.ValidateAcceptsRealCube~[  OK  ] CapiLut.ValidateAcceptsRealCube~[ RUN  ] CapiLut.ValidateExplainsFailure~[  OK  ] CapiLut.ValidateExplainsFailure~[ RUN  ] CapiLut.ValidateRejects1DLut~[  OK  ] CapiLut.ValidateRejects1DLut~[ RUN  ] CapiLut.BuiltinPresetsAreRealAndHonest~[  OK  ] CapiLut.BuiltinPresetsAreRealAndHonest~~===== 执行 1805/1806 cases | 1 failed | 1 checks failed =====~[SHARD_STATS] shard=0 shards=1 executed=1805 total=1806 failed=1~
build-and-test	运行全量测试	2026-11-20T14:42:53.2064027Z 崩溃用例起始行=7607
build-and-test	运行全量测试	2026-11-20T14:42:53.2110599Z 226 /tmp/case_out.txt
build-and-test	运行全量测试	2026-11-20T14:42:53.2187528Z ##[error][ RUN  ] CapiLut.BuiltinPresetsAreRealAndHonest~[  OK  ] CapiLut.BuiltinPresetsAreRealAndHonest~~===== 执行 1805/1806 cases | 1 failed | 1 checks failed =====~[SHARD_STATS] shard=0 shards=1 executed=1805 total=1806 failed=1~
build-and-test	运行全量测试	2026-11-20T14:42:53.2192875Z ##[error]Process completed with exit code 1.
```

</details>

## 最近运行

| | 工作流 | 结论 | 提交 | 创建时间 |
|---|---|---|---|---|
| ⏳ | CI Status | `running` | `e0d311b` | 2026-11-20T14:43:13Z |
| ⚪ | CI Status | `cancelled` | `e0d311b` | 2026-11-20T14:43:00Z |
| ✅ | Report | `success` | `e0d311b` | 2026-11-20T14:42:59Z |
| ❌ | CI | `failure` | `e0d311b` | 2026-11-20T14:33:43Z |
| ✅ | CI Status | `success` | `46172f3` | 2026-11-19T10:59:16Z |
| ✅ | Report | `success` | `46172f3` | 2026-11-19T10:59:04Z |
| ⚪ | CI Status | `cancelled` | `46172f3` | 2026-11-19T10:59:04Z |
| ❌ | CI | `failure` | `ea47686` | 2026-11-19T10:43:19Z |
| ✅ | CI Status | `success` | `46172f3` | 2026-11-19T09:09:49Z |
| ✅ | Report | `success` | `46172f3` | 2026-11-19T09:09:34Z |
| ⚪ | CI Status | `cancelled` | `46172f3` | 2026-11-19T09:09:33Z |
| ✅ | CI Status | `success` | `46172f3` | 2026-11-19T09:04:07Z |
| ✅ | Report | `success` | `46172f3` | 2026-11-19T09:03:53Z |
| ⚪ | CI Status | `cancelled` | `46172f3` | 2026-11-19T09:03:53Z |
| ✅ | CI Status | `success` | `46172f3` | 2026-11-19T08:56:33Z |
| ✅ | Report | `success` | `46172f3` | 2026-11-19T08:56:21Z |
| ⚪ | CI Status | `cancelled` | `46172f3` | 2026-11-19T08:56:21Z |
| ✅ | CI Status | `success` | `46172f3` | 2026-11-19T08:55:06Z |
| ⚪ | CI Status | `cancelled` | `46172f3` | 2026-11-19T08:54:52Z |
| ✅ | Report | `success` | `46172f3` | 2026-11-19T08:54:52Z |

