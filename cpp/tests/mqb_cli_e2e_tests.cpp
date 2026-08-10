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

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

void write_text(const fs::path& path, const std::string_view text) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

struct TempTree {
    fs::path root;

    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string>
run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& working_directory,
    const fs::path& source,
    const std::vector<std::string>& extra_arguments = {}) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = {path_text(source), "--env", "vs", "-DMQB_CLI_TEST=1"};
    spec.arguments.insert(spec.arguments.end(), extra_arguments.begin(), extra_arguments.end());
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected(
            "failed to launch mqb: " + result.error().message);
    }
    return std::move(*result);
}

void dump_failure(const mqb::process::ProcessResult& result) {
    std::cerr << "exit: " << result.exit_code << '\n'
              << "stdout:\n" << result.stdout_text << '\n'
              << "stderr:\n" << result.stderr_text << '\n';
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_cli_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_cli_e2e_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path header = tree.root / "sample.hpp";
    const fs::path source = tree.root / "sample.cpp";
    write_text(header, "#pragma once\ninline constexpr int sample_value = 41;\n");
    write_text(
        source,
        "#include \"sample.hpp\"\n"
        "#ifndef MQB_CLI_TEST\n"
        "#error MQB_CLI_TEST must be defined\n"
        "#endif\n"
        "int value() { return sample_value + MQB_CLI_TEST; }\n");

    const fs::path object = tree.root / ".mqb" / "obj" / "sample.cpp.obj";
    const fs::path cache = tree.root / ".mqb" / "cache" / "sample.cpp.mqbcache";
    const fs::path dependencies = tree.root / ".mqb" / "deps" / "sample.cpp.json";

    mqb::platform::windows::WindowsProcessRunner runner;

    auto cold = run_mqb(runner, mqb_executable, tree.root, source);
    expect(cold.has_value(), "cold invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) {
            dump_failure(*cold);
        }
        expect(cold->exit_code == 0, "cold build should succeed");
        expect(cold->stdout_text.find("[compile]") != std::string::npos,
               "cold build should report a compile action");
    }
    expect(fs::is_regular_file(object), "cold build should create collision-free object artifact");
    expect(fs::is_regular_file(cache), "cold build should persist compile cache");
    expect(fs::is_regular_file(dependencies), "cold build should create sourceDependencies metadata");

    auto warm = run_mqb(runner, mqb_executable, tree.root, source);
    expect(warm.has_value(), "warm invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) {
            dump_failure(*warm);
        }
        expect(warm->exit_code == 0, "warm build should succeed");
        expect(warm->stdout_text.find("[up-to-date]") != std::string::npos,
               "warm build should reuse the cached object");
    }

    std::error_code error_code;
    const auto first_object_time = fs::last_write_time(object, error_code);
    expect(!error_code, "object timestamp should be readable");
    if (!error_code) {
        write_text(header, "#pragma once\ninline constexpr int sample_value = 42;\n");
        fs::last_write_time(header, first_object_time + std::chrono::seconds{2}, error_code);
        expect(!error_code, "header timestamp should be adjustable for deterministic invalidation");
    }

    auto header_changed = run_mqb(runner, mqb_executable, tree.root, source);
    expect(header_changed.has_value(), "header-change invocation should launch");
    if (header_changed) {
        if (header_changed->exit_code != 0) {
            dump_failure(*header_changed);
        }
        expect(header_changed->exit_code == 0, "header-change build should succeed");
        expect(header_changed->stdout_text.find("[compile]") != std::string::npos,
               "header change should trigger compilation");
        expect(header_changed->stdout_text.find("dependency changed") != std::string::npos,
               "header change should be explained as dependency changed");
    }

    error_code.clear();
    const auto rebuilt_object_time = fs::last_write_time(object, error_code);
    expect(!error_code, "rebuilt object timestamp should be readable");
    if (!error_code) {
        fs::last_write_time(header, rebuilt_object_time - std::chrono::seconds{2}, error_code);
        expect(!error_code, "header timestamp should be normalized after rebuild");
    }

    auto warm_again = run_mqb(runner, mqb_executable, tree.root, source);
    expect(warm_again.has_value(), "second warm invocation should launch");
    if (warm_again) {
        if (warm_again->exit_code != 0) {
            dump_failure(*warm_again);
        }
        expect(warm_again->exit_code == 0, "second warm build should succeed");
        expect(warm_again->stdout_text.find("[up-to-date]") != std::string::npos,
               "rebuilt object should become reusable again");
    }

    auto release = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        source,
        {"--release"});
    expect(release.has_value(), "release invocation should launch");
    if (release) {
        if (release->exit_code != 0) {
            dump_failure(*release);
        }
        expect(release->exit_code == 0, "release build should succeed");
        expect(release->stdout_text.find("[compile]") != std::string::npos,
               "Debug to Release should trigger compilation without source edits");
        expect(release->stdout_text.find("compiler options changed") != std::string::npos,
               "configuration invalidation should be explained as compiler options changed");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_cli_e2e_tests passed\n";
    return 0;
}
