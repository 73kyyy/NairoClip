# CI 运行状态

> 本文件由 `.github/workflows/ci-status.yml` 在 runner 上自动生成，提交到 `ci-status` 分支。
> 开发容器不跑脚本、也读不到 Actions API，只能靠这条通道看结论。
> **不要手工编辑**——下一次运行会覆盖。

生成时间（UTC）：`2026-11-07T14:22:34Z`　触发：`workflow_run`

## 各工作流最近一次

| | 工作流 | 结论 | 提交 | 事件 | 运行 |
|---|---|---|---|---|---|
| ❌ | Android APK | `failure` | `a6b4e42` | `push` | [#116](https://github.com/73kyyy/NairoClip/actions/runs/42380761167) |
| ✅ | Android Cross-Compile | `success` | `a9d78ef` | `push` | [#72](https://github.com/73kyyy/NairoClip/actions/runs/42376501659) |
| ⏳ | CI | `running` | `a6b4e42` | `push` | [#316](https://github.com/73kyyy/NairoClip/actions/runs/42380761058) |
| ⏳ | CI Status | `running` | `a6b4e42` | `workflow_run` | [#388](https://github.com/73kyyy/NairoClip/actions/runs/42381293450) |
| ✅ | Report | `success` | `a6b4e42` | `workflow_run` | [#196](https://github.com/73kyyy/NairoClip/actions/runs/42381280281) |

## 失败细节

### Android APK · run #116 · `a6b4e42`

链接：https://github.com/73kyyy/NairoClip/actions/runs/42380761167

**失败点：**

- `Build APK` → `Assemble release APK`（failure）

<details><summary>失败日志尾部（截断）</summary>

```
… 省略前半段（超字节上限）…
rnal.reflect.NativeMethodAccessorImpl.invoke(NativeMethodAccessorImpl.java:77)
Build APK	Assemble release APK	2026-11-07T14:22:03.4834309Z 	at java.base/jdk.internal.reflect.DelegatingMethodAccessorImpl.invoke(DelegatingMethodAccessorImpl.java:43)
Build APK	Assemble release APK	2026-11-07T14:22:03.4835321Z 	at org.gradle.internal.reflect.JavaMethod.invoke(JavaMethod.java:125)
Build APK	Assemble release APK	2026-11-07T14:22:03.4835948Z 	... 119 more
Build APK	Assemble release APK	2026-11-07T14:22:03.4837490Z Caused by: com.android.ide.common.process.ProcessException: Error while executing process /usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja with arguments {-C /home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a nc_jni}
Build APK	Assemble release APK	2026-11-07T14:22:03.4839467Z 	at com.android.build.gradle.internal.process.GradleProcessResult.buildProcessException(GradleProcessResult.java:73)
Build APK	Assemble release APK	2026-11-07T14:22:03.4840797Z 	at com.android.build.gradle.internal.process.GradleProcessResult.assertNormalExitValue(GradleProcessResult.java:48)
Build APK	Assemble release APK	2026-11-07T14:22:03.4842002Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt.execute(ExecuteProcess.kt:277)
Build APK	Assemble release APK	2026-11-07T14:22:03.4842745Z 	... 136 more
Build APK	Assemble release APK	2026-11-07T14:22:03.4843638Z Caused by: org.gradle.process.internal.ExecException: Process 'command '/usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja'' finished with non-zero exit value 1
Build APK	Assemble release APK	2026-11-07T14:22:03.4844956Z 	at org.gradle.process.internal.DefaultExecHandle$ExecResultImpl.assertNormalExitValue(DefaultExecHandle.java:442)
Build APK	Assemble release APK	2026-11-07T14:22:03.4846196Z 	at com.android.build.gradle.internal.process.GradleProcessResult.assertNormalExitValue(GradleProcessResult.java:46)
Build APK	Assemble release APK	2026-11-07T14:22:03.4847063Z 	... 137 more
Build APK	Assemble release APK	2026-11-07T14:22:03.4847481Z 
Build APK	Assemble release APK	2026-11-07T14:22:03.4847633Z 
Build APK	Assemble release APK	2026-11-07T14:22:03.4847898Z BUILD FAILED in 14s
Build APK	Assemble release APK	2026-11-07T14:22:03.4848326Z 20 actionable tasks: 20 executed
Build APK	Assemble release APK	2026-11-07T14:22:03.8561267Z =========== GRADLE 错误行（job 日志可见）===========
Build APK	Assemble release APK	2026-11-07T14:22:03.8577756Z 41:C/C++: /home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp:2496:74: error: unknown type name 'jint32_t'
Build APK	Assemble release APK	2026-11-07T14:22:03.8578369Z 50:* What went wrong:
Build APK	Assemble release APK	2026-11-07T14:22:03.8578674Z 51:Execution failed for task ':app:buildCMakeRelWithDebInfo[arm64-v8a]'.
Build APK	Assemble release APK	2026-11-07T14:22:03.8579197Z 56:  /home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp:2496:74: error: unknown type name 'jint32_t'
Build APK	Assemble release APK	2026-11-07T14:22:03.8579878Z 75:org.gradle.api.tasks.TaskExecutionException: Execution failed for task ':app:buildCMakeRelWithDebInfo[arm64-v8a]'.
Build APK	Assemble release APK	2026-11-07T14:22:03.8581043Z 108:Caused by: org.gradle.internal.UncheckedException: com.android.ide.common.process.ProcessException: ninja: Entering directory `/home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a'
Build APK	Assemble release APK	2026-11-07T14:22:03.8581928Z 112:/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp:2496:74: error: unknown type name 'jint32_t'
Build APK	Assemble release APK	2026-11-07T14:22:03.8582699Z 246:Caused by: com.android.ide.common.process.ProcessException: ninja: Entering directory `/home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a'
Build APK	Assemble release APK	2026-11-07T14:22:03.8583467Z 250:/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp:2496:74: error: unknown type name 'jint32_t'
Build APK	Assemble release APK	2026-11-07T14:22:03.8584551Z 281:Caused by: com.android.ide.common.process.ProcessException: Error while executing process /usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja with arguments {-C /home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a nc_jni}
Build APK	Assemble release APK	2026-11-07T14:22:03.8585629Z 286:Caused by: org.gradle.process.internal.ExecException: Process 'command '/usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja'' finished with non-zero exit value 1
Build APK	Assemble release APK	2026-11-07T14:22:03.8588584Z =========== 首个 e: 前后 20 行 ===========
Build APK	Assemble release APK	2026-11-07T14:22:03.8608635Z （无 e: 行；末尾 150 行）
Build APK	Assemble release APK	2026-11-07T14:22:03.8617495Z 	at org.gradle.internal.execution.steps.ExecuteStep.access$000(ExecuteStep.java:44)
Build APK	Assemble release APK	2026-11-07T14:22:03.8618337Z 	at org.gradle.internal.execution.steps.ExecuteStep$1.call(ExecuteStep.java:59)
Build APK	Assemble release APK	2026-11-07T14:22:03.8618851Z 	at org.gradle.internal.execution.steps.ExecuteStep$1.call(ExecuteStep.java:56)
Build APK	Assemble release APK	2026-11-07T14:22:03.8619540Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$CallableBuildOperationWorker.execute(DefaultBuildOperationRunner.java:200)
Build APK	Assemble release APK	2026-11-07T14:22:03.8620391Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$CallableBuildOperationWorker.execute(DefaultBuildOperationRunner.java:195)
Build APK	Assemble release APK	2026-11-07T14:22:03.8621152Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$2.execute(DefaultBuildOperationRunner.java:66)
Build APK	Assemble release APK	2026-11-07T14:22:03.8621812Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$2.execute(DefaultBuildOperationRunner.java:59)
Build APK	Assemble release APK	2026-11-07T14:22:03.8622474Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner.execute(DefaultBuildOperationRunner.java:157)
Build APK	Assemble release APK	2026-11-07T14:22:03.8623128Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner.execute(DefaultBuildOperationRunner.java:59)
Build APK	Assemble release APK	2026-11-07T14:22:03.8623786Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner.call(DefaultBuildOperationRunner.java:53)
Build APK	Assemble release APK	2026-11-07T14:22:03.8624445Z 	at org.gradle.internal.operations.DefaultBuildOperationExecutor.call(DefaultBuildOperationExecutor.java:73)
Build APK	Assemble release APK	2026-11-07T14:22:03.8625040Z 	at org.gradle.internal.execution.steps.ExecuteStep.execute(ExecuteStep.java:56)
Build APK	Assemble release APK	2026-11-07T14:22:03.8625532Z 	at org.gradle.internal.execution.steps.ExecuteStep.execute(ExecuteStep.java:44)
Build APK	Assemble release APK	2026-11-07T14:22:03.8626301Z 	at org.gradle.internal.execution.steps.CancelExecutionStep.execute(CancelExecutionStep.java:41)
Build APK	Assemble release APK	2026-11-07T14:22:03.8626892Z 	at org.gradle.internal.execution.steps.TimeoutStep.executeWithoutTimeout(TimeoutStep.java:74)
Build APK	Assemble release APK	2026-11-07T14:22:03.8627672Z 	at org.gradle.internal.execution.steps.TimeoutStep.execute(TimeoutStep.java:55)
Build APK	Assemble release APK	2026-11-07T14:22:03.8628602Z 	at org.gradle.internal.execution.steps.PreCreateOutputParentsStep.execute(PreCreateOutputParentsStep.java:50)
Build APK	Assemble release APK	2026-11-07T14:22:03.8629678Z 	at org.gradle.internal.execution.steps.PreCreateOutputParentsStep.execute(PreCreateOutputParentsStep.java:28)
Build APK	Assemble release APK	2026-11-07T14:22:03.8630718Z 	at org.gradle.internal.execution.steps.RemovePreviousOutputsStep.execute(RemovePreviousOutputsStep.java:67)
Build APK	Assemble release APK	2026-11-07T14:22:03.8631981Z 	at org.gradle.internal.execution.steps.RemovePreviousOutputsStep.execute(RemovePreviousOutputsStep.java:37)
Build APK	Assemble release APK	2026-11-07T14:22:03.8633088Z 	at org.gradle.internal.execution.steps.BroadcastChangingOutputsStep.execute(BroadcastChangingOutputsStep.java:61)
Build APK	Assemble release APK	2026-11-07T14:22:03.8634229Z 	at org.gradle.internal.execution.steps.BroadcastChangingOutputsStep.execute(BroadcastChangingOutputsStep.java:26)
Build APK	Assemble release APK	2026-11-07T14:22:03.8635416Z 	at org.gradle.internal.execution.steps.CaptureOutputsAfterExecutionStep.execute(CaptureOutputsAfterExecutionStep.java:67)
Build APK	Assemble release APK	2026-11-07T14:22:03.8636634Z 	at org.gradle.internal.execution.steps.CaptureOutputsAfterExecutionStep.execute(CaptureOutputsAfterExecutionStep.java:45)
Build APK	Assemble release APK	2026-11-07T14:22:03.8637996Z 	at org.gradle.internal.execution.steps.ResolveInputChangesStep.execute(ResolveInputChangesStep.java:40)
Build APK	Assemble release APK	2026-11-07T14:22:03.8638990Z 	at org.gradle.internal.execution.steps.ResolveInputChangesStep.execute(ResolveInputChangesStep.java:29)
Build APK	Assemble release APK	2026-11-07T14:22:03.8639962Z 	at org.gradle.internal.execution.steps.BuildCacheStep.executeWithoutCache(BuildCacheStep.java:189)
Build APK	Assemble release APK	2026-11-07T14:22:03.8641072Z 	at org.gradle.internal.execution.steps.BuildCacheStep.lambda$execute$1(BuildCacheStep.java:75)
Build APK	Assemble release APK	2026-11-07T14:22:03.8641771Z 	at org.gradle.internal.Either$Right.fold(Either.java:175)
Build APK	Assemble release APK	2026-11-07T14:22:03.8642285Z 	at org.gradle.internal.execution.caching.CachingState.fold(CachingState.java:62)
Build APK	Assemble release APK	2026-11-07T14:22:03.8642758Z 	at org.gradle.internal.execution.steps.BuildCacheStep.execute(BuildCacheStep.java:73)
Build APK	Assemble release APK	2026-11-07T14:22:03.8643233Z 	at org.gradle.internal.execution.steps.BuildCacheStep.execute(BuildCacheStep.java:48)
Build APK	Assemble release APK	2026-11-07T14:22:03.8643765Z 	at org.gradle.internal.execution.steps.StoreExecutionStateStep.execute(StoreExecutionStateStep.java:46)
Build APK	Assemble release APK	2026-11-07T14:22:03.8644342Z 	at org.gradle.internal.execution.steps.StoreExecutionStateStep.execute(StoreExecutionStateStep.java:35)
Build APK	Assemble release APK	2026-11-07T14:22:03.8645300Z 	at org.gradle.internal.execution.steps.SkipUpToDateStep.executeBecause(SkipUpToDateStep.java:76)
Build APK	Assemble release APK	2026-11-07T14:22:03.8646237Z 	at org.gradle.internal.execution.steps.SkipUpToDateStep.lambda$execute$2(SkipUpToDateStep.java:54)
Build APK	Assemble release APK	2026-11-07T14:22:03.8647110Z 	at org.gradle.internal.execution.steps.SkipUpToDateStep.execute(SkipUpToDateStep.java:54)
Build APK	Assemble release APK	2026-11-07T14:22:03.8648277Z 	at org.gradle.internal.execution.steps.SkipUpToDateStep.execute(SkipUpToDateStep.java:36)
Build APK	Assemble release APK	2026-11-07T14:22:03.8649598Z 	at org.gradle.internal.execution.steps.legacy.MarkSnapshottingInputsFinishedStep.execute(MarkSnapshottingInputsFinishedStep.java:37)
Build APK	Assemble release APK	2026-11-07T14:22:03.8651065Z 	at org.gradle.internal.execution.steps.legacy.MarkSnapshottingInputsFinishedStep.execute(MarkSnapshottingInputsFinishedStep.java:27)
Build APK	Assemble release APK	2026-11-07T14:22:03.8652430Z 	at org.gradle.internal.execution.steps.ResolveIncrementalCachingStateStep.executeDelegate(ResolveIncrementalCachingStateStep.java:49)
Build APK	Assemble release APK	2026-11-07T14:22:03.8653844Z 	at org.gradle.internal.execution.steps.ResolveIncrementalCachingStateStep.executeDelegate(ResolveIncrementalCachingStateStep.java:27)
Build APK	Assemble release APK	2026-11-07T14:22:03.8655244Z 	at org.gradle.internal.execution.steps.AbstractResolveCachingStateStep.execute(AbstractResolveCachingStateStep.java:71)
Build APK	Assemble release APK	2026-11-07T14:22:03.8656444Z 	at org.gradle.internal.execution.steps.AbstractResolveCachingStateStep.execute(AbstractResolveCachingStateStep.java:39)
Build APK	Assemble release APK	2026-11-07T14:22:03.8657820Z 	at org.gradle.internal.execution.steps.ResolveChangesStep.execute(ResolveChangesStep.java:65)
Build APK	Assemble release APK	2026-11-07T14:22:03.8658715Z 	at org.gradle.internal.execution.steps.ResolveChangesStep.execute(ResolveChangesStep.java:36)
Build APK	Assemble release APK	2026-11-07T14:22:03.8659398Z 	at org.gradle.internal.execution.steps.ValidateStep.execute(ValidateStep.java:106)
Build APK	Assemble release APK	2026-11-07T14:22:03.8659871Z 	at org.gradle.internal.execution.steps.ValidateStep.execute(ValidateStep.java:55)
Build APK	Assemble release APK	2026-11-07T14:22:03.8660508Z 	at org.gradle.internal.execution.steps.AbstractCaptureStateBeforeExecutionStep.execute(AbstractCaptureStateBeforeExecutionStep.java:64)
Build APK	Assemble release APK	2026-11-07T14:22:03.8661410Z 	at org.gradle.internal.execution.steps.AbstractCaptureStateBeforeExecutionStep.execute(AbstractCaptureStateBeforeExecutionStep.java:43)
Build APK	Assemble release APK	2026-11-07T14:22:03.8662210Z 	at org.gradle.internal.execution.steps.AbstractSkipEmptyWorkStep.executeWithNonEmptySources(AbstractSkipEmptyWorkStep.java:125)
Build APK	Assemble release APK	2026-11-07T14:22:03.8663362Z 	at org.gradle.internal.execution.steps.AbstractSkipEmptyWorkStep.execute(AbstractSkipEmptyWorkStep.java:56)
Build APK	Assemble release APK	2026-11-07T14:22:03.8664293Z 	at org.gradle.internal.execution.steps.AbstractSkipEmptyWorkStep.execute(AbstractSkipEmptyWorkStep.java:36)
Build APK	Assemble release APK	2026-11-07T14:22:03.8665496Z 	at org.gradle.internal.execution.steps.legacy.MarkSnapshottingInputsStartedStep.execute(MarkSnapshottingInputsStartedStep.java:38)
Build APK	Assemble release APK	2026-11-07T14:22:03.8666901Z 	at org.gradle.internal.execution.steps.LoadPreviousExecutionStateStep.execute(LoadPreviousExecutionStateStep.java:36)
Build APK	Assemble release APK	2026-11-07T14:22:03.8667768Z 	at org.gradle.internal.execution.steps.LoadPreviousExecutionStateStep.execute(LoadPreviousExecutionStateStep.java:23)
Build APK	Assemble release APK	2026-11-07T14:22:03.8668580Z 	at org.gradle.internal.execution.steps.HandleStaleOutputsStep.execute(HandleStaleOutputsStep.java:75)
Build APK	Assemble release APK	2026-11-07T14:22:03.8669456Z 	at org.gradle.internal.execution.steps.HandleStaleOutputsStep.execute(HandleStaleOutputsStep.java:41)
Build APK	Assemble release APK	2026-11-07T14:22:03.8670514Z 	at org.gradle.internal.execution.steps.AssignMutableWorkspaceStep.lambda$execute$0(AssignMutableWorkspaceStep.java:35)
Build APK	Assemble release APK	2026-11-07T14:22:03.8671535Z 	at org.gradle.api.internal.tasks.execution.TaskExecution$4.withWorkspace(TaskExecution.java:289)
Build APK	Assemble release APK	2026-11-07T14:22:03.8672506Z 	at org.gradle.internal.execution.steps.AssignMutableWorkspaceStep.execute(AssignMutableWorkspaceStep.java:31)
Build APK	Assemble release APK	2026-11-07T14:22:03.8673704Z 	at org.gradle.internal.execution.steps.AssignMutableWorkspaceStep.execute(AssignMutableWorkspaceStep.java:22)
Build APK	Assemble release APK	2026-11-07T14:22:03.8674676Z 	at org.gradle.internal.execution.steps.ChoosePipelineStep.execute(ChoosePipelineStep.java:40)
Build APK	Assemble release APK	2026-11-07T14:22:03.8675561Z 	at org.gradle.internal.execution.steps.ChoosePipelineStep.execute(ChoosePipelineStep.java:23)
Build APK	Assemble release APK	2026-11-07T14:22:03.8676687Z 	at org.gradle.internal.execution.steps.ExecuteWorkBuildOperationFiringStep.lambda$execute$2(ExecuteWorkBuildOperationFiringStep.java:67)
Build APK	Assemble release APK	2026-11-07T14:22:03.8678170Z 	at org.gradle.internal.execution.steps.ExecuteWorkBuildOperationFiringStep.execute(ExecuteWorkBuildOperationFiringStep.java:67)
Build APK	Assemble release APK	2026-11-07T14:22:03.8679495Z 	at org.gradle.internal.execution.steps.ExecuteWorkBuildOperationFiringStep.execute(ExecuteWorkBuildOperationFiringStep.java:39)
Build APK	Assemble release APK	2026-11-07T14:22:03.8680585Z 	at org.gradle.internal.execution.steps.IdentityCacheStep.execute(IdentityCacheStep.java:46)
Build APK	Assemble release APK	2026-11-07T14:22:03.8681442Z 	at org.gradle.internal.execution.steps.IdentityCacheStep.execute(IdentityCacheStep.java:34)
Build APK	Assemble release APK	2026-11-07T14:22:03.8682255Z 	at org.gradle.internal.execution.steps.IdentifyStep.execute(IdentifyStep.java:48)
Build APK	Assemble release APK	2026-11-07T14:22:03.8683022Z 	at org.gradle.internal.execution.steps.IdentifyStep.execute(IdentifyStep.java:35)
Build APK	Assemble release APK	2026-11-07T14:22:03.8683735Z 	at org.gradle.internal.execution.impl.DefaultExecutionEngine$1.execute(DefaultExecutionEngine.java:61)
Build APK	Assemble release APK	2026-11-07T14:22:03.8684400Z 	at org.gradle.api.internal.tasks.execution.ExecuteActionsTaskExecuter.executeIfValid(ExecuteActionsTaskExecuter.java:127)
Build APK	Assemble release APK	2026-11-07T14:22:03.8685256Z 	at org.gradle.api.internal.tasks.execution.ExecuteActionsTaskExecuter.execute(ExecuteActionsTaskExecuter.java:116)
Build APK	Assemble release APK	2026-11-07T14:22:03.8685939Z 	at org.gradle.api.internal.tasks.execution.FinalizePropertiesTaskExecuter.execute(FinalizePropertiesTaskExecuter.java:46)
Build APK	Assemble release APK	2026-11-07T14:22:03.8686665Z 	at org.gradle.api.internal.tasks.execution.ResolveTaskExecutionModeExecuter.execute(ResolveTaskExecutionModeExecuter.java:51)
Build APK	Assemble release APK	2026-11-07T14:22:03.8687857Z 	at org.gradle.api.internal.tasks.execution.SkipTaskWithNoActionsExecuter.execute(SkipTaskWithNoActionsExecuter.java:57)
Build APK	Assemble release APK	2026-11-07T14:22:03.8688784Z 	at org.gradle.api.internal.tasks.execution.SkipOnlyIfTaskExecuter.execute(SkipOnlyIfTaskExecuter.java:74)
Build APK	Assemble release APK	2026-11-07T14:22:03.8689421Z 	at org.gradle.api.internal.tasks.execution.CatchExceptionTaskExecuter.execute(CatchExceptionTaskExecuter.java:36)
Build APK	Assemble release APK	2026-11-07T14:22:03.8690065Z 	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter$1.executeTask(EventFiringTaskExecuter.java:77)
Build APK	Assemble release APK	2026-11-07T14:22:03.8690682Z 	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter$1.call(EventFiringTaskExecuter.java:55)
Build APK	Assemble release APK	2026-11-07T14:22:03.8691413Z 	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter$1.call(EventFiringTaskExecuter.java:52)
Build APK	Assemble release APK	2026-11-07T14:22:03.8692111Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$CallableBuildOperationWorker.execute(DefaultBuildOperationRunner.java:200)
Build APK	Assemble release APK	2026-11-07T14:22:03.8693365Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$CallableBuildOperationWorker.execute(DefaultBuildOperationRunner.java:195)
Build APK	Assemble release APK	2026-11-07T14:22:03.8694558Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$2.execute(DefaultBuildOperationRunner.java:66)
Build APK	Assemble release APK	2026-11-07T14:22:03.8695167Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner$2.execute(DefaultBuildOperationRunner.java:59)
Build APK	Assemble release APK	2026-11-07T14:22:03.8695964Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner.execute(DefaultBuildOperationRunner.java:157)
Build APK	Assemble release APK	2026-11-07T14:22:03.8697116Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner.execute(DefaultBuildOperationRunner.java:59)
Build APK	Assemble release APK	2026-11-07T14:22:03.8698287Z 	at org.gradle.internal.operations.DefaultBuildOperationRunner.call(DefaultBuildOperationRunner.java:53)
Build APK	Assemble release APK	2026-11-07T14:22:03.8699321Z 	at org.gradle.internal.operations.DefaultBuildOperationExecutor.call(DefaultBuildOperationExecutor.java:73)
Build APK	Assemble release APK	2026-11-07T14:22:03.8700471Z 	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter.execute(EventFiringTaskExecuter.java:52)
Build APK	Assemble release APK	2026-11-07T14:22:03.8701394Z 	at org.gradle.execution.plan.LocalTaskNodeExecutor.execute(LocalTaskNodeExecutor.java:42)
Build APK	Assemble release APK	2026-11-07T14:22:03.8702428Z 	at org.gradle.execution.taskgraph.DefaultTaskExecutionGraph$InvokeNodeExecutorsAction.execute(DefaultTaskExecutionGraph.java:331)
Build APK	Assemble release APK	2026-11-07T14:22:03.8703684Z 	at org.gradle.execution.taskgraph.DefaultTaskExecutionGraph$InvokeNodeExecutorsAction.execute(DefaultTaskExecutionGraph.java:318)
Build APK	Assemble release APK	2026-11-07T14:22:03.8704978Z 	at org.gradle.execution.taskgraph.DefaultTaskExecutionGraph$BuildOperationAwareExecutionAction.lambda$execute$0(DefaultTaskExecutionGraph.java:314)
Build APK	Assemble release APK	2026-11-07T14:22:03.8706185Z 	at org.gradle.internal.operations.CurrentBuildOperationRef.with(CurrentBuildOperationRef.java:80)
Build APK	Assemble release APK	2026-11-07T14:22:03.8707485Z 	at org.gradle.execution.taskgraph.DefaultTaskExecutionGraph$BuildOperationAwareExecutionAction.execute(DefaultTaskExecutionGraph.java:314)
Build APK	Assemble release APK	2026-11-07T14:22:03.8708886Z 	at org.gradle.execution.taskgraph.DefaultTaskExecutionGraph$BuildOperationAwareExecutionAction.execute(DefaultTaskExecutionGraph.java:303)
Build APK	Assemble release APK	2026-11-07T14:22:03.8710041Z 	at org.gradle.execution.plan.DefaultPlanExecutor$ExecutorWorker.execute(DefaultPlanExecutor.java:463)
Build APK	Assemble release APK	2026-11-07T14:22:03.8710592Z 	at org.gradle.execution.plan.DefaultPlanExecutor$ExecutorWorker.run(DefaultPlanExecutor.java:380)
Build APK	Assemble release APK	2026-11-07T14:22:03.8711167Z 	at org.gradle.internal.concurrent.ExecutorPolicy$CatchAndRecordFailures.onExecute(ExecutorPolicy.java:64)
Build APK	Assemble release APK	2026-11-07T14:22:03.8711866Z 	at org.gradle.internal.concurrent.AbstractManagedExecutor$1.run(AbstractManagedExecutor.java:47)
Build APK	Assemble release APK	2026-11-07T14:22:03.8712699Z Caused by: com.android.ide.common.process.ProcessException: ninja: Entering directory `/home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a'
Build APK	Assemble release APK	2026-11-07T14:22:03.8713890Z [1/2] Building CXX object CMakeFiles/nc_jni.dir/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp.o
Build APK	Assemble release APK	2026-11-07T14:22:03.8714732Z FAILED: CMakeFiles/nc_jni.dir/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp.o 
Build APK	Assemble release APK	2026-11-07T14:22:03.8718149Z /usr/local/lib/android/sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ --target=aarch64-none-linux-android24 --sysroot=/usr/local/lib/android/sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/sysroot -Dnc_jni_EXPORTS -I/home/runner/work/NairoClip/NairoClip/android/app/src/main/cpp/../../../../../core/include -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security   -O2 -g -DNDEBUG -fPIC -std=gnu++17 -MD -MT CMakeFiles/nc_jni.dir/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp.o -MF CMakeFiles/nc_jni.dir/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp.o.d -o CMakeFiles/nc_jni.dir/home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp.o -c /home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp
Build APK	Assemble release APK	2026-11-07T14:22:03.8722158Z /home/runner/work/NairoClip/NairoClip/mobile/native/android/nc_jni.cpp:2496:74: error: unknown type name 'jint32_t'
Build APK	Assemble release APK	2026-11-07T14:22:03.8722986Z     return static_cast<jint>(nc_preview_set_cache_budget(ed, static_cast<jint32_t>(bytes)));
Build APK	Assemble release APK	2026-11-07T14:22:03.8723762Z                                                                          ^
Build APK	Assemble release APK	2026-11-07T14:22:03.8724138Z 1 error generated.
Build APK	Assemble release APK	2026-11-07T14:22:03.8724439Z ninja: build stopped: subcommand failed.
Build APK	Assemble release APK	2026-11-07T14:22:03.8724692Z 
Build APK	Assemble release APK	2026-11-07T14:22:03.8724852Z C++ build system [build] failed while executing:
Build APK	Assemble release APK	2026-11-07T14:22:03.8725294Z     /usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja \
Build APK	Assemble release APK	2026-11-07T14:22:03.8725681Z       -C \
Build APK	Assemble release APK	2026-11-07T14:22:03.8726169Z       /home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a \
Build APK	Assemble release APK	2026-11-07T14:22:03.8726756Z       nc_jni
Build APK	Assemble release APK	2026-11-07T14:22:03.8727092Z   from /home/runner/work/NairoClip/NairoClip/android/app
Build APK	Assemble release APK	2026-11-07T14:22:03.8727975Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt.execute(ExecuteProcess.kt:288)
Build APK	Assemble release APK	2026-11-07T14:22:03.8728998Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt$executeProcess$1.invoke(ExecuteProcess.kt:108)
Build APK	Assemble release APK	2026-11-07T14:22:03.8730067Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt$executeProcess$1.invoke(ExecuteProcess.kt:106)
Build APK	Assemble release APK	2026-11-07T14:22:03.8731089Z 	at com.android.build.gradle.internal.cxx.timing.TimingEnvironmentKt.time(TimingEnvironment.kt:32)
Build APK	Assemble release APK	2026-11-07T14:22:03.8732098Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt.executeProcess(ExecuteProcess.kt:106)
Build APK	Assemble release APK	2026-11-07T14:22:03.8733249Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt.executeProcess$default(ExecuteProcess.kt:85)
Build APK	Assemble release APK	2026-11-07T14:22:03.8734387Z 	at com.android.build.gradle.internal.cxx.build.CxxRegularBuilder.executeProcessBatch(CxxRegularBuilder.kt:332)
Build APK	Assemble release APK	2026-11-07T14:22:03.8735420Z 	at com.android.build.gradle.internal.cxx.build.CxxRegularBuilder.build(CxxRegularBuilder.kt:129)
Build APK	Assemble release APK	2026-11-07T14:22:03.8736464Z 	at com.android.build.gradle.tasks.ExternalNativeBuildTask$doTaskAction$$inlined$recordTaskAction$1.invoke(BaseTask.kt:70)
Build APK	Assemble release APK	2026-11-07T14:22:03.8737538Z 	at com.android.build.gradle.internal.tasks.Blocks.recordSpan(Blocks.java:51)
Build APK	Assemble release APK	2026-11-07T14:22:03.8738517Z 	at com.android.build.gradle.tasks.ExternalNativeBuildTask.doTaskAction(ExternalNativeBuildTask.kt:145)
Build APK	Assemble release APK	2026-11-07T14:22:03.8739601Z 	at com.android.build.gradle.internal.tasks.UnsafeOutputsTask$taskAction$$inlined$recordTaskAction$1.invoke(BaseTask.kt:65)
Build APK	Assemble release APK	2026-11-07T14:22:03.8740755Z 	at com.android.build.gradle.internal.tasks.Blocks.recordSpan(Blocks.java:51)
Build APK	Assemble release APK	2026-11-07T14:22:03.8741559Z 	at com.android.build.gradle.internal.tasks.UnsafeOutputsTask.taskAction(UnsafeOutputsTask.kt:63)
Build APK	Assemble release APK	2026-11-07T14:22:03.8742397Z 	at java.base/jdk.internal.reflect.NativeMethodAccessorImpl.invoke0(Native Method)
Build APK	Assemble release APK	2026-11-07T14:22:03.8743239Z 	at java.base/jdk.internal.reflect.NativeMethodAccessorImpl.invoke(NativeMethodAccessorImpl.java:77)
Build APK	Assemble release APK	2026-11-07T14:22:03.8744234Z 	at java.base/jdk.internal.reflect.DelegatingMethodAccessorImpl.invoke(DelegatingMethodAccessorImpl.java:43)
Build APK	Assemble release APK	2026-11-07T14:22:03.8745107Z 	at org.gradle.internal.reflect.JavaMethod.invoke(JavaMethod.java:125)
Build APK	Assemble release APK	2026-11-07T14:22:03.8745610Z 	... 119 more
Build APK	Assemble release APK	2026-11-07T14:22:03.8746880Z Caused by: com.android.ide.common.process.ProcessException: Error while executing process /usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja with arguments {-C /home/runner/work/NairoClip/NairoClip/android/app/.cxx/RelWithDebInfo/3h6a1m62/arm64-v8a nc_jni}
Build APK	Assemble release APK	2026-11-07T14:22:03.8748860Z 	at com.android.build.gradle.internal.process.GradleProcessResult.buildProcessException(GradleProcessResult.java:73)
Build APK	Assemble release APK	2026-11-07T14:22:03.8750068Z 	at com.android.build.gradle.internal.process.GradleProcessResult.assertNormalExitValue(GradleProcessResult.java:48)
Build APK	Assemble release APK	2026-11-07T14:22:03.8750719Z 	at com.android.build.gradle.internal.cxx.process.ExecuteProcessKt.execute(ExecuteProcess.kt:277)
Build APK	Assemble release APK	2026-11-07T14:22:03.8751104Z 	... 136 more
Build APK	Assemble release APK	2026-11-07T14:22:03.8751599Z Caused by: org.gradle.process.internal.ExecException: Process 'command '/usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja'' finished with non-zero exit value 1
Build APK	Assemble release APK	2026-11-07T14:22:03.8752319Z 	at org.gradle.process.internal.DefaultExecHandle$ExecResultImpl.assertNormalExitValue(DefaultExecHandle.java:442)
Build APK	Assemble release APK	2026-11-07T14:22:03.8753151Z 	at com.android.build.gradle.internal.process.GradleProcessResult.assertNormalExitValue(GradleProcessResult.java:46)
Build APK	Assemble release APK	2026-11-07T14:22:03.8753604Z 	... 137 more
Build APK	Assemble release APK	2026-11-07T14:22:03.8753705Z 
Build APK	Assemble release APK	2026-11-07T14:22:03.8753710Z 
Build APK	Assemble release APK	2026-11-07T14:22:03.8753787Z BUILD FAILED in 14s
Build APK	Assemble release APK	2026-11-07T14:22:03.8753974Z 20 actionable tasks: 20 executed
Build APK	Assemble release APK	2026-11-07T14:22:03.8754188Z ===============================================
Build APK	Assemble release APK	2026-11-07T14:22:03.8756560Z ##[error]Process completed with exit code 1.
```

</details>

## 最近运行

| | 工作流 | 结论 | 提交 | 创建时间 |
|---|---|---|---|---|
| ⏳ | CI Status | `running` | `a6b4e42` | 2026-11-07T14:22:24Z |
| ✅ | Report | `success` | `a6b4e42` | 2026-11-07T14:22:14Z |
| ⚪ | CI Status | `cancelled` | `a6b4e42` | 2026-11-07T14:22:14Z |
| ✅ | CI Status | `success` | `a6b4e42` | 2026-11-07T14:20:53Z |
| ✅ | Report | `success` | `a6b4e42` | 2026-11-07T14:20:41Z |
| ⚪ | CI Status | `cancelled` | `a6b4e42` | 2026-11-07T14:20:41Z |
| ❌ | Android APK | `failure` | `a6b4e42` | 2026-11-07T14:15:41Z |
| ⏳ | CI | `running` | `a6b4e42` | 2026-11-07T14:15:41Z |
| ✅ | CI Status | `success` | `bef0b74` | 2026-11-07T14:09:43Z |
| ⚪ | CI Status | `cancelled` | `bef0b74` | 2026-11-07T14:09:31Z |
| ✅ | Report | `success` | `bef0b74` | 2026-11-07T14:09:30Z |
| ❌ | Android APK | `failure` | `bef0b74` | 2026-11-07T14:03:26Z |
| ⏳ | CI | `running` | `bef0b74` | 2026-11-07T14:03:26Z |
| ✅ | CI Status | `success` | `33546be` | 2026-11-07T13:59:26Z |
| ⚪ | CI Status | `cancelled` | `33546be` | 2026-11-07T13:59:13Z |
| ✅ | Report | `success` | `33546be` | 2026-11-07T13:59:13Z |
| ✅ | CI Status | `success` | `33546be` | 2026-11-07T13:58:44Z |
| ✅ | Report | `success` | `33546be` | 2026-11-07T13:58:33Z |
| ✅ | CI Status | `success` | `33546be` | 2026-11-07T13:58:33Z |
| ❌ | Android APK | `failure` | `33546be` | 2026-11-07T13:55:36Z |

