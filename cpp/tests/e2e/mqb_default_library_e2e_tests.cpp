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

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_process(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& root,
    std::vector<std::string> arguments = {}) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = std::move(arguments);
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch process: " + result.error().message);
    return std::move(*result);
}

[[nodiscard]] bool build_static_library(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb,
    const fs::path& root,
    const std::string& source,
    const std::string& output) {
    auto result = run_process(
        runner,
        mqb,
        root,
        {source, "--no-discover", "--env", "vs", "--type", "static", "-o", output});
    expect(result.has_value(), "static-library fixture invocation should launch");
    if (!result) return false;
    if (result->exit_code != 0) dump_failure(*result);
    expect(result->exit_code == 0, "static-library fixture should build successfully");
    return result->exit_code == 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_default_library_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_default_library_e2e_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    write_text(tree.root / "default_provider.cpp", R"cpp(extern "C" int mqb_value() {
    return 41;
}
)cpp");
    write_text(tree.root / "explicit_provider.cpp", R"cpp(extern "C" int mqb_value() {
    return 73;
}
)cpp");
    write_text(tree.root / "consumer.cpp", R"cpp(extern "C" int mqb_value();
int main() {
    return mqb_value();
}
)cpp");

    expect(build_static_library(
               runner, mqb_executable, tree.root, "default_provider.cpp", "defaultlib"),
           "default-library provider should be available");
    expect(build_static_library(
               runner, mqb_executable, tree.root, "explicit_provider.cpp", "explicitlib"),
           "explicit-library provider should be available");

    const fs::path default_library = tree.root / ".mqb" / "bin" / "defaultlib.lib";
    const fs::path consumer = tree.root / ".mqb" / "bin" / "consumer.exe";
    expect(fs::is_regular_file(default_library), "default provider should produce defaultlib.lib");

    const std::vector<std::string> default_consumer_args{
        "consumer.cpp",
        "--no-discover",
        "--env", "vs",
        "-L", ".mqb/bin",
        "-o", "consumer",
        "/link",
        "/DEFAULTLIB:defaultlib.lib",
    };

    auto cold = run_process(runner, mqb_executable, tree.root, default_consumer_args);
    expect(cold.has_value(), "DEFAULTLIB consumer build should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0,
               "raw /DEFAULTLIB should allow LINK to resolve the provider without structured --lib");
        expect(cold->stdout_text.find("[compile] consumer.cpp") != std::string::npos,
               "cold DEFAULTLIB consumer should compile its source");
        expect(cold->stdout_text.find("[link] consumer.exe") != std::string::npos,
               "cold DEFAULTLIB consumer should invoke LINK");
    }
    auto cold_run = run_process(runner, consumer, tree.root);
    expect(cold_run.has_value() && cold_run->exit_code == 41,
           "consumer should execute the symbol provided only through /DEFAULTLIB");

    auto warm = run_process(runner, mqb_executable, tree.root, default_consumer_args);
    expect(warm.has_value(), "warm DEFAULTLIB consumer build should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm DEFAULTLIB consumer should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] consumer.cpp"),
               "warm DEFAULTLIB consumer should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] consumer.exe"),
               "unchanged DEFAULTLIB provider should reuse link cache");
        expect(warm->stdout_text.find("[link] consumer.exe") == std::string::npos,
               "unchanged DEFAULTLIB consumer should remain a zero-LINK no-op");
    }

    std::error_code time_error;
    const auto consumer_time = fs::last_write_time(consumer, time_error);
    expect(!time_error, "DEFAULTLIB fixture should read consumer output timestamp");

    write_text(tree.root / "default_provider.cpp", R"cpp(extern "C" int mqb_value() {
    return 42;
}
)cpp");
    expect(build_static_library(
               runner, mqb_executable, tree.root, "default_provider.cpp", "defaultlib"),
           "mutated default-library provider should rebuild");
    if (!time_error) {
        fs::last_write_time(
            default_library,
            consumer_time + std::chrono::seconds{2},
            time_error);
    }
    expect(!time_error, "DEFAULTLIB fixture should advance provider archive timestamp");

    auto provider_changed = run_process(
        runner, mqb_executable, tree.root, default_consumer_args);
    expect(provider_changed.has_value(), "DEFAULTLIB-only mutation consumer build should launch");
    if (provider_changed) {
        if (provider_changed->exit_code != 0) dump_failure(*provider_changed);
        expect(provider_changed->exit_code == 0,
               "consumer should relink after an effective DEFAULTLIB archive changes");
        expect(contains_line(provider_changed->stdout_text, "[up-to-date] consumer.cpp"),
               "DEFAULTLIB-only mutation must not recompile consumer source");
        expect(provider_changed->stdout_text.find("[link] consumer.exe") != std::string::npos,
               "effective DEFAULTLIB file freshness must invalidate the link cache");
    }
    auto changed_run = run_process(runner, consumer, tree.root);
    expect(changed_run.has_value() && changed_run->exit_code == 42,
           "consumer should observe mutated DEFAULTLIB archive behavior");

    auto suppressed = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"consumer.cpp", "--no-discover", "--env", "vs",
         "-L", ".mqb/bin", "-o", "suppressed",
         "/link", "/DEFAULTLIB:defaultlib.lib", "/NODEFAULTLIB:DEFAULTLIB"});
    expect(suppressed.has_value(), "NODEFAULTLIB suppression build should launch");
    if (suppressed) {
        expect(suppressed->exit_code != 0,
               "NODEFAULTLIB:name should suppress the matching raw DEFAULTLIB and leave the symbol unresolved");
        expect(suppressed->stdout_text.find("[link] suppressed.exe") != std::string::npos,
               "suppression probe should reach LINK rather than being rewritten by MQB");
    }

    auto explicit_priority = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"consumer.cpp", "--no-discover", "--env", "vs",
         "-L", ".mqb/bin", "-l", "explicitlib", "-o", "explicit_priority",
         "/link", "/DEFAULTLIB:defaultlib.lib"});
    expect(explicit_priority.has_value(), "explicit-vs-default library build should launch");
    if (explicit_priority) {
        if (explicit_priority->exit_code != 0) dump_failure(*explicit_priority);
        expect(explicit_priority->exit_code == 0,
               "explicit library plus DEFAULTLIB should link successfully");
    }
    const fs::path explicit_exe = tree.root / ".mqb" / "bin" / "explicit_priority.exe";
    auto explicit_run = run_process(runner, explicit_exe, tree.root);
    expect(explicit_run.has_value() && explicit_run->exit_code == 73,
           "explicit structured library must retain priority over a raw DEFAULTLIB declaration");

    auto ignored_missing = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"consumer.cpp", "--no-discover", "--env", "vs",
         "-L", ".mqb/bin", "-l", "explicitlib", "-o", "ignored_missing",
         "/link", "/DEFAULTLIB:does-not-exist.lib", "/NODEFAULTLIB:does-not-exist"});
    expect(ignored_missing.has_value(), "ignored missing DEFAULTLIB build should launch");
    if (ignored_missing) {
        if (ignored_missing->exit_code != 0) dump_failure(*ignored_missing);
        expect(ignored_missing->exit_code == 0,
               "a NODEFAULTLIB-suppressed missing DEFAULTLIB must not make MQB stricter than LINK");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_default_library_e2e_tests passed\n";
    return 0;
}
