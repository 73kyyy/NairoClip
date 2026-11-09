# NairoClip P0—P6 分阶段整改执行计划 V1.0

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-PLAN-00-07 |
| 文档性质 | 实施计划（第一轮交付物之二） |
| 依据 | 《NC-RECT-00-07 整改与实施任务书》第十二、十三章；《NC-AUDIT-00-07 审计报告》 |
| 基线 | `main` @ `90ebd9bb` |
| 状态 | 待批准，**尚未执行任何代码改动** |

---

## 0. 总体策略

### 0.1 依赖关系

```
P0 构建恢复 ──┬─→ P1 契约层 ──→ P2 JNI/C ABI ──→ P3 线程/生命周期 ──┬─→ P4 GPU 渲染 ──┐
              │                     │                              └─→ P5 原生 UI ────┴─→ P6 质量收口
              └─────────────────────┴──→ P6 设备画像（自 P0 起持续采集）
```

**硬约束**：
- P0 不通过，**不允许**进入 P1 及以后（任务书 §3.1）。
- P1 不完成，P2/P4/P5 **禁止**大规模改动（否则接口返工）。
- P6 从 P0 起贯穿，不放到最后。
- P4 与 P5 在 P2/P3 稳定后可并行，但共用同一 Editor API 与同一工程数据源。

### 0.2 三条不可逾越的红线

1. **不动现有 332 个 `nc_*` 签名**——新增契约层并行，旧接口标注弃用。
2. **不删任何现有功能代码**——WebView 保留到 P5 功能对等并通过回归。
3. **不在本地编译**——全部验证走 GitHub Actions（`ci-status` 分支回读结果）。

---

## P0 构建系统与基础工程恢复

**优先级**：最高 · **规模**：S · **前置**：无

### P0.1 目标

让 `Android APK`、`C++ Core`、`JNI`、`单元测试` 四条流水线全部转绿，产出可安装 APK。

### P0.2 规范依据

任务书 §3.2–§3.5；规范 00 §7（发布阻断项）。

### P0.3 现状证据（2026-11-09 按 CI 实测更新）

| 事实 | 出处 |
|---|---|
| `Android APK` **实为 success**（`HANDOFF.md` 的"APK 构建失败"已过期） | CI run 42429168792，HEAD `90ebd9bb` |
| 真正的红灯是 `CI` → `build-and-test` 第 10 步「C ABI 调用点检查」 | CI run 42429168790 |
| 根因：`tools/check_capi_calls.py` 未剥注释，把 `capi.cpp:2793` 注释里的 `nc_surge_preset_describe()` 当成 0 参数调用点 | 本地复现一致 |
| **已修复**：新增 `mask_comments()`，CI 增加自检步骤，13 个用例 | PR #1 → main `61dc318e` |
| `build-and-test` 已转绿（含全量用例与用例数对账） | CI run 42586291600 |
| `build.gradle.kts` 已把 `mobile/native/android` 加入 `java.srcDir` 并删除旧副本 | `android/app/build.gradle.kts:20-32` |
| 签名口令有弱默认值 `"nairoclip"` | `build.gradle.kts:89-91` |

### P0.4 任务分解

| ID | 任务 | 产物 |
|---|---|---|
| ~~T0.1~~ | ~~读取 `latest.json` 定位失败步骤~~ | ✅ 已完成：失败点为「C ABI 调用点检查」 |
| ~~T0.2~~ | ~~锁定 Kotlin 编译错误~~ | ✅ 不适用：APK 本就通过，无需修 Kotlin |
| T0.3 | 修正 Gradle 依赖/源码集（同名类重复、Unresolved reference） | `build.gradle.kts` 修改 |
| T0.4 | 验证 NDK 交叉编译 `libnairoclip_core.a` 与 `nc_jni.so` 链接 | CI 绿 |
| T0.5 | 验证 FFmpeg `.so` 落位 `jniLibs/arm64-v8a/` | CI 绿 |
| T0.6 | 移除签名口令弱默认值：缺失即构建失败 | 安全修复 |
| T0.7 | CI 增加 APK 产物冒烟：`aapt dump badging` + 安装启动检查 | workflow 修改 |

### P0.5 涉及文件

| 类型 | 文件 |
|---|---|
| 修改 | `android/app/build.gradle.kts`、`.github/workflows/android-apk.yml` |
| 可能修改 | `android/app/src/main/cpp/CMakeLists.txt`、`MainActivity.kt` |
| 新增 | `.github/workflows/apk-smoke.yml`（可选） |
| 删除 | 无 |

### P0.6 风险与缓解

| 风险 | 缓解 |
|---|---|
| Kotlin 错误在 CI 才暴露，本地无法复现 | 依赖 `gradle-log` artifact + `--console=plain` 全量日志 |
| 修复引入新依赖导致体积/兼容问题 | 仅允许 androidx 同族依赖，版本锁定 |
| 签名口令改为必填阻断现有 CI | 先在 workflow 注入 secrets，再切必填 |

### P0.7 测试计划

1. `ci.yml`（Linux 三平台构建 + ctest）绿
2. `android-build.yml`（core 交叉编译）绿
3. `android-apk.yml`（APK 打包）绿
4. APK 在真机/模拟器安装并启动到首页

### P0.8 回滚方案

每个修复独立提交；任一提交导致回归即 `git revert <sha>`，不回滚整阶段。

### P0.9 验收标准（对应任务书 §3.5）

- [ ] Android 工程真实编译通过
- [ ] C++ 核心真实编译通过
- [ ] JNI 正确链接（`-Wl,--no-undefined` 通过）
- [ ] APK 正常生成
- [ ] APK 可安装、可启动、核心页面可打开
- [ ] CI 执行关键检查
- [ ] 失败原因有记录、产物与日志可追溯

---

## P1 统一数据模型与跨层接口契约

**优先级**：最高 · **规模**：L · **前置**：P0

### P1.1 目标

建立 `Kotlin ↔ JNI ↔ C ABI ↔ C++ Core` 的统一版本化契约，实现 Command / Query / Event / Task 四类分离，并建立**单一接口定义源**。

### P1.2 规范依据

任务书 §4.1–§4.6；规范 03 §3–§5、规范 07 §4–§7、§11。

### P1.3 现状证据

| 事实 | 证据 |
|---|---|
| 命令只有 `apply/revert` | `core/include/nairoclip/core/command.h` |
| `commandId`/`schemaVersion`/`expectedRevision`/`transactionId` 全仓命中 0 | 检索 |
| 错误码只有 9 值枚举 | `core/include/nairoclip/core/types.h` |
| 无 `NC-` 错误码 | 检索命中 0 |
| 无事件总线 | `eventBus`/`subscribe`/`EventListener` 命中 0 |
| 有可复用的派发生成器 | `design/tools/gen_api_dispatch.py` |

### P1.4 任务分解

| ID | 任务 | 产物 |
|---|---|---|
| T1.1 | 新建 `core/include/nairoclip/core/contract/error.h`：`NC-<DOMAIN>-<CODE>` 八域错误码 + `ErrorDetail{code,message,field,recoverable,diagnostic}` | 错误码体系 |
| T1.2 | 新建 `contract/command.h`：`CommandRequest{schemaVersion, commandId, projectId, expectedRevision, commandType, payload, origin, transactionId}` + `CommandResult{status, newRevision, changeSetId, undoAvailable, error}` | 命令信封 |
| T1.3 | 新建 `contract/query.h`：`QueryRequest` / `QueryResult{revision, page, projection}`，支持视口分页 | 查询契约 |
| T1.4 | 新建 `contract/event.h`：`EventEnvelope{schemaVersion,eventId,eventType,projectId,revision,occurredAt,correlationId,payload}` + `EventBus`（订阅/退订/合并/丢弃策略/revision 跳变触发重拉） | 事件通道 |
| T1.5 | 新建 `contract/task.h`：`TaskHandle{taskId,type,state,progress,cancelToken}`，覆盖导入/缩略图/代理/导出/AI | 任务模型 |
| T1.6 | 为 `TimelineEditor` 增加 `applyCommand(CommandRequest)` 统一入口，内部映射到现有 `Command` 子类（**不改现有命令类**） | 门面扩展 |
| T1.7 | 新建 `core/src/contract/*.cpp` 实现 | 实现 |
| T1.8 | **统一接口定义源**：在 `tools/` 新增 `gen_contract.py`，从 `capi.h` + contract 头生成 Kotlin 常量、JNI 映射表、API 文档、契约测试桩；生成物带「自动生成，请勿手改」头 | 代码生成器 |
| T1.9 | 契约测试 `tests/test_contract_error.cpp` / `_command.cpp` / `_event.cpp` / `_task.cpp` | 测试 |
| T1.10 | 完成一条**端到端纵向切片**：`clip.move` 走完整信封链路（Kotlin→JNI→C ABI→Core→事件→Kotlin） | 切片 |

### P1.5 涉及文件

| 类型 | 文件 |
|---|---|
| 新增 | `core/include/nairoclip/core/contract/{error,command,query,event,task,version}.h`（6）<br>`core/src/contract/{error,command,query,event,task}.cpp`（5）<br>`tools/gen_contract.py`、`tools/contract_schema.json`<br>`tests/test_contract_*.cpp`（4–6） |
| 修改 | `core/include/nairoclip/core/editor.h`（新增 `applyCommand`）<br>`core/src/editor.cpp`<br>`core/CMakeLists.txt`<br>`CMakeLists.txt`（若需注册生成目标） |
| 删除 | 无 |

### P1.6 风险与缓解

| 风险 | 缓解 |
|---|---|
| 信封层与现有 `Command` 双轨 | 信封是**包装**不是替代；现有 114 个 JNI op 继续可用，逐步切换 |
| revision 语义与 `editRevision` 冲突 | 新增 `ProjectRevision`（工程级、单调、持久化），`editRevision` 保留为内部缓存签名 |
| 代码生成器成为新的事实源 | 生成物只从 `capi.h`/`contract/*.h` 派生，PR 必须带 `--check` 一致性校验 |
| 契约设计过度抽象 | 每条字段必须能映射到规范原文条款，不自创字段 |

### P1.7 测试计划

1. 错误码映射测试：每个 `ErrorCode` → 唯一 `NC-*` 码
2. 命令幂等测试：同 `commandId` 重复提交返回同结果
3. revision 冲突测试：`expectedRevision` 过期返回 `NC-PROJECT-REVISION_CONFLICT`
4. 事件顺序/重复/丢失/重连快照测试
5. 跨语言 round-trip：Kotlin 序列化 → C ABI → C++ → 回传字段不丢
6. `gen_contract.py --check` 在 CI 中校验生成物与头文件一致

### P1.8 回滚方案

契约层为纯新增，回滚只需 `git revert` 引入提交，旧链路不受影响。

### P1.9 验收标准（对应任务书 §4.6）

- [ ] 核心接口清单完整（覆盖工程/序列/轨道/片段/时间轴/关键帧/媒体/预览/导出 9 域）
- [ ] Command/Query/Event/Task 明确分离
- [ ] 请求响应结构统一
- [ ] 错误码统一（8 域）
- [ ] 接口版本可追踪
- [ ] 异步任务有状态与取消
- [ ] 工程 revision 机制明确
- [ ] Kotlin/JNI/C ABI/C++ 契约一致（生成器保证）
- [ ] 至少一条端到端纵向切片
- [ ] 契约测试可验证跨层调用

---

## P2 JNI、C ABI 与 C++ 分发层整改

**优先级**：高 · **规模**：M · **前置**：P1

### P2.1 目标

把 114 个 `op == "..."` 分支的业务逻辑迁出 JNI，JNI 只保留参数转换、句柄管理、异常映射、结果回传。

### P2.2 规范依据

任务书 §5.1–§5.5；规范 01 §4、规范 03 §6。

### P2.3 现状证据

| 事实 | 证据 |
|---|---|
| `nc_jni.cpp` 113,949 字节 | 文件大小 |
| 114 个 `op == "..."` 分支 | `grep -c "op =="` |
| 20 个 `JNIEXPORT` | 同上 |
| 参数解析、状态拼装均在 JNI | `nc_jni.cpp:405+` |

### P2.4 任务分解（严格按任务书 §5.2 五步法）

| ID | 任务 | 产物 |
|---|---|---|
| T2.1 | **审计**：枚举 114 个 op，记录入参/出参/副作用/对应 C ABI | `docs/op_inventory.md` |
| T2.2 | **归类**：每个 op 映射到 Command 或 Query（禁止新增第三类） | 映射表 |
| T2.3 | **迁移**：在 C++ 新增 `EngineDispatcher::dispatch(op, payload) → Result`，业务判断下沉 | `core/src/dispatch.cpp` |
| T2.4 | **收敛**：JNI 改为「取字符串 → 调用 dispatch → 回填」，单函数不超过 20 行 | `nc_jni.cpp` 大幅缩减 |
| T2.5 | **兼容**：旧 op 名全部保留为别名，`dispatch` 内部路由 | 别名表 |
| T2.6 | **C ABI 版本化**：新增结构体带 `{size, version}` 头；旧扁平接口标注 `@deprecated`（不删） | `capi_version.h` |
| T2.7 | **句柄生命周期**：定义 Engine/Project/Sequence/Task/Media/Render 六类句柄的创建-使用-销毁规则，Kotlin 侧 `AutoCloseable` 包装 | `docs/handle_lifecycle.md` |
| T2.8 | 跨层接口测试：`tests/test_dispatch.cpp` + Kotlin 侧契约测试 | 测试 |

### P2.5 涉及文件

| 类型 | 文件 |
|---|---|
| 新增 | `core/include/nairoclip/core/dispatch.h`、`core/src/dispatch.cpp`、`core/include/nairoclip/core/capi_version.h`、`docs/op_inventory.md`、`docs/handle_lifecycle.md`、`tests/test_dispatch.cpp` |
| 修改 | `mobile/native/android/nc_jni.cpp`（大幅缩减）、`core/include/nairoclip/core/capi.h`（仅加弃用标注）、`mobile/native/android/NativeEngineBridge.kt`（句柄生命周期） |
| 删除 | 无（op 分支迁走但不删 op 名） |

### P2.6 风险与缓解

| 风险 | 缓解 |
|---|---|
| 114 个 op 一次性迁移风险过大 | 分批：先 Query 类（无副作用），再 Command 类；每批独立提交与回归 |
| JNI 改动导致现有功能静默失效 | 建立 op 级回归清单，每个 op 至少一条测试 |
| 结构体版本化与旧 ABI 并存 | 新老接口并行，老接口冻结不改 |

### P2.7 测试计划

1. `op_inventory.md` 覆盖 114/114，无遗漏
2. 每个 op 迁移前后行为一致（对照测试）
3. 句柄泄漏检测：创建/销毁 10 万次无增长
4. 错误跨层传递：C++ 错误码 → JNI → Kotlin 原样可读

### P2.8 回滚方案

按 op 分批提交，每批可独立 revert；`dispatch` 层保留开关 `NC_USE_DISPATCH=0/1` 用于灰度对比。

### P2.9 验收标准（对应任务书 §5.5）

- [ ] JNI 业务逻辑明显收敛（`nc_jni.cpp` 行数下降 ≥ 60%）
- [ ] C++ 分发层职责明确
- [ ] C ABI 结构体具备版本兼容策略
- [ ] 资源生命周期清楚（六类句柄文档齐备）
- [ ] 错误跨层正确传递
- [ ] 无未处理悬空句柄
- [ ] 跨层接口测试通过

---

## P3 线程模型、并发与资源生命周期

**优先级**：高 · **规模**：L · **前置**：P1、P2

### P3.1 目标

建立十个线程域，消除全局串行锁对播放/预览的阻塞，明确任务取消语义。

### P3.2 规范依据

任务书 §6.1–§6.5；规范 01 §3。

### P3.3 现状证据

| 事实 | 证据 |
|---|---|
| `TimelineEditor` 明示非线程安全、仅 UI 线程 | `editor.h` 头注释 |
| Kotlin 侧 `@Synchronized` 串行所有引擎调用 | `NativeEngineBridge.kt` |
| 仅 `thread_pool.cpp` 使用 `std::thread` | `grep -rn "std::thread"` |

### P3.4 任务分解

| ID | 任务 | 产物 |
|---|---|---|
| T3.1 | 编写 `docs/thread_model.md`：定义 10 个线程域、归属、优先级、通信机制 | 线程模型文档 |
| T3.2 | 引入 `EngineThread`：所有 Command 经单队列串行执行，返回 Future/回调 | 命令队列 |
| T3.3 | 引入 `PlaybackClock`：独立播放时钟线程，驱动播放头与预览刷新 | 播放线程 |
| T3.4 | 解码/渲染/音频/导入/缩略图/导出/AI 各自 Worker 与有界队列（含背压） | 调度域 |
| T3.5 | **锁分析**（任务书 §6.4 六问）：分析 `@Synchronized` 保护的资源、持有时间、死锁可能、是否阻塞播放 | `docs/lock_analysis.md` |
| T3.6 | 缩小锁范围：句柄级细粒度锁 + 线程归属，替代全局锁 | 改造 |
| T3.7 | 任务取消语义：`cancelToken` 贯穿，取消后不再回调 | 实现 |
| T3.8 | 并发压测：播放中连续编辑/缩放/导出 | 测试 |

### P3.5 涉及文件

| 类型 | 文件 |
|---|---|
| 新增 | `core/include/nairoclip/core/engine_thread.h`、`playback_clock.h`、`task_queue.h`；`core/src/engine_thread.cpp`、`playback_clock.cpp`；`docs/thread_model.md`、`docs/lock_analysis.md`；`tests/test_concurrency.cpp` |
| 修改 | `core/include/nairoclip/core/editor.h`、`editor.cpp`、`thread_pool.cpp`、`mobile/native/android/NativeEngineBridge.kt` |
| 删除 | 无（锁逐步替换，不直接删） |

### P3.6 风险与缓解

| 风险 | 缓解 |
|---|---|
| 去掉全局锁引入数据竞争 | 先文档化再改；每步加 TSan 任务（CI 可选 job） |
| 命令异步化导致 UI 状态不一致 | revision 校验 + 事件回推，UI 只信 revision |
| 线程数膨胀 | 单例线程池 + 有界队列，禁止每任务新建线程 |

### P3.7 测试计划

1. 并发压力：1000 片段工程，播放中持续编辑 5 分钟无崩溃
2. 取消测试：导出/AI 任务取消后资源释放、无回调
3. 死锁检测：CI 跑 TSan（`-fsanitize=thread`）
4. 播放不阻塞：UI 操作期间掉帧率 < 阈值

### P3.8 回滚方案

线程域逐个启用（feature flag），任一域异常可单独回退到同步路径。

### P3.9 验收标准（对应任务书 §6.5）

- [ ] 线程域文档完整
- [ ] 共享资源所有权明确
- [ ] 线程间通信机制统一
- [ ] 播放期间不被普通 UI 操作长期阻塞
- [ ] 任务取消与对象销毁安全
- [ ] 并发压力测试通过
- [ ] 无已知数据竞争与死锁

---

## P4 GPU 渲染架构与 CPU 回退

**优先级**：高 · **规模**：XL · **前置**：P2、P3

### P4.1 目标

实现真实 GLES 渲染闭环：解码帧 → 纹理 → GPU 处理 → 多轨合成 → Surface 显示；保留 CPU fallback。

### P4.2 规范依据

任务书 §7.1–§7.5；规范 01 §5。

### P4.3 现状证据

| 事实 | 证据 |
|---|---|
| 仅有 `IRenderBackend` 抽象 | `core/include/nairoclip/core/render_engine.h` |
| 全仓无 `eglCreate`/`glTexImage`/`GLES` 实现调用 | 检索命中 0 |
| 有 SurfaceView 零拷贝预览桥 | `PreviewSurfaceView.kt`、`PreviewBridge.kt` |

### P4.4 任务分解

| ID | 任务 | 产物 |
|---|---|---|
| T4.1 | `GpuContext`：EGL 初始化/上下文丢失重建/Surface 尺寸变化 | 上下文层 |
| T4.2 | `TextureManager`：外部纹理（OES）接入 MediaCodec Surface、普通纹理池、生命周期 | 纹理层 |
| T4.3 | `ShaderManager` + `FramebufferManager`：离屏 FBO、多轨合成 Pass | 着色/帧缓冲 |
| T4.4 | `RenderGraph`：按效果栈生成 Pass 序列，与 CPU 路径共用效果定义 | 渲染图 |
| T4.5 | `DeviceCapability`：GLES 版本/扩展/最大纹理/外部纹理/内存预算/驱动黑名单 | 能力探测 |
| T4.6 | CPU fallback：GPU 不可用/初始化失败/显存不足/上下文丢失时自动降级 | 回退路径 |
| T4.7 | 导出路径复用同一效果定义（预览/导出一致性） | 一致性 |
| T4.8 | 真机性能基线：首帧、稳定帧率、掉帧、GPU 内存、温度 | 性能数据 |

### P4.5 涉及文件

| 类型 | 文件 |
|---|---|
| 新增 | `core/include/nairoclip/core/gpu/{context,texture,shader,framebuffer,render_graph,capability}.h`；`core/src/gpu/*.cpp`；`docs/render_architecture.md`；`tests/test_gpu_capability.cpp` |
| 修改 | `render_engine.h`、`preview.cpp`、`export_executor.cpp`、`transition_render.cpp`、`mobile/native/android/PreviewSurfaceView.kt` |
| 删除 | 无（CPU 路径保留为默认回退） |

### P4.6 风险与缓解

| 风险 | 缓解 |
|---|---|
| 厂商驱动差异导致黑屏/花屏 | 能力探测 + 驱动黑名单 + 一键切 CPU |
| 上下文丢失（切后台/来电） | 资源可重建设计，工程状态不受影响 |
| GPU 路径与 CPU 路径效果不一致 | 共用效果定义 + 逐像素对比测试 |
| 一次性替换预览导致功能回归 | GPU 路径按 feature flag 灰度，默认 CPU，达标后切默认 |

### P4.7 测试计划

1. 能力探测矩阵：至少 3 个 GPU 厂商 × 2 个 Android 版本
2. 上下文丢失恢复测试（切后台 30 次）
3. GPU/CPU 输出一致性：同一帧 PSNR 阈值
4. 性能：1080p 多轨播放帧率、GPU 内存峰值、连续 30 分钟温度

### P4.8 回滚方案

`RenderBackend` 运行时开关 + 编译期开关双保险；默认走 CPU，GPU 异常自动降级。

### P4.9 验收标准（对应任务书 §7.5）

- [ ] 真正创建 GPU Context
- [ ] 真正创建与管理纹理
- [ ] 真正执行 Shader
- [ ] 显示真实解码视频帧
- [ ] 至少一种 GPU 图像处理
- [ ] 至少一种多图层合成
- [ ] 播放中持续刷新
- [ ] GPU 失败可回退 CPU
- [ ] 真机测试有日志与性能数据
- [ ] GPU 预览与最终渲染效果定义一致

---

## P5 原生 UI 渐进迁移

**优先级**：高 · **规模**：XL · **前置**：P1、P2（可与 P4 并行）

### P5.1 目标

将编辑器从 WebView 迁移到 Android 原生 Kotlin UI，六个阶段推进，最终 WebView 退场。

### P5.2 规范依据

任务书 §8.1–§8.7；规范 00 §3、02 §2、06 全篇。

### P5.3 现状证据

| 事实 | 证据 |
|---|---|
| 全屏 WebView 承载编辑器 | `MainActivity.kt` |
| 编辑器交互 5,435 行 JS | `mobile/assets/editor.js` |
| 前端共 12,392 行 JS | `wc -l mobile/assets/*.js` |
| 15 个 HTML 页面 | `mobile/pages/` |
| 二级工具栏横向展开已实现（JS） | `editor.js` 头部注释第 5 条 |
| 工具表硬编码 | `XFORM_ITEMS`/`XF_SPEC` |

### P5.4 六阶段任务

| 阶段 | 内容 | 关键产物 |
|---|---|---|
| **5-1 原生框架** | Activity、页面容器、导航、生命周期、全局状态、Editor API 客户端、主题、**页面注册表** | `PageRegistry`、`EditorClient`、`Theme` |
| **5-2 首页与项目** | 项目列表/新建/打开/重命名/删除/恢复/最近记录 | `HomeScreen` |
| **5-3 素材管理** | 相册/文件选择、视频/图片/音频列表、预览、搜索、分类、缩略图 | `MediaScreen` |
| **5-4 编辑器主界面** | 预览、播放控制、一级/二级工具栏、参数面板、多轨时间轴、播放头、轨控、关键帧 | `EditorScreen`、`TimelineView` |
| **5-5 专业模块** | 变换/裁剪/旋转/缩放/位置/透明度/速度/调色/特效/转场/蒙版/字幕/音频/曲线 | 各 `Panel` |
| **5-6 WebView 退场** | 删除未用 HTML/JS/CSS、清适配代码、更新构建与架构文档 | 删除清单 |

### P5.5 关键设计要求

1. **Toolbar Registry**：`toolId` 稳定（如 `transform.position`），登记 categoryId/parentToolId/labelKey/iconId/targetScope/capability/parameterSchema/commandId/undoPolicy/preview/export/availability/testIds
2. **时间轴原生化**：自定义 `View` + Canvas 绘制 + 视口虚拟化，**禁止一段片一 View**
3. **参数面板**：Slider/Numeric/XY/Curve/Color/Toggle 六类统一控件，双向同步、单位、精度、默认值恢复、关键帧状态
4. **单一数据源**：原生 UI 只持视图状态，工程数据经 Editor API

### P5.6 涉及文件（概览）

| 类型 | 文件 |
|---|---|
| 新增 | `android/app/src/main/java/com/nairoclip/app/ui/**`（页面/组件/主题/注册表）；`ui/timeline/TimelineView.kt`；`ui/panel/{Slider,Numeric,XY,Curve,Color,Toggle}Panel.kt`；`ui/registry/ToolbarRegistry.kt`、`PageRegistry.kt` |
| 修改 | `MainActivity.kt`（WebView → Compose/原生容器）、`AndroidManifest.xml`、`build.gradle.kts` |
| 删除（仅 5-6 且回归通过后） | `mobile/pages/*.html`、`mobile/assets/editor.js` 等已迁移文件 |

### P5.7 风险与缓解

| 风险 | 缓解 |
|---|---|
| 迁移期间两套 UI 并存导致双事实源 | 明确切换边界：迁移模块走原生，未迁移走 WebView；工程数据只经 Editor API |
| 时间轴原生重写性能不达标 | 先做绘制原型 + 压测（1000 片段/100 轨），达标再推进 |
| 工具数量庞大，逐一迁移耗时 | 按 Registry 驱动批量生成面板，避免手写 |
| 回归覆盖不足 | 每迁移一模块，补齐该模块 UI 自动化 + 回归用例 |

### P5.8 测试计划

1. 工具栏交互自动化：展开/收起/切换/横向滚动/参数加载/无选区状态
2. 时间轴：拖动/裁剪/分割/多选/吸附/缩放/播放头定位
3. 页面：旋转/返回键/进程重建/低内存回收
4. 新旧对比：同一工程在两套 UI 下时间值与导出结果一致

### P5.9 回滚方案

保留 WebView 通道至 5-6；每个模块迁移独立分支，失败可退回 WebView 版本。

### P5.10 验收标准（对应任务书 §8.7）

- [ ] 原生页面框架可用
- [ ] 首页与项目管理完成迁移
- [ ] 素材导入与管理完成迁移
- [ ] 编辑器主框架完成迁移
- [ ] 原生预览链路可用
- [ ] 原生时间轴可用
- [ ] 工具栏交互符合既定要求
- [ ] 参数面板连接真实引擎
- [ ] 关键帧可真实修改与预览
- [ ] 不存在双重数据源
- [ ] 已迁移功能通过回归测试

---

## P6 设备画像、兼容性与质量体系

**优先级**：贯穿全程 · **规模**：L · **前置**：自 P0 起

### P6.1 目标

建立设备能力识别、Tier 分级与动态质量调度，输出兼容性测试矩阵。

### P6.2 规范依据

任务书 §9.1–§9.6；规范 04 全篇。

### P6.3 现状证据

| 事实 | 证据 |
|---|---|
| 仅有内存压力响应 | `MemoryGuard.kt` |
| 无 GPU/编解码能力采集 | 检索无 |
| 无 Tier 分级 | 检索无 |

### P6.4 任务分解

| ID | 任务 | 产物 |
|---|---|---|
| T6.1 | `DeviceProfile` 采集 19 项：CPU 架构/核数/性能等级、RAM/可用内存、GPU vendor/renderer/API/最大纹理/扩展、解码能力、编码能力、音频能力、分辨率、刷新率、系统版本、温度、存储 | `DeviceProfile.kt` |
| T6.2 | Tier 1–4 分级算法（基于实测分 + 能力检测，禁止按设备名） | `DeviceTier.kt` |
| T6.3 | 动态质量调度：预览分辨率/帧率、解码并发、纹理缓存、渲染缓存、特效质量、缩略图并发、代理策略、AI 并发、后台任务数 | `QualityPolicy.kt` |
| T6.4 | 兼容矩阵：系统版本 × CPU 架构 × GPU 厂商 × RAM 档 × 屏幕 × 刷新率 × 格式 × 分辨率 × 帧率 × 音频 | `docs/compat_matrix.md` |
| T6.5 | 低内存保护与热降载联动 | 实现 |
| T6.6 | 质量不变量测试：降质不改变时间/位置/导出语义 | 测试 |

### P6.5 涉及文件

| 类型 | 文件 |
|---|---|
| 新增 | `mobile/native/android/DeviceProfile.kt`、`DeviceTier.kt`、`QualityPolicy.kt`；`docs/compat_matrix.md`；`tests/test_quality_policy.cpp` |
| 修改 | `MemoryGuard.kt`、`PreviewSurfaceView.kt`、`render_engine.h`、`preview.cpp` |
| 删除 | 无 |

### P6.6 风险与缓解

| 风险 | 缓解 |
|---|---|
| 采集涉及隐私 | 只采集设备能力，不采集用户数据；本地存储不上传 |
| 分级误判导致低端机体验差 | 分级可用运行时实测修正（首帧时间/掉帧率反馈） |
| 质量降级改变工程语义 | 质量与工程数据严格分离，降级只影响预览 |

### P6.7 验收标准（对应任务书 §9.6）

- [ ] 设备能力画像可用
- [ ] Tier 分级机制可用
- [ ] GPU 能力检测准确
- [ ] 系统编解码能力检测准确
- [ ] 动态质量调度可用
- [ ] 低内存保护可用
- [ ] 设备兼容性测试有记录
- [ ] 不同设备下工程结果一致

---

## 7. 跨阶段：统一接口定义源

任务书 §4.5 要求禁止长期维护 Kotlin/JNI/C ABI/C++/JS 五套独立接口副本。落地方案：

```
capi.h + contract/*.h          （唯一手写源）
        │
        ├─ gen_contract.py ─→ Kotlin 常量/接口（生成，勿手改）
        │                  ─→ JNI 映射表
        │                  ─→ API 文档
        │                  ─→ 契约测试桩
        └─ gen_api_dispatch.py ─→ design/tools/api_dispatch.inc（已存在）
```

CI 增加 `--check` 步骤：生成物与源不一致即失败。

> **注**：审计发现 `api_dispatch.inc` **确为自动生成**（`gen_api_dispatch.py`），`docs/HANDOFF.md` §0.3 称其"手工维护"属**过期信息**，整改时一并修订文档。

---

## 8. 分支、提交与验收制度

### 8.1 分支策略

| 阶段 | 分支 | 合并方式 |
|---|---|---|
| P0 | `fix/p0-build` | PR → main |
| P1 | `feat/p1-contract` | PR → main |
| P2 | `refactor/p2-jni-dispatch`（按 op 批次再分子分支） | PR → main |
| P3 | `feat/p3-threading` | PR → main |
| P4 | `feat/p4-gpu-backend` | PR → main |
| P5 | `feat/p5-native-ui`（每阶段一分支） | PR → main |
| P6 | `feat/p6-device-profile` | PR → main |

### 8.2 提交规范（任务书 §13.4）

```
fix(build): restore android build
feat(api): introduce unified editor contract
refactor(jni): move dispatch into engine layer
feat(render): implement gpu preview backend
feat(android): migrate native editor shell
```

### 8.3 每阶段交付物（任务书 §13.3）

实际修改/新增/删除文件清单、核心差异、构建结果、测试结果、未解决问题、已知风险、规范符合性结论、Commit ID。

### 8.4 验收等级

按任务书第十四章 A/B/C/D/F 评定，**只有 A 可标记完成**。

---

## 9. 里程碑与建议排序

| 里程碑 | 内容 | 依赖 | 完成标志 |
|---|---|---|---|
| M-0 | P0 完成 | — | CI 全绿 + APK 可装可开 |
| M-1 | P1 完成 | M-0 | 契约测试通过 + 一条端到端切片 |
| M-2 | P2 完成 | M-1 | JNI 收敛 60%+ + op 全覆盖回归 |
| M-3 | P3 完成 | M-2 | 并发压测通过 + TSan 无告警 |
| M-4 | P4 完成 | M-3 | 真机 GPU 60fps + CPU 回退可切 |
| M-5 | P5-1~5-4 完成 | M-2 | 原生编辑器主界面可用 |
| M-6 | P5-5~5-6 完成 | M-5 | WebView 退场 |
| M-7 | P6 收口 | 全程 | 兼容矩阵 + 分级调度上线 |

---

## 10. 第一轮交付物与下一步

### 10.1 本轮（已产出）

1. 《NC-AUDIT-00-07 规范符合性审计报告 V1.0》
2. 《NC-PLAN-00-07 分阶段整改执行计划 V1.0》（本文档）

### 10.2 下一步（需授权后执行）

1. 批准本计划 → 进入 **P0**
2. P0 第一步：读取 `ci-status` 分支最新结果，定位 Kotlin 编译错误
3. 修复并以 PR 形式提交，CI 验证

---

## 11. 待决策事项

| # | 决策点 | 选项 | 建议 |
|---|---|---|---|
| 1 | P0 允许修改 CI 配置的程度 | ①仅修 workflow bug ②可新增检查 job | ②（新增冒烟检查，不放松任何现有检查） |
| 2 | P1 契约层是否引入 JSON schema 作为唯一源 | ①C++ 头文件为源 ②JSON schema 生成 C++/Kotlin | ①（改动最小，沿用现有代码习惯）；后续可演进 |
| 3 | P5 原生 UI 技术选型 | ①Jetpack Compose ②自定义 View 为主 | ②（时间轴自定义绘制，Compose 用于外框与面板） |
| 4 | GPU 后端首期范围 | ①仅 GLES3 ②GLES3 + Vulkan | ①（Vulkan 单独立项评估） |
| 5 | WebView 退场判据 | ①功能对等即删 ②保留一个版本周期 | ②（降低回滚风险） |
| 6 | 每阶段是否都要独立 PR + 人工评审 | ①是 ②合并大 PR | ① |

---

*本计划为执行草案，未经项目负责人批准不得据此修改任何代码。*
