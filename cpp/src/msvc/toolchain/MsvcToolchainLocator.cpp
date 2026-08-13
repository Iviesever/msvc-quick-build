#include "mqb/msvc/MsvcToolchainLocator.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "PortableToolchainDiscovery.hpp"
#include "ToolchainDiscoveryPrimitives.hpp"
#include "VisualStudioToolchainDiscovery.hpp"

namespace mqb::msvc {

namespace fs = std::filesystem;

std::expected<MsvcToolchain, ToolchainError>
MsvcToolchainLocator::discover(const DiscoveryOptions& options) const {
    if (options.preference != ToolchainPreference::visual_studio) {
        for (const auto& portable_root : options.portable_roots) {
            std::error_code error_code;
            if (fs::is_directory(portable_root, error_code)) {
                return detail::discover_portable_toolchain(portable_root, options);
            }
        }

        if (options.preference == ToolchainPreference::portable) {
            return std::unexpected(detail::toolchain_failure(
                ToolchainErrorCode::toolchain_not_found,
                "portable toolchain was requested but no portable_msvc root exists"));
        }
    }

    return detail::discover_visual_studio_toolchain(runner_, options);
}

} // namespace mqb::msvc
