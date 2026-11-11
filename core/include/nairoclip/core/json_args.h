#pragma once
// 结构感知的 JSON 参数查找（零依赖）。
//
// 背景（P0 缺陷 NC-P0-FINDKEY-REPAIR）：
//   JNI 参数解析原先用 `json.find("\"" + key + "\"")` 做**子串搜索**定位键名，
//   再取其后第一个 ':' —— 不理解 JSON 结构，导致：
//     · 字符串值里出现键名文本时误命中（{"a":"value","b":1} 读 value → 1）；
//     · 嵌套对象/数组里的同名键被当成根成员（{"a":{"value":0.1}} 读 value → 0.1）；
//     · 重复键静默取第一个。
//   本模块只做一件事：**在根对象的直接成员里**找到目标键，返回该值在原文中的
//   字节区间（span）与值类型；值的语义转换仍由调用方按原有规则完成
//   （数值仍走 strtod，保证与修复前逐位一致）。
//
// 与 core/json.h 的分工：
//   json.h 是完整的值模型（工程文件用），其数字解析对整型走 strtoll、
//   对象 set() 会静默合并重复键 —— 两者都不满足本处「保持既有数值语义」
//   与「重复键必须拒绝」的要求。故本模块只复用其所在的 core 层，
//   不修改 json.h/json.cpp，避免影响 .nclip 存储格式。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 查找结果状态。调用方据此区分「字段不存在」「类型不匹配」「格式错误」。
enum class JsonMemberStatus {
    Ok = 0,         ///< 找到，且类型符合调用方期望
    Missing,        ///< 键不存在（合法的"没传"）
    NotObject,      ///< 根不是 JSON 对象（入参根本不是参数对象）
    Malformed,      ///< JSON 语法错误
    DuplicateKey,   ///< 目标键在根对象里出现多次（拒绝，不取第一个/最后一个）
    TypeMismatch,   ///< 找到但类型不符（如数值键给了字符串）
};

/// JSON 值类型。
enum class JsonValueKind {
    Unknown = 0,
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

/// 值在原文中的字节区间 [begin, end)，以及其类型。
struct JsonMemberSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    JsonValueKind kind = JsonValueKind::Unknown;
};

struct JsonLookup {
    JsonMemberStatus status = JsonMemberStatus::Missing;
    JsonMemberSpan span;
    /// 失败时的可读说明（含字节偏移），供诊断/日志使用。
    std::string message;
};

/// 在 **根对象的直接成员** 中查找 key。
///
/// 语义要点：
///   · 只匹配根对象的直接成员，不进入字符串、数组、嵌套对象内部；
///   · 字符串键按 JSON 规则解码后比较（`\uXXXX` 与代理对均正确）；
///   · 目标键重复出现 → DuplicateKey（明确拒绝，结果不确定的行为一律不采用）；
///   · 扫描覆盖整个根对象，因此"键在畸形 JSON 里"会被判 Malformed 而非 Ok。
JsonLookup findJsonMember(const std::string& json, const std::string& key);

/// 同上，并校验值类型：找到但类型不等于 expected → TypeMismatch。
/// 用于把「字段不存在」「类型不匹配」「JSON 畸形」三种情况区分开。
JsonLookup findJsonMember(const std::string& json, const std::string& key,
                          JsonValueKind expected);

// ---- 值提取（保持与修复前一致的数值语义）----------------------------------

/// 取 Number 类型的值：对 span 原文调用 strtod。
/// 非 Number（含字符串 "0.5"）→ 返回 def。这与修复前行为一致：
/// 旧实现对 `{"k":"0.5"}` 同样返回 def（strtod 在引号处失败）。
double jsonNumberValue(const std::string& json, const JsonMemberSpan& span, double def);

/// 取 String 类型的值（解码转义）。非 String → 空串。
std::string jsonStringValue(const std::string& json, const JsonMemberSpan& span);

/// 取 Number 数组（扁平）。语义与修复前一致：逐个 strtod，遇非数字停止，
/// 结果为空则返回 false。
bool jsonNumberArray(const std::string& json, const JsonMemberSpan& span,
                     std::vector<double>* out);

/// 取无符号整数数组（扁平）。语义与修复前一致：逐个 strtoull（基数 10），
/// 遇非数字停止，结果为空则返回 false。
bool jsonU64Array(const std::string& json, const JsonMemberSpan& span,
                  std::vector<std::uint64_t>* out);

/// 状态的可读名（测试与日志用）。
const char* jsonMemberStatusName(JsonMemberStatus status);

// ---- 编辑前置校验（P1-1：重复键必须拒绝，不得回落默认值）-------------------

/// 检测**根对象**是否存在重复键（任意键）。
/// 返回 true 表示存在重复；*outKey / *outMessage 给出第一个重复键与说明。
/// 嵌套对象内部的重复键不影响根解析，因此不计入。
bool hasDuplicateRootKey(const std::string& json, std::string* outKey,
                         std::string* outMessage);

/// 编辑前置校验：返回空串 = 通过；非空 = 错误消息，**调用方必须拒绝执行编辑**。
///
/// 为什么需要它：`JNum`/`JInt` 的签名只能返回一个值，无法向调用方报告
/// "重复键"这类错误，只能回落默认值 —— 那会让错误轨道/错误参数被静默执行。
/// 因此把重复键检测提前到**执行任何 nc_* 编辑之前**。
std::string precheckEditArgs(const std::string& json);

}  // namespace core
}  // namespace nairoclip
