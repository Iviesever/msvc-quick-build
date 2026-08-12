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

void write_config(
    const fs::path& root,
    const std::string_view runtime,
    const std::string_view subsystem) {
    const std::string config =
        "{\n"
        "  \"version\": 1,\n"
        "  \"build\": {\n"
        "    \"standard\": \"17\",\n"
        "    \"runtime\": \"" + std::string{runtime} + "\",\n"
        "    \"subsystem\": \"" + std::string{subsystem} + "\",\n"
        "    \"output\": \"config_policy\"\n"
        "  }\n"
        "}\n";
    write_text(root / "mqb.json", config);
}

[[nodiscard]] bool contains_line(
    const std::string_view output,
    const std::string_view expected) {
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

void dump_failure(const mqb::process::ProcessResult& result) {
    std::cerr << "exit: " << result.exit_code << '\n'
              << "stdout:\n" << result.stdout_text << '\n'
              << "stderr:\n" << result.stderr_text << '\n';
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
    const fs::path& root) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = {"main.cpp", "--env", "vs", "--no-discover"};
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch mqb: " + result.error().message);
    return std::move(*result);
}

void expect_success(
    const std::expected<mqb::process::ProcessResult, std::string>& result,
    const std::string_view message) {
    expect(result.has_value(), message);
    if (result && result->exit_code != 0) dump_failure(*result);
    if (result) expect(result->exit_code == 0, message);
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_runtime_subsystem_config_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path()
            / ("mqb_runtime_subsystem_config_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    write_text(tree.root / "main.cpp", R"cpp(#include <windows.h>
int main() { return 0; }
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 0; }
)cpp");

    mqb::platform::windows::WindowsProcessRunner runner;

    write_config(tree.root, "MT", "console");
    auto cold = run_mqb(runner, mqb_executable, tree.root);
    expect_success(cold, "cold MT/console config build should succeed");
    if (cold) {
        expect(cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "cold config should compile the TU");
        expect(cold->stdout_text.find("[link] config_policy.exe") != std::string::npos,
               "cold config should link the configured target");
    }

    auto warm = run_mqb(runner, mqb_executable, tree.root);
    expect_success(warm, "warm MT/console config build should succeed");
    if (warm) {
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "unchanged config runtime should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] config_policy.exe"),
               "unchanged config subsystem should reuse link cache");
    }

    write_config(tree.root, "MD", "console");
    auto runtime_changed = run_mqb(runner, mqb_executable, tree.root);
    expect_success(runtime_changed, "runtime-only config change should succeed");
    if (runtime_changed) {
        expect(runtime_changed->stdout_text.find("[compile] main.cpp") != std::string::npos
                   && runtime_changed->stdout_text.find("compiler options changed") != std::string::npos,
               "config runtime change should invalidate compile identity");
        expect(runtime_changed->stdout_text.find("[link] config_policy.exe") != std::string::npos,
               "config runtime change should relink after recompilation");
    }

    auto runtime_warm = run_mqb(runner, mqb_executable, tree.root);
    expect_success(runtime_warm, "warm MD/console config build should succeed");
    if (runtime_warm) {
        expect(contains_line(runtime_warm->stdout_text, "[up-to-date] main.cpp"),
               "unchanged MD runtime should keep compile cache warm");
        expect(contains_line(runtime_warm->stdout_text, "[up-to-date] config_policy.exe"),
               "unchanged console subsystem should keep link cache warm");
    }

    write_config(tree.root, "MD", "windows");
    auto subsystem_changed = run_mqb(runner, mqb_executable, tree.root);
    expect_success(subsystem_changed, "subsystem-only config change should succeed");
    if (subsystem_changed) {
        expect(contains_line(subsystem_changed->stdout_text, "[up-to-date] main.cpp"),
               "config subsystem-only change must not recompile the TU");
        expect(subsystem_changed->stdout_text.find("[link] config_policy.exe") != std::string::npos
                   && subsystem_changed->stdout_text.find("linker options changed") != std::string::npos,
               "config subsystem change should invalidate link identity only");
    }

    auto subsystem_warm = run_mqb(runner, mqb_executable, tree.root);
    expect_success(subsystem_warm, "warm MD/windows config build should succeed");
    if (subsystem_warm) {
        expect(contains_line(subsystem_warm->stdout_text, "[up-to-date] main.cpp"),
               "unchanged windows subsystem should keep compile cache warm");
        expect(contains_line(subsystem_warm->stdout_text, "[up-to-date] config_policy.exe"),
               "unchanged windows subsystem should keep link cache warm");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_runtime_subsystem_config_e2e_tests passed\n";
    return 0;
}
