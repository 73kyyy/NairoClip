#include "test_framework.h"
#include "nairoclip/core/json.h"
using namespace nairoclip::core;
NC_TEST(Json, ValueBasics) {
    const JsonValue n = JsonValue::null();
    NC_CHECK(n.isNull());
    const JsonValue b = JsonValue::boolean(true);
    NC_CHECK(b.isBool());
    NC_CHECK(b.asBool());
    const JsonValue i = JsonValue::integer(42);
    NC_CHECK(i.isNumber());
    NC_CHECK(i.isInteger());
    NC_CHECK_EQ(i.asInteger(), std::int64_t(42));
    const JsonValue d = JsonValue::number(1.5);
    NC_CHECK(!d.isInteger());
    const JsonValue s = JsonValue::string("hello");
    NC_CHECK(s.isString());
    NC_CHECK_EQ(s.asString(), std::string("hello"));
}
NC_TEST(Json, ObjectAndArrayAccess) {
    JsonValue obj = JsonValue::object();
    obj.set("a", JsonValue::integer(1));
    obj.set("b", JsonValue::string("x"));
    NC_CHECK(obj.has("a"));
    NC_CHECK(!obj.has("c"));
    NC_CHECK_EQ(obj.get("a").asInteger(), std::int64_t(1));
    NC_CHECK_EQ(obj.get("c").asInteger(), std::int64_t(0));
    JsonValue arr = JsonValue::array();
    arr.push(JsonValue::integer(1));
    arr.push(JsonValue::integer(2));
    NC_CHECK_EQ(arr.size(), std::size_t(2));
    NC_CHECK_EQ(arr.at(1).asInteger(), std::int64_t(2));
    NC_CHECK_EQ(arr.at(9).type(), JsonValue::Type::Null);
}
NC_TEST(Json, RoundtripBasic) {
    JsonValue root = JsonValue::object();
    root.set("name", JsonValue::string("NairoClip 工程"));
    root.set("version", JsonValue::integer(1));
    root.set("fps", JsonValue::number(29.97));
    root.set("flag", JsonValue::boolean(false));
    JsonValue arr = JsonValue::array();
    arr.push(JsonValue::integer(10));
    arr.push(JsonValue::integer(20));
    root.set("marks", std::move(arr));
    const std::string text = toJsonString(root);
    JsonValue parsed;
    JsonParseError err;
    NC_CHECK(parseJson(text, parsed, err));
    NC_CHECK_EQ(parsed.get("name").asString(), std::string("NairoClip 工程"));
    NC_CHECK_EQ(parsed.get("version").asInteger(), std::int64_t(1));
    NC_CHECK(parsed.get("flag").asBool(true) == false);
    NC_CHECK_EQ(parsed.get("marks").size(), std::size_t(2));
    NC_CHECK_EQ(parsed.get("marks").at(0).asInteger(), std::int64_t(10));
}
NC_TEST(Json, EscapeRoundtrip) {
    const std::string tricky = "line1\nline2\t\\ \"quotes\" / slash";
    JsonValue root = JsonValue::object();
    root.set("s", JsonValue::string(tricky));
    const std::string text = toJsonString(root);
    JsonValue parsed;
    JsonParseError err;
    NC_CHECK(parseJson(text, parsed, err));
    NC_CHECK_EQ(parsed.get("s").asString(), tricky);
}
NC_TEST(Json, UnicodeEscape) {
    const std::string text = "{\"k\":\"\\u4e2d\\u6587\"}";
    JsonValue parsed;
    JsonParseError err;
    NC_CHECK(parseJson(text, parsed, err));
    NC_CHECK_EQ(parsed.get("k").asString(), std::string("中文"));
}
NC_TEST(Json, ParseErrors) {
    JsonValue v;
    JsonParseError err;
    NC_CHECK(!parseJson("", v, err));
    NC_CHECK(!parseJson("{", v, err));
    NC_CHECK(!parseJson("[1,", v, err));
    NC_CHECK(!parseJson("{'a':1}", v, err));
    NC_CHECK(!parseJson("1 2", v, err));
    NC_CHECK(!parseJson("tru", v, err));
    NC_CHECK(!err.message.empty());
}
NC_TEST(Json, Int64Precision) {
    const std::string text = "{\"v\":9007199254740993}";
    JsonValue parsed;
    JsonParseError err;
    NC_CHECK(parseJson(text, parsed, err));
    NC_CHECK_EQ(parsed.get("v").asInteger(), std::int64_t(9007199254740993LL));
    const std::string out = toJsonString(parsed);
    NC_CHECK_EQ(out, std::string("{\"v\":9007199254740993}"));
}
NC_TEST(Json, PrettyPrintContainsNewlines) {
    JsonValue root = JsonValue::object();
    root.set("a", JsonValue::integer(1));
    const std::string pretty = toJsonString(root, true);
    NC_CHECK(pretty.find('\n') != std::string::npos);
    JsonValue parsed;
    JsonParseError err;
    NC_CHECK(parseJson(pretty, parsed, err));
    NC_CHECK_EQ(parsed.get("a").asInteger(), std::int64_t(1));
}

NC_TEST(Json, RejectsExcessiveNestingDepth) {
    std::string text(65, '[');
    text += "0";
    text.append(65, ']');
    JsonValue parsed;
    JsonParseError error;
    NC_CHECK(!parseJson(text, parsed, error));
    NC_CHECK(error.message.find("深度") != std::string::npos);
}

NC_TEST(Json, RejectsOversizedString) {
    std::string text = "{\"value\":\"";
    text.append(4u * 1024u * 1024u + 1u, 'x');
    text += "\"}";
    JsonValue parsed;
    JsonParseError error;
    NC_CHECK(!parseJson(text, parsed, error));
    NC_CHECK(error.message.find("字符串") != std::string::npos);
}

NC_TEST(Json, RejectsExcessiveArrayItems) {
    std::string text = "[";
    for (std::size_t i = 0; i < 100001u; ++i) {
        if (i) text += ',';
        text += '0';
    }
    text += ']';
    JsonValue parsed;
    JsonParseError error;
    NC_CHECK(!parseJson(text, parsed, error));
    NC_CHECK(error.message.find("数组") != std::string::npos);
}

NC_TEST(Json, AcceptsConfiguredMaximumNestingDepth) {
    std::string text(64, '[');
    text += "0";
    text.append(64, ']');
    JsonValue parsed;
    JsonParseError error;
    NC_CHECK(parseJson(text, parsed, error));
}
