#pragma once

#include <string_view>

namespace mqb {

enum class BuildConfiguration {
    debug,
    release,
};

enum class Architecture {
    x86,
    x64,
};

enum class CppStandard {
    cpp20,
    cpp23,
    latest,
};

[[nodiscard]] std::string_view to_string(BuildConfiguration value) noexcept;
[[nodiscard]] std::string_view to_string(Architecture value) noexcept;
[[nodiscard]] std::string_view to_string(CppStandard value) noexcept;

} // namespace mqb
