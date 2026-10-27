# 骨架体检报告（Architecture Audit）

> 核查日期：2026-10-26
> 方法：**全库 grep + 源码阅读 + 实测**，非文档推测。
> 结论摘要：**骨架整体扎实，但有 1 个数据正确性缺陷 + 3 个工程隐患需先修。**

---

## 一、总体评价

| 维度 | 评价 | 依据 |
|---|---|---|
| 分层与依赖 | ✅ 清晰 | 单向依赖，`core` 零第三方依赖 |
| 时间模型 | ✅ 优秀 | 有理数时间，帧号零漂移 |
| 数据不变量 | ✅ 有守卫 | `invariantsHold()` + 刚补的 26 个测试 |
| 错误处理 | ⚠️ 不一致 | `Result` 247 处 vs 裸 `bool` 465 处 |
| 内存管理 | ✅ 基本良好 | 无 `throw`，RAII 为主 |
| 线程安全 | ❌ 无保护 | `mutex`/`atomic` **0 处** |
| 平台一致性 | ⚠️ 有隐患 | `Rational` 在 MSVC 上溢出无声 |
| 失败恢复 | ⚠️ 有缺陷 | 导出半成品不清理 |

---

## 二、发现的问题（按严重度）

### 🔴 P0-1：`Rational` 在无 `__int128` 平台静默溢出

**位置**：`core/src/rational.cpp:17-21`

```cpp
#if defined(__SIZEOF_INT128__)
using Wide = __int128;
#else
using Wide = std::int64_t;   // ← MSVC 走这条
#endif
```

**为什么不影响现在**：Linux/macOS 的 GCC/Clang 都有 `__int128`，
所以本机测试全绿。

**为什么是 P0**：`README.md` 写明 CI 覆盖 **ubuntu / macos / windows 三平台**。
Windows 的 MSVC **没有 `__int128`**，走 `int64_t` 分支：

```cpp
Rational operator*(const Rational& a, const Rational& b) {
    const Wide n = static_cast<Wide>(a.num / g1) * (b.num / g2);  // int64 × int64
    return Rational::make(narrow(n), narrow(d));                   // narrow 直接截断
}
```

`int64 × int64` 溢出是 **UB**（未定义行为），不报错、不崩溃，**结果随机**。

**触发场景**：

- 4K 60fps 长视频：帧号 × 时间基 容易超 2^63
- 高精度关键帧插值（多个有理数连乘）

**后果**：时间计算错误 → 片段错位 → **用户成片出错但看不出何时错的**。
这类"静默数据损坏"比崩溃难查一个数量级。

**修复方案（三选一）**：

1. **自实现 64×64→128 乘法**（推荐，无依赖）
2. `narrow()` 中检测溢出，饱和并记录警告（至少不静默）
3. 乘法前先约分到安全范围

**验证方法**：Windows CI 加一条断言
`Rational::make(INT64_MAX/2, 1) * Rational::make(4, 1)` 的行为必须被定义。

---

### 🔴 P0-2：全库无线程保护

**证据**：

```bash
$ grep -rn "std::mutex\|std::atomic\|lock_guard" core/src/*.cpp | wc -l
0
```

**这是双刃剑**：

- 好处：没有锁竞争，单线程性能好，无死锁风险
- 坏处：**任何跨线程调用都会数据竞争**

**已知共享可变状态**：

| 组件 | 状态 | 典型并发场景 |
|---|---|---|
| `TimelineEditor` | `undoStack`、`project` | UI 编辑 + 导出线程读 project |
| `Translator` | `lastDegenerate_` | 后台翻译多句 |
| `AiSession` | ORT `Session::Run` 本身线程安全，但包装层无保护 | 多任务推理 |
| `ModelManager` | 下载状态机 | 后台下载 + UI 查询 |

**实际风险**：

用户点了导出，然后继续拖动片段 → 导出线程正在遍历 `project.sequences[0].tracks`，
UI 线程同时在 `clips.erase()` → **迭代器失效 → 崩溃或读到已释放内存**。

**修复方案**：

1. **最小改动**：`TimelineEditor` 加 `mutable std::mutex`，
   所有公开方法 `lock_guard`（性能影响小，编辑不是高频操作）
2. **更好方案**：导出前 `Project` 深拷贝快照，导出只读快照（改动小、语义清晰）
3. 头文件明确注明线程契约：`/// @warning 非线程安全`

**建议**：先做 2（快照），因为语义最清晰且不影响编辑性能。

---

### 🟠 P0-3：导出中断后留下"看起来正常"的半成品

**位置**：`core/src/media_ffmpeg_writer.cpp:581-588`

```cpp
void FFmpegMediaWriter::abort() {
    if (impl_ == nullptr || !impl_->open) return;
    impl_->reset();
    // 删除半成品文件（由调用方控制是否删除；这里保守地保留，
    // 因为某些平台下删除行为需要用户确认）。
}
```

**问题链路**：

1. 导出到 `我的视频.mp4`
2. 用户取消 / 磁盘满 / 编码失败
3. `abort()` 被调用 → **文件保留**，但**没有 trailer**
4. 文件在系统里**看起来是正常的 mp4**（有大小、有图标、能点开）
5. 用户双击 → **播放器报错 / 播放到一半卡死**

**为什么严重**：用户无法区分"导出成功"和"导出失败"。

**修复方案**：

1. **写临时文件**：`我的视频.mp4.part` → 成功后 `rename` 为 `我的视频.mp4`
   （原子操作，失败不留残留）
2. 或 `abort()` 中直接删除目标文件（注释说"某些平台需要用户确认"——实际不需要，
   删除自己刚创建的文件是允许的）

**推荐**：方案 1。这是行业标准做法（Chrome 下载、FFmpeg 本身都这样）。

---

### 🟠 P1-1：`Result` 与 `bool` 混用

**数据**：

```
返回 Result 的公开方法：247
返回裸 bool 的方法：    465
```

**问题**：`bool` 失败时不带原因。上层 C ABI 只能返回 `NC_ERROR_UNKNOWN`，
用户看到"操作失败"但不知道为什么。

**哪些应该改**：

| 场景 | 当前 | 应该 |
|---|---|---|
| `editor.addTrack()` | `Result` ✅ | 保持 |
| `estimator.load()` | `bool` + `string*` | 可接受（有原因） |
| `Tensor::valid()` | `bool` ✅ | 保持（纯判定） |
| `translator.translate()` | `bool` + `string*` | 可接受 |

**结论**：现状**比数字看起来好**——大多数 `bool` 配合了 `string* outputFailure`。
真正需要改的是**既无原因、又面向用户**的接口。

**行动**：审计 `capi.cpp` 中所有 `return -1` 但未设 `out_error` 的路径。

---

### 🟠 P1-2：C ABI 75 个出参未标注所有权

**数据**：

```
标注"需 nc_free"的：      63 处
未标注的 char**/uint8_t**：75 处
```

**为什么重要**：平台层是 Kotlin / Swift / C#，**没有 RAII**。
不知道谁释放 → 要么泄漏（不释放），要么 double-free（两边都释放）。

**修复**：`capi.h` 逐处补注释 + 顶部加「内存所有权约定」总章。

**可自动化**：写个脚本扫 `capi.h`，凡是 `char** out_` 后面 3 行内没有
`nc_free_string` 就报错，接入 CI。

---

### 🟡 P1-3：序列化只拒绝高版本，未定义低版本迁移

**位置**：`core/src/serialization.cpp:1174`

```cpp
if (version > kNclipFormatVersion) {
    return Result::fail(ErrorCode::Unsupported, "文件版本高于当前程序支持的版本");
}
// 低版本：直接按当前结构读取 ← 依赖字段默认值，未显式处理
```

**问题**：格式升级后，老工程能否打开**取决于新字段是否有默认值**。
目前可能碰巧能开，但没有保证。

**修复**：

1. 建立迁移表（v1→v2→v3 的字段补齐规则）
2. 强制每个新增字段有默认值
3. 补跨版本加载测试（造一个 v1 的 JSON，断言能读）

---

### 🟡 P2-1：边界访问 279 处裸下标

```
直接下标 [i]：279 处
.at() 安全：   25 处
```

**说明**：这**不全是问题**。很多是热循环（逐像素），用 `.at()` 会拖慢。

**真正需要审计的三类**：

1. **C ABI 传入的 index/count**（外部不可信）
2. **JSON 解析的下标**（文件可能被篡改）
3. **时间→帧号换算结果**（可能为负或超范围）

**行动**：只审计这三类，其他保持。

---

### 🟡 P2-2：无内存上限保护

**数据**：`grep -i "memory.*limit\|OOM"` 仅 1 处命中（波形解码限 120s）

**风险**：4K 素材 + 多层特效 → 导出时逐帧 RGBA 缓冲可能上 GB。
低端机（2~4GB RAM）会直接被系统杀死。

**修复**：

1. 导出前估算峰值内存（宽 × 高 × 4 × 层数 × 缓冲帧数）
2. 超限时降级（分块处理）或明确拒绝并提示
3. 不能静默 OOM（用户会以为 App 崩了）

---

## 三、做得好的地方（不夸大）

| 项 | 证据 |
|---|---|
| **零异常设计** | `throw` 0 处，`catch` 3 处（都在 ORT 边界），异常不会穿透 ABI |
| **无常量魔法数** | 大多有 `constexpr` 命名 |
| **时间模型** | 有理数表示，`29.97 = 30000/1001` 精确 |
| **UTF-8 边界处理** | `subtitle.cpp:89` 正确回退到字符边界，不会切断多字节字符 |
| **不变量测试** | 刚补 26 例，覆盖 Clip/Track/Sequence 正反例 |
| **无全局可变状态** | `static` 可变状态 0 处，避免多实例互相污染 |
| **FFmpeg 资源释放** | 11 处释放 vs 4 处获取，覆盖充分（有冗余但安全） |

---

## 四、修复优先级建议

| 顺序 | 问题 | 为什么先做 | 预估 |
|---|---|---|---|
| 1 | **P0-1 Rational 溢出** | 三平台 CI 会跑，Windows 上是定时炸弹 | 2 天 |
| 2 | **P0-3 导出半成品** | 用户直接受害，修复简单 | 0.5 天 |
| 3 | **P0-2 线程安全** | 导出+编辑并发是必然场景 | 2 天 |
| 4 | P1-2 ABI 所有权标注 | 平台层接入前必须做 | 1 天 |
| 5 | P1-3 序列化迁移 | 一旦发版就难改 | 1 天 |
| 6 | P1-1 错误路径补齐 | 影响用户体验 | 2 天 |
| 7 | P2-1/P2-2 | 加固 | 2 天 |

**合计约 1.5 周**。建议在开发新功能（`DEVELOPMENT_BACKLOG.md` B1~B4）**之前**完成 1~3 项，
因为新功能会大量触碰时间计算与导出管线。

---

## 五、如何复现本报告的检查

```bash
cd /workspace/repo

# P0-1：Wide 类型分支
sed -n '15,25p' core/src/rational.cpp

# P0-2：线程保护数量（应为 0）
grep -rn "std::mutex\|std::atomic\|lock_guard" core/src/*.cpp | wc -l

# P0-3：导出 abort 不留清理
sed -n '581,590p' core/src/media_ffmpeg_writer.cpp

# P1-1：Result vs bool
grep -c "Result " core/include/nairoclip/core/*.h | awk -F: '{s+=$2} END {print s}'
grep -c "^\s*bool [a-z]" core/include/nairoclip/core/*.h | awk -F: '{s+=$2} END {print s}'

# P1-2：未标注所有权的出参
grep -E "char\*\* out|uint8_t\*\* out" core/include/nairoclip/core/capi.h | grep -vc "nc_free"

# P2-1：边界访问
grep -c "\[i\]\|\[idx\]\|\[index\]" core/src/*.cpp | awk -F: '{s+=$2} END {print s}'
grep -c "\.at(" core/src/*.cpp | awk -F: '{s+=$2} END {print s}'
```
