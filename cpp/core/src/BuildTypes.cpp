#include "mqb/core/BuildTypes.hpp"

namespace mqb {

std::string_view to_string(const BuildConfiguration value) noexcept {
    switch (value) {
    case BuildConfiguration::debug:
        return "debug";
    case BuildConfiguration::release:
        return "release";
    }
    return "unknown";
}

std::string_view to_string(const Architecture value) noexcept {
    switch (value) {
    case Architecture::x86:
        return "x86";
    case Architecture::x64:
        return "x64";
    }
    return "unknown";
}

std::string_view to_string(const CppStandard value) noexcept {
    switch (value) {
    case CppStandard::cpp20:
        return "c++20";
    case CppStandard::cpp23:
        return "c++23";
    case CppStandard::latest:
        return "latest";
    }
    return "unknown";
}

} // namespace mqb
