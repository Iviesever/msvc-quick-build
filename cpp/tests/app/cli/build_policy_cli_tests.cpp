#include <iostream>
#include <string_view>
#include <vector>

#include "Cli.hpp"

namespace {
int failures = 0;
void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
} // namespace

int main() {
    using namespace std::string_view_literals;

    {
        const std::vector arguments{
            "main.cpp"sv,
            "--compiler-arg"sv, "/W4"sv,
            "--compiler-arg=/WX"sv,
            "--linker-arg"sv, "/MAP:one.map"sv,
            "--linker-arg=/OPT:NOREF"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "native raw build-policy options should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 2
                       && parsed->compiler_arguments[0] == "/W4"
                       && parsed->compiler_arguments[1] == "/WX",
                   "compiler arguments should preserve occurrence order");
            expect(parsed->linker_arguments.size() == 2
                       && parsed->linker_arguments[0] == "/MAP:one.map"
                       && parsed->linker_arguments[1] == "/OPT:NOREF",
                   "linker arguments should preserve occurrence order");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv,
            "--type"sv, "dll"sv,
            "--runtime"sv, "MT"sv,
            "--ltcg"sv,
            "--subsystem=windows"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "typed native target/runtime/LTCG/subsystem options should parse");
        if (parsed) {
            expect(parsed->target_kind_override == mqb::TargetKind::dynamic_library,
                   "--type dll should select typed DLL output");
            expect(parsed->build.target_kind == mqb::TargetKind::dynamic_library,
                   "typed DLL selection should also update BuildRequest");
            expect(parsed->runtime_override == mqb::RuntimeLibrary::mt,
                   "--runtime MT should select static release CRT");
            expect(parsed->ltcg_override == true,
                   "--ltcg should enable the coupled LTCG policy");
            expect(parsed->subsystem_override == mqb::LinkSubsystem::windows,
                   "--subsystem=windows should select Windows subsystem");
        }
    }

    {
        const std::vector<std::vector<std::string_view>> legacyCases{
            {"main.cpp"sv, "-type"sv, "dll"sv},
            {"main.cpp"sv, "-runtime"sv, "MDd"sv},
            {"main.cpp"sv, "-ltcg"sv},
            {"main.cpp"sv, "-subsystem"sv, "console"sv},
            {"main.cpp"sv, "-flags"sv, "/W4"sv},
            {"main.cpp"sv, "-link_flags"sv, "/MAP:legacy.map"sv},
        };
        for (const auto& arguments : legacyCases) {
            auto parsed = mqb::cli::parse_arguments(arguments);
            expect(!parsed, "PowerShell-era build-policy alias should be rejected");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "--ltcg"sv, "--no-ltcg"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value() && parsed->ltcg_override == false,
               "later --no-ltcg should override earlier CLI enablement");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--type=exe"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value() && parsed->target_kind_override == mqb::TargetKind::executable,
               "--type=exe should explicitly select executable output");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--type"sv, "static"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value() && parsed->target_kind_override == mqb::TargetKind::static_library,
               "--type static should select the librarian-backed target kind");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--runtime"sv, "dynamic"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "unknown runtime should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--subsystem"sv, "gui"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "unknown subsystem should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--compiler-arg"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "missing raw compiler argument should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "--linker-arg="sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "empty raw linker argument should be rejected");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_build_policy_cli_tests passed\n";
    return 0;
}
