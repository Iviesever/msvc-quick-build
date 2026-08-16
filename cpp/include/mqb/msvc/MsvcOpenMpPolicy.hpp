#pragma once

#include <array>
#include <cctype>
#include <span>
#include <string>
#include <string_view>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"

namespace mqb::msvc {

class MsvcOpenMpPolicy {
public:
    [[nodiscard]] static bool classic_runtime_enabled(
        const std::span<const std::string> arguments) noexcept {
        bool enabled = false;
        for (const auto& argument : arguments) {
            const std::string_view body = option_body(argument);
            if (ascii_iequals(body, "openmp")
                || ascii_iequals(body, "openmp:experimental")) {
                enabled = true;
            } else if (ascii_iequals(body, "openmp-")) {
                enabled = false;
            }
        }
        return enabled;
    }

    static void apply_link_policy(
        const CompilerOptions& compiler_options,
        LinkOptions& link_options) noexcept {
        link_options.msvc_openmp_runtime =
            classic_runtime_enabled(compiler_options.additional_arguments);
    }

    [[nodiscard]] static constexpr std::array<std::string_view, 2>
    implicit_library_candidates() noexcept {
        // MSVC selects VCOMPD only when _DEBUG is defined and omp.h is included;
        // otherwise it selects VCOMP. That decision is translation-unit-local,
        // while MQB's link freshness policy is target-level. Observing every
        // available candidate is conservative: it may cause an extra relink if
        // an unused runtime changes, but it cannot miss the runtime LINK reads.
        return {"vcomp.lib", "vcompd.lib"};
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
