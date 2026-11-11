// P0 专项：FindKey JSON 键解析缺陷的对抗性测试（NC-P0-FINDKEY-REPAIR）。
//
// 编号 FK-001..FK-024 与任务书一一对应。每条都保留原始输入与预期结果，
// 便于人工复核；不写"仅名称"的空壳用例。
//
// 被测对象：core::findJsonMember 及其值提取函数 —— 这正是 nc_jni.cpp 中
// JNum / JInt / JStrField / JVecDouble / JVecU64 现在的实现基础，
// 因此本套测试是**仓库正式回归测试**的一部分，不是独立模拟程序。

#include "test_framework.h"

#include <cstdint>
#include <string>
#include <vector>

#include "nairoclip/core/json_args.h"

using namespace nairoclip::core;

namespace {

/// 复刻 nc_jni.cpp 中 JNum 的语义（Number → strtod，其余 → 默认值）。
double jnum(const std::string& json, const std::string& key, double def) {
    const JsonLookup hit = findJsonMember(json, key, JsonValueKind::Number);
    if (hit.status != JsonMemberStatus::Ok) return def;
    return jsonNumberValue(json, hit.span, def);
}

/// 复刻 JInt：经 double 中转。
std::int64_t jint(const std::string& json, const std::string& key, std::int64_t def) {
    return static_cast<std::int64_t>(jnum(json, key, static_cast<double>(def)));
}

/// 复刻 JStrField。
std::string jstr(const std::string& json, const std::string& key) {
    const JsonLookup hit = findJsonMember(json, key, JsonValueKind::String);
    if (hit.status != JsonMemberStatus::Ok) return std::string();
    return jsonStringValue(json, hit.span);
}

/// 定位状态（不校验类型）：用于判断 Missing / Malformed / DuplicateKey。
JsonMemberStatus statusOf(const std::string& json, const std::string& key) {
    return findJsonMember(json, key).status;
}

/// 按"数值键"校验类型：Missing / TypeMismatch / Malformed 均可区分。
JsonMemberStatus statusNum(const std::string& json, const std::string& key) {
    return findJsonMember(json, key, JsonValueKind::Number).status;
}

/// 按"字符串键"校验类型。
JsonMemberStatus statusStr(const std::string& json, const std::string& key) {
    return findJsonMember(json, key, JsonValueKind::String).status;
}

}  // namespace

// ======================= A. 基础字段定位 =======================

// FK-001：正常直接成员键
NC_TEST(FindKey, FK001_DirectMember) {
    NC_CHECK_EQ(jnum("{\"value\":0.8}", "value", 0.25), 0.8);
    NC_CHECK_EQ(jnum("{\"opacity\":0.25}", "opacity", 1.0), 0.25);
    NC_CHECK_EQ(jint("{\"track_index\":3}", "track_index", 0), std::int64_t(3));
    NC_CHECK_EQ(jstr("{\"name\":\"track\"}", "name"), std::string("track"));
    NC_CHECK_EQ(statusOf("{\"value\":0.8}", "value"), JsonMemberStatus::Ok);
}

// FK-002：目标键缺失 → Missing，取值回落默认值
NC_TEST(FindKey, FK002_MissingKey) {
    NC_CHECK_EQ(statusOf("{\"x\":1}", "value"), JsonMemberStatus::Missing);
    NC_CHECK_EQ(jnum("{\"x\":1}", "value", 0.75), 0.75);
    NC_CHECK_EQ(jint("{\"x\":1}", "track_index", -1), std::int64_t(-1));
    NC_CHECK_EQ(jstr("{\"x\":1}", "name"), std::string());
    // 键名是另一个键的前缀，不得命中
    NC_CHECK_EQ(statusOf("{\"values\":1}", "value"), JsonMemberStatus::Missing);
}

// FK-003：目标键值为空字符串（对字符串键是**有效值**，不是缺失）
NC_TEST(FindKey, FK003_EmptyStringValue) {
    NC_CHECK_EQ(statusOf("{\"name\":\"\"}", "name"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jstr("{\"name\":\"\"}", "name"), std::string());
    // 数值键给了空串 → 类型不匹配，回落默认值
    NC_CHECK_EQ(statusNum("{\"value\":\"\"}", "value"), JsonMemberStatus::TypeMismatch);
    NC_CHECK_EQ(jnum("{\"value\":\"\"}", "value", 0.9), 0.9);
}

// FK-004：目标键值为 null → 与"未提供"等价，回落默认值
NC_TEST(FindKey, FK004_NullValue) {
    NC_CHECK_EQ(statusNum("{\"value\":null}", "value"), JsonMemberStatus::TypeMismatch);
    NC_CHECK_EQ(jnum("{\"value\":null}", "value", 0.6), 0.6);
    NC_CHECK_EQ(jstr("{\"name\":null}", "name"), std::string());
    NC_CHECK_EQ(jnum("{\"value\":null}", "value", 0.0), 0.0);
}

// FK-005：目标键类型不匹配（字符串/布尔/对象/数组 给数值键）
NC_TEST(FindKey, FK005_TypeMismatch) {
    NC_CHECK_EQ(statusNum("{\"value\":\"abc\"}", "value"), JsonMemberStatus::TypeMismatch);
    NC_CHECK_EQ(statusNum("{\"value\":true}", "value"), JsonMemberStatus::TypeMismatch);
    NC_CHECK_EQ(statusNum("{\"value\":{}}", "value"), JsonMemberStatus::TypeMismatch);
    NC_CHECK_EQ(statusNum("{\"value\":[]}", "value"), JsonMemberStatus::TypeMismatch);
    // 全部回落默认值，不得读到任何数值
    NC_CHECK_EQ(jnum("{\"value\":\"abc\"}", "value", 1.5), 1.5);
    NC_CHECK_EQ(jnum("{\"value\":true}", "value", 1.5), 1.5);
    NC_CHECK_EQ(jnum("{\"value\":{}}", "value", 1.5), 1.5);
    NC_CHECK_EQ(jnum("{\"value\":[]}", "value", 1.5), 1.5);
    // 字符串键给了数值 → 类型不匹配
    NC_CHECK_EQ(statusStr("{\"name\":123}", "name"), JsonMemberStatus::TypeMismatch);
    NC_CHECK_EQ(jstr("{\"name\":123}", "name"), std::string());
}

// FK-006：重复键必须拒绝（明确策略：不取第一个，也不取最后一个）
NC_TEST(FindKey, FK006_DuplicateKeyRejected) {
    const std::string dup = "{\"value\":0.1,\"value\":0.8}";
    NC_CHECK_EQ(statusOf(dup, "value"), JsonMemberStatus::DuplicateKey);
    // 拒绝 → 回落默认值（既不是 0.1 也不是 0.8）
    NC_CHECK_EQ(jnum(dup, "value", 0.5), 0.5);
    NC_CHECK_EQ(jnum(dup, "value", 0.5) == 0.1, false);
    NC_CHECK_EQ(jnum(dup, "value", 0.5) == 0.8, false);
    // 非目标键重复不影响目标键
    NC_CHECK_EQ(statusOf("{\"a\":1,\"a\":2,\"value\":0.3}", "value"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jnum("{\"a\":1,\"a\":2,\"value\":0.3}", "value", 0.0), 0.3);
}

// ======================= B. 字符串干扰 =======================

// FK-007：字符串值中包含目标键名（原始 C2 的核心场景）
NC_TEST(FindKey, FK007_KeyNameInsideStringValue) {
    const std::string j = "{\"a\":\"value\",\"b\":1}";
    NC_CHECK_EQ(statusOf(j, "value"), JsonMemberStatus::Missing);
    NC_CHECK_EQ(jnum(j, "value", 0.8), 0.8);  // 修复前会读到 1.0
    // 字符串键同理
    NC_CHECK_EQ(statusOf("{\"a\":\"name\",\"b\":1}", "name"), JsonMemberStatus::Missing);
    NC_CHECK_EQ(jstr("{\"a\":\"name\",\"b\":1}", "name"), std::string());
}

// FK-008：字符串值中包含完整的 "key":value 片段
NC_TEST(FindKey, FK008_KeyValueFragmentInsideString) {
    const std::string j = "{\"description\":\"\\\"value\\\":0.1\",\"value\":0.8}";
    NC_CHECK_EQ(statusOf(j, "value"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jnum(j, "value", 0.0), 0.8);  // 必须是外层真值 0.8，不是 0.1
    // 只有内嵌片段、没有真键
    const std::string j2 = "{\"description\":\"\\\"value\\\":0.1\"}";
    NC_CHECK_EQ(statusOf(j2, "value"), JsonMemberStatus::Missing);
    NC_CHECK_EQ(jnum(j2, "value", 0.42), 0.42);
}

// FK-009：字符串中包含转义引号
NC_TEST(FindKey, FK009_EscapedQuotes) {
    NC_CHECK_EQ(jstr("{\"name\":\"a\\\"b\"}", "name"), std::string("a\"b"));
    NC_CHECK_EQ(jnum("{\"note\":\"a\\\"value\\\"b\",\"value\":0.7}", "value", 0.0), 0.7);
}

// FK-010：字符串中包含反斜杠转义（含 \\ 与 \n \t）
NC_TEST(FindKey, FK010_BackslashEscapes) {
    NC_CHECK_EQ(jstr("{\"name\":\"a\\\\b\"}", "name"), std::string("a\\b"));
    NC_CHECK_EQ(jstr("{\"name\":\"a\\nb\"}", "name"), std::string("a\nb"));
    NC_CHECK_EQ(jstr("{\"name\":\"a\\tb\"}", "name"), std::string("a\tb"));
    NC_CHECK_EQ(jstr("{\"name\":\"a\\/b\"}", "name"), std::string("a/b"));
    // 反斜杠后的引号不结束字符串
    NC_CHECK_EQ(statusOf("{\"a\":\"x\\\\\",\"value\":0.4}", "value"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jnum("{\"a\":\"x\\\\\",\"value\":0.4}", "value", 0.0), 0.4);
}

// FK-011：中文、emoji、\uXXXX 转义（含代理对）
NC_TEST(FindKey, FK011_UnicodeAndEmoji) {
    NC_CHECK_EQ(jstr("{\"name\":\"中文轨道\"}", "name"), std::string("中文轨道"));
    NC_CHECK_EQ(jstr("{\"name\":\"轨道🎬\"}", "name"), std::string("轨道🎬"));
    // \u 转义按 JSON 规则解码
    NC_CHECK_EQ(jstr("{\"name\":\"\\u4e2d\\u6587\"}", "name"), std::string("中文"));
    // 代理对 → 单个 4 字节字符
    NC_CHECK_EQ(jstr("{\"name\":\"\\ud83c\\udfac\"}", "name"), std::string("🎬"));
    // 中文值里出现键名文本，不得误命中
    NC_CHECK_EQ(statusOf("{\"name\":\"track_index 中文\",\"b\":1}", "track_index"),
                JsonMemberStatus::Missing);
}

// FK-012：空字符串中不得产生误匹配
NC_TEST(FindKey, FK012_NoFalseMatchFromEmptyString) {
    NC_CHECK_EQ(statusOf("{\"a\":\"\",\"value\":0.2}", "value"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jnum("{\"a\":\"\",\"value\":0.2}", "value", 0.0), 0.2);
    NC_CHECK_EQ(statusOf("{\"\":1}", "value"), JsonMemberStatus::Missing);
    // 空对象
    NC_CHECK_EQ(statusOf("{}", "value"), JsonMemberStatus::Missing);
}

// ======================= C. 嵌套对象与数组 =======================

// FK-013：嵌套对象存在同名键 → 不得取嵌套内的值
NC_TEST(FindKey, FK013_NestedObjectSameKey) {
    const std::string j = "{\"a\":{\"value\":0.1},\"value\":0.8}";
    NC_CHECK_EQ(statusOf(j, "value"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jnum(j, "value", 0.0), 0.8);  // 修复前读到 0.1
    // 只有嵌套、无直接成员 → Missing
    NC_CHECK_EQ(statusOf("{\"a\":{\"value\":0.1}}", "value"), JsonMemberStatus::Missing);
    NC_CHECK_EQ(jnum("{\"a\":{\"value\":0.1}}", "value", 0.55), 0.55);
}

// FK-014：数组字符串包含目标键名
NC_TEST(FindKey, FK014_ArrayStringContainsKeyName) {
    const std::string j = "{\"arr\":[\"value\"],\"b\":2}";
    NC_CHECK_EQ(statusOf(j, "value"), JsonMemberStatus::Missing);
    NC_CHECK_EQ(jnum(j, "value", 0.8), 0.8);  // 修复前读到 2.0
}

// FK-015：数组内部对象存在同名键
NC_TEST(FindKey, FK015_ObjectInsideArray) {
    NC_CHECK_EQ(statusOf("{\"list\":[{\"value\":0.1},{\"value\":0.2}]}", "value"),
                JsonMemberStatus::Missing);
    NC_CHECK_EQ(jnum("{\"list\":[{\"value\":0.1}]}", "value", 0.33), 0.33);
    // 直接成员与数组并存
    NC_CHECK_EQ(jnum("{\"list\":[{\"value\":0.1}],\"value\":0.9}", "value", 0.0), 0.9);
}

// FK-016：嵌套层级包含多个同名键
NC_TEST(FindKey, FK016_MultipleSameKeysNested) {
    const std::string j = "{\"a\":{\"b\":{\"value\":0.1}},\"c\":[{\"value\":0.2}],\"value\":0.7}";
    NC_CHECK_EQ(statusOf(j, "value"), JsonMemberStatus::Ok);
    NC_CHECK_EQ(jnum(j, "value", 0.0), 0.7);
    // 三层嵌套且无直接成员
    NC_CHECK_EQ(statusOf("{\"a\":{\"b\":{\"value\":0.1}}}", "value"), JsonMemberStatus::Missing);
}

// FK-017：直接键与嵌套键同时存在 → 必须读直接键
NC_TEST(FindKey, FK017_DirectOverNested) {
    const std::string j = "{\"nested\":{\"track_index\":9},\"track_index\":2}";
    NC_CHECK_EQ(jint(j, "track_index", 0), std::int64_t(2));  // 必须是 2，不是 9
    const std::string j2 = "{\"track_index\":2,\"nested\":{\"track_index\":9}}";
    NC_CHECK_EQ(jint(j2, "track_index", 0), std::int64_t(2));
}

// ======================= D. 顺序与真实业务复现 =======================

// FK-018：原始 C2 复现 —— {"a":"value","b":1} 读 value
NC_TEST(FindKey, FK018_ReproC2) {
    const std::string j = "{\"a\":\"value\",\"b\":1}";
    // 修复前：1.0（误命中字符串内的 "value"，随后读到 b 的值）
    NC_CHECK_EQ(jnum(j, "value", 0.8), 0.8);
}

// FK-019：原始 C6 复现 —— 嵌套对象同名键
NC_TEST(FindKey, FK019_ReproC6) {
    const std::string j = "{\"a\":{\"value\":0.1},\"value\":0.8}";
    // 修复前：0.1
    NC_CHECK_EQ(jnum(j, "value", 0.0), 0.8);
}

// FK-020：原始 C11 复现 —— 数组字符串
NC_TEST(FindKey, FK020_ReproC11) {
    const std::string j = "{\"arr\":[\"value\"],\"b\":2}";
    // 修复前：2.0
    NC_CHECK_EQ(jnum(j, "value", 0.8), 0.8);
}

// FK-021：原始 R1 复现 —— adjust_lut 的 strength 被 path 值污染
NC_TEST(FindKey, FK021_ReproR1) {
    const std::string j = "{\"path\":\"strength\",\"track_index\":2,\"strength\":0.5}";
    // 修复前：strength 读到 2.0（track_index 的值）→ 被 0..1 校验拒绝
    NC_CHECK_EQ(jnum(j, "strength", 1.0), 0.5);
    NC_CHECK_EQ(jint(j, "track_index", 0), std::int64_t(2));
    NC_CHECK_EQ(jstr(j, "path"), std::string("strength"));
}

// FK-022：原始 R2 复现 —— track_index 被静默解析为错误轨道（最严重）
NC_TEST(FindKey, FK022_ReproR2) {
    const std::string j = "{\"path\":\"track_index\",\"strength\":0.5,\"track_index\":2}";
    // 修复前：track_index 读到 0.5 → 截断为 0 → 静默操作错误轨道
    NC_CHECK_EQ(jint(j, "track_index", 0), std::int64_t(2));
    NC_CHECK_EQ(jnum(j, "strength", 1.0), 0.5);
    NC_CHECK_EQ(jstr(j, "path"), std::string("track_index"));
}

// FK-023：字段顺序变化不得影响结果
NC_TEST(FindKey, FK023_FieldOrderIndependence) {
    const char* orders[] = {
        "{\"value\":0.8,\"b\":1}",
        "{\"b\":1,\"value\":0.8}",
        "{\"z\":0,\"value\":0.8,\"b\":1}",
        "{\"value\":0.8}",
    };
    for (const char* j : orders) {
        NC_CHECK_EQ(jnum(j, "value", 0.0), 0.8);
    }
    // 干扰项在前后都不得影响
    NC_CHECK_EQ(jnum("{\"note\":\"value\",\"value\":0.8}", "value", 0.0), 0.8);
    NC_CHECK_EQ(jnum("{\"value\":0.8,\"note\":\"value\"}", "value", 0.0), 0.8);
}

// FK-024：九组参数的真实请求样例回归（18 个 op 的典型 argv）
NC_TEST(FindKey, FK024_NineGroupsRealSamples) {
    // G1 opacity
    NC_CHECK_EQ(jnum("{\"track_index\":0,\"clip_id\":1,\"value\":0.6}", "value", 1.0), 0.6);
    NC_CHECK_EQ(jnum("{\"track_index\":0,\"clip_id\":1,\"opacity\":0.3}", "opacity", 1.0), 0.3);
    // G2 add_track
    NC_CHECK_EQ(jstr("{\"name\":\"视频轨\",\"type\":0}", "name"), std::string("视频轨"));
    NC_CHECK_EQ(jint("{\"name\":\"视频轨\",\"type\":0}", "type", 0), std::int64_t(0));
    // G3 blend
    NC_CHECK_EQ(jint("{\"track_index\":0,\"clip_id\":1,\"mode\":2}", "mode", 0), std::int64_t(2));
    // G4 effect_add
    NC_CHECK_EQ(jint("{\"track_index\":0,\"clip_id\":1,\"type\":3,\"value\":0.5}", "type", 0),
                std::int64_t(3));
    NC_CHECK_EQ(jnum("{\"track_index\":0,\"clip_id\":1,\"type\":3,\"value\":0.5}", "value", 0.5),
                0.5);
    // G5 keyframe（短名链：prop/property、at_us/t、v）
    const std::string kf = "{\"track_index\":0,\"clip_id\":1,\"prop\":1,\"at_us\":500000,\"v\":0.9}";
    NC_CHECK_EQ(jint(kf, "prop", 0), std::int64_t(1));
    NC_CHECK_EQ(jint(kf, "at_us", -1), std::int64_t(500000));
    NC_CHECK_EQ(jnum(kf, "v", 1.0), 0.9);
    // G5 规范链（property/time_us/value）
    const std::string kf2 =
        "{\"track_index\":0,\"clip_id\":1,\"property\":1,\"time_us\":500000,\"value\":0.9}";
    NC_CHECK_EQ(jint(kf2, "property", 0), std::int64_t(1));
    NC_CHECK_EQ(jint(kf2, "time_us", 0), std::int64_t(500000));
    NC_CHECK_EQ(jnum(kf2, "value", 0.0), 0.9);
    // G6 remove_keyframe
    NC_CHECK_EQ(jint("{\"track_index\":0,\"clip_id\":1,\"prop\":2,\"at_us\":100}", "prop", 0),
                std::int64_t(2));
    // G7 adjust_effect_add
    NC_CHECK_EQ(jint("{\"track_index\":1,\"type\":5,\"value\":0.7}", "track_index", 0),
                std::int64_t(1));
    // G8 adjust_effect_remove
    NC_CHECK_EQ(jint("{\"track_index\":1,\"effect_id\":42}", "effect_id", 0), std::int64_t(42));
    // G9 adjust_lut
    NC_CHECK_EQ(jstr("{\"track_index\":1,\"path\":\"/tmp/a.cube\",\"strength\":0.8}", "path"),
                std::string("/tmp/a.cube"));
    NC_CHECK_EQ(jnum("{\"track_index\":1,\"path\":\"/tmp/a.cube\",\"strength\":0.8}", "strength",
                     1.0),
                0.8);
    // G9 空 path（清除 LUT 的合法语义必须保留）
    NC_CHECK_EQ(statusOf("{\"track_index\":1,\"path\":\"\",\"strength\":1.0}", "path"),
                JsonMemberStatus::Ok);
    NC_CHECK_EQ(jstr("{\"track_index\":1,\"path\":\"\",\"strength\":1.0}", "path"),
                std::string());
}

// ======================= 附加：调用点与语义保持 =======================

// 数值语义保持：JInt 仍经 double 中转（>2^53 有精度损失，与修复前一致）
NC_TEST(FindKey, NumericSemanticsPreserved) {
    NC_CHECK_EQ(jint("{\"n\":9007199254740993}", "n", 0), std::int64_t(9007199254740992));
    NC_CHECK_EQ(jint("{\"n\":9007199254740992}", "n", 0), std::int64_t(9007199254740992));
    NC_CHECK_EQ(jint("{\"n\":-5}", "n", 0), std::int64_t(-5));
    NC_CHECK_EQ(jint("{\"n\":1e3}", "n", 0), std::int64_t(1000));
    NC_CHECK_EQ(jint("{\"n\":1.9}", "n", 0), std::int64_t(1));  // 向零截断
    NC_CHECK_EQ(jint("{\"n\":-1.9}", "n", 0), std::int64_t(-1));
    NC_CHECK_EQ(jnum("{\"n\":0.1}", "n", 0.0), 0.1);
    // 浮点字符串给数值键 → 与修复前一致地回落默认值
    NC_CHECK_EQ(jnum("{\"n\":\"0.5\"}", "n", 0.25), 0.25);
}

// 数组读取（JVecDouble / JVecU64 的等价语义）
NC_TEST(FindKey, VectorReadersPreserveSemantics) {
    std::vector<double> d;
    const JsonLookup hit = findJsonMember("{\"a\":[3.5,2.0]}", "a", JsonValueKind::Array);
    NC_CHECK_EQ(hit.status, JsonMemberStatus::Ok);
    NC_CHECK(jsonNumberArray("{\"a\":[3.5,2.0]}", hit.span, &d));
    NC_CHECK_EQ(d.size(), std::size_t(2));
    NC_CHECK_EQ(d[0], 3.5);
    NC_CHECK_EQ(d[1], 2.0);

    std::vector<std::uint64_t> u;
    const JsonLookup hit2 = findJsonMember("{\"ids\":[1,2,3]}", "ids", JsonValueKind::Array);
    NC_CHECK_EQ(hit2.status, JsonMemberStatus::Ok);
    NC_CHECK(jsonU64Array("{\"ids\":[1,2,3]}", hit2.span, &u));
    NC_CHECK_EQ(u.size(), std::size_t(3));
    NC_CHECK_EQ(u[2], std::uint64_t(3));

    // 数组里出现键名文本不得影响
    std::vector<std::uint64_t> u2;
    NC_CHECK(!jsonU64Array("{\"a\":[\"ids\"]}",
                           findJsonMember("{\"a\":[\"ids\"]}", "a").span, &u2));
}

// 畸形 JSON / 非对象根必须被区分（不得静默取值）
NC_TEST(FindKey, MalformedAndNotObjectAreDistinct) {
    NC_CHECK_EQ(statusOf("{\"value\":0.8", "value"), JsonMemberStatus::Malformed);
    NC_CHECK_EQ(statusOf("{\"value\" 0.8}", "value"), JsonMemberStatus::Malformed);
    NC_CHECK_EQ(statusOf("[1,2,3]", "value"), JsonMemberStatus::NotObject);
    NC_CHECK_EQ(statusOf("0.8", "value"), JsonMemberStatus::NotObject);
    NC_CHECK_EQ(statusOf("", "value"), JsonMemberStatus::Malformed);
    NC_CHECK_EQ(statusOf("{\"value\":0.8} trailing", "value"), JsonMemberStatus::Malformed);
    // 畸形时回落默认值
    NC_CHECK_EQ(jnum("{\"value\":0.8", "value", 0.1), 0.1);
}

// 状态名可读（诊断用）
NC_TEST(FindKey, StatusNames) {
    NC_CHECK_EQ(std::string(jsonMemberStatusName(JsonMemberStatus::Ok)), std::string("Ok"));
    NC_CHECK_EQ(std::string(jsonMemberStatusName(JsonMemberStatus::Missing)),
                std::string("Missing"));
    NC_CHECK_EQ(std::string(jsonMemberStatusName(JsonMemberStatus::DuplicateKey)),
                std::string("DuplicateKey"));
    NC_CHECK_EQ(std::string(jsonMemberStatusName(JsonMemberStatus::TypeMismatch)),
                std::string("TypeMismatch"));
}
