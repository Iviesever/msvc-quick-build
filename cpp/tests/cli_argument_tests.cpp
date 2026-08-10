#include <iostream>
#include <string_view>
#include <vector>

#include "Cli.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"

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
    using namespace std::string_view_literals;

    {
        const std::vector<std::string_view> arguments{};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "missing source should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "single source should parse");
        if (parsed) {
            expect(parsed->build.sources.size() == 1 && parsed->build.sources.front() == "main.cpp",
                   "single source should be preserved in ordered source list");
            expect(parsed->build.configuration == mqb::BuildConfiguration::debug,
                   "default configuration should be debug");
            expect(parsed->build.architecture == mqb::Architecture::x64,
                   "default architecture should be x64");
            expect(parsed->build.standard == mqb::CppStandard::cpp23,
                   "default standard should be C++23");
            expect(parsed->toolchain_preference == mqb::msvc::ToolchainPreference::automatic,
                   "default toolchain preference should be automatic");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv,
            "src/utils.cpp"sv,
            "tests/helper.cxx"sv,
            "--release"sv,
            "--std"sv,
            "latest"sv,
            "--x86"sv,
            "--env"sv,
            "portable"sv,
            "--portable-root"sv,
            "tool chain"sv,
            "-Iinclude dir"sv,
            "-D"sv,
            "VALUE=42"sv,
            "--verbose"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "multi-source option set should parse");
        if (parsed) {
            expect(parsed->build.sources.size() == 3,
                   "all explicit sources should be collected");
            if (parsed->build.sources.size() == 3) {
                expect(parsed->build.sources[0] == "main.cpp"
                           && parsed->build.sources[1] == "src/utils.cpp"
                           && parsed->build.sources[2] == "tests/helper.cxx",
                       "source order should remain stable for deterministic linking");
            }
            expect(parsed->build.configuration == mqb::BuildConfiguration::release,
                   "release flag should override default");
            expect(parsed->build.architecture == mqb::Architecture::x86,
                   "x86 flag should be parsed");
            expect(parsed->build.standard == mqb::CppStandard::latest,
                   "latest standard should be parsed");
            expect(parsed->toolchain_preference == mqb::msvc::ToolchainPreference::portable,
                   "portable toolchain preference should be parsed");
            expect(parsed->portable_roots.size() == 1,
                   "portable root should be collected");
            expect(parsed->include_directories.size() == 1,
                   "attached include directory should be collected");
            expect(parsed->defines.size() == 1 && parsed->defines.front() == "VALUE=42",
                   "separate define value should be collected");
            expect(parsed->verbose, "verbose flag should be parsed");
        }
    }

    {
        const std::vector arguments{"--help"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value() && parsed->show_help,
               "help should not require a source file");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--wat"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "unknown options should be rejected");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_cli_argument_tests passed\n";
    return 0;
}
