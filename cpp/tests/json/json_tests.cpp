#include <iostream>
#include <string_view>

#include "mqb/json/Json.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using mqb::json::Kind;

    {
        auto parsed = mqb::json::parse(R"json({
  "version": 1,
  "enabled": true,
  "name": "M\\u002eCore",
  "items": [null, -1.5e+2, "\u65e5\u672c", "\ud83d\ude80"]
})json");
        expect(parsed.has_value(), "valid nested JSON should parse");
        if (parsed) {
            expect(parsed->kind == Kind::object, "root should be object");
            expect(parsed->object.at("version").scalar == "1", "integer spelling should be preserved");
            expect(parsed->object.at("enabled").boolean, "boolean should be preserved");
            expect(parsed->object.at("items").kind == Kind::array
                       && parsed->object.at("items").array.size() == 4,
                   "array contents should be preserved");
            expect(parsed->object.at("items").array[2].scalar == "日本",
                   "BMP Unicode escapes should decode as UTF-8");
            expect(parsed->object.at("items").array[3].scalar == "🚀",
                   "surrogate pair should decode as UTF-8");
        }
    }

    {
        auto duplicate = mqb::json::parse(R"json({"a": 1, "a": 2})json");
        expect(!duplicate, "duplicate object keys should be rejected");
        if (!duplicate) {
            expect(duplicate.error().message.find("duplicate JSON object key") != std::string::npos,
                   "duplicate-key error should be explicit");
        }
    }

    {
        auto invalid_number = mqb::json::parse(R"json({"n": 01})json");
        expect(!invalid_number, "leading-zero JSON number should be rejected");
    }

    {
        auto trailing = mqb::json::parse(R"json({"a": 1} trailing)json");
        expect(!trailing, "trailing non-whitespace should be rejected");
    }

    {
        auto bad_surrogate = mqb::json::parse(R"json("\ud83dX")json");
        expect(!bad_surrogate, "unpaired high surrogate should be rejected");
    }

    {
        auto control = mqb::json::parse(std::string_view{"\"line\nfeed\"", 11});
        expect(!control, "raw control characters inside JSON strings should be rejected");
    }

    {
        auto empty = mqb::json::parse("   \r\n\t");
        expect(!empty, "whitespace-only input should be rejected");
        if (!empty) {
            expect(empty.error().line >= 1 && empty.error().column >= 1,
                   "parse errors should carry source coordinates");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_json_tests passed\n";
    return 0;
}
