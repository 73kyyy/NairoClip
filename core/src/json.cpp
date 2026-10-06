#include "nairoclip/core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nairoclip {
namespace core {

namespace {

const JsonValue& nullInstance() {
    static const JsonValue v;
    return v;
}

// ---------------- 序列化辅助 ----------------

void escapeString(const std::string& in, std::string& out) {
    out.push_back('"');
    for (const char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void writeValue(const JsonValue& v, std::string& out, bool pretty, int depth);

void writeIndent(std::string& out, int depth) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
}

void writeValue(const JsonValue& v, std::string& out, bool pretty, int depth) {
    switch (v.type()) {
        case JsonValue::Type::Null:
            out += "null";
            break;
        case JsonValue::Type::Bool:
            out += v.asBool() ? "true" : "false";
            break;
        case JsonValue::Type::Number: {
            if (v.isInteger()) {
                out += std::to_string(v.asInteger());
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.17g", v.asNumber());
                out += buf;
            }
            break;
        }
        case JsonValue::Type::String:
            escapeString(v.asString(), out);
            break;
        case JsonValue::Type::Array: {
            if (v.size() == 0) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (i > 0) out.push_back(',');
                if (pretty) writeIndent(out, depth + 1);
                writeValue(v.at(i), out, pretty, depth + 1);
            }
            if (pretty) writeIndent(out, depth);
            out.push_back(']');
            break;
        }
        case JsonValue::Type::Object: {
            if (v.members().empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            bool first = true;
            for (const auto& kv : v.members()) {
                if (!first) out.push_back(',');
                first = false;
                if (pretty) writeIndent(out, depth + 1);
                escapeString(kv.first, out);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                writeValue(kv.second, out, pretty, depth + 1);
            }
            if (pretty) writeIndent(out, depth);
            out.push_back('}');
            break;
        }
    }
}

// ---------------- 解析器 ----------------

class Parser {
public:
    Parser(const std::string& text) : text_(text) {}

    bool parse(JsonValue& out) {
        skipWhitespace();
        if (!parseValue(out)) return false;
        skipWhitespace();
        if (pos_ != text_.size()) {
            setError("尾部存在多余字符");
            return false;
        }
        return true;
    }

    const JsonParseError& error() const { return error_; }

private:
    const std::string& text_;
    std::size_t pos_ = 0;
    JsonParseError error_;

    void setError(const std::string& msg) {
        if (error_.message.empty()) {
            error_.offset = pos_;
            error_.message = msg;
        }
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }
    char advance() { return atEnd() ? '\0' : text_[pos_++]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parseValue(JsonValue& out) {
        if (atEnd()) {
            setError("意外的结尾");
            return false;
        }
        switch (peek()) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = JsonValue::string(std::move(s));
                return true;
            }
            case 't': return parseLiteral("true", JsonValue::boolean(true), out);
            case 'f': return parseLiteral("false", JsonValue::boolean(false), out);
            case 'n': return parseLiteral("null", JsonValue::null(), out);
            default: return parseNumber(out);
        }
    }

    bool parseLiteral(const char* literal, JsonValue value, JsonValue& out) {
        const std::size_t len = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, len, literal) != 0) {
            setError("非法字面量");
            return false;
        }
        pos_ += len;
        out = std::move(value);
        return true;
    }

    bool parseObject(JsonValue& out) {
        ++pos_;  // '{'
        out = JsonValue::object();
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                setError("对象的键必须是字符串");
                return false;
            }
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (advance() != ':') {
                setError("键后缺少冒号");
                return false;
            }
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value)) return false;
            out.set(key, std::move(value));
            skipWhitespace();
            const char c = advance();
            if (c == ',') continue;
            if (c == '}') return true;
            setError("对象中缺少逗号或右花括号");
            return false;
        }
    }

    bool parseArray(JsonValue& out) {
        ++pos_;  // '['
        out = JsonValue::array();
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value)) return false;
            out.push(std::move(value));
            skipWhitespace();
            const char c = advance();
            if (c == ',') continue;
            if (c == ']') return true;
            setError("数组中缺少逗号或右方括号");
            return false;
        }
    }

    bool parseString(std::string& out) {
        ++pos_;  // '"'
        while (true) {
            if (atEnd()) {
                setError("字符串未闭合");
                return false;
            }
            const char c = advance();
            if (c == '"') return true;
            if (c == '\\') {
                if (atEnd()) {
                    setError("转义未完成");
                    return false;
                }
                const char e = advance();
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        unsigned int code = 0;
                        if (!parseHex4(code)) return false;
                        appendUtf8(out, code);
                        break;
                    }
                    default:
                        setError("非法转义字符");
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
    }

    bool parseHex4(unsigned int& code) {
        if (pos_ + 4 > text_.size()) {
            setError("\\u 转义不完整");
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned int>(c - 'A' + 10);
            else {
                setError("非法的十六进制数字");
                return false;
            }
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int code) {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool parseNumber(JsonValue& out) {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (atEnd() || peek() < '0' || peek() > '9') {
            setError("非法数字");
            return false;
        }
        while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
        bool isInt = true;
        if (!atEnd() && peek() == '.') {
            isInt = false;
            ++pos_;
            if (atEnd() || peek() < '0' || peek() > '9') {
                setError("小数点后缺少数字");
                return false;
            }
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            isInt = false;
            ++pos_;
            if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
            if (atEnd() || peek() < '0' || peek() > '9') {
                setError("指数部分缺少数字");
                return false;
            }
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string token = text_.substr(start, pos_ - start);
        if (isInt) {
            errno = 0;
            char* endPtr = nullptr;
            const long long v = std::strtoll(token.c_str(), &endPtr, 10);
            if (errno != 0) {
                // 超范围整数退化为 double
                out = JsonValue::number(std::strtod(token.c_str(), nullptr));
                return true;
            }
            out = JsonValue::integer(static_cast<std::int64_t>(v));
        } else {
            out = JsonValue::number(std::strtod(token.c_str(), nullptr));
        }
        return true;
    }
};

}  // namespace

// ---------------- JsonValue ----------------

const JsonValue& JsonValue::nullValue() { return nullInstance(); }

JsonValue JsonValue::null() { return JsonValue{}; }

JsonValue JsonValue::boolean(bool v) {
    JsonValue j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}

JsonValue JsonValue::number(double v) {
    JsonValue j;
    j.type_ = Type::Number;
    j.number_ = v;
    j.isInt_ = false;
    return j;
}

JsonValue JsonValue::integer(std::int64_t v) {
    JsonValue j;
    j.type_ = Type::Number;
    j.number_ = static_cast<double>(v);
    j.int_ = v;
    j.isInt_ = true;
    return j;
}

JsonValue JsonValue::string(std::string v) {
    JsonValue j;
    j.type_ = Type::String;
    j.string_ = std::move(v);
    return j;
}

JsonValue JsonValue::array() {
    JsonValue j;
    j.type_ = Type::Array;
    return j;
}

JsonValue JsonValue::object() {
    JsonValue j;
    j.type_ = Type::Object;
    return j;
}

bool JsonValue::asBool(bool fallback) const {
    if (type_ != Type::Bool) return fallback;
    return bool_;
}

double JsonValue::asNumber(double fallback) const {
    if (type_ != Type::Number) return fallback;
    return isInt_ ? static_cast<double>(int_) : number_;
}

std::int64_t JsonValue::asInteger(std::int64_t fallback) const {
    if (type_ != Type::Number) return fallback;
    if (isInt_) return int_;
    return static_cast<std::int64_t>(number_);
}

const std::string& JsonValue::asString() const {
    static const std::string empty;
    return type_ == Type::String ? string_ : empty;
}

void JsonValue::push(JsonValue value) {
    if (type_ != Type::Array) {
        *this = JsonValue::array();
    }
    array_.push_back(std::move(value));
}

std::size_t JsonValue::size() const {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

const JsonValue& JsonValue::at(std::size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) return nullInstance();
    return array_[index];
}

void JsonValue::set(const std::string& key, JsonValue value) {
    if (type_ != Type::Object) {
        *this = JsonValue::object();
    }
    for (auto& kv : object_) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    object_.emplace_back(key, std::move(value));
}

const JsonValue& JsonValue::get(const std::string& key) const {
    if (type_ == Type::Object) {
        for (const auto& kv : object_) {
            if (kv.first == key) return kv.second;
        }
    }
    return nullInstance();
}

bool JsonValue::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : object_) {
        if (kv.first == key) return true;
    }
    return false;
}

const std::vector<std::pair<std::string, JsonValue>>& JsonValue::members() const {
    static const std::vector<std::pair<std::string, JsonValue>> empty;
    return type_ == Type::Object ? object_ : empty;
}

// ---------------- 公开接口 ----------------

bool parseJson(const std::string& text, JsonValue& out, JsonParseError& error) {
    Parser parser(text);
    const bool ok = parser.parse(out);
    if (!ok) {
        error = parser.error();
    }
    return ok;
}

std::string toJsonString(const JsonValue& value, bool pretty) {
    std::string out;
    writeValue(value, out, pretty, 0);
    return out;
}

}  // namespace core
}  // namespace nairoclip
