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
    // Keep the existing values first: BuildSignature hashes the enum value and
    // existing C++20/23/latest caches must not be invalidated just because
    // stable-v5 parity adds older ordinary-TU modes.
    cpp20,
    cpp23,
    latest,
    cpp14,
    cpp17,
};

enum class BuildReason {
    missing_cache_entry,
    missing_output,
    source_changed,
    dependency_changed,
    toolchain_changed,
    compiler_options_changed,
    link_inputs_changed,
    linker_options_changed,
    explicit_rebuild,
};

[[nodiscard]] std::string_view to_string(BuildConfiguration value) noexcept;
[[nodiscard]] std::string_view to_string(Architecture value) noexcept;
[[nodiscard]] std::string_view to_string(CppStandard value) noexcept;
[[nodiscard]] std::string_view to_string(BuildReason value) noexcept;

} // namespace mqb
