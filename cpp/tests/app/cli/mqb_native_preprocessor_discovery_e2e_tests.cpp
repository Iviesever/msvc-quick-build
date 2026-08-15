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

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_native_preprocessor_discovery_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path()
            / ("mqb_native_include_discovery_" + std::to_string(unique)),
    };

    write_text(
        tree.root / "main.cpp",
        "#include <widget.hpp>\n"
        "int main() { return widget() == 42 ? 0 : 1; }\n");
    write_text(
        tree.root / "include/widget.hpp",
        "#pragma once\n"
        "int widget();\n");
    write_text(
        tree.root / "include/widget.cpp",
        "#include \"widget.hpp\"\n"
        "int widget() { return 42; }\n");

    mqb::platform::windows::WindowsProcessRunner runner;
    auto build = run_process(
        runner,
        mqb_executable,
        tree.root,
        {
            "main.cpp",
            "--env", "vs",
            "/Iinclude",
            "-o", "native_include_discovery",
        });
    expect(build.has_value(), "candidate attached-/I smart-discovery build should launch");
    if (build) {
        if (build->exit_code != 0) dump_failure(*build);
        expect(build->exit_code == 0,
               "attached native /I should feed smart discovery and compile the connected widget.cpp");
        expect(build->stdout_text.find("[discover] 2 translation units") != std::string::npos,
               "smart discovery should report entry plus include-root-owned widget.cpp");
    }

    const fs::path executable = tree.root / ".mqb/bin/native_include_discovery.exe";
    expect(fs::is_regular_file(executable),
           "attached-/I discovery build should produce the executable");
    if (fs::is_regular_file(executable)) {
        auto run = run_process(runner, executable, tree.root);
        expect(run.has_value(), "attached-/I discovery executable should launch");
        if (run) {
            if (run->exit_code != 0) dump_failure(*run);
            expect(run->exit_code == 0,
                   "discovered widget.cpp should link and satisfy runtime behavior");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_native_preprocessor_discovery_e2e_tests passed\n";
    return 0;
}
