#include "mqb/msvc/MsvcParameterCapabilities.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace mqb::msvc {
namespace {

struct ParsedVersion {
    int major{};
    int minor{};
    bool valid{};
};

[[nodiscard]] ParsedVersion parse_version(const std::string_view version) noexcept {
    ParsedVersion parsed;
    std::size_t index = 0;

    const auto read_component = [&](int& value) mutable {
        if (index >= version.size() || !std::isdigit(static_cast<unsigned char>(version[index]))) {
            return false;
        }
        value = 0;
        while (index < version.size()
               && std::isdigit(static_cast<unsigned char>(version[index]))) {
            value = (value * 10) + (version[index] - '0');
            ++index;
        }
        return true;
    };

    if (!read_component(parsed.major)) return parsed;
    if (index >= version.size() || version[index] != '.') return parsed;
    ++index;
    if (!read_component(parsed.minor)) return parsed;
    parsed.valid = true;
    return parsed;
}

[[nodiscard]] std::string upper_ascii(const std::string_view value) {
    std::string result{value};
    for (char& ch : result) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return result;
}

[[nodiscard]] std::string_view option_body(const std::string_view argument) noexcept {
    if (argument.size() >= 2 && (argument.front() == '/' || argument.front() == '-')) {
        return argument.substr(1);
    }
    return {};
}

[[nodiscard]] ParameterCapability conditional_capability(
    const bool version_known,
    const bool changed,
    const ParameterLifecycle changed_lifecycle,
    const std::string_view guidance) noexcept {
    if (!version_known) {
        return ParameterCapability{
            .lifecycle = ParameterLifecycle::indeterminate,
            .guidance = "MQB cannot determine this option's lifecycle because the MSVC tools version is not parseable",
        };
    }
    return ParameterCapability{
        .lifecycle = changed ? changed_lifecycle : ParameterLifecycle::active,
        .guidance = changed ? guidance : std::string_view{},
    };
}

} // namespace

bool MsvcParameterCapabilities::is_visual_studio_2026_or_later(
    const std::string_view vc_tools_version) noexcept {
    const ParsedVersion version = parse_version(vc_tools_version);
    if (!version.valid) return false;
    if (version.major != 14) return version.major > 14;
    return version.minor >= 50;
}

ParameterCapability MsvcParameterCapabilities::inspect(
    const ParameterTool tool,
    const std::string_view argument,
    const std::string_view vc_tools_version) noexcept {
    const ParsedVersion parsed = parse_version(vc_tools_version);
    const bool version_known = parsed.valid;
    const bool vs2026_or_later = version_known
        && (parsed.major > 14 || (parsed.major == 14 && parsed.minor >= 50));
    const std::string_view body = option_body(argument);

    if (tool == ParameterTool::compiler && body == "await") {
        return conditional_capability(
            version_known,
            vs2026_or_later,
            ParameterLifecycle::deprecated,
            "/await is deprecated starting with Visual Studio 2026; prefer standard C++20 coroutines or /await:strict for earlier language modes");
    }

    if (tool == ParameterTool::linker && upper_ascii(body) == "DEBUG:FASTLINK") {
        return conditional_capability(
            version_known,
            vs2026_or_later,
            ParameterLifecycle::removed,
            "/DEBUG:FASTLINK is removed starting with Visual Studio 2026; use /DEBUG:FULL");
    }

    return ParameterCapability{};
}

std::string_view to_string(const ParameterLifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case ParameterLifecycle::active: return "active";
    case ParameterLifecycle::deprecated: return "deprecated";
    case ParameterLifecycle::removed: return "removed";
    case ParameterLifecycle::indeterminate: return "indeterminate";
    }
    return "unknown";
}

} // namespace mqb::msvc
