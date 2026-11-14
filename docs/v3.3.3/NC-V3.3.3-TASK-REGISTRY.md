# NC-V3.3.3 任务注册表

> 唯一 ID · 唯一实施定义 · 重复任务只能引用

## S0
| ID | 名称 | 前置 | 审批 | 负责人角色 | 验收 |
|---|---|---|---|---|---|
| S0-1 | 基线核验（只读） | — | 否 | 审计 | 逐项标证据等级 |

## 统计口径（★本轮统一）
| 口径 | 数量 |
|---|---|
| 执行行总数 | **52** |
| 引用任务 | **4**（S4-0A/B/C、S4-1a） |
| **唯一实施任务** | **48** |
| S1 逻辑任务 | **10**（S1-1…S1-8，S1-5a 计 1） |
| S1 拆分执行行 | **15** |

> **10 与 15 的关系**：逻辑单元 10 个；S1-4 拆为 S1-4A（+0）、S1-8 拆为 S1-8A/B/C/D（+3）、S1-5a 拆为 A/B/C（+2）→ 10 − 3 + 1 + 4 + 3 = **15**。**拆分不增加逻辑任务总量**。

## S1（逻辑 10 单元 / 执行 15 行）
| ID | 名称 | 前置 | 审批 | 负责人角色 | 涉及文件 | 验收 |
|---|---|---|---|---|---|---|
| S1-1 | Dispatcher 生命周期+崩溃捕获 | — | A-12 | C++ Core / Android | `capi.cpp`(`nc_project_load:429`)、`MainActivity.kt`(`:57/:181`) | ASAN load→dispatch ×100 无 UAF |
| S1-2 | JNI 表驱动+参数别名+契约工具 | **A-1** | A-1+A-12 | Kotlin/JNI | `nc_jni.cpp`、`tools/check_op_contract.py`(新) | 51 项七类验收；6 处参数错位修复 |
| S1-3 | 工程格式+保存+恢复 | **S1-1** | A-12 | 持久化 | `nc_jni.cpp`、`serialization.cpp`、`recovery.cpp`、`capi.cpp` | 保存→重开逐字段一致；`Project::id` 保留；原子写 |
| **S1-4A** | 媒体导入业务 | **S1-1** | A-12 | Android+前端 | `editor.js`、`MainActivity.kt`、`capi.cpp:692` | 导入→预览可见；解码失败不静默 |
| **S1-8A** | 导入安全边界（同批 S1-4A） | **S1-1** | A-12 | 安全 | 同上（同函数，同批） | 假路径/超限被拒 + 明确错误码 |
| **S1-8B** | JSON 深度与结构限制 | — | A-12 | 安全 | `json.cpp` | 超深/超长/超大被拒；无栈溢出 |
| **S1-8C** | 工程加载尺寸与资源限制 | S1-8B | A-12 | 安全+持久化 | `json.cpp`、`serialization.cpp` | 四层防护闭环；半加载不污染 |
| **S1-8D** | Android 备份策略 | — | A-12 | Android+安全 | `AndroidManifest.xml`、`res/xml/backup_rules.xml`(新)、`data_extraction_rules.xml`(新) | `allowBackup`+API23-30+API31+ 三处齐配 |
| S1-5a-A | 编码器**静态**探针 | — | **无需** | 构建 | 只读 | 编码器/解码器清单 + 许可风险 |
| S1-5a-B | MediaCodec **能力**探针 | 设备 | A-12 | 媒体 | 只读 | 设备能力清单 |
| S1-5a-C | **真实帧编码**探针 | 构建/设备 | A-12 | 媒体 | 可能改 `build-android.sh` | 真实输出文件 + 校验 |
| S1-5b | 测试回归验证 | — | A-12 | 测试 | `ci.yml`、`tests/*` | 验证既有修复；无回归则以报告结束 |
| **S1-5c** | **基础真实导出链路**（=S4-1a，唯一） | S1-2+S1-7+**A-2** | **A-3** | Kotlin/JNI+媒体 | `nc_jni.cpp`、`editor.js`、`10-editor.html` | 真实可播放文件；不阻塞 UI |
| S1-6 | CI 门禁动态化 | S1-5b | A-13+A-12 | CI | `ci.yml`、`tools/test_inventory.json`(新) | 门禁=实际数；下降必失败 |
| S1-7 | TaskRegistry+AsyncExecutor+线程安全 | **S1-2** | A-12 | C++ Core | `nc_jni.cpp`、`contract/task.cpp`、`thread_pool.cpp`、`task.h`、Engine 生命周期、CMake、测试 | 13 项分析（见主文档附录 B）+ 15 类测试全绿 |

## S2（4 串行）
S2-1 Revision 原子化+loadGeneration（`command.h:125`、`capi.cpp:441`）· S2-2 Command 8 条绕过（`capi.cpp:541/1988/2021/2107/2122/3170`、`template_apply.cpp:174-178`）· S2-3 EventBus+Undo/Redo（`event.cpp:55/81`、`editor.cpp:739-745`）· S2-4 Query 层+任务扩展

## S3（3 串行）
S3-1 Rational 时间（`capi.h:575/956/1025/2046`）· S3-2 Alpha+Oracle+Golden（`canvas.cpp:198/220`、`preview.cpp:830`、`tests/test_b1_supplement.cpp:326`）· S3-3 一致性（`clip.h:236`、`preview.cpp:403`）

## S4（13 行 / 其中 4 行为引用 → 唯一新增 9）
S4-0A/B/C（**引用** S1-5a-A/B/C，不重复实施）· **S4-1a（引用 S1-5c，不重复定义、不重复计数）** · S4-1b 编码器兼容完善 · **S4-2a HEVC 系统解码能力与交付验证** · **S4-2b 实时音频播放（技术路径待方案决策）** · S4-3 导出能力完善 · **S4-4A 本地 ONNX Runtime 集成（集成方式待方案决策）** · S4-4B 云端 AI · S4-5a GPU G0 · S4-5b GPU G0.5 · S4-5c GPU G1

### M-2 · S4-2a 定义（★本轮明确）

**S4-2a = HEVC 系统解码能力与交付验证**，包含：
1. Android **MediaCodec** HEVC 能力探测
2. Profile / Level / 分辨率 / 实际设备能力检查
3. FFmpeg **`hevc_mediacodec` 包装器**适配评估
4. 必要时调整 FFmpeg 构建配置（`build-android.sh:138`，**条件性写入权**）
5. 真实 HEVC 样本解码验证
6. 不支持 / 失败场景的错误处理
7. 保持编解码架构红线

**明确禁止**：
- ❌ FFmpeg 自有 **`hevc` 软件解码器**打包（`--enable-decoder=hevc`）
- ❌ 自行引入第三方 HEVC 软件解码实现
- ❌ 仅凭 **parser**（`:142` 有 `hevc` parser）或**静态 decoder 列表**宣布解码交付完成

> **若当前工程无法在不违反编解码红线的情况下完成交付，必须报告阻塞原因，不得擅自改变技术原则。**

### M-3 · S4-2b 定义（★本轮明确）

**S4-2b 实时音频播放技术路径待方案决策。** 候选：
| 候选 | 可能涉及 |
|---|---|
| Android **AudioTrack** | JNI / C ABI（PCM 拉取） |
| **AAudio** | C++ 源文件 + CMake |
| **Oboe** | Gradle 依赖 + CMake |
| 现有 **AudioMixer** + 平台输出端组合 | AudioMixer 为**离线混音器**（0 播放/设备/回调），**不能单独视为完整设备播放实现** |

> **不得提前确定最终技术栈**；文件所有权按最终方案登记。

### M-4 · S4-4A 定义（★本轮明确）

**S4-4A 本地 ONNX Runtime 集成方式待方案决策。**
- ✅ `core/CMakeLists.txt:185-213` **已存在 ORT 条件查找与链接逻辑**（`NAIROCLIP_ORT_ROOT` / `ONNXRUNTIME_ROOT` / 4 条候选路径）
- 预编译原生库路径 **可能复用现有 CMake 集成**（无需改 CMake）
- AAR 方案可能涉及 **Gradle**
- 自行构建方案可能涉及**构建脚本**
- 必须验证 **Android ABI / 动态库打包 / 模型加载 / 真实推理**
- **Apache-2.0 依赖按普通开源许可证流程审查**；**不得把普通 ORT 开源依赖无理由升级为 A-8 商业专有 SDK 审批**
- **附加执行提供程序（EP）与第三方组件需单独核对许可证**
- **尚未确定的文件不得提前登记为确定写入任务**

## S5（11 单元）
S5-0A 只读调研 · S5-0B 冻结（需 S1-3+S3-1）· S5-1 宿主导航 · S5-2 外围页 · S5-3 编辑器外壳 · S5-4 预览 · S5-5 时间轴 · S5-6 工具栏/面板 · S5-7 其他面板 · S5-8 旧代码清理 · S5-9 WebView 退出验收

## S6（5 单元）
S6-1 16KB · S6-2 ABI/版本/适配 · S6-3 性能 · S6-4 签名 · S6-5 APK/AAB+真机验收
