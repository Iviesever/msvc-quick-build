#pragma once

#include <cctype>
#include <span>
#include <string>
#include <string_view>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"

namespace mqb::msvc {

class MsvcFuzzerPolicy {
public:
    [[nodiscard]] static bool compiler_enabled(
        const std::span<const std::string> arguments) noexcept {
        for (const auto& argument : arguments) {
            if (ascii_iequals(option_body(argument), "fsanitize=fuzzer")) {
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
            link_options.fuzzer_runtime_library =
                effective_runtime_library(compiler_options);
        } else {
            link_options.fuzzer_runtime_library.reset();
        }
    }

    [[nodiscard]] static std::string inferred_library_name(
        const RuntimeLibrary runtime,
        const Architecture architecture) {
        const std::string_view runtime_name = [&]() -> std::string_view {
            switch (runtime) {
            case RuntimeLibrary::md: return "MD";
            case RuntimeLibrary::mdd: return "MDd";
            case RuntimeLibrary::mt: return "MT";
            case RuntimeLibrary::mtd: return "MTd";
            }
            return "MD";
        }();
        const std::string_view arch = architecture == Architecture::x86
            ? "i386"
            : "x86_64";
        return "clang_rt.fuzzer_" + std::string{runtime_name}
            + "-" + std::string{arch} + ".lib";
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
};

} // namespace mqb::msvc