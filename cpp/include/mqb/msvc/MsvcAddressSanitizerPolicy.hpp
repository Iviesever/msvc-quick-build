#pragma once

#include <cctype>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

class MsvcAddressSanitizerPolicy {
public:
    [[nodiscard]] static bool compiler_enabled(
        const std::span<const std::string> arguments) noexcept {
        for (const auto& argument : arguments) {
            if (ascii_iequals(option_body(argument), "fsanitize=address")) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static RuntimeLibrary effective_runtime_library(
        const CompilerOptions& options) noexcept {
        if (options.runtime_library) {
            return *options.runtime_library;
        }
        return options.configuration == BuildConfiguration::debug
            ? RuntimeLibrary::mdd
            : RuntimeLibrary::md;
    }

    static void apply_link_policy(
        const CompilerOptions& compiler_options,
        LinkOptions& link_options) noexcept {
        if (compiler_enabled(compiler_options.additional_arguments)) {
            link_options.address_sanitizer_runtime_library =
                effective_runtime_library(compiler_options);
        } else {
            link_options.address_sanitizer_runtime_library.reset();
        }
    }

    [[nodiscard]] static bool inferred_libraries_enabled(
        const std::span<const std::string> linker_arguments) noexcept {
        // LINK enables inferred ASan libraries by default. Preserve native
        // last-option-wins behavior for the explicit advanced override.
        bool enabled = true;
        for (const auto& argument : linker_arguments) {
            const std::string_view body = option_body(argument);
            if (ascii_iequals(body, "INFERASANLIBS")) {
                enabled = true;
            } else if (ascii_iequals(body, "INFERASANLIBS:NO")) {
                enabled = false;
            }
        }
        return enabled;
    }

    [[nodiscard]] static std::vector<std::string> inferred_library_names(
        const RuntimeLibrary runtime,
        const Architecture architecture,
        const TargetKind target_kind,
        const std::string_view toolchain_version) {
        const std::string arch = architecture == Architecture::x86
            ? "i386"
            : "x86_64";
        const bool static_crt = runtime == RuntimeLibrary::mt
            || runtime == RuntimeLibrary::mtd;
        const bool debug_crt = runtime == RuntimeLibrary::mdd
            || runtime == RuntimeLibrary::mtd;

        if (modern_runtime_model(toolchain_version)) {
            return {
                "clang_rt.asan_dynamic-" + arch + ".lib",
                "clang_rt.asan_"
                    + std::string{static_crt ? "static" : "dynamic"}
                    + "_runtime_thunk-" + arch + ".lib",
            };
        }

        // Visual Studio versions before the 17.7 Preview 3 runtime unification
        // used configuration- and target-kind-specific ASan libraries.
        if (!static_crt) {
            if (debug_crt) {
                return {
                    "clang_rt.asan_dbg_dynamic-" + arch + ".lib",
                    "clang_rt.asan_dbg_dynamic_runtime_thunk-" + arch + ".lib",
                };
            }
            return {
                "clang_rt.asan_dynamic-" + arch + ".lib",
                "clang_rt.asan_dynamic_runtime_thunk-" + arch + ".lib",
            };
        }

        if (target_kind == TargetKind::dynamic_library) {
            return {
                "clang_rt.asan_"
                    + std::string{debug_crt ? "dbg_dll_thunk" : "dll_thunk"}
                    + "-" + arch + ".lib",
            };
        }
        if (debug_crt) {
            return {
                "clang_rt.asan_dbg-" + arch + ".lib",
                "clang_rt.asan_cxx_dbg-" + arch + ".lib",
            };
        }
        return {
            "clang_rt.asan-" + arch + ".lib",
            "clang_rt.asan_cxx-" + arch + ".lib",
        };
    }

    static void apply_runtime_path(
        process::ProcessSpec& spec,
        const MsvcToolchain& toolchain) {
        // ASan runtime DLLs live beside the selected MSVC compiler. Both
        // Visual Studio and portable discovery capture a toolchain PATH that
        // includes that directory. Override only PATH for the launched target;
        // do not leak INCLUDE/LIB or other build-only variables into programs.
        for (const auto& variable : toolchain.environment) {
            if (ascii_iequals(variable.name, "PATH")) {
                spec.environment.push_back(variable);
                return;
            }
        }
    }

private:
    [[nodiscard]] static std::string_view option_body(
        const std::string_view argument) noexcept {
        if (argument.size() >= 2
            && (argument.front() == '/' || argument.front() == '-')) {
            return argument.substr(1);
        }
        return {};
    }

    [[nodiscard]] static bool ascii_iequals(
        const std::string_view left,
        const std::string_view right) noexcept {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto a = static_cast<unsigned char>(left[index]);
            const auto b = static_cast<unsigned char>(right[index]);
            if (std::tolower(a) != std::tolower(b)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::optional<std::pair<unsigned, unsigned>>
    major_minor(const std::string_view version) noexcept {
        std::size_t index = 0;
        unsigned major = 0;
        bool major_digit = false;
        while (index < version.size()
            && std::isdigit(static_cast<unsigned char>(version[index])) != 0) {
            major_digit = true;
            major = major * 10u + static_cast<unsigned>(version[index] - '0');
            ++index;
        }
        if (!major_digit || index >= version.size() || version[index] != '.') {
            return std::nullopt;
        }
        ++index;
        unsigned minor = 0;
        bool minor_digit = false;
        while (index < version.size()
            && std::isdigit(static_cast<unsigned char>(version[index])) != 0) {
            minor_digit = true;
            minor = minor * 10u + static_cast<unsigned>(version[index] - '0');
            ++index;
        }
        if (!minor_digit) {
            return std::nullopt;
        }
        return std::pair{major, minor};
    }

    [[nodiscard]] static bool modern_runtime_model(
        const std::string_view toolchain_version) noexcept {
        const auto parsed = major_minor(toolchain_version);
        if (!parsed) {
            // Unknown future/custom version strings should prefer the current
            // runtime model rather than silently selecting obsolete library names.
            return true;
        }
        const auto [major, minor] = *parsed;
        if (major == 19u || major == 14u) {
            return minor >= 37u;
        }
        if (major > 19u) {
            return true;
        }
        // Version schemes other than compiler 19.x / VC Tools 14.x are not
        // historical MSVC schemes known to MQB; use the current model.
        return major != 18u && major != 17u && major != 16u;
    }
};

} // namespace mqb::msvc
