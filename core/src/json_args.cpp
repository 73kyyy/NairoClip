// 结构感知的 JSON 参数查找实现（P0 修复 NC-P0-FINDKEY-REPAIR）。
//
// 设计取舍见 json_args.h 顶部注释。本文件**不修改** core/json.h 的任何行为，
// 只新增一条独立的、只读的成员查找路径。

#include "nairoclip/core/json_args.h"

#include <cstdlib>

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

/// 把码点按 UTF-8 追加。
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

    /// 解析一个 JSON 字符串（pos 必须指向 '"'），解码转义后写入 out。
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
            // 转义
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
                    // 代理对：高代理 + 紧随的低代理
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos + 1 < s.size() &&
                        s[pos] == '\\' && s[pos + 1] == 'u') {
                        const std::size_t save = pos;
                        pos += 2;
                        std::uint32_t lo = 0;
                        if (readHex4(&lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            pos = save;  // 不是低代理：回退，按独立码点处理
                        }
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
            if (!parseString(nullptr)) return false;
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

    bool skipObject() {
        ++pos;  // '{'
        skipWs();
        if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
        while (true) {
            skipWs();
            if (!parseString(nullptr)) return false;
            skipWs();
            if (pos >= s.size() || s[pos] != ':') return fail("对象成员缺少冒号");
            ++pos;
            if (!skipValue(nullptr)) return false;
            skipWs();
            if (pos >= s.size()) return fail("对象未闭合");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == '}') { ++pos; return true; }
            return fail("对象成员之间缺少逗号");
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
            if (!sc.parseString(&k)) {
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
    // 越界保护：strtod 不应越过 span 末尾
    if (static_cast<std::size_t>(end - json.c_str()) > span.end) return def;
    return v;
}

std::string jsonStringValue(const std::string& json, const JsonMemberSpan& span) {
    if (span.kind != JsonValueKind::String) return std::string();
    if (span.begin + 1 >= span.end || span.end > json.size()) return std::string();
    // 复用 Scanner 的解码逻辑：把区间首尾的引号去掉后重新解析一次
    Scanner sc(json);
    sc.pos = span.begin;
    std::string out;
    if (!sc.parseString(&out)) return std::string();
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
        if (end == json.c_str() + p) break;  // 非数字：停止，避免死循环
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

}  // namespace core
}  // namespace nairoclip
