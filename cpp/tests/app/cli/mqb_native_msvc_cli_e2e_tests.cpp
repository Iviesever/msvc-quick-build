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

void force_newer_timestamp(const fs::path& path) {
    std::error_code error_code;
    const auto current = fs::last_write_time(path, error_code);
    if (!error_code) {
        fs::last_write_time(path, current + std::chrono::seconds{2}, error_code);
    }
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
        const std::vector arguments{"build"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "mqb build should parse without an explicit source");
        if (parsed) {
            expect(parsed->command == mqb::cli::Command::build,
                   "build should record command intent");
            expect(parsed->build.sources.empty(),
                   "build should defer omitted source resolution until project setup");
            expect(!parsed->build.run_after_build,
                   "build command should not request execution");
        }
    }

    {
        const std::vector arguments{"run"sv, "--"sv, "child"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "mqb run should parse without an explicit source");
        if (parsed) {
            expect(parsed->command == mqb::cli::Command::run,
                   "run should record command intent");
            expect(parsed->build.run_after_build,
                   "run command should reuse build-after-run execution policy");
            expect(parsed->build.sources.empty(),
                   "run should defer omitted source resolution until project setup");
            expect(parsed->build.run_arguments.size() == 1
                       && parsed->build.run_arguments.front() == "child",
                   "run command should make the outer -- delimiter immediately useful");
        }
    }

    {
        const std::vector arguments{"build"sv, "main.cpp"sv, "/O2"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "build with an explicit source should parse");
        if (parsed) {
            expect(parsed->build.sources.size() == 1
                       && parsed->build.sources.front() == "main.cpp",
                   "explicit source should remain authoritative under build command");
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "/O2",
                   "build command should preserve native compiler syntax");
        }
    }

    {
        const std::vector arguments{
            "build"sv, "/O2"sv, "/link"sv, "/DEBUG:FULL"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(),
               "build command should allow native compiler/linker policy before default entry resolves");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "/O2",
                   "source-less build command should preserve compiler policy");
            expect(parsed->linker_arguments.size() == 1
                       && parsed->linker_arguments.front() == "/DEBUG:FULL",
                   "source-less build command should preserve /link tail");
        }
    }

    {
        const std::vector arguments{"build"sv, "--run"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "mqb build --run should fail instead of creating two command spellings");
    }

    {
        const std::vector arguments{"main.cpp"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "source-first compatibility syntax should remain valid");
        if (parsed) {
            expect(parsed->command == mqb::cli::Command::direct,
                   "source-first syntax should remain direct mode");
        }
    }

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
            expect(parsed->include_directories.empty() && parsed->defines.empty(),
                   "spaced native /I and /D should remain raw at the CLI boundary");
            expect(parsed->compiler_arguments.size() == 5
                       && parsed->compiler_arguments[0] == "/I"
                       && parsed->compiler_arguments[1] == "include dir"
                       && parsed->compiler_arguments[2] == "/D"
                       && parsed->compiler_arguments[3] == "FEATURE=1"
                       && parsed->compiler_arguments[4] == "/W4",
                   "spaced native /I and /D should preserve option/operand ordering for parameter routing");
            expect(parsed->build.sources.size() == 1
                       && parsed->build.sources.front() == "main.cpp",
                   "native /I and /D operands must not become positional sources");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "/Iinclude"sv, "/DATTACHED=1"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "attached native /I and /D forms should parse");
        if (parsed) {
            expect(parsed->include_directories.empty() && parsed->defines.empty(),
                   "attached native options should stay out of CLI-owned structured parsing");
            expect(parsed->compiler_arguments.size() == 2
                       && parsed->compiler_arguments[0] == "/Iinclude"
                       && parsed->compiler_arguments[1] == "/DATTACHED=1",
                   "attached /I and /D should remain opaque native tokens for parameter routing");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "/FI"sv, "forced header.hpp"sv, "/W4"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "split native /FI form should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 3
                       && parsed->compiler_arguments[0] == "/FI"
                       && parsed->compiler_arguments[1] == "forced header.hpp"
                       && parsed->compiler_arguments[2] == "/W4",
                   "split /FI must preserve option/operand ordering for parameter/discovery routing");
            expect(parsed->build.sources.size() == 1
                       && parsed->build.sources.front() == "main.cpp",
                   "native /FI operand must not become a positional source");
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
        const std::vector arguments{"main.cpp"sv, "@hidden.rsp"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "native response syntax should reach parameter routing");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "@hidden.rsp",
                   "@response must not be misclassified as a positional source");
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

void verify_native_candidate_e2e(const fs::path& mqb_executable) {
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

    auto response = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "--env", "vs", "--no-discover", "@hidden.rsp"});
    expect(response.has_value(), "direct compiler response syntax invocation should launch");
    if (response) {
        expect(response->exit_code == 2, "direct @response should fail closed before cl.exe");
        expect(response->stderr_text.find("response files") != std::string::npos,
               "direct @response should retain parameter-engine safety diagnostics");
    }

    const fs::path forced_root = tree.root / "forced-include";
    const fs::path forced_header = forced_root / "forced.hpp";
    const fs::path forced_source = forced_root / "main.cpp";
    write_text(forced_header, "#pragma once\n#define FORCED_EXIT_CODE 17\n");
    write_text(forced_source, "int main() { return FORCED_EXIT_CODE; }\n");

    const std::vector<std::string> forced_build_args{
        "main.cpp", "--env", "vs", "-o", "forced_include", "/FI", "forced.hpp"};
    auto forced_build = run_process(
        runner,
        mqb_executable,
        forced_root,
        forced_build_args);
    expect(forced_build.has_value(), "native /FI smart-discovery build should launch");
    if (forced_build) {
        if (forced_build->exit_code != 0) dump_failure(*forced_build);
        expect(forced_build->exit_code == 0,
               "indexed native /FI should compile successfully with smart discovery enabled");
    }

    const fs::path forced_executable =
        forced_root / ".mqb" / "bin" / "forced_include.exe";
    auto forced_run = run_process(runner, forced_executable, forced_root);
    expect(forced_run.has_value(), "native /FI executable should launch");
    if (forced_run) {
        expect(forced_run->exit_code == 17,
               "initial forced header should affect compiled executable behavior");
    }

    write_text(forced_header, "#pragma once\n#define FORCED_EXIT_CODE 23\n");
    force_newer_timestamp(forced_header);
    auto forced_rebuild = run_process(
        runner,
        mqb_executable,
        forced_root,
        forced_build_args);
    expect(forced_rebuild.has_value(), "native /FI rebuild after header mutation should launch");
    if (forced_rebuild) {
        if (forced_rebuild->exit_code != 0) dump_failure(*forced_rebuild);
        expect(forced_rebuild->exit_code == 0,
               "forced-header mutation should remain a successful incremental build");
    }

    auto forced_rerun = run_process(runner, forced_executable, forced_root);
    expect(forced_rerun.has_value(), "rebuilt native /FI executable should launch");
    if (forced_rerun) {
        expect(forced_rerun->exit_code == 23,
               "forced header must participate in compile freshness instead of reusing stale object state");
    }
}

void verify_command_candidate_e2e(const fs::path& mqb_executable) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_command_cli_" + std::to_string(unique))};
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    const fs::path conventional = tree.root / "conventional";
    write_text(
        conventional / "main.cpp",
        "#include <cstdio>\n"
        "int main(int argc, char** argv) {\n"
        "  std::printf(\"conventional:%s\\n\", argc > 1 ? argv[1] : \"none\");\n"
        "  return 0;\n"
        "}\n");

    auto build = run_process(
        runner,
        mqb_executable,
        conventional,
        {"build", "--env", "vs", "--no-discover", "-o", "command_build", "/W4"});
    expect(build.has_value(), "source-less mqb build should launch");
    if (build) {
        if (build->exit_code != 0) dump_failure(*build);
        expect(build->exit_code == 0,
               "mqb build should resolve the unique conventional main.cpp and build it");
    }
    expect(fs::is_regular_file(conventional / ".mqb/bin/command_build.exe"),
           "mqb build should produce the conventional-entry target");

    auto run = run_process(
        runner,
        mqb_executable,
        conventional,
        {"run", "--env", "vs", "--no-discover", "-o", "command_run", "--", "hello"});
    expect(run.has_value(), "source-less mqb run should launch");
    if (run) {
        if (run->exit_code != 0) dump_failure(*run);
        expect(run->exit_code == 0, "mqb run should build and run the conventional entry");
        expect(run->stdout_text.find("conventional:hello") != std::string::npos,
               "mqb run should preserve child argv after --");
    }

    const fs::path configured = tree.root / "configured";
    write_text(
        configured / "main.cpp",
        "#include <cstdio>\nint main() { std::puts(\"wrong-conventional\"); return 0; }\n");
    write_text(
        configured / "app/start.cpp",
        "#include <cstdio>\n"
        "int main(int argc, char** argv) {\n"
        "  std::printf(\"configured:%s\\n\", argc > 1 ? argv[1] : \"none\");\n"
        "  return 0;\n"
        "}\n");
    write_text(
        configured / "explicit.cpp",
        "#include <cstdio>\n"
        "int main(int argc, char** argv) {\n"
        "  std::printf(\"explicit:%s\\n\", argc > 1 ? argv[1] : \"none\");\n"
        "  return 0;\n"
        "}\n");
    write_text(
        configured / "mqb.json",
        R"json({
  "version": 1,
  "build": {
    "entry": "app/start.cpp",
    "output": "configured_target"
  }
})json");

    auto configured_run = run_process(
        runner,
        mqb_executable,
        configured,
        {"run", "--env", "vs", "--no-discover", "--", "cfg"});
    expect(configured_run.has_value(), "configured-entry mqb run should launch");
    if (configured_run) {
        if (configured_run->exit_code != 0) dump_failure(*configured_run);
        expect(configured_run->exit_code == 0,
               "mqb run should build and execute build.entry from mqb.json");
        expect(configured_run->stdout_text.find("configured:cfg") != std::string::npos,
               "build.entry should take precedence over a conventional main.cpp");
        expect(configured_run->stdout_text.find("wrong-conventional") == std::string::npos,
               "configured build.entry should prevent conventional fallback selection");
    }

    auto explicit_run = run_process(
        runner,
        mqb_executable,
        configured,
        {"run", "explicit.cpp", "--env", "vs", "--no-discover", "-o", "explicit_target",
         "--", "chosen"});
    expect(explicit_run.has_value(), "explicit-source mqb run should launch");
    if (explicit_run) {
        if (explicit_run->exit_code != 0) dump_failure(*explicit_run);
        expect(explicit_run->exit_code == 0,
               "explicit source should remain valid when build.entry is configured");
        expect(explicit_run->stdout_text.find("explicit:chosen") != std::string::npos,
               "explicit source should take precedence over build.entry");
    }

    const fs::path ambiguous = tree.root / "ambiguous";
    write_text(ambiguous / "main.cpp", "int main() { return 0; }\n");
    write_text(ambiguous / "src/main.cpp", "int main() { return 0; }\n");
    auto ambiguity = run_process(
        runner,
        mqb_executable,
        ambiguous,
        {"build", "--env", "vs", "--no-discover"});
    expect(ambiguity.has_value(), "ambiguous default-entry build should launch");
    if (ambiguity) {
        expect(ambiguity->exit_code == 2,
               "multiple conventional default entries should fail before toolchain execution");
        expect(ambiguity->stderr_text.find("multiple conventional default entries")
                   != std::string::npos,
               "ambiguous default-entry failure should explain how to disambiguate");
    }

    const fs::path absent = tree.root / "absent";
    fs::create_directories(absent);
    auto no_entry = run_process(
        runner,
        mqb_executable,
        absent,
        {"build", "--env", "vs", "--no-discover"});
    expect(no_entry.has_value(), "missing default-entry build should launch");
    if (no_entry) {
        expect(no_entry->exit_code == 2,
               "zero conventional default entries should fail before toolchain execution");
        expect(no_entry->stderr_text.find("no default entry found") != std::string::npos,
               "zero-candidate failure should explain how to provide an entry");
    }

    const fs::path missing_configured = tree.root / "missing-configured";
    write_text(missing_configured / "main.cpp", "int main() { return 0; }\n");
    write_text(
        missing_configured / "mqb.json",
        R"json({"version":1,"build":{"entry":"missing.cpp"}})json");
    auto missing = run_process(
        runner,
        mqb_executable,
        missing_configured,
        {"build", "--env", "vs", "--no-discover"});
    expect(missing.has_value(), "missing configured-entry build should launch");
    if (missing) {
        expect(missing->exit_code == 2,
               "invalid configured build.entry should fail instead of falling back to main.cpp");
        expect(missing->stderr_text.find("configured build.entry does not exist")
                   != std::string::npos,
               "missing configured entry should retain a dedicated path diagnostic");
    }
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_native_msvc_cli_e2e_tests <mqb-executable>\n";
        return 2;
    }

    verify_parser_contract();
    verify_native_candidate_e2e(fs::path{argv[1]});
    verify_command_candidate_e2e(fs::path{argv[1]});

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_native_msvc_cli_e2e_tests passed\n";
    return 0;
}