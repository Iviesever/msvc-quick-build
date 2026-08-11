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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void write_text(const fs::path& path, std::string_view text) {
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
    const std::vector<std::string>& extra_arguments) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = {"main.cpp", "--env", "vs", "--no-discover"};
    spec.arguments.insert(spec.arguments.end(), extra_arguments.begin(), extra_arguments.end());
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

[[nodiscard]] std::vector<std::string> raw_policy(
    int value,
    const fs::path& map_file) {
    return {
        "-o", "policy",
        "--compiler-arg", "/DPOLICY_VALUE=" + std::to_string(value),
        "--linker-arg", "/MAP:" + path_text(map_file),
    };
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_build_policy_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_build_policy_e2e_" + std::to_string(unique))};
    fs::create_directories(tree.root);

    write_text(tree.root / "main.cpp", R"cpp(#include <cstdio>
#ifndef POLICY_VALUE
#error POLICY_VALUE must be supplied by raw build policy
#endif
int main() {
    std::printf("policy=%d\n", POLICY_VALUE);
    return 0;
}
)cpp");

    mqb::platform::windows::WindowsProcessRunner runner;
    const fs::path map_file = tree.root / "policy.map";
    const fs::path executable = tree.root / ".mqb" / "bin" / "policy.exe";

    auto cold = run_mqb(runner, mqb_executable, tree.root, raw_policy(1, map_file));
    expect(cold.has_value(), "cold raw-policy invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "cold raw-policy build should succeed");
        expect(cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "cold raw compiler policy should compile the TU");
        expect(cold->stdout_text.find("[link] policy.exe") != std::string::npos,
               "cold raw linker policy should link the target");
    }
    expect(fs::is_regular_file(map_file), "raw /MAP linker argument should produce its side artifact");

    auto cold_run = run_executable(runner, executable, tree.root);
    expect(cold_run.has_value() && cold_run->exit_code == 0,
           "cold raw-policy executable should run");
    if (cold_run) {
        expect(cold_run->stdout_text.find("policy=1") != std::string::npos,
               "raw compiler define should affect executable behavior");
    }

    auto warm = run_mqb(runner, mqb_executable, tree.root, raw_policy(1, map_file));
    expect(warm.has_value(), "warm raw-policy invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm raw-policy build should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "unchanged raw compiler policy should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] policy.exe"),
               "unchanged raw linker policy should reuse link cache");
    }

    auto compiler_changed = run_mqb(runner, mqb_executable, tree.root, raw_policy(2, map_file));
    expect(compiler_changed.has_value(), "compiler-policy change invocation should launch");
    if (compiler_changed) {
        if (compiler_changed->exit_code != 0) dump_failure(*compiler_changed);
        expect(compiler_changed->exit_code == 0, "compiler-policy changed build should succeed");
        expect(compiler_changed->stdout_text.find("[compile] main.cpp") != std::string::npos
                   && compiler_changed->stdout_text.find("compiler options changed") != std::string::npos,
               "raw compiler argument change should invalidate compile recipe identity");
        expect(compiler_changed->stdout_text.find("[link] policy.exe") != std::string::npos,
               "fresh compile from raw compiler change should force relink");
    }

    auto compiler_changed_run = run_executable(runner, executable, tree.root);
    expect(compiler_changed_run.has_value() && compiler_changed_run->exit_code == 0,
           "compiler-policy changed executable should run");
    if (compiler_changed_run) {
        expect(compiler_changed_run->stdout_text.find("policy=2") != std::string::npos,
               "changed raw compiler argument should affect executable behavior");
    }

    // Change link recipe identity independently from the compiler recipe while
    // retaining the already-proven /MAP argument. Delete the map first so the
    // relink must recreate it; this verifies the changed linker invocation was
    // actually executed without depending on link.exe's semantics for swapping
    // /MAP output filenames across consecutive links.
    std::error_code remove_error;
    fs::remove(map_file, remove_error);
    expect(!remove_error && !fs::exists(map_file),
           "test should remove the first map artifact before linker-only rebuild");

    auto linker_policy = raw_policy(2, map_file);
    linker_policy.emplace_back("--linker-arg");
    linker_policy.emplace_back("/INCREMENTAL:NO");
    auto linker_changed = run_mqb(runner, mqb_executable, tree.root, linker_policy);
    expect(linker_changed.has_value(), "linker-policy change invocation should launch");
    if (linker_changed) {
        if (linker_changed->exit_code != 0) dump_failure(*linker_changed);
        expect(linker_changed->exit_code == 0, "linker-policy changed build should succeed");
        expect(contains_line(linker_changed->stdout_text, "[up-to-date] main.cpp"),
               "linker-only policy change must not recompile the TU");
        expect(linker_changed->stdout_text.find("[link] policy.exe") != std::string::npos
                   && linker_changed->stdout_text.find("linker options changed") != std::string::npos,
               "raw linker argument change should invalidate link recipe identity only");
    }
    expect(fs::is_regular_file(map_file),
           "linker-only rebuild should execute raw /MAP policy and recreate its side artifact");

    const fs::path config_map = tree.root / "config-policy.map";
    const std::string config = std::string{R"json({
  "version": 1,
  "build": {
    "standard": "17",
    "output": "config_policy",
    "compiler_args": ["/DPOLICY_VALUE=3"],
    "linker_args": ["/MAP:)json"}
        + path_text(config_map)
        + R"json("]
  }
})json";
    write_text(tree.root / "mqb.json", config);

    auto config_cold = run_mqb(runner, mqb_executable, tree.root, {});
    expect(config_cold.has_value(), "config build-policy invocation should launch");
    if (config_cold) {
        if (config_cold->exit_code != 0) dump_failure(*config_cold);
        expect(config_cold->exit_code == 0, "C++17 config build-policy target should succeed");
        expect(config_cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "config standard/compiler policy should produce a fresh compile");
        expect(config_cold->stdout_text.find("[link] config_policy.exe") != std::string::npos,
               "config linker policy should produce the configured target");
    }
    expect(fs::is_regular_file(config_map), "mqb.json linker_args should reach link.exe");

    const fs::path config_executable = tree.root / ".mqb" / "bin" / "config_policy.exe";
    auto config_run = run_executable(runner, config_executable, tree.root);
    expect(config_run.has_value() && config_run->exit_code == 0,
           "config build-policy executable should run");
    if (config_run) {
        expect(config_run->stdout_text.find("policy=3") != std::string::npos,
               "mqb.json compiler_args should affect executable behavior");
    }

    auto config_warm = run_mqb(runner, mqb_executable, tree.root, {});
    expect(config_warm.has_value(), "warm config build-policy invocation should launch");
    if (config_warm) {
        if (config_warm->exit_code != 0) dump_failure(*config_warm);
        expect(config_warm->exit_code == 0, "warm config build-policy target should succeed");
        expect(contains_line(config_warm->stdout_text, "[up-to-date] main.cpp"),
               "unchanged config compiler policy should be reusable");
        expect(contains_line(config_warm->stdout_text, "[up-to-date] config_policy.exe"),
               "unchanged config linker policy should be reusable");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_build_policy_e2e_tests passed\n";
    return 0;
}
