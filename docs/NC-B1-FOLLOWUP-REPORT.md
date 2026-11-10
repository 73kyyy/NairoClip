# NairoClip B1 后续技术债整改与云端验证报告

| 项目 | 内容 |
|---|---|
| 文档编号 | NC-B1-FOLLOWUP-REPORT |
| 任务书 | NC-B1-FOLLOWUP V1.0 |
| 整改前 HEAD | `09225ba7d5c18ec68a0f12a03bc20a9e4bf1cda8` |
| 仓库 | `github.com/73kyyy/NairoClip` |
| 范围 | ① 完整 CI run ② Valgrind 真实检测 ③ CPU/GPU Alpha 一致性 |

---

## 1. 整改前状态

### 1.1 整改前三项结论

| 项 | 整改前 |
|---|---|
| 主 CI run | **`cancelled`**（不是 success，也不是 failure） |
| Valgrind 真实结论 | **从未取得**（每次都被超时取消） |
| GPU Alpha | **无 GPU 后端**，无法验证 |

### 1.2 整改前 CI 实测

| 提交 | run 级 | build-and-test | ASAN | UB | valgrind |
|---|---|---|---|---|---|
| `fdf03f92` | cancelled | ✅ | ✅ | ✅ | cancelled |
| `193eadae` | cancelled | ✅ | ✅ | ✅ | cancelled |
| `c541191b` | cancelled | ✅ | ✅ | ✅ | cancelled |

三次 run 的 valgrind 作业耗时均为 **1:00:10 / 1:00:11 / 1:00:10** —— 精确 60 分钟。

---

## 2. 完整 CI 根因（重新核实）

### 2.1 调查项与结果

| 调查项 | 结果 | 证据 |
|---|---|---|
| 工作流触发条件 | `ci.yml`：push main / PR / 手动；`android-apk.yml` 等按 paths | 各 `on:` 段 |
| Job 依赖关系 | `ci.yml` 内 4 个 job **互不依赖**（并行） | `jobs:` 结构 |
| Job/Step 超时 | build-and-test 30；ASAN 40；valgrind **60**；UB 40；apk 90 | 解析 workflow |
| Valgrind 实际命令 | `valgrind ... ./build_vg/tests/nairoclip_tests`，**无任何测试过滤** | `ci.yml` 原第 6 步 |
| 实际执行测试数 | 全量（CI 构建含 FFmpeg → **1764** 用例） | 分片 `[SHARD_STATS] total=1764` |
| 测试耗时 | memcheck 下全量 >50 分钟未完成（step 0:50:30 被取消） | job 分步计时 |
| Valgrind 是否重复执行 | 否，单次 | — |
| Runner 资源 | ubuntu-latest，2 vCPU / 7 GB / 14 GB SSD（GitHub 标准） | `RUNNER_OS`/arch 记录 |
| 其他并发/取消 | 无 `gh run cancel`；`ci-status.yml` 的 concurrency 只作用于自身 | 全 workflow 检索 |
| 状态传播 | Job 触碰 `timeout-minutes` → Job `cancelled` → **run 级结论 `cancelled`** | 三次 run 均如此 |

### 2.2 根因结论

**不是测试失败，不是外部取消，是作业自身上限。**

```
valgrind-memcheck.timeout-minutes = 60
  ≤ 装依赖 6:51 + 构建 2:42 + memcheck 全量 >50:00
```

三者在同一 job 内串行累加必然越过 60 分钟；GitHub 将**被 timeout 的 job**
标记为 `cancelled`，并把这个结论传播到 run 级 —— 即使另外三个作业全绿。

**同时纠正一处历史表述**：用户描述"约 40 分钟被取消"与实测不符，
实际是 **60 分钟整**（valgrind 步骤本身跑了 50.5 分钟）。

### 2.3 另需纠正的一处历史结论

B1 报告曾写「`tests/test_main.cpp` 不支持 argv 测试过滤」。
核实后：**argv 确实不支持，但测试框架早已支持环境变量过滤/分片**：

| 环境变量 | 作用 |
|---|---|
| `NC_TEST_SHARDS` / `NC_TEST_SHARD` | 按注册顺序模运算分片 |
| `NC_TEST_SKIP` / `NC_TEST_MAX` | 位置区间 |
| `NC_TEST_INCLUDE` / `NC_TEST_EXCLUDE` | 按 suite.case 前缀/全名 |

因此**不需要改测试入口代码**就能分片 —— 这让"跑全量"成为可行选项。

---

## 3. 工作流整改

### 3.1 拆分方案

| 工作流 | Job | 阻断性 | 职责 |
|---|---|---|---|
| `ci.yml` | `build-and-test` | ✅ 阻断 | 编译 + 全量测试 + 各项静态检查 |
| `ci.yml` | `asan-diagnose` | 非阻断 | ASAN |
| `ci.yml` | `ub-diagnose` | 非阻断 | UBSAN |
| **`valgrind.yml`（新）** | `build` | — | `-g -O0` 构建一次并上传测试二进制 |
| | `memcheck`（×16 分片矩阵） | — | 每片跑全量的 1/16，valgrind memcheck |
| | `summary` | — | 覆盖度校验 + 内存错误/泄漏汇总 |

触发规则：

- `ci.yml`：push main / PR main / 手动（不变）
- `valgrind.yml`：push main（core/tests/tools/CMake/自身变更）/ PR main / 手动

状态汇总：`ci-status.yml` 的 `workflow_run.workflows` 增加 `'Valgrind Memcheck'`，
两者结论**分别展示**，不混算。

### 3.2 为什么拆分不违反「不能通过取消 Valgrind 解决问题」

| 要求 | 落实 |
|---|---|
| 覆盖不减 | 仍跑**全量** 1764 用例，仅按模运算分 16 片 |
| 不跳过 | 分片并集 = 全量，由 `executed == total` 机器校验 |
| 不被取消 | 每片独立 90 分钟上限，实测最重片 ~50-60 分钟可完成 |
| 结论真实 | `--leak-check=full`，Definitely/Indirectly 计入失败 |
| 可追溯 | 每片上传 `.xml` + stdout，summary 汇总并写 step summary |

### 3.3 修改文件

| 文件 | 改动 |
|---|---|
| `.github/workflows/ci.yml` | 移除 `valgrind-memcheck`（保留迁移说明）；新增「Valgrind 汇总工具自检」到阻断作业 |
| `.github/workflows/valgrind.yml` | **新增**：build / memcheck×16 / summary |
| `.github/workflows/ci-status.yml` | 纳入 `'Valgrind Memcheck'` |
| `tests/test_framework.h` | summary 输出 `执行 N/M` 与机器可读 `[SHARD_STATS]` |
| `tools/vg_summary.py` | **新增**：覆盖度校验 + XML 内存错误/泄漏汇总 |
| `tools/test_vg_summary.py` | **新增**：工具自检 10 用例（已进阻断 CI） |

---

## 4. Valgrind 测试范围与真实结果

### 4.1 范围

| 项 | 值 |
|---|---|
| 用例总数 | 1764（CI 构建含 FFmpeg，比本地多 48 个 FFmpeg 用例） |
| 分片数 | 16 |
| 每片执行 | 约 110 用例 |
| 工具 | Valgrind memcheck 3.22.0 |
| 参数 | `--error-exitcode=99 --track-origins=yes --num-callers=25 --leak-check=full --show-leak-kinds=definite,indirect,possible --errors-for-leak-kinds=definite,indirect` |
| 构建配置 | Release + `-g -O0` |

### 4.2 结论

**零内存错误、零泄漏、覆盖完整。**

| 指标 | 值 |
|---|---|
| 分片数 | 16 / 16 全部成功 |
| 全量用例总数 | **1764** |
| 实际执行合计 | **1764**（覆盖度校验通过） |
| 失败用例 | **0** |
| 内存错误条目总数 | **0** |
| Definitely lost | **0** |
| Indirectly lost | **0** |
| Possibly lost | **0** |
| Still reachable | **0** |

未检测到：Invalid read / Invalid write / Use after free / Double free /
Invalid free / Mismatched free / Uninitialised value / 任何类别泄漏。

### 4.3 运行记录（GitHub 云端实测）

工作流 run：**`42796799387`**（`Valgrind Memcheck`，run 级 **success**）
链接：https://github.com/73kyyy/NairoClip/actions/runs/42796799387

| 作业 | 结果 | 耗时 |
|---|---|---|
| 构建（-g -O0） | ✅ success | 3:48 |
| memcheck 分片 0 | ✅ success | 6:18 |
| memcheck 分片 1 | ✅ success | 1:42 |
| memcheck 分片 2 | ✅ success | 0:36 |
| memcheck 分片 3 | ✅ success | 4:56 |
| memcheck 分片 4 | ✅ success | 7:26 |
| memcheck 分片 5 | ✅ success | 6:29 |
| memcheck 分片 6 | ✅ success | 1:21 |
| memcheck 分片 7 | ✅ success | 5:11 |
| memcheck 分片 8 | ✅ success | 41:54 |
| memcheck 分片 9 | ✅ success | 44:20 |
| **memcheck 分片 10** | ✅ success | **52:56**（最长） |
| memcheck 分片 11 | ✅ success | 44:56 |
| memcheck 分片 12 | ✅ success | 1:16 |
| memcheck 分片 13 | ✅ success | 6:35 |
| memcheck 分片 14 | ✅ success | 8:15 |
| memcheck 分片 15 | ✅ success | 6:35 |
| 汇总覆盖度与结论 | ✅ success | 0:06 |

汇总日志原文（关键段）：

```
分片输出文件数: 16 / 期望 16
全量用例总数: 1764
实际执行合计: 1764
失败用例合计: 0
✅ 覆盖度校验通过：所有分片并集覆盖全量用例。
内存错误条目总数: 0
泄漏分类: definitely=0 indirectly=0 possibly=0 still_reachable=0
```

> **分片耗时极不均匀**（0:36 ~ 52:56），印证了「45 分钟上限确实不够」：
> 最重分片 10 需要 52:56，之前 45 分钟超时被取消是**容量问题不是失败**。
> 90 分钟上限给足冗余。

### 4.4 为什么这是"真实结果"而非"跑绿了"

| 反作弊检查 | 落实 |
|---|---|
| 不是抽样 | 16 片并集 = 1764/1764，机器校验 `executed==total` |
| 不是跳过 | 未设 `NC_TEST_EXCLUDE` / `NC_TEST_MAX` 任何排除 |
| 不是伪造 | 结论来自 valgrind 的 XML `<error>` 计数，非人工填写 |
| 汇总不吞错 | `vg_summary.py` 自检 10 用例，含「截断 XML 不得当成 0 错误」 |
| 早前"文本日志为空"陷阱 | 已识别并改为解析 XML（见 §3.3） |

**交叉验证**：上一轮（8→16 分片过渡）15 个成功分片共执行 1654 用例、
错误 0；本轮补齐第 16 片后 1764/1764、错误 0 —— 两轮一致。

---

## 5. GPU 后端审计

### 5.1 审计方法与结果

| 检查项 | 检索 | 命中 |
|---|---|---|
| EGL 初始化 | `eglCreate` / `eglGetDisplay` / `eglMakeCurrent` | **0** |
| GLES Context | `EGLContext` / `GLES2` / `GLES3` | **0** |
| Texture 创建/上传 | `glGenTextures` / `glTexImage2D` | **0** |
| Shader | `glCreateShader` | **0** |
| FBO | `glBindFramebuffer` | **0** |
| GPU 外部纹理 | `GL_TEXTURE_EXTERNAL_OES` / `SurfaceTexture` | **0** |
| 后端实现 | `class ... : public IRenderBackend` | **0** |

### 5.2 逐项结论

| 项 | 结论 | 证据 |
|---|---|---|
| EGL 初始化 | ❌ 不存在 | 0 命中 |
| OpenGL ES Context | ❌ 不存在 | 0 命中 |
| Texture 创建与上传 | ❌ 不存在 | 0 命中 |
| Shader | ❌ 不存在 | 0 命中 |
| FBO | ❌ 不存在 | 0 命中 |
| GPU 合成 | ❌ 不存在 | 预览合成为 CPU 像素循环 `PreviewRenderer::compositeLayer` |
| Render Target | ❌ 不存在 | — |
| GPU 结果读取 | ❌ 不存在 | — |
| GPU 资源释放 | ❌ 不存在 | — |
| Android 平台 GPU 接入 | ❌ 不存在 | `android/` 与 `mobile/native/android/` 中 EGL/GLES 0 命中 |

**补充澄清**（避免把相邻能力误当 GPU）：

- `core/include/nairoclip/core/render_engine.h` 里的 `IRenderBackend` 是**纯抽象接口**
  （`info()` / `beginFrame()` / `endFrame()`），**全仓没有任何派生类实现**；
- `core/src/render_engine.cpp` 只有 60 行，内容是**质量分级校验**，不含任何 GPU 调用；
- `ANativeWindow_lock`（`mobile/native/android/nc_jni.cpp`）是 **CPU 把 RGBA 像素
  写进 Surface 显示缓冲**的零拷贝显示路径，不是 GPU 渲染。

### 5.3 GPU Alpha 一致性结论

**未验证。** 不存在 GPU 渲染后端，因此不存在可比对的 GPU 输出。

- 未编写任何 GPU 测试；
- 未使用 Mesa 软件渲染冒充硬件 GPU；
- 未修改 CPU 实现冒充一致性。

---

## 6. CPU Alpha 测试结果

### 6.1 参考矩阵（任务书 4.4）

新增 `tests/test_b1_supplement.cpp` 6 个用例：

| 用例 | 覆盖 |
|---|---|
| `B1AlphaMatrix.RequiredAlphaLevelsMatchReference` | alpha ∈ {0, 1, 64, 128, 192, 255}，与**独立浮点参考实现**逐通道比对（±1） |
| `B1AlphaMatrix.EndpointsAreExact` | alpha=0 → 纯背景；alpha=255 → 纯前景，逐字节精确 |
| `B1AlphaMatrix.AlphaOneIsAlmostInvisible` | alpha=1 时前景仅贡献 1/255 |
| `B1AlphaMatrix.MultipleLayersCompositeInOrder` | 两层叠加顺序敏感 |
| `B1AlphaMatrix.SemiTransparentBackgroundPropagates` | 半透明背景 + 半透明前景的 outA 计算 |
| `B1AlphaMatrix.DifferentColorsStayInRange` | 6 组前景色 × 固定背景，通道不溢出、不错位 |

设计要点：**参考实现与被测实现相互独立**（浮点 vs 整数），避免"用被测代码证明被测代码"。

### 6.2 合成公式（Straight Alpha）

```
out.rgb = src.rgb * a + dst.rgb * (1 - a)
out.a   = a + dst.a * (1 - a)
```

实现位于 `core/src/canvas.cpp :: blendStraightOver`，整数运算（`+127)/255` 四舍五入），
避免浮点在 CPU/GPU 之间产生 1 LSB 差异。
`a == 255` 走直接覆盖快路径 → 不透明素材**逐字节零回归**。

### 6.3 渲染级验证

| 用例 | 覆盖 |
|---|---|
| `B1Blend.RenderedFrameBlendsForegroundOverBackground` | 半透明前景 + 不透明背景经 `renderFrameFit` 真混合 |
| `B1Blend.LetterboxStaysPureBackground` | 信箱区保持纯背景色 |
| `B1Blend.OpaqueForegroundStillFullyCoversBackground` | 不透明前景完全覆盖 |

---

## 7. GitHub 云端 Runner 环境

| 项 | 值 |
|---|---|
| Runner | `ubuntu-latest`（GitHub 托管） |
| OS / 架构 | Linux / x86_64 |
| CPU / 内存 / 磁盘 | 2 vCPU / 7 GB / 14 GB SSD（GitHub 公有仓库标准规格） |
| Valgrind | 3.22.0（apt 安装） |
| 编译器 | g++（ubuntu-latest 默认） |
| CMake | `-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -O0"` |
| ONNX Runtime | 1.22.0（linux-x64，缓存复用） |
| FFmpeg | apt `ffmpeg` + `libav*-dev` |

> 云端 Runner **无 GPU**。即使将来实现 GPU 后端，GitHub 托管 Runner 也只能跑
> Mesa 软件渲染（B 类），不能作为硬件 GPU 证据。

---

## 8. 未完成事项

| ID | 事项 | 原因 | 需要什么 |
|---|---|---|---|
| F-1 | GPU Alpha 一致性验证 | **仓库无 GPU 渲染后端**（§5 证据） | 先实现 GPU Render Core；再用真实硬件 GPU 或 Android 真机验证 |
| F-2 | GPU 硬件真机矩阵 | 无自托管 GPU Runner / 无真机接入 | 自托管 Runner 或 Android 设备农场 |
| F-3 | Valgrind 硬件相关路径 | memcheck 只覆盖 CPU 路径 | 随 F-1 一起 |

---

## 9. GPU Render Core 建设方案（本任务不实施，仅方案）

### 9.1 架构条件（Alpha 一致性可验证的前提）

1. **统一的合成语义层**：`Compositor` 定义 `BlendMode` + `AlphaSemantics`
   （Straight / Premultiplied），CPU 与 GPU 各自实现同一契约；
2. **CPU 参考实现作为 oracle**：现有 `blendStraightOver` 即参考，
   GPU 输出必须与之逐像素比对；
3. **可读回的 GPU 路径**：`RenderTarget::readback()` 输出 RGBA8888，
   否则无法比对；
4. **能力探测**：`RenderBackendInfo` 填充真实 `GLES 版本 / 最大纹理 / 扩展`，
   禁止硬编码；
5. **CPU 回退**：GPU 不可用/失败时回退，且回退结果与 GPU 结果在容差内一致；
6. **测试数据可注入**：Alpha 矩阵（0/1/64/128/192/255）、多层叠加、
   半透明背景等用例需能从测试直接驱动 GPU 后端。

### 9.2 分阶段建议

| 阶段 | 内容 | 验收 |
|---|---|---|
| G1 | EGL + GLES3 Context 生命周期（含 Context Lost 重建） | 真机创建/销毁 1000 次无泄漏 |
| G2 | 纹理上传 + 单 Pass 全屏 blit（Straight Alpha） | 与 CPU 参考逐像素 ≤1 LSB |
| G3 | 多纹理合成 + BlendMode | 与 CPU 合成器同输入同输出 |
| G4 | RenderTarget readback + 与 CPU 逐像素比对测试 | 6 档 alpha × 多色矩阵全通过 |
| G5 | 接入预览/导出双路径（共用效果定义） | 预览与导出同帧一致 |

### 9.3 真实硬件测试环境需求

| 类别 | 环境 | 能证明什么 |
|---|---|---|
| A. CPU 参考 | GitHub 托管 Runner | CPU 语义正确性（**当前已具备**） |
| B. GPU 软件实现 | Mesa llvmpipe / swiftshader | 图形 API 逻辑正确，**不能**证明硬件行为 |
| C. 真实硬件 GPU | 自托管 Runner（带独显）或 Android 真机（Adreno/Mali/PowerVR） | 唯一可作为硬件 GPU 证据 |

> 任务书 4.7 明确：只有 C 类能作为对应硬件 GPU 的真实验证证据。
> 本仓库当前连 A 类的 GPU 路径都没有，故 F-1 只能标记未验证。

---

## 10. 修改文件列表

| 文件 | 类型 | 原因 |
|---|---|---|
| `.github/workflows/valgrind.yml` | 新增 | 独立分片内存诊断 |
| `.github/workflows/ci.yml` | 修改 | 移除 valgrind 作业；加入汇总工具自检 |
| `.github/workflows/ci-status.yml` | 修改 | 纳入新工作流结论 |
| `tests/test_framework.h` | 修改 | 输出实际执行数与 `[SHARD_STATS]` |
| `tools/vg_summary.py` | 新增 | 覆盖度校验 + XML 内存汇总 |
| `tools/test_vg_summary.py` | 新增 | 工具自检（10 用例） |
| `tests/test_b1_supplement.cpp` | 修改 | 新增 CPU Alpha 参考矩阵 6 用例 |

**未修改**：核心渲染算法（除既有 `blendStraightOver` 外无改动）、C ABI、JNI、Android UI、
其余 71 个 Command、`frame_fit`。

---

## 11. 测试记录

| 测试 | 位置 | 结果 |
|---|---|---|
| `tools/test_vg_summary.py`（10 用例） | 本地 + **CI 阻断作业** | ✅ 全通过 |
| CPU Alpha 矩阵（6 用例） | `tests/test_b1_supplement.cpp` | ✅ 全通过 |
| 本地全量单元测试 | 1716 用例 | 1712 通过 / 4 失败（`PreviewQuality`，需 FFmpeg，环境限制） |
| **CI 全量测试** | 1764 用例（含 FFmpeg） | ✅ **success**（run 42796799442） |
| **CI ASAN** | — | ✅ success |
| **CI UBSAN** | — | ✅ success |
| **Valgrind memcheck** | 1764 用例 / 16 分片 | ✅ **0 错误**（run 42796799387） |

### 11.1 CI run 结果（整改后，云端实测）

| 工作流 | run ID | run 级 | 作业 |
|---|---|---|---|
| `CI` | **42796799442** | ✅ **success** | build-and-test ✅ / ASAN ✅ / UB ✅ |
| `Valgrind Memcheck` | **42796799387** | ✅ **success** | build ✅ / 16 分片全 ✅ / 汇总 ✅ |
| `Android APK` | **42814098289** | ✅ success | Build APK + 冒烟 ✅ |

**主 CI run 不再因 Valgrind 超时被 cancelled —— 任务一达成。**
且 Valgrind 不是"被取消"，而是**独立跑完并给出真实结论** —— 任务二达成。

---

## 12. 提交号

| 提交 | 内容 |
|---|---|
| `180f22e3` | 首版：拆出独立 valgrind 工作流（8 分片）+ 测试覆盖度可审计 + Alpha 参考矩阵 |
| `ce62123b` | 分片 8 → 16（解决慢用例集中） |
| `1957fe0f` | 汇总改解析 XML + 独立工具 + 自检 10 用例；分片超时 45 → 90 |
| `db9a3b38` | 本报告（docs/NC-B1-FOLLOWUP-REPORT.md） |
| `02cfbdf1` | 合并 PR #21 入 main（**代码 + 报告**） |

---

## 13. 最终 main HEAD

**`02cfbdf1181aab8e300d27d53831c9281dedeeb3`**（PR #21 合并提交，含全部代码与本报告）

> 其后若仅补填文档字段，不改变任何代码结论。

---

## 14. main 侧复验（不是只引用分支结果）

整改成果合并后，在 **main @ `02cfbdf1`** 上重新跑了完整验证：

| 工作流 | run ID | run 级 | 作业 |
|---|---|---|---|
| `CI` | **42805407508** | ✅ **success** | build-and-test ✅ / ASAN ✅ / UB ✅ |
| `Valgrind Memcheck` | **42805407989** | ✅ **success** | build ✅ / 16 分片全 ✅ / 汇总 ✅ |
| `Android APK` | **42814098289** | ✅ **success** | Build APK ✅（含冒烟） |

main 侧 Valgrind 汇总日志（与分支结果一致）：

```
分片输出文件数: 16 / 期望 16
全量用例总数: 1764
实际执行合计: 1764
失败用例合计: 0
✅ 覆盖度校验通过：所有分片并集覆盖全量用例。
内存错误条目总数: 0
泄漏分类: definitely=0 indirectly=0 possibly=0 still_reachable=0
```

运行链接：
- CI: https://github.com/73kyyy/NairoClip/actions/runs/42805407508
- Valgrind: https://github.com/73kyyy/NairoClip/actions/runs/42805407989
- Android APK: https://github.com/73kyyy/NairoClip/actions/runs/42814098289

---

## 15. 结论

| 任务 | 结论 |
|---|---|
| ① 完整 CI run | ✅ **已解决**：run 级 `success`，不再因 valgrind 超时 `cancelled` |
| ② Valgrind 真实检测 | ✅ **已取得**：1764/1764 全覆盖，0 内存错误、0 泄漏 |
| ③ CPU/GPU Alpha 一致性 | ⚠️ **GPU 侧未验证**：仓库无 GPU 后端（§5 证据），已交付建设方案（§9） |

**三项中两项完成、一项如实标注无法验证。**
未通过缩小测试范围、跳过检测、关闭检查或伪造 GPU 结果来制造通过。

**停止执行，等待人工验收。未开始 B2/B3/B4/D。**
