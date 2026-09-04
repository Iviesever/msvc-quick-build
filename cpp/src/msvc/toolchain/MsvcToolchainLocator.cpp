#include "mqb/msvc/MsvcToolchainLocator.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

#include "PortableToolchainDiscovery.hpp"
#include "ToolchainDiscoveryPrimitives.hpp"
#include "VisualStudioToolchainCache.hpp"
#include "VisualStudioToolchainDiscovery.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/msvc/MsvcToolchainEnvironmentIdentity.hpp"

namespace mqb::msvc {

namespace fs = std::filesystem;

std::expected<MsvcToolchain, ToolchainError>
MsvcToolchainLocator::discover(const DiscoveryOptions& options) const {
    diagnostics::ScopedPerformancePhase phase{
        diagnostics::PerformancePhase::toolchain_discovery};

    if (options.preference != ToolchainPreference::visual_studio) {
        for (const auto& portable_root : options.portable_roots) {
            std::error_code error_code;
            if (fs::is_directory(portable_root, error_code)) {
                auto portable = detail::discover_portable_toolchain(portable_root, options);
                if (portable) seal_compiler_environment_identity(*portable);
                return portable;
            }
        }

        if (options.preference == ToolchainPreference::portable) {
            return std::unexpected(detail::toolchain_failure(
                ToolchainErrorCode::toolchain_not_found,
                "portable toolchain was requested but no portable_msvc root exists"));
        }
    }

    const auto cache_file = detail::visual_studio_toolchain_cache_file(options);
    if (cache_file) {
        if (auto cached = detail::reuse_visual_studio_toolchain_cache(*cache_file, options)) {
            if (cached_visual_studio_environment_is_fresh(*cached)) {
                seal_compiler_environment_identity(*cached);
                return std::move(*cached);
            }
        }
    }

    if (auto ambient = detail::adopt_ambient_visual_studio_toolchain(runner_, options)) {
        if (cache_file) {
            detail::save_visual_studio_toolchain_cache_best_effort(
                *cache_file,
                options,
                *ambient);
        }
        seal_compiler_environment_identity(*ambient);
        return std::move(*ambient);
    }

    auto discovered = detail::discover_visual_studio_toolchain(runner_, options);
    if (discovered && cache_file) {
        // Persist raw compiler-binary evidence. Compiler environment identity is
        // sealed only after the persistent discovery cache is written so its
        // existing binary-stamp validation remains a pure executable check.
        detail::save_visual_studio_toolchain_cache_best_effort(
            *cache_file,
            options,
            *discovered);
    }
    if (discovered) seal_compiler_environment_identity(*discovered);
    return discovered;
}

} // namespace mqb::msvc
