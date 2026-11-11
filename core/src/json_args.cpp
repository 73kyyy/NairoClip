// 结构感知的 JSON 参数查找实现（P0 修复 NC-P0-FINDKEY-REPAIR）。
//
// 设计取舍见 json_args.h 顶部注释。本文件**不修改** core/json.h 的任何行为，
// 只新增一条独立的、只读的成员查找路径。
//
// P0-FIX-2 追加的三项安全加固：
//   P1-1 重复键：hasDuplicateRootKey / precheckEditArgs 供编辑入口在执行前拒绝；
//   P1-2 代理项：孤立高/低代理、错误组合、不完整代理对一律 Malformed，
//                绝不产出非法 UTF-8，也不静默替换；
//   P2-1 内嵌 NUL：`\u0000` 在 C ABI（NUL 结尾字符串）下会被截断，
//                故在解析层直接拒绝，避免静默截断。

#include "nairoclip/core/json_args.h"

#include <cstdlib>
#include <set>

namespace nairoclip {
namespace core {
namespace {

bool isWs(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// 把码点按 UTF-8 追加。**不接受**代理区码点（由调用方保证）。
void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool isHighSurrogate(std::uint32_t cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
bool isLowSurrogate(std::uint32_t cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }

/// 单遍扫描器：只做结构遍历，不构造值树（避免额外分配）。
struct Scanner {
    const std::string& s;
    std::size_t pos = 0;
    std::string error;

    explicit Scanner(const std::string& text) : s(text) {}

    bool fail(const char* msg) {
        if (error.empty()) {
            error = std::string(msg) + " @offset " + std::to_string(pos);
        }
        return false;
    }

    void skipWs() {
        while (pos < s.size() && isWs(s[pos])) ++pos;
    }

    /// 只跳过一段字符串，**不解码、不校验**（用于值与非根键）。
    bool skipString() {
        if (pos >= s.size() || s[pos] != '"') return fail("字符串必须以引号开始");
        ++pos;
        while (true) {
            if (pos >= s.size()) return fail("字符串未闭合");
            const char c = s[pos];
            if (c == '"') { ++pos; return true; }
            if (c != '\\') { ++pos; continue; }
            ++pos;
            if (pos >= s.size()) return fail("转义序列未完成");
            if (s[pos] == 'u') {
                pos += 1;
                if (pos + 4 > s.size()) return fail("\\u 需要 4 位十六进制");
                for (int i = 0; i < 4; ++i) {
                    if (hexVal(s[pos + static_cast<std::size_t>(i)]) < 0) {
                        return fail("\\u 含非十六进制字符");
                    }
                }
                pos += 4;
                continue;
            }
            ++pos;  // 其它转义占 1 字节
        }
    }

    /// 解析并**严格解码**一个字符串（pos 必须指向 '"'）。
    ///
    /// P1-2：孤立代理 / 错误组合 / 不完整代理对 → Malformed，绝不产出非法 UTF-8。
    /// P2-1：`\u0000` → 拒绝（下游 C ABI 是 NUL 结尾字符串，内嵌 NUL 会截断）。
    bool parseString(std::string* out) {
        if (pos >= s.size() || s[pos] != '"') return fail("字符串必须以引号开始");
        ++pos;
        std::string decoded;
        while (true) {
            if (pos >= s.size()) return fail("字符串未闭合");
            const char c = s[pos];
            if (c == '"') {
                ++pos;
                if (out != nullptr) *out = std::move(decoded);
                return true;
            }
            if (c != '\\') {
                decoded.push_back(c);
                ++pos;
                continue;
            }
            ++pos;
            if (pos >= s.size()) return fail("转义序列未完成");
            const char e = s[pos++];
            switch (e) {
                case '"': decoded.push_back('"'); break;
                case '\\': decoded.push_back('\\'); break;
                case '/': decoded.push_back('/'); break;
                case 'b': decoded.push_back('\b'); break;
                case 'f': decoded.push_back('\f'); break;
                case 'n': decoded.push_back('\n'); break;
                case 'r': decoded.push_back('\r'); break;
                case 't': decoded.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!readHex4(&cp)) return false;
                    if (cp == 0) {
                        // P2-1：C ABI 用 const char*（NUL 结尾），内嵌 NUL 会被截断。
                        return fail("\\u0000 不受支持：下游 C ABI 为 NUL 结尾字符串，会截断");
                    }
                    if (isHighSurrogate(cp)) {
                        // 必须紧跟一个低代理，否则是孤立高代理 / 不完整代理对。
                        if (pos + 1 >= s.size() || s[pos] != '\\' || s[pos + 1] != 'u') {
                            return fail("孤立高代理项（缺少配对的低代理）");
                        }
                        pos += 2;
                        std::uint32_t lo = 0;
                        if (!readHex4(&lo)) return false;
                        if (!isLowSurrogate(lo)) {
                            return fail("代理对组合非法（高代理后不是低代理）");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (isLowSurrogate(cp)) {
                        return fail("孤立低代理项（缺少配对的高代理）");
                    }
                    appendUtf8(decoded, cp);
                    break;
                }
                default:
                    return fail("非法转义字符");
            }
        }
    }

    bool readHex4(std::uint32_t* out) {
        if (pos + 4 > s.size()) return fail("\\u 需要 4 位十六进制");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const int h = hexVal(s[pos + static_cast<std::size_t>(i)]);
            if (h < 0) return fail("\\u 含非十六进制字符");
            v = (v << 4) | static_cast<std::uint32_t>(h);
        }
        pos += 4;
        *out = v;
        return true;
    }

    /// 跳过任意一个值，返回其类型。
    bool skipValue(JsonValueKind* kindOut) {
        skipWs();
        if (pos >= s.size()) return fail("缺少值");
        const char c = s[pos];
        if (c == '"') {
            if (!skipString()) return false;
            if (kindOut) *kindOut = JsonValueKind::String;
            return true;
        }
        if (c == '{') {
            if (!skipObject()) return false;
            if (kindOut) *kindOut = JsonValueKind::Object;
            return true;
        }
        if (c == '[') {
            if (!skipArray()) return false;
            if (kindOut) *kindOut = JsonValueKind::Array;
            return true;
        }
        if (c == 't') {
            if (!expect("true")) return false;
            if (kindOut) *kindOut = JsonValueKind::Bool;
            return true;
        }
        if (c == 'f') {
            if (!expect("false")) return false;
            if (kindOut) *kindOut = JsonValueKind::Bool;
            return true;
        }
        if (c == 'n') {
            if (!expect("null")) return false;
            if (kindOut) *kindOut = JsonValueKind::Null;
            return true;
        }
        if (!skipNumber()) return false;
        if (kindOut) *kindOut = JsonValueKind::Number;
        return true;
    }

    bool expect(const char* lit) {
        const std::string l(lit);
        if (s.compare(pos, l.size(), l) != 0) return fail("非法字面量");
        pos += l.size();
        return true;
    }

    /// 数字只做**边界**识别，不做数值转换（转换交给 strtod，保持既有语义）。
    bool skipNumber() {
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
        bool digits = false;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { ++pos; digits = true; }
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { ++pos; digits = true; }
        }
        if (digits && pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            const std::size_t save = pos;
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            bool expDigits = false;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { ++pos; expDigits = true; }
            if (!expDigits) pos = save;  // 不是合法指数：回退
        }
        if (!digits) return fail("非法数字");
        return true;
    }

    /// 跳过嵌套对象：键用 skipString（不解码），因此嵌套里的病态字符串
    /// 不会牵连根对象的解析。
    bool skipObject() {
        ++pos;  // '{'
        skipWs();
        if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
        while (true) {
            skipWs();
            if (!skipString()) return false;
            skipWs();
            if (pos >= s.size() || s[pos] != ':') return fail("对象成员缺少冒号");
            ++pos;
            if (!skipValue(nullptr)) return false;
            skipWs();
            if (pos >= s.size()) return fail("对象未闭合");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == '}') { ++pos; return true; }
            return fail("成员之间缺少逗号");
        }
    }

    bool skipArray() {
        ++pos;  // '['
        skipWs();
        if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
        while (true) {
            if (!skipValue(nullptr)) return false;
            skipWs();
            if (pos >= s.size()) return fail("数组未闭合");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == ']') { ++pos; return true; }
            return fail("数组元素之间缺少逗号");
        }
    }
};

JsonLookup makeError(JsonMemberStatus st, const std::string& msg) {
    JsonLookup r;
    r.status = st;
    r.message = msg;
    return r;
}

}  // namespace

const char* jsonMemberStatusName(JsonMemberStatus status) {
    switch (status) {
        case JsonMemberStatus::Ok: return "Ok";
        case JsonMemberStatus::Missing: return "Missing";
        case JsonMemberStatus::NotObject: return "NotObject";
        case JsonMemberStatus::Malformed: return "Malformed";
        case JsonMemberStatus::DuplicateKey: return "DuplicateKey";
        case JsonMemberStatus::TypeMismatch: return "TypeMismatch";
    }
    return "Unknown";
}

JsonLookup findJsonMember(const std::string& json, const std::string& key) {
    Scanner sc(json);
    sc.skipWs();
    if (sc.pos >= json.size()) {
        return makeError(JsonMemberStatus::Malformed, "空输入");
    }
    if (json[sc.pos] != '{') {
        return makeError(JsonMemberStatus::NotObject,
                         std::string("根不是 JSON 对象，首字符为 '") + json[sc.pos] + "'");
    }
    ++sc.pos;

    bool found = false;
    JsonMemberSpan span;
    sc.skipWs();
    if (sc.pos < json.size() && json[sc.pos] == '}') {
        ++sc.pos;
    } else {
        while (true) {
            sc.skipWs();
            std::string k;
            if (!sc.parseString(&k)) {  // 根键严格解码（含代理项/NUL 校验）
                return makeError(JsonMemberStatus::Malformed, sc.error);
            }
            sc.skipWs();
            if (sc.pos >= json.size() || json[sc.pos] != ':') {
                return makeError(JsonMemberStatus::Malformed, "成员缺少冒号");
            }
            ++sc.pos;
            sc.skipWs();
            const std::size_t vb = sc.pos;
            JsonValueKind kind = JsonValueKind::Unknown;
            if (!sc.skipValue(&kind)) {
                return makeError(JsonMemberStatus::Malformed, sc.error);
            }
            const std::size_t ve = sc.pos;
            if (k == key) {
                if (found) {
                    return makeError(JsonMemberStatus::DuplicateKey,
                                     "键 \"" + key + "\" 重复出现（拒绝，不取第一个/最后一个）");
                }
                found = true;
                span.begin = vb;
                span.end = ve;
                span.kind = kind;
            }
            sc.skipWs();
            if (sc.pos >= json.size()) {
                return makeError(JsonMemberStatus::Malformed, "对象未闭合");
            }
            if (json[sc.pos] == ',') {
                ++sc.pos;
                continue;
            }
            if (json[sc.pos] == '}') {
                ++sc.pos;
                break;
            }
            return makeError(JsonMemberStatus::Malformed, "成员之间缺少逗号");
        }
    }

    // 根对象之后不得有多余内容（保证"畸形 JSON"能被识别出来）
    sc.skipWs();
    if (sc.pos != json.size()) {
        return makeError(JsonMemberStatus::Malformed, "根对象之后存在多余内容");
    }

    if (!found) {
        return makeError(JsonMemberStatus::Missing, "键 \"" + key + "\" 不存在");
    }

    // 命中值是字符串时，此处就做一次严格解码校验：
    // 若含孤立代理 / 内嵌 NUL，直接判 Malformed，而不是让调用方拿到空串
    // （那等于"静默替换"，是 P1-2 / P2-1 明确禁止的）。
    if (span.kind == JsonValueKind::String) {
        Scanner v(json);
        v.pos = span.begin;
        std::string tmp;
        if (!v.parseString(&tmp)) {
            return makeError(JsonMemberStatus::Malformed,
                             "键 \"" + key + "\" 的字符串值非法：" + v.error);
        }
    }

    JsonLookup r;
    r.status = JsonMemberStatus::Ok;
    r.span = span;
    return r;
}

JsonLookup findJsonMember(const std::string& json, const std::string& key,
                          JsonValueKind expected) {
    JsonLookup r = findJsonMember(json, key);
    if (r.status != JsonMemberStatus::Ok) return r;
    if (r.span.kind != expected) {
        JsonLookup bad;
        bad.status = JsonMemberStatus::TypeMismatch;
        bad.span = r.span;
        bad.message = "键 \"" + key + "\" 类型不匹配（期望 " +
                      std::to_string(static_cast<int>(expected)) + "，实际 " +
                      std::to_string(static_cast<int>(r.span.kind)) + "）";
        return bad;
    }
    return r;
}

double jsonNumberValue(const std::string& json, const JsonMemberSpan& span, double def) {
    if (span.kind != JsonValueKind::Number) return def;
    if (span.begin >= span.end || span.end > json.size()) return def;
    char* end = nullptr;
    const double v = std::strtod(json.c_str() + span.begin, &end);
    if (end == json.c_str() + span.begin) return def;
    if (static_cast<std::size_t>(end - json.c_str()) > span.end) return def;
    return v;
}

std::string jsonStringValue(const std::string& json, const JsonMemberSpan& span) {
    if (span.kind != JsonValueKind::String) return std::string();
    if (span.begin + 1 >= span.end || span.end > json.size()) return std::string();
    Scanner sc(json);
    sc.pos = span.begin;
    std::string out;
    if (!sc.parseString(&out)) return std::string();  // findJsonMember 已校验，正常不会到这里
    return out;
}

bool jsonNumberArray(const std::string& json, const JsonMemberSpan& span,
                     std::vector<double>* out) {
    if (out == nullptr) return false;
    out->clear();
    if (span.kind != JsonValueKind::Array) return false;
    std::size_t p = span.begin + 1;
    const std::size_t limit = (span.end > span.begin) ? span.end - 1 : span.begin;
    while (p < limit) {
        const char c = json[p];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') { ++p; continue; }
        char* end = nullptr;
        const double v = std::strtod(json.c_str() + p, &end);
        if (end == json.c_str() + p) break;
        out->push_back(v);
        p = static_cast<std::size_t>(end - json.c_str());
    }
    return !out->empty();
}

bool jsonU64Array(const std::string& json, const JsonMemberSpan& span,
                  std::vector<std::uint64_t>* out) {
    if (out == nullptr) return false;
    out->clear();
    if (span.kind != JsonValueKind::Array) return false;
    std::size_t p = span.begin + 1;
    const std::size_t limit = (span.end > span.begin) ? span.end - 1 : span.begin;
    while (p < limit) {
        const char c = json[p];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') { ++p; continue; }
        char* end = nullptr;
        const unsigned long long v = std::strtoull(json.c_str() + p, &end, 10);
        if (end == json.c_str() + p) break;
        out->push_back(static_cast<std::uint64_t>(v));
        p = static_cast<std::size_t>(end - json.c_str());
    }
    return !out->empty();
}

// ---- P1-1：编辑前置校验 ----------------------------------------------------

bool hasDuplicateRootKey(const std::string& json, std::string* outKey,
                         std::string* outMessage) {
    if (outKey != nullptr) outKey->clear();
    if (outMessage != nullptr) outMessage->clear();

    Scanner sc(json);
    sc.skipWs();
    if (sc.pos >= json.size() || json[sc.pos] != '{') return false;  // 非对象：交给 findJsonMember 判定
    ++sc.pos;

    std::set<std::string> seen;
    sc.skipWs();
    if (sc.pos < json.size() && json[sc.pos] == '}') return false;

    while (true) {
        sc.skipWs();
        std::string k;
        if (!sc.parseString(&k)) return false;  // 解析失败由 findJsonMember 报 Malformed
        if (!seen.insert(k).second) {
            if (outKey != nullptr) *outKey = k;
            if (outMessage != nullptr) {
                *outMessage = "参数对象存在重复键 \"" + k + "\"（拒绝执行，避免静默回落默认值）";
            }
            return true;
        }
        sc.skipWs();
        if (sc.pos >= json.size() || json[sc.pos] != ':') return false;
        ++sc.pos;
        if (!sc.skipValue(nullptr)) return false;
        sc.skipWs();
        if (sc.pos >= json.size()) return false;
        if (json[sc.pos] == ',') { ++sc.pos; continue; }
        if (json[sc.pos] == '}') return false;
        return false;
    }
}

std::string precheckEditArgs(const std::string& json) {
    std::string key;
    std::string msg;
    if (hasDuplicateRootKey(json, &key, &msg)) return msg;
    return std::string();
}

}  // namespace core
}  // namespace nairoclip
