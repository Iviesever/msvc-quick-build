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
    // BuildSignature hashes these numeric values. Keep existing identities
    // stable across parity expansion so old C++20/23/latest caches remain valid.
    cpp20 = 0,
    cpp23 = 1,
    latest = 2,
    cpp14 = 3,
    cpp17 = 4,
};

enum class TargetKind {
    executable,
    dynamic_library,
    static_library,
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
    archive_inputs_changed,
    archive_recipe_changed,
    explicit_rebuild,
};

[[nodiscard]] std::string_view to_string(BuildConfiguration value) noexcept;
[[nodiscard]] std::string_view to_string(Architecture value) noexcept;
[[nodiscard]] std::string_view to_string(CppStandard value) noexcept;
[[nodiscard]] std::string_view to_string(TargetKind value) noexcept;
[[nodiscard]] std::string_view to_string(BuildReason value) noexcept;

} // namespace mqb
