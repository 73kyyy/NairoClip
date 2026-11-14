# NairoClip P0-FIX-2 后续安全缺陷修复报告

| 项 | 值 |
|---|---|
| 任务编号 | NC-P0-FIX-2 |
| 依据 | `docs/NC-P0-FINAL-HUMAN-REVIEW-REPORT.md` 的 P-06 / R-1 / R-2 |
| 基线 | `main` @ `2ad6fb22`；分支 `fix/p0-findkey`（起点 `73d16a74`） |
| 本轮提交 | `57d2f687477d8e9f7e1e76a59b9f340be8015295` |
| PR #28 | **open / merged=false**（本轮不合并） |
| 人工已确认 | ① 接受畸形 JSON 拒绝 ② 接受 `\uXXXX` 正确解码（C-3 关闭） |
| 结论 | **三项缺陷全部修复**；ASAN 本地受环境限制未执行（CI 已覆盖） |

---

## 1. P-06 修复证据：重复键静默回落默认值（P1-1）

### 1.1 问题回顾

`findJsonMember` 能检出 `DuplicateKey`，但 `JNum`/`JInt` 的签名只能返回一个值，
无法向调用方报错 → **只能回落默认值**。实测：
`{"track_index":2,"track_index":1}` → `JInt` 返回 `0` → 静默操作**轨道 0**。

### 1.2 修复方式

新增编辑前置校验（`core/json_args`）：

```cpp
/// 根对象任意键重复 → true（嵌套内部重复不计入）
bool hasDuplicateRootKey(const std::string& json, std::string* outKey,
                         std::string* outMessage);

/// 空串 = 通过；非空 = 错误消息，调用方必须拒绝执行编辑
std::string precheckEditArgs(const std::string& json);
```

在 `nc_jni.cpp` 的编辑分发入口 **`DispatchEdit` 第一条语句**接入：

```cpp
bool DispatchEdit(NcEditor* ed, const std::string& op, const std::string& a,
                  std::string* out) {
    /* P1-1：执行**任何**编辑之前先拒绝歧义参数对象。... */
    const std::string argsError = nairoclip::core::precheckEditArgs(a);
    if (!argsError.empty()) {
        *out = Err(argsError);
        return true;                      // ← 立即返回，不进入任何 nc_* 编辑调用
    }
    const int32_t ti = static_cast<int32_t>(JInt(a, "track", JInt(a, "track_index", 0)));
    ...
```

**关键性质**：校验是函数第一条语句，`return` 早于**全部** `nc_*` 调用
→ 拒绝时不可能修改轨道/媒体/项目状态。

### 1.3 全部 JNI 入口审计（30 个，逐项）

| 入口 | 是否执行工程编辑 | 是否读 JSON 参数（`FindKey` 路径） | 结论 |
|---|---|---|---|
| **`nativeCall`** | **是**（op 分发） | **是**（`DispatchEdit`，115 个键） | **P1-1 适用，已加校验** |
| `nativeApplyCommand` | 是（`nc_apply_command`） | **否**（0 次参数读取；整串交给 C ABI 的契约解析器） | 不适用；重复键由 `core/json.h` 处理（§7 P-06） |
| `nativePresetFrame` | 是（`nc_add_effect` / `nc_model_install`） | **否**（`jstring kind` / `jint style` / `jlong` 强类型参数） | 不适用 |
| `nativeInstallModel` | 模型安装（非工程编辑） | 否 | 不适用 |
| `nativeModels` | 否（查询） | 否 | 不适用 |
| `nativeCreate` / `nativeDestroy` | 编辑器生命周期 | 否 | 不适用 |
| `nativeState` | 否（查询） | 否 | 不适用 |
| `nativeFrame` / `nativeBlitFrame` / `nativeThumb` / `nativeWaveform` | 否（渲染/查询） | 否 | 不适用 |
| `nativePreviewSetCacheBudget` / `SetCacheScale` / `GetCacheBudget` | 否（预览配置，非工程数据） | 否 | 不适用 |
| `nativeTaskBegin/Heartbeat/Complete/Fail/Cancel/CancelAck/ClearResult/Query/Tick` | 否（任务登记表状态） | 否 | 不适用 |
| `nativeRegisterTextRasterizer` / `nativeReleaseMemory` / `nativeAttachSurface` / `nativeDetachSurface` / `nativeSurfaceReady` / `nativeInstallCrashHandler` | 否 | 否 | 不适用 |

> **结论**：30 个入口中只有 `nativeCall` 走 `FindKey` 参数读取路径，
> 因此只有它存在"重复键→默认值"的风险，**已加固**。
> 其余入口要么不读 JSON 参数，要么不执行工程编辑 —— 已逐项核实，未仅凭 `DispatchEdit` 一处下结论。

### 1.4 实测证据

| 载荷 | 前置校验 | 说明 |
|---|---|---|
| `{"track_index":2,"clip_id":1}` | **PASS** | 干净载荷不误拦 |
| `{"track_index":2,"track_index":1}` | **拒绝**（点名 `track_index`） | 修复目标 |
| `{"track_index":0,"track_index":0}` | **拒绝** | 值相同也拒绝（策略一致） |
| `{"name":"a","name":"b"}` | **拒绝**（点名 `name`） | 字符串键同样 |
| `{"a":{"x":1,"x":2},"track_index":2}` | **PASS** | **嵌套内部重复不影响根** |
| `{"a":1,"a":2,"track_index":2}` | **拒绝**（点名 `a`） | 非目标键也拦（整对象无歧义才执行） |
| `{}` | **PASS** | — |

**FK-037 用真实 C ABI 证明状态不被修改**：
`nc_editor_create` → 记录 `nc_track_count` / `nc_contract_revision` →
校验拒绝 → 断言两者**完全不变** → 随后合法 `nc_add_track` → 断言 +1（证明观测有效）。

---

## 2. R-1 修复证据：Unicode 孤立代理项（P1-2）

### 2.1 修复前（V1.4 交付版）

| 输入 | 状态 | 输出字节 |
|---|---|---|
| `\ud83c`（孤立高） | Ok | **`ED A0 BC`**（非法 UTF-8） |
| `\udfac`（孤立低） | Ok | **`ED BE AC`** |

### 2.2 修复后

| ID | 输入 | 状态 | 结果 |
|---|---|---|---|
| U1 | `\u0041` | Ok | `41` → `A` ✅ |
| U2 | `\u4e2d\u6587` | Ok | `E4 B8 AD E6 96 87` → `中文` ✅ |
| U3 | `\ud83c\udfac`（合法代理对） | Ok | `F0 9F 8E AC` → `🎬` ✅ |
| **U4** | `\ud83c`（孤立高） | **Malformed** | 拒绝：`孤立高代理项（缺少配对的低代理）` ✅ |
| **U5** | `\udfac`（孤立低） | **Malformed** | 拒绝：`孤立低代理项（缺少配对的高代理）` ✅ |
| **U4b** | `\ud83c\ud83c`（高+高） | **Malformed** | 拒绝：`代理对组合非法（高代理后不是低代理）` ✅ |
| **U4c** | `\ud83c\u0041`（高 + 普通） | **Malformed** | 同上 ✅ |
| U6 | `\u12`（不完整） | Malformed | `\u 含非十六进制字符` ✅ |
| U7 | `\x41`（非法转义） | Malformed | `非法转义字符` ✅ |
| U8 | `\uZZZZ` | Malformed | `\u 含非十六进制字符` ✅ |
| U9 | 原始 `中文 🎬` | Ok | 原样透传 ✅ |
| UB | `a\u00e9b` | Ok | `61 C3 A9 62` → `aéb` ✅ |

### 2.3 实现要点

1. **合法代理对**：`0x10000 + ((hi-0xD800)<<10) + (lo-0xDC00)` → 4 字节 UTF-8；
2. **非法组合一律 `Malformed`**：孤立高、孤立低、高+高、高+非低代理、不完整；
3. **禁止产出非法 UTF-8**：`appendUtf8` 只在码点非代理区时被调用；
4. **禁止静默替换**：不替换为 U+FFFD，直接拒绝并给出可读原因；
5. **不牵连无关字段**：值与嵌套键走 `skipString`（只跳过、不校验），
   只有**根键**与**命中的目标值**做严格解码 —— 避免"某字段有病态字符串导致所有参数失效"。

---

## 3. R-2 处理证据：`\u0000` 内嵌 NUL 截断（P2-1）

### 3.1 传递链审查

```
JSON `\u0000`
  → jsonStringValue 解码为 std::string（含内嵌 NUL，长度感知）
  → JStrField 返回 std::string
  → 调用点 `.c_str()`  → const char*
  → C ABI `nc_*(..., const char* name/path, ...)`  ← **NUL 结尾语义**
  → 在第一个 NUL 处**静默截断**
```

### 3.2 长度感知方案评估

| 方案 | 可行性 | 结论 |
|---|---|---|
| A. C ABI 改为「指针 + 长度」 | 需**修改公开 C ABI 契约**（`nc_add_track`/`nc_set_clip_text`/`nc_adjustment_set_lut` 等全部签名） | ❌ **本轮明令禁止**（不修改 C ABI 公开契约） |
| B. 在进入 C ABI 前拒绝 | 解析层拒绝 `\u0000` | ✅ **采用** |
| C. 静默截断 | — | ❌ 明令禁止 |

### 3.3 采用方案 B：解析层拒绝

`parseString` 在 `\u` 分支：`cp == 0` → `fail("\u0000 不受支持：下游 C ABI 为 NUL 结尾字符串，会截断")`。

### 3.4 实测

| 输入 | 状态 | 结果 |
|---|---|---|
| `{"name":"\u0000"}` | **Malformed** | 拒绝 ✅ |
| `{"name":"abc\u0000def"}` | **Malformed** | 拒绝 ✅ |
| 同上，`jstr()` 返回值 | — | **空串**，且**不等于 `"abc"`**（证明未静默截断）✅ |
| `{"name":"abc"}`（对照） | Ok | `abc` ✅ |

### 3.5 兼容性影响

| 影响面 | 评估 |
|---|---|
| 前端是否会产生 `\u0000` | ❌ 不会 —— `JSON.stringify` 把 U+0000 编码为 `\u0000` **仅在字符串真含 NUL 时**；UI 无法输入 NUL |
| 修复前行为 | 返回字面 `u0000`（同样错误，但不会截断） |
| 修复后行为 | 明确拒绝 + 可读原因 |
| 下游 C ABI | **零改动** |

---

## 4. 新增测试与结果

### 4.1 新增用例（FK-025 ~ FK-038，共 14 个）

| 编号 | 用例 | 覆盖 |
|---|---|---|
| FK-025 | `DuplicateRootKeyRejectedByPrecheck` | 根重复键拒绝（6 种载荷）+ 消息点名 |
| FK-026 | `NestedDuplicateDoesNotBlock` | 嵌套内部重复不误报 |
| FK-027 | `NonTargetDuplicateStillBlocked` | 非目标键重复也拦 |
| FK-028 | `LoneHighSurrogateRejected` | 孤立高代理 + 不得静默替换 |
| FK-029 | `LoneLowSurrogateRejected` | 孤立低代理 |
| FK-030 | `BadSurrogatePairRejected` | 高+高、高+普通 |
| FK-031 | `ValidSurrogatePairAccepted` | 合法代理对 + 边界码点（`\ud800\udc00`、`\udbff\udfff`） |
| FK-032 | `EmbeddedNulRejected` | `\u0000`、`abc\u0000def`，且**不得退化为 `abc`** |
| FK-033 | `BmpChineseEmojiStillWork` | BMP / 中文 / emoji / `\u007f` / `\u0080` 边界 |
| FK-034 | `NineGroupsWithDuplicateAllBlocked` | **九组真实载荷**各带重复键 → 全部拒绝 |
| FK-035 | `NormalPayloadsPassPrecheck` | 7 类正常载荷不误拦 |
| FK-036 | `PrecheckDefersMalformedToLookup` | 畸形/非对象由查找层判 `Malformed` |
| FK-037 | `RejectedArgsDoNotTouchProjectState` | **真实 C ABI** 断言拒绝路径不改 `track_count`/`revision` |
| FK-038 | `PrecheckAndLookupAreLayered` | 校验层拒绝 vs 查找层返回 `DuplicateKey`（不猜值） |

### 4.2 本地执行结果

```
命令：g++ -std=c++17 -O1 -Wall -Wextra -I. -I../core/include \
        test_main.cpp test_json_args.cpp ../core/src/json_args.cpp（+ 全 core 目标文件）
结果：===== 执行 42/42 cases | 0 failed | 0 checks failed =====
      [SHARD_STATS] shard=0 shards=1 executed=42 total=42 failed=0
```

### 4.3 UBSAN

```
命令：g++ -std=c++17 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
        （全部 100 个 core 源文件 + 测试）
结果：✅ 42/42 通过，无任何 UBSAN 报告
```

### 4.4 ASAN（本地未执行 —— 环境限制，如实记录）

```
命令：g++ -std=c++17 -O1 -g -fsanitize=address,undefined ...
结果：链接成功，但运行时报
      AddressSanitizer: CHECK failed: sanitizer_allocator_primary64.h:131
      (0x500000000000, 0xfffffffffffffff4)
原因：本机为 Android proot 环境，ASAN 影子内存无法建立地址空间映射。
判定：**环境限制，非代码问题**。ASAN 证据以 CI 的 asan-diagnose 作业为准。
```

---

## 5. 原有测试回归

| 项 | 结果 |
|---|---|
| FK-001 ~ FK-024 | ✅ 全部通过（含在新 42 个用例内） |
| 原有 4 个附加用例（NumericSemanticsPreserved / VectorReadersPreserveSemantics / MalformedAndNotObjectAreDistinct / StatusNames） | ✅ 全部通过 |
| **804 个合法输入兼容性扫描** | ✅ **数值 0 差异、字符串 0 差异**（与修复前完全一致） |
| 全量回归（CI） | ✅ **1806 用例 / 0 失败**（基线 1792，**+14**） |

### 5.1 关于 CI 的 1 项差异（继续如实记录）

| 分支/运行 | total | executed | 差 |
|---|---|---|---|
| `main` @ `2ad6fb22`（CI 单进程） | 1764 | 1763 | −1 |
| 上一轮（`0a376f36`，CI 单进程） | 1792 | 1791 | −1 |
| **本轮（`57d2f687`，CI 单进程）** | **1806** | **1805** | **−1** |
| 上一轮 Valgrind（16 分片） | 1792 | 1792 | **0** |

`executed` 1791 → 1805 = **+14**，正好等于新增用例数。
该 −1 差异在 `main` 基线上就存在，**属既有现象，未修改测试基线掩盖**。

---

## 6. 编译与 CI 结果

| 检查 | 命令 / 工作流 | 环境 | 结果 | 证据 |
|---|---|---|---|---|
| `json_args.cpp` 编译 | `g++ -std=c++17 -Wall -Wextra -Werror -c` | 本地 | ✅ | 无警告 |
| JNI 辅助块编译 | 提取 54 行原样编译 `-Werror` | 本地 | ✅ | 通过 |
| FK 全量（本地） | 见 §4.2 | 本地 | ✅ 42/42 | 原始输出 |
| UBSAN（本地） | 见 §4.3 | 本地 | ✅ 42/42 | 无报告 |
| **全量回归** | `ci.yml` / `build-and-test` | GitHub | ✅ **success** | run **42965866598** |
| **ASAN** | `asan-diagnose` | GitHub | ✅ success | 同 run |
| **UBSAN** | `ub-diagnose` | GitHub | ✅ success | 同 run |
| **Android Cross-Compile** | `android-build.yml` | NDK r27d | ✅ success | 本轮 dispatch |
| **Android APK**（NDK 编译 `nc_jni.cpp`） | `android-apk.yml` | NDK r27d | ✅ success | 本轮 dispatch |
| **Valgrind Memcheck** | `valgrind.yml` | GitHub | ✅ **success** | run **42965866655** |

### 6.1 Valgrind Memcheck（run 42965866655，run 级 success）

```
分片输出文件数: 16 / 期望 16
全量用例总数: 1806
实际执行合计: 1806
失败用例合计: 0
✅ 覆盖度校验通过：所有分片并集覆盖全量用例。
内存错误条目总数: 0
泄漏分类: definitely=0 indirectly=0 possibly=0 still_reachable=0
```

**16 分片全部 success；1806/1806 全覆盖（含新增 14 个 FK 用例）；
零内存错误、零泄漏。**

> 与 CI 单进程的 `1805/1806` 相比，Valgrind 分片运行覆盖 **1806/1806**，
> 进一步证明全部用例均可执行且通过；CI 的 −1 差异属既有现象（§5.1）。

---

## 7. 修改文件与代码差异

提交 `57d2f687`（相对 `73d16a74`）：

| 文件 | 状态 | 差异 | 说明 |
|---|---|---|---|
| `core/include/nairoclip/core/json_args.h` | 修改 | **+15 / −0** | 新增 `hasDuplicateRootKey` / `precheckEditArgs` 声明 |
| `core/src/json_args.cpp` | 修改 | **+125 / −19** | 代理项严格校验、NUL 拒绝、重复键检测、目标值预校验 |
| `mobile/native/android/nc_jni.cpp` | 修改 | **+12 / −0** | `DispatchEdit` 入口 5 行前置校验 + 注释 |
| `tests/test_json_args.cpp` | 修改 | **+194 / −0** | FK-025 ~ FK-038（14 个用例） |
| **合计** | 4 文件 | **+346 / −19** | — |

**未修改**：C ABI（`capi.h`/`capi.cpp`）、Editor API、GPU、音频、媒体、
B1 报告、GPU 架构文档、任何 CI 工作流文件、`core/json.h`/`json.cpp`。

---

## 8. PR #28 更新状态

| 项 | 值 |
|---|---|
| 状态 | **open / merged = false** |
| 分支 | `fix/p0-findkey` |
| 最新提交 | `57d2f687`（本轮） |
| 分支累计 | 4 commits（`9e329979` → `a5efaaa2` → `0a376f36` → `73d16a74` → `57d2f687`） |
| 相对 main 的文件 | 8 个（4 新增 / 4 修改） |
| **是否合并** | **否**（本轮不合并，等待人工最终验收） |

---

## 9. 尚未解决的问题

| ID | 级别 | 问题 | 状态 |
|---|---|---|---|
| **C-1** | — | 真机嵌套键兼容性测试 | ❌ **未完成**（无真机接入）。静态审计已确认 args 顶层扁平（`engine.js:94` `Object.assign({}, payload)`、`nc-api.js:41` 直接透传） |
| **C-2** | — | 真机 JNI 字符串生命周期测试 | ❌ **未完成**（步骤见上轮报告 §5.4） |
| C-3 | — | 两项行为变化确认 | ✅ **已由人工确认关闭** |
| P-04 | P3 | 无性能基准 | 未处理（参数对象 <1KB） |
| **P-06'** | P2 | `nativeApplyCommand` 路径的重复键由 `core/json.h` 的 `set()` 静默覆盖（不报错） | **本轮未改**（属 B3/契约层范围，且需改共享解析器）。影响面：该路径的 JSON 由 Kotlin 侧构造，不产生重复键 |
| P-07 | P3 | `JStr()` 中 `std::string out(utf)` 抛 `bad_alloc` 时不会 `ReleaseStringUTFChars` | 既有风险，与修复前同形，未改 |
| P-08 | P3 | ASAN 无法在本机 proot 环境运行 | 环境限制，CI 已覆盖 |

---

## 10. 结论

| 项 | 结论 |
|---|---|
| **P-06（重复键）** | ✅ 已修复：编辑前拒绝，30 个入口逐项审计，状态不被修改（FK-037 实测） |
| **R-1（孤立代理）** | ✅ 已修复：全部非法组合 → `Malformed`，不产非法 UTF-8，不静默替换 |
| **R-2（内嵌 NUL）** | ✅ 已处理：评估长度感知方案不可行（需改 C ABI），改为解析层拒绝，禁止静默截断 |
| 新增测试 | ✅ 14 个（FK-025~FK-038），本地 42/42 |
| 原有回归 | ✅ 804 样本零差异；CI 1806 用例 0 失败 |
| 编译 / CI / NDK | ✅ 全部通过 |

**本轮只修复与提交，未合并 PR #28，未启动 B2-1，未申请 V1.5。**
等待人工最终验收。
