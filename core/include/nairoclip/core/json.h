#pragma once
// 最小 JSON 实现（零依赖）：值模型 + 解析器 + 序列化器。
// 定位：工程文件（.nclip）的存储格式。理性数(int64)以字符串存储避免浮点误差。

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace nairoclip {
namespace core {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;
    static JsonValue null();
    static JsonValue boolean(bool v);
    static JsonValue number(double v);
    static JsonValue integer(std::int64_t v);
    static JsonValue string(std::string v);
    static JsonValue array();
    static JsonValue object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isInteger() const { return type_ == Type::Number && isInt_; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::int64_t asInteger(std::int64_t fallback = 0) const;
    const std::string& asString() const;

    // 数组操作
    void push(JsonValue value);
    std::size_t size() const;
    const JsonValue& at(std::size_t index) const;

    // 对象操作
    void set(const std::string& key, JsonValue value);
    const JsonValue& get(const std::string& key) const;
    bool has(const std::string& key) const;
    const std::vector<std::pair<std::string, JsonValue>>& members() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::int64_t int_ = 0;
    bool isInt_ = false;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<std::pair<std::string, JsonValue>> object_;

    static const JsonValue& nullValue();
};

/// 解析错误信息。
struct JsonParseError {
    std::size_t offset = 0;
    std::string message;
};

/// 解析 JSON 文本。成功返回 true 并填充 out。
bool parseJson(const std::string& text, JsonValue& out, JsonParseError& error);

/// 序列化。pretty=true 时缩进美化。
std::string toJsonString(const JsonValue& value, bool pretty = false);

}  // namespace core
}  // namespace nairoclip
