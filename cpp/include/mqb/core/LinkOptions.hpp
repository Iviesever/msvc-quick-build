#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"

namespace mqb {

enum class LinkSubsystem {
    console,
    windows,
};

struct LinkOptions {
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    TargetKind target_kind{TargetKind::executable};
    LinkSubsystem subsystem{LinkSubsystem::console};
    // Coupled with CompilerOptions::link_time_code_generation. False preserves
    // the historical link signature/recipe; true makes /LTCG structured policy.
    bool link_time_code_generation{false};
    // Present when all target translation units are compiled with
    // /fsanitize=address. The effective CRT mode is carried across the
    // compile->link boundary so MQB can track LINK's inferred ASan libraries and
    // enforce the sanitizer's non-incremental link contract without rewriting
    // the user's native compiler/linker argv.
    std::optional<RuntimeLibrary> address_sanitizer_runtime_library;
    // Present when /fsanitize=address also leaves MSVC's VCAsan default-library
    // directive enabled. This is separate from /INFERASANLIBS: the compiler
    // injects vcasan*.lib through ordinary default-library metadata, and /Zl or
    // /fno-sanitize-address-vcasan-lib can suppress that compiler-side input.
    std::optional<RuntimeLibrary> address_sanitizer_vcasan_runtime_library;
    // Present when all target translation units are compiled with
    // /fsanitize=fuzzer. cl.exe injects the matching clang_rt.fuzzer_<CRT>
    // default-library directive into objects; the CRT mode is carried here so
    // MQB can seal that implicit linker file input without re-emitting it.
    std::optional<RuntimeLibrary> fuzzer_runtime_library;
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    std::vector<std::string> additional_arguments;
};

} // namespace mqb