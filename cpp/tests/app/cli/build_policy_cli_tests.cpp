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

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
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
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        TempTree tree{
            .root = fs::temp_directory_path()
                / ("mqb_native_librarian_policy_" + std::to_string(unique)),
        };
        fs::create_directories(tree.root);

        const std::vector arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "/W4"sv,
            "/lib"sv,
            "/EXPORT:mqb_export"sv,
            "/WX"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "native /lib boundary should parse as native argv before project setup");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 4
                       && parsed->compiler_arguments[0] == "/W4"
                       && parsed->compiler_arguments[1] == "/lib"
                       && parsed->compiler_arguments[2] == "/EXPORT:mqb_export"
                       && parsed->compiler_arguments[3] == "/WX",
                   "CLI parser should preserve /lib and its tail until ownership routing");

            auto project = mqb::app::prepare_project(*parsed, tree.root);
            expect(project.has_value(), "project setup should split and route native /lib policy");
            if (project) {
                expect(parsed->compiler_arguments.size() == 1
                           && parsed->compiler_arguments[0] == "/W4",
                       "compiler argv before /lib should remain compiler-owned");
                expect(parsed->librarian_arguments.size() == 2
                           && parsed->librarian_arguments[0] == "/EXPORT:mqb_export"
                           && parsed->librarian_arguments[1] == "/WX",
                       "every native option after /lib should become routed librarian policy in order");
                expect(project->effective.target_kind == mqb::TargetKind::static_library,
                       "native librarian policy should coexist with the typed static target kind");
            }
        }

        const std::vector uppercase_arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "/LIB"sv,
            "/WX"sv,
        };
        auto uppercase = mqb::cli::parse_arguments(uppercase_arguments);
        expect(uppercase.has_value(), "uppercase /LIB boundary should parse before ownership routing");
        if (uppercase) {
            auto project = mqb::app::prepare_project(*uppercase, tree.root);
            expect(project.has_value(), "uppercase /LIB should be accepted as the librarian boundary");
            if (project) {
                expect(uppercase->compiler_arguments.empty()
                           && uppercase->librarian_arguments.size() == 1
                           && uppercase->librarian_arguments.front() == "/WX",
                       "uppercase /LIB should route its tail exclusively to librarian argv");
            }
        }

        const std::vector shorthand_arguments{
            "main.cpp"sv,
            "-l"sv, "kernel32"sv,
            "-luser32"sv,
        };
        auto shorthand = mqb::cli::parse_arguments(shorthand_arguments);
        expect(shorthand.has_value(), "-l library shorthand must remain valid");
        if (shorthand) {
            expect(shorthand->libraries.size() == 2
                       && shorthand->libraries[0] == "kernel32"
                       && shorthand->libraries[1] == "user32",
                   "-l separated and attached forms must retain library shorthand semantics");
        }

        for (const auto rejected : {"-lib"sv, "-LIB"sv}) {
            const std::vector dash_arguments{
                "main.cpp"sv,
                "--type"sv, "static"sv,
                rejected,
                "/WX"sv,
            };
            auto dash = mqb::cli::parse_arguments(dash_arguments);
            expect(!dash, "dash -lib spellings must be rejected instead of colliding with -l");
            if (!dash) {
                expect(dash.error().message.find("use '/lib'") != std::string::npos,
                       "rejected -lib spelling should point users to the slash librarian boundary");
            }
        }

        const std::vector raw_dash_arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "--compiler-arg"sv, "-lib"sv,
            "/WX"sv,
        };
        auto raw_dash = mqb::cli::parse_arguments(raw_dash_arguments);
        expect(raw_dash.has_value(), "raw --compiler-arg should preserve -lib for parameter routing");
        if (raw_dash) {
            auto project = mqb::app::prepare_project(*raw_dash, tree.root);
            expect(!project,
                   "raw -lib must not regain librarian-boundary semantics inside ProjectSetup");
        }

        const std::vector empty_tail_arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "/lib"sv,
        };
        auto empty_tail = mqb::cli::parse_arguments(empty_tail_arguments);
        expect(empty_tail.has_value(), "bare /lib should remain syntactically parseable at CLI stage");
        if (empty_tail) {
            auto project = mqb::app::prepare_project(*empty_tail, tree.root);
            expect(!project && project.error().message.find("requires at least one librarian option") != std::string::npos,
                   "bare /lib should fail closed at ownership routing");
        }

        const std::vector owned_output_arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "/lib"sv,
            "/OUT:hijack.lib"sv,
        };
        auto owned_output = mqb::cli::parse_arguments(owned_output_arguments);
        expect(owned_output.has_value(), "Class A librarian option should reach ownership routing");
        if (owned_output) {
            auto project = mqb::app::prepare_project(*owned_output, tree.root);
            expect(!project && project.error().message.find("/OUT:hijack.lib") != std::string::npos,
                   "Class A /OUT after /lib should be rejected before product execution");
        }

        const std::vector wrong_tool_arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "/lib"sv,
            "/W4"sv,
        };
        auto wrong_tool = mqb::cli::parse_arguments(wrong_tool_arguments);
        expect(wrong_tool.has_value(), "compiler-only option after /lib should parse before classification");
        if (wrong_tool) {
            auto project = mqb::app::prepare_project(*wrong_tool, tree.root);
            expect(!project,
                   "compiler-only option after /lib must fail librarian classification instead of leaking back to cl.exe");
        }

        const std::vector duplicate_boundary_arguments{
            "main.cpp"sv,
            "--type"sv, "static"sv,
            "/lib"sv,
            "/WX"sv,
            "/LIB"sv,
            "/EXPORT:duplicate"sv,
        };
        auto duplicate_boundary = mqb::cli::parse_arguments(duplicate_boundary_arguments);
        expect(duplicate_boundary.has_value(), "duplicate /lib boundary should parse before routing");
        if (duplicate_boundary) {
            auto project = mqb::app::prepare_project(*duplicate_boundary, tree.root);
            expect(!project && project.error().message.find("duplicate native MSVC /lib separator") != std::string::npos,
                   "duplicate /lib boundary should be rejected deterministically");
        }

        const std::string_view help = mqb::cli::usage();
        expect(help.find("/lib <librarian-options...>") != std::string_view::npos,
               "--help contract should expose the native /lib librarian boundary");
        expect(help.find("-lib`/`-LIB` is rejected") != std::string_view::npos,
               "--help contract should expose the chosen dash -lib rejection policy");
        expect(help.find("build.librarian_args") != std::string_view::npos,
               "--help contract should expose the librarian config surface");
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
        write_text(
            widget_source,
            "#include \"widget.hpp\"\n"
            "int widget() { return 0; }\n");

        const std::vector arguments{
            "main.cpp"sv,
            "/UATTACHED"sv,
            "/Iinclude"sv,
            "/DATTACHED=1"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "attached native preprocessor options should parse before project setup");
        if (parsed) {
            expect(parsed->include_directories.empty() && parsed->defines.empty(),
                   "attached /I and /D should remain parameter-engine inputs at the CLI boundary");
            expect(parsed->compiler_arguments.size() == 3,
                   "native preprocessor options should initially remain raw compiler arguments");

            auto project = mqb::app::prepare_project(*parsed, tree.root);
            expect(project.has_value(), "project setup should expose native include semantics");
            if (project) {
                const std::string expected_include = "/I" + path_text(include_root.lexically_normal());
                expect(parsed->compiler_arguments.size() == 3
                           && parsed->compiler_arguments[0] == "/UATTACHED"
                           && parsed->compiler_arguments[1] == expected_include
                           && parsed->compiler_arguments[2] == "/DATTACHED=1",
                       "native preprocessor argv order should survive semantic extraction");
                expect(parsed->include_directories.empty() && parsed->defines.empty(),
                       "native /I and /D metadata must not be re-emitted through structured compiler lists");
                expect(parsed->discovery_include_directories.size() == 1
                           && parsed->discovery_include_directories.front().lexically_normal()
                               == include_root.lexically_normal(),
                       "attached /I should feed an invocation-relative discovery-only include directory");

                auto discovered = mqb::discovery::SourceDiscovery::discover({
                    .project_root = tree.root,
                    .entry = main_source,
                    .include_directories = parsed->discovery_include_directories,
                });
                expect(discovered.has_value(),
                       "smart discovery should accept semantic include directories extracted from native /I");
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