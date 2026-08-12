#pragma once

#include <span>
#include <string_view>

namespace mqb::app {

class Application {
public:
    [[nodiscard]] int run(std::span<const std::string_view> arguments);
};

} // namespace mqb::app
