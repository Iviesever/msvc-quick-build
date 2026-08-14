#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Cli.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

void dump_failure(const mqb::process::ProcessResult& result) {
    std::cerr << "exit: " << result.exit_code << '\n'
              << "stdout:\n" << result.stdout_text << '\n'
              << "stderr:\n" << result.stderr_text << '\n';
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string>
run_process(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& working_directory,
    std::vector<std::string> arguments = {}) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = std::move(arguments);
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected("failed to launch process: " + result.error().message);
    }
    return std::move(*result);
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

void verify_parser_contract() {
    using namespace std::string_view_literals;

    {
        const std::vector arguments{"main.cpp"sv, "/W4"sv, "/O2"sv, "/fp:fast"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "slash-prefixed native compiler switches should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 3
                       && parsed->compiler_arguments[0] == "/W4"
                       && parsed->compiler_arguments[1] == "/O2"
                       && parsed->compiler_arguments[2] == "/fp:fast",
                   "native compiler switch order and spelling should be preserved");
        }
    }

    {
        const std::vector arguments{"-W4"sv, "-std:c++20"sv, "main.cpp"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "single-dash MSVC compiler syntax should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 2
                       && parsed->compiler_arguments[0] == "-W4"
                       && parsed->compiler_arguments[1] == "-std:c++20",
                   "single-dash native compiler switches should preserve spelling");
            expect(parsed->build.sources.size() == 1
                       && parsed->build.sources.front() == "main.cpp",
                   "compiler switches before the source must not become sources");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv, "/I"sv, "include dir"sv, "/D"sv, "FEATURE=1"sv, "/W4"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "spaced native /I and /D forms should parse");
        if (parsed) {
            expect(parsed->include_directories.size() == 1
                       && parsed->include_directories.front() == "include dir",
                   "spaced /I should use the structured include path");
            expect(parsed->defines.size() == 1 && parsed->defines.front() == "FEATURE=1",
                   "spaced /D should use the structured define path");
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "/W4",
                   "spaced /I and /D values must not leak into raw compiler argv");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv, "/O2"sv, "/std:c++20"sv, "/link"sv,
            "/STACK:8388608"sv, "/DEBUG:FULL"sv, "/subsystem:console"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "native /link tail should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 2
                       && parsed->compiler_arguments[0] == "/O2"
                       && parsed->compiler_arguments[1] == "/std:c++20",
                   "compiler switches before /link should remain compiler arguments");
            expect(parsed->linker_arguments.size() == 3
                       && parsed->linker_arguments[0] == "/STACK:8388608"
                       && parsed->linker_arguments[1] == "/DEBUG:FULL"
                       && parsed->linker_arguments[2] == "/subsystem:console",
                   "all build argv after /link should enter linker routing verbatim");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "-O2"sv, "-link"sv, "-stack:4194304"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "single-dash -link boundary should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "-O2",
                   "-link must not be mistaken for the MQB -l shorthand");
            expect(parsed->linker_arguments.size() == 1
                       && parsed->linker_arguments.front() == "-stack:4194304",
                   "single-dash linker switches should be preserved after -link");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv, "--run"sv, "/O2"sv, "/link"sv, "/DEBUG:FULL"sv,
            "--"sv, "child argument"sv, "/W4"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "outer -- run boundary should remain valid after /link");
        if (parsed) {
            expect(parsed->build.run_after_build, "--run should remain an MQB option before /link");
            expect(parsed->linker_arguments.size() == 1
                       && parsed->linker_arguments.front() == "/DEBUG:FULL",
                   "linker tail should stop at the outer -- delimiter");
            expect(parsed->build.run_arguments.size() == 2
                       && parsed->build.run_arguments[0] == "child argument"
                       && parsed->build.run_arguments[1] == "/W4",
                   "program argv after -- must remain opaque");
        }
    }

    {
        const std::vector arguments{"/link"sv, "/DEBUG:FULL"sv, "main.cpp"sv};
        expect(!mqb::cli::parse_arguments(arguments), "/link before every source should fail");
    }
    {
        const std::vector arguments{"main.cpp"sv, "/link"sv};
        expect(!mqb::cli::parse_arguments(arguments), "/link without linker options should fail");
    }
    {
        const std::vector arguments{"main.cpp"sv, "/link"sv, "/DEBUG:FULL"sv, "/link"sv};
        expect(!mqb::cli::parse_arguments(arguments), "duplicate /link boundaries should fail");
    }
    {
        const std::vector arguments{"main.cpp"sv, "/link"sv, "/DEBUG:FULL"sv, "--verbose"sv};
        expect(!mqb::cli::parse_arguments(arguments), "MQB long options after /link should fail");
    }
    {
        const std::vector arguments{"main.cpp"sv, "/LINK"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "compiler option spelling should remain case-sensitive");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "/LINK",
                   "uppercase /LINK should flow to the parameter engine, not act as /link");
        }
    }
    {
        const std::vector arguments{"main.cpp"sv, "--wat"sv};
        expect(!mqb::cli::parse_arguments(arguments), "unknown MQB --long options must remain errors");
    }
    {
        const std::vector arguments{"main.cpp"sv, "/I"sv};
        expect(!mqb::cli::parse_arguments(arguments), "bare /I without a directory should fail");
    }
    {
        const std::vector arguments{"main.cpp"sv, "/D"sv};
        expect(!mqb::cli::parse_arguments(arguments), "bare /D without a definition should fail");
    }
}

void verify_candidate_e2e(const fs::path& mqb_executable) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_native_msvc_cli_" + std::to_string(unique))};
    fs::create_directories(tree.root);

    write_text(
        tree.root / "main.cpp",
        "#include <cstdio>\n"
        "#ifndef NATIVE_VALUE\n#error NATIVE_VALUE must be provided\n#endif\n"
        "int main() { std::printf(\"native=%d\\n\", NATIVE_VALUE); return NATIVE_VALUE == 7 ? 0 : 1; }\n");

    mqb::platform::windows::WindowsProcessRunner runner;
    const fs::path map_file = tree.root / "native.map";
    auto build = run_process(
        runner,
        mqb_executable,
        tree.root,
        {
            "main.cpp",
            "--env", "vs",
            "--no-discover",
            "-o", "native_msvc",
            "/D", "NATIVE_VALUE=7",
            "/std:c++20",
            "/MT",
            "/W4",
            "/O2",
            "/link",
            "/SUBSYSTEM:CONSOLE",
            "/MAP:" + path_text(map_file),
            "/DEBUG:FULL",
        });
    expect(build.has_value(), "candidate native-syntax build should launch");
    if (build) {
        if (build->exit_code != 0) dump_failure(*build);
        expect(build->exit_code == 0,
               "candidate should compile and link direct native compiler/linker syntax");
    }
    expect(fs::is_regular_file(map_file), "direct native /MAP should reach link.exe");

    const fs::path executable = tree.root / ".mqb" / "bin" / "native_msvc.exe";
    auto run = run_process(runner, executable, tree.root);
    expect(run.has_value(), "native-syntax executable should launch");
    if (run) {
        if (run->exit_code != 0) dump_failure(*run);
        expect(run->exit_code == 0, "native-syntax executable should succeed");
        expect(run->stdout_text.find("native=7") != std::string::npos,
               "direct /D syntax should affect executable behavior");
    }

    auto conflict = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "--env", "vs", "--no-discover", "--runtime", "MT", "/MD"});
    expect(conflict.has_value(), "native typed/raw conflict invocation should launch");
    if (conflict) {
        expect(conflict->exit_code == 2,
               "direct /MD should conflict with same-layer --runtime MT before cl.exe");
        expect(conflict->stderr_text.find("conflicting typed and native MSVC values for runtime library")
                   != std::string::npos,
               "direct native semantic conflict should preserve parameter-engine diagnostics");
    }

    auto owned = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "--env", "vs", "--no-discover", "/Foescape.obj"});
    expect(owned.has_value(), "native MQB-owned escape invocation should launch");
    if (owned) {
        expect(owned->exit_code == 2, "direct /Fo should fail before cl.exe");
        expect(owned->stderr_text.find("MQB-owned") != std::string::npos,
               "direct /Fo should retain ownership-aware diagnostics");
    }

    auto removed = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "--env", "vs", "--no-discover", "/D", "NATIVE_VALUE=7",
         "/link", "/DEBUG:FASTLINK"});
    expect(removed.has_value(), "removed linker option invocation should launch");
    if (removed) {
        expect(removed->exit_code == 2, "direct removed /DEBUG:FASTLINK should fail closed");
        expect(removed->stderr_text.find("DEBUG:FASTLINK") != std::string::npos,
               "removed native linker option should report the rejected switch");
    }
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_native_msvc_cli_e2e_tests <mqb-executable>\n";
        return 2;
    }

    verify_parser_contract();
    verify_candidate_e2e(fs::path{argv[1]});

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_native_msvc_cli_e2e_tests passed\n";
    return 0;
}
