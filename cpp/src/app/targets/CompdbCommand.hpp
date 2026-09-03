#pragma once

#include <span>
#include <string_view>

namespace mqb::app {

[[nodiscard]] int run_compdb_command(
    std::span<const std::string_view> arguments);

[[nodiscard]] std::string_view compdb_usage() noexcept;

} // namespace mqb::app
