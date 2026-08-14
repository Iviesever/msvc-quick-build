#include <iostream>
#include <string_view>

#include "mqb/msvc/MsvcParameterCapabilities.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using mqb::msvc::MsvcParameterCapabilities;
    using mqb::msvc::ParameterLifecycle;
    using mqb::msvc::ParameterTool;

    expect(!MsvcParameterCapabilities::is_visual_studio_2026_or_later("14.44.35207"),
           "v143-era 14.44 toolsets must remain pre-VS2026");
    expect(MsvcParameterCapabilities::is_visual_studio_2026_or_later("14.50.35717"),
           "14.50 is the VS2026/v145 lifecycle boundary");
    expect(MsvcParameterCapabilities::is_visual_studio_2026_or_later("14.51.36100"),
           "14.51 must remain VS2026-or-later");
    expect(!MsvcParameterCapabilities::is_visual_studio_2026_or_later("unknown"),
           "unparseable versions must not be guessed as VS2026");

    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::compiler, "/await", "14.44.35207").lifecycle
               == ParameterLifecycle::active,
           "/await remains active before VS2026");
    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::compiler, "/await", "14.50.35717").lifecycle
               == ParameterLifecycle::deprecated,
           "/await is deprecated starting with VS2026");
    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::compiler, "/await:strict", "14.51.36100").lifecycle
               == ParameterLifecycle::active,
           "/await:strict is not part of the /await deprecation rule");
    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::compiler, "/await", "unknown").lifecycle
               == ParameterLifecycle::indeterminate,
           "conditional compiler lifecycle must fail closed on unknown toolset versions");

    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::linker, "/DEBUG:FASTLINK", "14.44.35207").lifecycle
               == ParameterLifecycle::active,
           "/DEBUG:FASTLINK remains available on pre-VS2026 toolsets");
    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::linker, "/debug:fastlink", "14.50.35717").lifecycle
               == ParameterLifecycle::removed,
           "/DEBUG:FASTLINK is removed starting with VS2026");
    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::linker, "/DEBUG:FASTLINK", "unknown").lifecycle
               == ParameterLifecycle::indeterminate,
           "conditional linker lifecycle must fail closed on unknown toolset versions");

    expect(MsvcParameterCapabilities::inspect(
               ParameterTool::compiler, "/W4", "unknown").lifecycle
               == ParameterLifecycle::active,
           "non-conditional options do not depend on toolset version parsing");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_parameter_capabilities_tests passed\n";
    return 0;
}
