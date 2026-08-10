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
            expect(parsed->discover_sources,
                   "single-source smart discovery should be enabled by default");
            expect(!parsed->build.output_name.has_value(),
                   "output name should remain unset by default");
            expect(!parsed->build.run_after_build,
                   "run mode should remain disabled by default");
            expect(parsed->build.run_arguments.empty(),
                   "run argument list should remain empty by default");
            expect(parsed->library_directories.empty() && parsed->libraries.empty(),
                   "library inputs should be empty by default");
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
        const std::vector arguments{"main.cpp"sv, "--no-discover"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value() && !parsed->discover_sources,
               "--no-discover should disable single-source smart discovery");
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
            "--output"sv,
            "product"sv,
            "--run"sv,
            "--env"sv,
            "portable"sv,
            "--portable-root"sv,
            "tool chain"sv,
            "-Iinclude dir"sv,
            "-D"sv,
            "VALUE=42"sv,
            "-Lvendor libs"sv,
            "--lib-path"sv,
            "other libs"sv,
            "-lmath"sv,
            "--lib"sv,
            "codec.lib"sv,
            "--verbose"sv,
            "--"sv,
            "hello world"sv,
            "--child-option"sv,
            ""sv,
            "a b c"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "multi-source run option set should parse");
        if (parsed) {
            expect(parsed->build.sources.size() == 3,
                   "all explicit sources should be collected");
            if (parsed->build.sources.size() == 3) {
                expect(parsed->build.sources[0] == "main.cpp"
                           && parsed->build.sources[1] == "src/utils.cpp"
                           && parsed->build.sources[2] == "tests/helper.cxx",
                       "source order should remain stable for deterministic linking");
            }
            expect(parsed->discover_sources,
                   "parser keeps discovery policy enabled; main treats multi-source sets as explicit");
            expect(parsed->build.output_name.has_value()
                       && *parsed->build.output_name == "product",
                   "output name should be parsed");
            expect(parsed->build.run_after_build,
                   "run flag should be parsed");
            expect(parsed->build.run_arguments.size() == 4,
                   "all argv elements after -- should be preserved");
            if (parsed->build.run_arguments.size() == 4) {
                expect(parsed->build.run_arguments[0] == "hello world"
                           && parsed->build.run_arguments[1] == "--child-option"
                           && parsed->build.run_arguments[2].empty()
                           && parsed->build.run_arguments[3] == "a b c",
                       "run argv must preserve spaces, leading dashes, and empty arguments");
            }
            expect(parsed->library_directories.size() == 2,
                   "all library search directories should be collected");
            if (parsed->library_directories.size() == 2) {
                expect(parsed->library_directories[0] == "vendor libs"
                           && parsed->library_directories[1] == "other libs",
                       "library search directory order should be preserved");
            }
            expect(parsed->libraries.size() == 2,
                   "all requested libraries should be collected");
            if (parsed->libraries.size() == 2) {
                expect(parsed->libraries[0] == "math" && parsed->libraries[1] == "codec.lib",
                       "requested library order should be preserved");
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
        const std::vector arguments{
            "main.cpp"sv,
            "--output=demo"sv,
            "--lib-path=vendor"sv,
            "--lib=foo"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "attached long forms should parse");
        if (parsed) {
            expect(parsed->build.output_name.has_value() && *parsed->build.output_name == "demo",
                   "attached long output value should be preserved");
            expect(parsed->library_directories.size() == 1
                       && parsed->library_directories.front() == "vendor",
                   "attached long library path should be preserved");
            expect(parsed->libraries.size() == 1 && parsed->libraries.front() == "foo",
                   "attached long library value should be preserved");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "--run"sv, "--"sv, "--no-discover"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "options after -- should become program arguments");
        if (parsed) {
            expect(parsed->discover_sources,
                   "discovery parsing must stop at --");
            expect(parsed->build.run_arguments.size() == 1
                       && parsed->build.run_arguments.front() == "--no-discover",
                   "option-looking argv after -- must be preserved verbatim");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "--"sv, "argument"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "program arguments without --run should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "-o"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "missing output value should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "-L"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "missing library directory should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "-l"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "missing library name should be rejected");
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
