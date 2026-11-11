# NairoClip P0 专项修复报告：FindKey JSON 键解析缺陷

| 项 | 值 |
|---|---|
| 任务编号 | NC-P0-FINDKEY-REPAIR |
| 优先级 | P0 / 阻断级 |
| 依据 | `docs/NC-B2-V1.4-INDEPENDENT-REVIEW-REPORT.md` 问题 D-01 |
| 基线 HEAD（修复前） | `2ad6fb22ab89675627681dde1bf3180bc13a19e7` |
| 修复分支 | `fix/p0-findkey` |
| PR | #28（**未合并**，等待人工审查） |
| 结论 | **有条件通过**（核心缺陷已修复；待验证项见 §14） |

---

## 1. 缺陷根因

`mobile/native/android/nc_jni.cpp` 修复前的键定位实现：

```cpp
bool FindKey(const std::string& json, const std::string& key, size_t* outPos) {
    const std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);            // ← 纯子串搜索
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());   // ← 取其后第一个 ':'
    if (p == std::string::npos) return false;
    *outPos = p + 1;
    return true;
}
```

**它不理解 JSON 结构**，只在字节流里找 `"key"` 这四个字符。由此产生四类误命中：

| 类别 | 机制 |
|---|---|
| 字符串值内出现键名文本 | 命中字符串内容，随后取"其后第一个 `:`" → 读到**下一个键**的值 |
| 嵌套对象同名键 | 命中嵌套层级，把嵌套值当根成员 |
| 数组字符串含键名 | 同上 |
| 重复键 | 静默取第一个，结果不确定 |

**`JNum` / `JInt` / `JStrField` / `JVecDouble` / `JVecU64` 全部经 `FindKey`**，
因此 9 组 / 18 个 op 的参数读取全部受影响。

---

## 2. 修复前实测（复现证据）

用修复前的 4 个函数**逐字复制**编译运行：

| ID | 输入 JSON | 键 | 期望 | 修复前实际 |
|---|---|---|---|---|
| C2 | `{"a":"value","b":1}` | value | 0.8 | **1.0** |
| C6 | `{"a":{"value":0.1},"value":0.8}` | value | 0.8 | **0.1** |
| C11 | `{"arr":["value"],"b":2}` | value | 0.8 | **2.0** |
| **R1** | `{"path":"strength","track_index":2,"strength":0.5}` | strength | 0.5 | **2.0** |
| **R2** | `{"path":"track_index","strength":0.5,"track_index":2}` | track_index | 2 | **0.5 → 截断为 0** |

**R2 是最严重的**：`track_index` 被读成 `0.5`，`JInt` 截断为 `0` →
**静默操作错误轨道**，无任何错误提示。

---

## 3. 修复方案

### 3.1 依赖选择（任务书 3.2 要求）

**已审查仓库现有 JSON 能力**：

| 候选 | 结论 |
|---|---|
| `core/include/nairoclip/core/json.h` + `core/src/json.cpp`（505 行，零依赖完整解析器） | **不复用其解析路径**，理由见下 |
| 第三方库（nlohmann / rapidjson / cppjson / cJSON / picojson） | 仓库**均不存在**，且不引入（任务书要求未经批准不得引入） |

**为何不直接复用 `core/json.h`**（两个硬冲突）：

| 冲突 | 说明 |
|---|---|
| 数值语义 | `json.cpp:348` 对整型 token 走 `strtoll`，会把 `9007199254740993` 解析为**精确值**；而修复前 `JInt` 经 `strtod`→double 得 `9007199254740992`。直接复用会**改变合法输入语义**（违反任务书 3.3「不得修改已确认的 INT64 边界规则」） |
| 重复键 | `JsonValue::set()`（`json.cpp:452`）对重复键**静默覆盖**，无法实现任务书 3.1(10) 要求的"明确拒绝" |

**决定**：新增 `core/json_args`，只做「结构感知的键定位 + 值区间提取」，
**数值转换仍走原 `strtod`**。这样既修好结构缺陷，又保证数值语义逐位不变。
同时**不修改** `core/json.h/json.cpp`（它们是 `.nclip` 工程文件格式的解析器，
改动会波及存储层，超出本任务边界）。

### 3.2 新增模块

| 文件 | 作用 |
|---|---|
| `core/include/nairoclip/core/json_args.h` | 接口：`findJsonMember` / 值提取 / 状态枚举 |
| `core/src/json_args.cpp` | 单遍扫描实现（约 430 行） |

**核心接口**

```cpp
enum class JsonMemberStatus { Ok, Missing, NotObject, Malformed, DuplicateKey, TypeMismatch };
enum class JsonValueKind { Unknown, Null, Bool, Number, String, Array, Object };
struct JsonMemberSpan { std::size_t begin, end; JsonValueKind kind; };
struct JsonLookup { JsonMemberStatus status; JsonMemberSpan span; std::string message; };

JsonLookup findJsonMember(const std::string& json, const std::string& key);
JsonLookup findJsonMember(const std::string& json, const std::string& key, JsonValueKind expected);

double      jsonNumberValue(const std::string&, const JsonMemberSpan&, double def);
std::string jsonStringValue(const std::string&, const JsonMemberSpan&);
bool        jsonNumberArray(const std::string&, const JsonMemberSpan&, std::vector<double>*);
bool        jsonU64Array(const std::string&, const JsonMemberSpan&, std::vector<std::uint64_t>*);
```

**满足任务书 3.1 的十条要求**

| 要求 | 实现 |
|---|---|
| 1 明确解析目标对象 | 只解析**根对象**（首字符必须是 `{`，否则 `NotObject`） |
| 2 只匹配直接成员 | 逐成员扫描，键值对之间按 `,` / `}` 推进，不进入值内部 |
| 3 不在字符串值中搜索 | `skipValue` 遇 `"` 走 `parseString`（处理转义），跳过整段 |
| 4 不在数组元素中搜索 | `skipArray` 递归跳过，不匹配任何键 |
| 5 不误取嵌套同名键 | `skipObject` 递归跳过嵌套对象 |
| 6 正确识别转义与结构边界 | `\" \\ \/ \b \f \n \r \t \uXXXX`（含代理对） |
| 7 空串/中文/Unicode/emoji | 空串 → `String` 且内容为空；中文/emoji 原样透传；`\u` 正确解码 |
| 8 对象/数组/数字/布尔/null | `JsonValueKind` 六种全部识别 |
| 9 区分不存在/类型不匹配/畸形 | `Missing` / `TypeMismatch` / `Malformed` 三态分离 |
| 10 重复键明确拒绝 | `DuplicateKey`，**既不取第一个也不取最后一个** |

### 3.3 JNI 侧改动

5 个辅助函数改为委托，**签名不变、语义不变**：

```cpp
double JNum(const std::string& json, const std::string& key, double def) {
    const auto hit = findMember(json, key, JsonValueKind::Number);
    if (hit.status != JsonMemberStatus::Ok) return def;
    return jsonNumberValue(json, hit.span, def);
}
int64_t JInt(const std::string& json, const std::string& key, int64_t def) {
    return static_cast<int64_t>(JNum(json, key, static_cast<double>(def)));  // 仍经 double
}
```

**256 个调用点（115 个不同键名）一行未改。**

---

## 4. 修复前后实现差异

| 维度 | 修复前 | 修复后 |
|---|---|---|
| 键定位 | `json.find("\"key\"")` 子串搜索 | 结构遍历，只匹配根对象直接成员 |
| 字符串值内键名 | **误命中** | 跳过 |
| 嵌套对象/数组同名键 | **误命中** | 跳过 |
| 重复键 | 静默取第一个 | `DuplicateKey`，回落默认值 |
| 畸形 JSON | 仍可能取值 | `Malformed`，回落默认值 |
| 根非对象 | 可能取值 | `NotObject` |
| 数值语义 | `strtod` | **`strtod`（不变）** |
| 整数语义 | 经 double 中转 | **经 double 中转（不变）** |
| 字符串转义 | `\n \t` 与通用 `\x` | 完整 JSON 转义（含 `\uXXXX`、代理对） |
| 空字符串 | `""` | `""`（不变） |

---

## 5. 调用点清单（修复前后行为）

### 5.1 直接依赖

| 函数 | 出现次数 | 修复前 | 修复后 |
|---|---|---|---|
| `JNum` | 62 | 子串搜索 | 结构查找 + `strtod` |
| `JInt` | 148 | 经 `JNum` | 经 `JNum`（不变） |
| `JStrField` | 32 | 子串搜索 + 跳过至引号 | 结构查找 + 解码 |
| `JVecDouble` | 7 | 子串搜索 + 逐 `strtod` | 结构查找 + 逐 `strtod` |
| `JVecU64` | 7 | 子串搜索 + 逐 `strtoull` | 结构查找 + 逐 `strtoull` |
| **合计** | **256** | — | — |

### 5.2 九组 / 十八个 op

| 组 | 短名 op | 规范 op | 该组读取调用数 | 修复前风险 | 修复后 |
|---|---|---|---|---|---|
| G1 | `opacity` | `nc_set_clip_opacity` | 2 | 中 | ✅ |
| G2 | `add_track` | `nc_add_track` | 4 | 高（`name` 含键名时） | ✅ |
| G3 | `blend` | `nc_set_clip_blend_mode` | 2 | 中 | ✅ |
| G4 | `effect_add` | `nc_add_effect` | 4 | 中 | ✅ |
| G5 | `keyframe` | `nc_insert_keyframe` | 8 | 高（`v`/`value`/时间） | ✅ |
| G6 | `remove_keyframe` | `nc_remove_keyframe` | 6 | 高 | ✅ |
| G7 | `adjust_effect_add` | `nc_adjustment_add_effect` | 6 | 中 | ✅ |
| G8 | `adjust_effect_remove` | `nc_adjustment_remove_effect` | 4 | 中 | ✅ |
| **G9** | `adjust_lut` | `nc_adjustment_set_lut` | 6 | **最高**（`path` 为字符串，易含键名） | ✅ |

### 5.3 是否存在不同 JSON 层级语义

**静态审计结论**：JNI 读取的 115 个键**全部是根级标量**；
前端 `mobile/assets/engine.js:96` 以 `bridge.call(op, JSON.stringify(args))` 发送
**扁平 args 对象**；未发现任何 op 依赖读取嵌套层级的键。

**修复前能读到嵌套键（副作用）**，修复后按设计读不到：

| 载荷 | 键 | 修复前 | 修复后 |
|---|---|---|---|
| `{"wrap":{"scale":2}}` | scale | 2.0 | 默认值 |
| `{"a":{"b":{"track_index":3}}}` | track_index | 3 | 默认值 |
| `{"text":"{\"scale\":9}"}` | scale | 默认值 | 默认值 |

> **该差异是修复的目标本身**（嵌套键本就不该被当根成员读取）。
> 但因缺少真机抓包，无法 100% 证明无调用方依赖 → 列为 §11 的 P2 待验证项。

---

## 6. FK-001 ~ FK-024 测试结果

测试文件：`tests/test_json_args.cpp`（注册进 `tests/CMakeLists.txt`，
编译进仓库正式测试二进制 `nairoclip_tests`，**非独立模拟程序**）。

### 6.1 本地执行

```
命令：g++ -std=c++17 -O1 -Wall -Wextra -I. -I../core/include \
        test_main.cpp test_json_args.cpp ../core/src/json_args.cpp -o /tmp/fk_tests
      /tmp/fk_tests
退出码：0
结果：===== 执行 28/28 cases | 0 failed | 0 checks failed =====
```

### 6.2 逐条结果

| 编号 | 用例 | 输入要点 | 预期 | 结果 |
|---|---|---|---|---|
| FK-001 | 正常直接成员键 | `{"value":0.8}` | 0.8 / Ok | ✅ |
| FK-002 | 键缺失 | `{"x":1}`；`{"values":1}` | Missing，回落默认 | ✅ |
| FK-003 | 空字符串值 | `{"name":""}`；`{"value":""}` | 字符串键 Ok+空串；数值键 TypeMismatch | ✅ |
| FK-004 | null 值 | `{"value":null}` | TypeMismatch，回落默认 | ✅ |
| FK-005 | 类型不匹配 | 字符串/布尔/对象/数组给数值键 | 全部 TypeMismatch | ✅ |
| FK-006 | 重复键拒绝 | `{"value":0.1,"value":0.8}` | DuplicateKey；既非 0.1 也非 0.8 | ✅ |
| FK-007 | 字符串值含键名 | `{"a":"value","b":1}` | Missing（修复前 1.0） | ✅ |
| FK-008 | 字符串含完整 `"key":value` | `{"description":"\"value\":0.1","value":0.8}` | 取外层 0.8 | ✅ |
| FK-009 | 转义引号 | `{"name":"a\"b"}` | `a"b` | ✅ |
| FK-010 | 反斜杠转义 | `\\` `\n` `\t` `\/` | 正确解码 | ✅ |
| FK-011 | 中文 + emoji + `\u` | `中文轨道` / `轨道🎬` / `\u4e2d\u6587` / 代理对 | 全部正确 | ✅ |
| FK-012 | 空串不误匹配 | `{"a":"","value":0.2}`；`{}` | 0.2 / Missing | ✅ |
| FK-013 | 嵌套同名键 | `{"a":{"value":0.1},"value":0.8}` | 0.8（修复前 0.1） | ✅ |
| FK-014 | 数组字符串含键名 | `{"arr":["value"],"b":2}` | Missing（修复前 2.0） | ✅ |
| FK-015 | 数组内对象同名键 | `{"list":[{"value":0.1}]}` | Missing | ✅ |
| FK-016 | 多层同名键 | 三层嵌套 + 直接成员 | 取直接成员 | ✅ |
| FK-017 | 直接键 vs 嵌套键 | `{"nested":{"track_index":9},"track_index":2}` | 2 | ✅ |
| FK-018 | 原始 C2 复现 | 同 FK-007 | 0.8 | ✅ |
| FK-019 | 原始 C6 复现 | 同 FK-013 | 0.8 | ✅ |
| FK-020 | 原始 C11 复现 | 同 FK-014 | 0.8 | ✅ |
| FK-021 | 原始 R1 复现 | `{"path":"strength","track_index":2,"strength":0.5}` | strength=0.5 | ✅ |
| FK-022 | 原始 R2 复现 | `{"path":"track_index","strength":0.5,"track_index":2}` | track_index=2 | ✅ |
| FK-023 | 字段顺序无关 | 4 种顺序 + 干扰项前后置 | 全部 0.8 | ✅ |
| FK-024 | 九组真实样例 | 18 个 op 的典型 argv | 全部正确 | ✅ |

附加用例（任务书第五节要求）：`NumericSemanticsPreserved`（数值语义保持）、
`VectorReadersPreserveSemantics`（数组读取）、`MalformedAndNotObjectAreDistinct`
（畸形/非对象区分）、`StatusNames` —— 共 **28 个用例全部通过**。

### 6.3 新旧差分（证明测试确实抓住缺陷）

```
ID   输入JSON                                            旧值   新值   判定
C2   {"a":"value","b":1}                                 1.00   0.80  **已修正**
C6   {"a":{"value":0.1},"value":0.8}                     0.10   0.80  **已修正**
C11  {"arr":["value"],"b":2}                             2.00   0.80  **已修正**
R1   {"path":"strength","track_index":2,"strength":0.5}  2.00   0.50  **已修正**
R2   {"path":"track_index","strength":0.5,"track_index":2} 0.50  2.00  **已修正**
```

### 6.4 兼容性扫描（证明合法输入未变）

对 12 个键 × 17 种数值形态 × 3 种字段顺序 + 8 种字符串形态 × 2 种顺序：

```
对比样本数: 804
数值不一致: 0
字符串不一致: 0
✅ 全部合法输入样本：新旧结果完全一致
```

---

## 7. 九组 / 十八个 op 回归结果

| 组 | 关键回归断言（FK-024） | 结果 |
|---|---|---|
| G1 opacity | `value`=0.6 / `opacity`=0.3 | ✅ |
| G2 add_track | `name`=中文轨道、`type`=0 | ✅ |
| G3 blend | `mode`=2 | ✅ |
| G4 effect_add | `type`=3、`value`=0.5 | ✅ |
| G5 keyframe（短名链） | `prop`=1、`at_us`=500000、`v`=0.9 | ✅ |
| G5 keyframe（规范链） | `property`=1、`time_us`=500000、`value`=0.9 | ✅ |
| G6 remove_keyframe | `prop`=2、`at_us`=100 | ✅ |
| G7 adjust_effect_add | `track_index`=1 | ✅ |
| G8 adjust_effect_remove | `effect_id`=42 | ✅ |
| G9 adjust_lut | `path`=/tmp/a.cube、`strength`=0.8；空 path 仍为 Ok+空串 | ✅ |

---

## 8. 编译与 CI 结果

| 检查 | 命令 / 工作流 | 环境 | 退出码 | 结果 | 证据 |
|---|---|---|---|---|---|
| `json_args.cpp` 编译 | `g++ -std=c++17 -Wall -Wextra -Werror -c core/src/json_args.cpp -Icore/include` | 本地 aarch64 | 0 | ✅ | 无警告 |
| JNI 辅助块编译 | `g++ ... /tmp/helper_tu.cpp core/src/json_args.cpp`（被替换的 54 行原样编译） | 本地 | 0 | ✅ | `-Werror` 通过 |
| FK 子集 | 见 §6.1 | 本地 | 0 | ✅ 28/28 | 原始输出 |
| **全量回归** | `ci.yml` → `build-and-test` | GitHub ubuntu-latest | — | ✅ **success** | run 42953486054 |
| 全量用例数 | 同上 | — | — | **1792**（基线 1764，**+28**） | `[SHARD_STATS]` |
| 实际执行 | 同上 | — | — | **1791**（基线 1763，**+28**） | 见 §8.1 |
| 失败用例 | 同上 | — | — | **0** | — |
| **ASAN** | `asan-diagnose` | GitHub | — | ✅ success | 同 run |
| **UBSAN** | `ub-diagnose` | GitHub | — | ✅ success | 同 run |
| **Android Cross-Compile** | `android-build.yml` | GitHub + NDK r27d | — | ✅ success | 见下 |
| **Android APK（NDK 编译 nc_jni.cpp）** | `android-apk.yml` | GitHub + NDK r27d | — | ✅ success | run 42955399380 |
| **Valgrind Memcheck** | `valgrind.yml` | GitHub | — | ✅ **success** | run 42953487626 |
| 真机 JNI 测试 | — | — | — | **未执行** | 见 §11 P-05 |

### 8.1 关于「1791/1792」

**main 基线本身就是 `1763/1764`（少 1 例）**，属既有现象，与本修复无关：

| 分支 | total | executed | 差 |
|---|---|---|---|
| `main` @ `2ad6fb22`（CI 单进程） | 1764 | 1763 | −1 |
| `fix/p0-findkey`（CI 单进程） | **1792** | **1791** | −1 |
| `fix/p0-findkey`（Valgrind 16 分片） | **1792** | **1792** | **0** |

`executed` 从 1763 → 1791 = **+28**，正好等于新增用例数 → **28 个 FK 用例全部执行**；
Valgrind 分片运行进一步证明 **1792/1792 全覆盖**。

### 8.2 Valgrind Memcheck（run 42953487626，run 级 success）

```
分片输出文件数: 16 / 期望 16
全量用例总数: 1792
实际执行合计: 1792
失败用例合计: 0
✅ 覆盖度校验通过：所有分片并集覆盖全量用例。
内存错误条目总数: 0
泄漏分类: definitely=0 indirectly=0 possibly=0 still_reachable=0
```

**16 个分片全部 success，覆盖 1792/1792（含新增 28 个 FK 用例），
零内存错误、零泄漏。**

> 注意：Valgrind 分片运行报告 `executed == total == 1792`，
> 而 CI 单进程运行报告 `1791/1792`。两者差 1 例，
> 且 **main 基线同样是 `1763/1764`（也差 1 例）** →
> 该差异是 CI 单进程测试宿主（崩溃/内存类用例的行为）造成的既有现象，
> 与本修复无关；Valgrind 的 1792/1792 证明**全部用例均可执行且通过**。

### 8.3 NDK 编译证据

APK 构建 run 42955399380 中：

- `Setup Android NDK r27d` ✅
- `Assemble release APK` ✅（该步骤经 Gradle→CMake 编译 `mobile/native/android/nc_jni.cpp`）
- `APK smoke check` ✅，且断言 APK 内含 **`lib/arm64-v8a/libnc_jni.so`**
  → 修改后的 `nc_jni.cpp` 在 NDK 下**编译并链接成功**。

---

## 9. 性能与依赖影响

| 维度 | 影响 |
|---|---|
| 依赖 | **零新增第三方依赖**。新增模块仅用 `<cstddef>/<cstdint>/<string>/<vector>/<cstdlib>` |
| 许可 | 自有代码，无许可变化，不影响 APK 合规边界 |
| 体积 | `json_args.cpp` 约 430 行；静态库增量约 6 KB/ABI（估算） |
| 时间 | 单遍扫描，O(n) 与修复前同为线性；不再有"找到后还要继续找 `:`"的二次查找 |
| 内存 | 无值树构造、无额外堆分配（仅返回区间）；`jsonStringValue` 有一次字符串分配，与修复前 `JStrField` 相同 |
| 栈 | `Scanner` 递归深度 = JSON 嵌套深度（参数对象通常 ≤3 层） |

> 性能数据为**静态推断**，未做基准测试；参数对象规模极小（<1 KB），
> 相对 C ABI 调用本身可忽略。列为 §11 P3。

---

## 10. 修改文件清单

| 文件 | 类型 | 行数变化 | 说明 |
|---|---|---|---|
| `core/include/nairoclip/core/json_args.h` | **新增** | +100 | 接口与状态枚举 |
| `core/src/json_args.cpp` | **新增** | +430 | 单遍结构扫描实现 |
| `core/CMakeLists.txt` | 修改 | +1 | 注册 `src/json_args.cpp` |
| `mobile/native/android/nc_jni.cpp` | 修改 | −55/+54 | 5 个辅助函数改为委托；删除 `FindKey` |
| `tests/test_json_args.cpp` | **新增** | +560 | FK-001~FK-024 + 4 个附加用例 |
| `tests/CMakeLists.txt` | 修改 | +1 | 注册 `test_json_args.cpp` |

**未修改**：C ABI（`capi.h`/`capi.cpp`）、Editor API、GPU、音频、媒体、B1 报告、
GPU 架构文档、`core/json.h`/`json.cpp`、任何 CI 工作流文件。

---

## 11. 未解决问题

| ID | 级别 | 问题 | 状态 |
|---|---|---|---|
| P-01 | **P2** | **嵌套键不再可读**（修复前可读到）。静态审计未见调用方依赖，但缺真机抓包证明 | 待真机验证 |
| P-02 | **P2** | **畸形 JSON 现在拒绝**（修复前会返回值）。属有意收紧，4 例已列入测试 | 待人工确认可接受 |
| P-03 | P2 | `\uXXXX` 现在按 JSON 规则解码（修复前返回字面 `u4e2d`） | 有意修正，FK-011 覆盖 |
| P-04 | P3 | 性能未做基准测试（静态推断可忽略） | 待需要时补 |
| P-05 | P3 | 真机 JNI 字符串生命周期测试（`GetStringUTFChars` 释放、Modified UTF-8）**未执行** | 需真机 |
| P-06 | P3 | 重复键回落默认值而非返回独立错误码（`JNum` 签名无法传递状态） | 若要显式错误需改调用点，超出本任务边界 |

---

## 12. 是否具备进入 B2 文档修订阶段的条件

**具备。** 理由：

1. P0 缺陷（子串误命中）已从**结构解析层**修复，5 个复现全部修正；
2. 修复未把责任转嫁给调用方（256 个调用点零改动）；
3. 合法输入语义经 804 样本扫描**零差异**；
4. 仓库正式测试覆盖 FK-001~FK-024，全量回归 0 失败，ASAN/UBSAN 通过；
5. NDK 编译（APK + Cross-Compile）通过。

**但**：V1.4 文档本身仍需修订（独立复核报告 D-02/D-03/D-05 三项），
且本报告 §11 的 P-01/P-02 需人工确认。

---

## 13. Git 记录

| 项 | 值 |
|---|---|
| 修复前 HEAD | `2ad6fb22ab89675627681dde1bf3180bc13a19e7` |
| 修复提交 | `9e32997952551f7d80e31ba46c01446e2a9ea2cb` |
| 分支 | `fix/p0-findkey` |
| PR | #28（**未合并**） |
| 变更文件数 | 6（3 新增 / 3 修改） |

---

## 14. 结论

# **有条件通过**

**核心缺陷已从 JSON 结构层修复**，专项测试（FK-001~FK-024，28 用例）与
九组 18 个 op 回归证据完整，全量 CI / ASAN / UBSAN / Valgrind / NDK 编译全部通过。

### 已完成的验证（全部通过）

| 验证 | 结果 |
|---|---|
| 5 个复现（C2/C6/C11/R1/R2） | ✅ 全部修正（新旧差分实测） |
| FK-001~FK-024 + 4 附加用例 | ✅ 28/28 |
| 合法输入兼容性（804 样本） | ✅ 零差异 |
| 全量回归（1792 用例） | ✅ 0 失败 |
| ASAN / UBSAN | ✅ success |
| Valgrind（16 分片） | ✅ 1792/1792，0 错误 0 泄漏 |
| Android APK + Cross-Compile（NDK） | ✅ success，`libnc_jni.so` 已打包 |

### 前置条件（满足后方可申请 V1.5 审批）

| # | 条件 | 状态 |
|---|---|---|
| C-1 | **真机验证嵌套键行为变化无回归**（P-01）：确认无调用方依赖"读嵌套键"这一修复前副作用 | **未执行**（需真机抓包） |
| C-2 | **真机 JNI 字符串生命周期测试**（P-05）：`GetStringUTFChars` 释放、Modified UTF-8 | **未执行**（需真机） |
| C-3 | 人工确认「畸形 JSON 现在拒绝」（P-02）与「`\uXXXX` 现在解码」（P-03）属可接受的有意收紧 | **待确认** |

> C-1/C-2 属**环境限制**（本环境无 Android 真机接入），已在 §11 如实登记，
> 未用 C ABI 测试或本地程序替代真机证据。

**本任务不自动授权 B2-1。** 需人工批准（并处理 C-1~C-3）后才能进入 V1.5 文档修订阶段。
