#pragma once

#include <string_view>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace mqb::msvc {

enum class ParameterLifecycle {
    active,
    deprecated,
    removed,
    indeterminate,
};

struct ParameterCapability {
    ParameterLifecycle lifecycle{ParameterLifecycle::active};
    std::string_view guidance;
};

class MsvcParameterCapabilities {
public:
    [[nodiscard]] static ParameterCapability inspect(
        ParameterTool tool,
        std::string_view argument,
        std::string_view vc_tools_version) noexcept;

    [[nodiscard]] static bool is_visual_studio_2026_or_later(
        std::string_view vc_tools_version) noexcept;
};

[[nodiscard]] std::string_view to_string(ParameterLifecycle lifecycle) noexcept;

} // namespace mqb::msvc
