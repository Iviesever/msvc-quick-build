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

enum class BuildReason {
    missing_cache_entry,
    missing_output,
    source_changed,
    dependency_changed,
    toolchain_changed,
    compiler_options_changed,
    linker_options_changed,
    explicit_rebuild,
};

[[nodiscard]] std::string_view to_string(BuildConfiguration value) noexcept;
[[nodiscard]] std::string_view to_string(Architecture value) noexcept;
[[nodiscard]] std::string_view to_string(CppStandard value) noexcept;
[[nodiscard]] std::string_view to_string(BuildReason value) noexcept;

} // namespace mqb
