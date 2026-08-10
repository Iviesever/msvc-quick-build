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
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

[[nodiscard]] bool contains_output_line(
    const std::string_view output,
    const std::string_view expected) {
    std::size_t begin = 0;
    while (begin <= output.size()) {
        const std::size_t newline = output.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos ? output.size() : newline;
        std::string_view line = output.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line == expected) {
            return true;
        }
        if (newline == std::string_view::npos) {
            break;
        }
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

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string>
run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& working_directory,
    const std::vector<fs::path>& sources,
    const fs::path& include_directory,
    const std::vector<std::string>& extra_arguments = {}) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    for (const auto& source : sources) {
        spec.arguments.push_back(path_text(source));
    }
    spec.arguments.insert(
        spec.arguments.end(),
        {"--env", "vs", "-DMQB_CLI_TEST=1", "-I", path_text(include_directory)});
    spec.arguments.insert(spec.arguments.end(), extra_arguments.begin(), extra_arguments.end());
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected("failed to launch mqb: " + result.error().message);
    }
    return std::move(*result);
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string>
run_executable(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& working_directory) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected(
            "failed to launch generated executable: " + result.error().message);
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
        .root = fs::temp_directory_path() / ("mqb_cli_multi_tu_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path include_dir = tree.root / "include";
    const fs::path value_header = include_dir / "value.hpp";
    const fs::path utils_header = include_dir / "utils.hpp";
    const fs::path main_source = tree.root / "main.cpp";
    const fs::path utils_source = tree.root / "src" / "utils.cpp";
    const fs::path helper_a = tree.root / "a" / "helper.cpp";
    const fs::path helper_b = tree.root / "b" / "helper.cpp";

    write_text(value_header, "#pragma once\ninline constexpr int configured_value = 41;\n");
    write_text(utils_header, "#pragma once\nint util_value();\n");
    write_text(
        utils_source,
        "#include \"utils.hpp\"\n"
        "#include \"value.hpp\"\n"
        "int util_value() { return configured_value; }\n");
    write_text(helper_a, "int helper_a() { return 10; }\n");
    write_text(helper_b, "int helper_b() { return 20; }\n");
    write_text(
        main_source,
        "#include <cstdio>\n"
        "#include \"utils.hpp\"\n"
        "#ifndef MQB_CLI_TEST\n"
        "#error MQB_CLI_TEST must be defined\n"
        "#endif\n"
        "int helper_a();\n"
        "int helper_b();\n"
        "int main() {\n"
        "    std::printf(\"mqb-multi=%d\\n\", util_value() + helper_a() + helper_b() + MQB_CLI_TEST);\n"
        "    return 0;\n"
        "}\n");

    const std::vector<fs::path> sources{main_source, utils_source, helper_a, helper_b};
    const fs::path main_object = tree.root / ".mqb" / "obj" / "main.cpp.obj";
    const fs::path utils_object = tree.root / ".mqb" / "obj" / "src" / "utils.cpp.obj";
    const fs::path helper_a_object = tree.root / ".mqb" / "obj" / "a" / "helper.cpp.obj";
    const fs::path helper_b_object = tree.root / ".mqb" / "obj" / "b" / "helper.cpp.obj";
    const fs::path executable = tree.root / ".mqb" / "bin" / "main.exe";
    const fs::path link_cache = tree.root / ".mqb" / "cache" / "link" / "main.linkcache";

    mqb::platform::windows::WindowsProcessRunner runner;

    auto cold = run_mqb(runner, mqb_executable, tree.root, sources, include_dir);
    expect(cold.has_value(), "cold multi-TU invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "cold multi-TU build should succeed");
        expect(cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "cold build should compile main.cpp");
        expect(cold->stdout_text.find("[compile] src/utils.cpp") != std::string::npos,
               "cold build should compile nested utils.cpp");
        expect(cold->stdout_text.find("[compile] a/helper.cpp") != std::string::npos,
               "cold build should compile first same-basename helper");
        expect(cold->stdout_text.find("[compile] b/helper.cpp") != std::string::npos,
               "cold build should compile second same-basename helper");
        expect(cold->stdout_text.find("[link] main.exe") != std::string::npos,
               "cold build should link one target executable");
    }

    expect(fs::is_regular_file(main_object), "main object should use project-level layout");
    expect(fs::is_regular_file(utils_object), "nested object should preserve relative source path");
    expect(fs::is_regular_file(helper_a_object), "first helper object should exist");
    expect(fs::is_regular_file(helper_b_object), "second helper object should exist independently");
    expect(helper_a_object != helper_b_object,
           "same-basename sources from different directories must have different artifacts");
    expect(fs::is_regular_file(executable), "multi-TU build should create target executable");
    expect(fs::is_regular_file(link_cache), "multi-TU build should persist one target link cache");

    auto cold_run = run_executable(runner, executable, tree.root);
    expect(cold_run.has_value(), "cold-built multi-TU executable should launch");
    if (cold_run) {
        expect(cold_run->exit_code == 0, "cold-built executable should return zero");
        expect(cold_run->stdout_text.find("mqb-multi=72") != std::string::npos,
               "cold-built executable should combine all translation units");
    }

    auto warm = run_mqb(runner, mqb_executable, tree.root, sources, include_dir);
    expect(warm.has_value(), "warm multi-TU invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm multi-TU build should succeed");
        expect(contains_output_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "warm build should skip main.cpp");
        expect(contains_output_line(warm->stdout_text, "[up-to-date] src/utils.cpp"),
               "warm build should skip utils.cpp");
        expect(contains_output_line(warm->stdout_text, "[up-to-date] a/helper.cpp"),
               "warm build should skip helper A");
        expect(contains_output_line(warm->stdout_text, "[up-to-date] b/helper.cpp"),
               "warm build should skip helper B");
        expect(contains_output_line(warm->stdout_text, "[up-to-date] main.exe"),
               "warm build should skip link.exe");
    }

    std::error_code error_code;
    const auto utils_object_time = fs::last_write_time(utils_object, error_code);
    expect(!error_code, "utils object timestamp should be readable");
    if (!error_code) {
        write_text(value_header, "#pragma once\ninline constexpr int configured_value = 42;\n");
        fs::last_write_time(value_header, utils_object_time + std::chrono::seconds{2}, error_code);
        expect(!error_code, "value header timestamp should be made newer deterministically");
    }

    auto header_changed = run_mqb(runner, mqb_executable, tree.root, sources, include_dir);
    expect(header_changed.has_value(), "header-change multi-TU invocation should launch");
    if (header_changed) {
        if (header_changed->exit_code != 0) dump_failure(*header_changed);
        expect(header_changed->exit_code == 0, "header-change build should succeed");
        expect(contains_output_line(header_changed->stdout_text, "[up-to-date] main.cpp"),
               "header private to utils.cpp must not rebuild main.cpp");
        expect(header_changed->stdout_text.find("[compile] src/utils.cpp") != std::string::npos,
               "header private to utils.cpp should rebuild utils.cpp");
        expect(header_changed->stdout_text.find("dependency changed") != std::string::npos,
               "partial rebuild should remain explainable");
        expect(contains_output_line(header_changed->stdout_text, "[up-to-date] a/helper.cpp"),
               "unrelated helper A should remain cached");
        expect(contains_output_line(header_changed->stdout_text, "[up-to-date] b/helper.cpp"),
               "unrelated helper B should remain cached");
        expect(header_changed->stdout_text.find("[link] main.exe") != std::string::npos,
               "any rebuilt TU should force target relink");
        expect(header_changed->stdout_text.find("explicit rebuild") != std::string::npos,
               "compile-to-link handoff should not depend only on timestamps");
    }

    auto changed_run = run_executable(runner, executable, tree.root);
    expect(changed_run.has_value(), "partially rebuilt executable should launch");
    if (changed_run) {
        expect(changed_run->exit_code == 0, "partially rebuilt executable should return zero");
        expect(changed_run->stdout_text.find("mqb-multi=73") != std::string::npos,
               "partial rebuild must update final executable behavior");
    }

    error_code.clear();
    const auto rebuilt_utils_time = fs::last_write_time(utils_object, error_code);
    if (!error_code) {
        fs::last_write_time(value_header, rebuilt_utils_time - std::chrono::seconds{2}, error_code);
    }
    expect(!error_code, "test should normalize value header timestamp after rebuild");

    auto release = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        sources,
        include_dir,
        {"--release"});
    expect(release.has_value(), "release multi-TU invocation should launch");
    if (release) {
        if (release->exit_code != 0) dump_failure(*release);
        expect(release->exit_code == 0, "release multi-TU build should succeed");
        expect(release->stdout_text.find("compiler options changed") != std::string::npos,
               "Debug to Release should invalidate compile recipes");
        expect(release->stdout_text.find("[link] main.exe") != std::string::npos,
               "Debug to Release should relink target");
        expect(release->stdout_text.find("linker options changed") != std::string::npos,
               "Debug to Release should invalidate link recipe");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_cli_e2e_tests passed\n";
    return 0;
}
