#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>
#include <vector>

#include "Cli.hpp"
#include "../../../src/app/project/ProjectSetup.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"

namespace {
namespace fs = std::filesystem;
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

[[nodiscard]] bool contains_source(
    const std::vector<fs::path>& sources,
    const fs::path& source) {
    const auto normalized = source.lexically_normal();
    for (const auto& candidate : sources) {
        if (candidate.lexically_normal() == normalized) return true;
    }
    return false;
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};
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
        const std::vector<std::vector<std::string_view>> autoJobsCases{
            {"main.cpp"sv, "-j"sv, "auto"sv},
            {"main.cpp"sv, "-jauto"sv},
            {"main.cpp"sv, "--jobs"sv, "auto"sv},
            {"main.cpp"sv, "--jobs=auto"sv},
        };
        for (const auto& arguments : autoJobsCases) {
            auto parsed = mqb::cli::parse_arguments(arguments);
            expect(parsed.has_value(), "every documented auto jobs spelling should parse");
            if (parsed) {
                expect(parsed->jobs.has_value() && parsed->jobs->is_automatic(),
                       "auto jobs spelling should preserve typed automatic policy");
            }
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

    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        TempTree tree{
            .root = fs::temp_directory_path()
                / ("mqb_native_include_policy_" + std::to_string(unique)),
        };
        const fs::path main_source = tree.root / "main.cpp";
        const fs::path include_root = tree.root / "include";
        const fs::path widget_source = include_root / "widget.cpp";
        write_text(
            main_source,
            "#include <widget.hpp>\n"
            "int main() { return widget(); }\n");
        write_text(include_root / "widget.hpp", "#pragma once\nint widget();\n");
        write_text(widget_source, "int widget() { return 0; }\n");

        const std::vector arguments{"main.cpp"sv, "/Iinclude"sv, "/DATTACHED=1"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "attached native preprocessor options should parse before project setup");
        if (parsed) {
            expect(parsed->include_directories.empty() && parsed->defines.empty(),
                   "attached /I and /D should remain parameter-engine inputs at the CLI boundary");
            expect(parsed->compiler_arguments.size() == 2,
                   "attached preprocessor options should initially remain native compiler arguments");

            auto project = mqb::app::prepare_project(*parsed, tree.root);
            expect(project.has_value(), "project setup should normalize attached native preprocessor policy");
            if (project) {
                expect(parsed->compiler_arguments.empty(),
                       "normalized native /I and /D should leave opaque compiler passthrough");
                expect(parsed->include_directories.size() == 1
                           && parsed->include_directories.front().lexically_normal()
                               == include_root.lexically_normal(),
                       "attached /I should become an invocation-relative structured include directory");
                expect(parsed->defines.size() == 1 && parsed->defines.front() == "ATTACHED=1",
                       "attached /D should become a structured preprocessor definition");

                auto discovered = mqb::discovery::SourceDiscovery::discover({
                    .project_root = tree.root,
                    .entry = main_source,
                    .include_directories = parsed->include_directories,
                });
                expect(discovered.has_value(),
                       "smart discovery should accept include directories normalized from native /I");
                if (discovered) {
                    expect(contains_source(discovered->sources, widget_source),
                           "native attached /I should connect include-root-owned widget.cpp in discovery");
                }
            }
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_build_policy_cli_tests passed\n";
    return 0;
}
