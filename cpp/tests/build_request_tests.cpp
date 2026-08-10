#include <iostream>
#include <string_view>

#include "mqb/core/BuildRequest.hpp"
#include "mqb/core/BuildTypes.hpp"

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
    const mqb::BuildRequest request{};

    expect(request.sources.empty(), "source list should be empty by default");
    expect(request.configuration == mqb::BuildConfiguration::debug,
           "default configuration should be debug");
    expect(request.architecture == mqb::Architecture::x64,
           "default architecture should be x64");
    expect(request.standard == mqb::CppStandard::cpp23,
           "default C++ standard should be C++23");
    expect(!request.run_after_build,
           "run_after_build should be false by default");

    expect(mqb::to_string(mqb::BuildConfiguration::release) == "release",
           "release configuration should stringify correctly");
    expect(mqb::to_string(mqb::Architecture::x86) == "x86",
           "x86 architecture should stringify correctly");
    expect(mqb::to_string(mqb::CppStandard::latest) == "latest",
           "latest standard should stringify correctly");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_core_tests passed\n";
    return 0;
}
