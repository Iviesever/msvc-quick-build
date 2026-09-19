#pragma once
#include <span>
#include <string_view>
namespace mqb::app {
[[nodiscard]] int run_storage_command(std::span<const std::string_view> arguments);
}
