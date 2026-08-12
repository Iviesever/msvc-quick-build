#pragma once

#include <cstddef>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mqb::json {

enum class Kind {
    null_value,
    boolean,
    number,
    string,
    array,
    object,
};

struct Value {
    Kind kind{Kind::null_value};
    std::size_t line{1};
    std::size_t column{1};
    bool boolean{};
    std::string scalar;
    std::vector<Value> array;
    std::map<std::string, Value, std::less<>> object;
};

using Object = decltype(Value{}.object);

struct Error {
    std::size_t line{1};
    std::size_t column{1};
    std::string message;
};

[[nodiscard]] std::expected<Value, Error> parse(std::string_view text);

} // namespace mqb::json
