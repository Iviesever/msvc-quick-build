#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
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

[[nodiscard]] bool contains_line(std::string_view output, std::string_view expected) {
    std::size_t begin = 0;
    while (begin <= output.size()) {
        const std::size_t newline = output.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos ? output.size() : newline;
        std::string_view line = output.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line == expected) return true;
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
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

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb,
    const fs::path& root,
    const std::string& jobs) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = {
        "main.cpp",
        "a.cpp",
        "b.cpp",
        "c.cpp",
        "--env",
        "vs",
        "--jobs",
        jobs,
        "--verbose",
        "-o",
        "parallel",
    };
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch mqb: " + result.error().message);
    return std::move(*result);
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_executable(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& root) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch built executable: " + result.error().message);
    return std::move(*result);
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_parallel_cli_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_parallel_cli_e2e_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    write_text(tree.root / "main.cpp", R"cpp(#include <cstdio>
int a();
int b();
int c();
int main() {
    std::printf("parallel=%d\n", 1 + a() + b() + c());
    return 0;
}
)cpp");
    write_text(tree.root / "a.cpp", "int a() { return 2; }\n");
    write_text(tree.root / "b.cpp", "int b() { return 3; }\n");
    write_text(tree.root / "c.cpp", "int c() { return 4; }\n");

    mqb::platform::windows::WindowsProcessRunner runner;

    auto cold = run_mqb(runner, mqb_executable, tree.root, "4");
    expect(cold.has_value(), "cold real parallel MQB invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0,
               "cold same-directory four-TU parallel build should succeed");
        expect(cold->stdout_text.find("  jobs:    4") != std::string::npos,
               "verbose target output should report four active compile jobs");
        expect(cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "cold parallel build should compile main.cpp");
        expect(cold->stdout_text.find("[compile] a.cpp") != std::string::npos,
               "cold parallel build should compile a.cpp");
        expect(cold->stdout_text.find("[compile] b.cpp") != std::string::npos,
               "cold parallel build should compile b.cpp");
        expect(cold->stdout_text.find("[compile] c.cpp") != std::string::npos,
               "cold parallel build should compile c.cpp");
        expect(cold->stdout_text.find("[link] parallel.exe") != std::string::npos,
               "cold parallel build should link after all four TUs finish");
    }

    const fs::path executable = tree.root / ".mqb" / "bin" / "parallel.exe";
    expect(fs::is_regular_file(executable),
           "parallel target executable should be produced");
    auto cold_run = run_executable(runner, executable, tree.root);
    expect(cold_run.has_value() && cold_run->exit_code == 0,
           "parallel-built executable should launch");
    if (cold_run) {
        expect(cold_run->stdout_text.find("parallel=10") != std::string::npos,
               "parallel-built executable should contain every TU contribution");
    }

    auto warm = run_mqb(runner, mqb_executable, tree.root, "1");
    expect(warm.has_value(), "warm sequential-policy MQB invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0,
               "warm build should succeed after changing only compile job count");
        expect(warm->stdout_text.find("  jobs:    1") != std::string::npos,
               "verbose target output should report explicit sequential job policy");
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "changing -j must not invalidate main.cpp compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] a.cpp"),
               "changing -j must not invalidate a.cpp compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] b.cpp"),
               "changing -j must not invalidate b.cpp compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] c.cpp"),
               "changing -j must not invalidate c.cpp compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] parallel.exe"),
               "changing -j must not invalidate link cache");
    }

    auto warm_run = run_executable(runner, executable, tree.root);
    expect(warm_run.has_value() && warm_run->exit_code == 0,
           "warm reused executable should still launch");
    if (warm_run) {
        expect(warm_run->stdout_text.find("parallel=10") != std::string::npos,
               "warm reused executable behavior should remain unchanged");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_parallel_cli_e2e_tests passed\n";
    return 0;
}
