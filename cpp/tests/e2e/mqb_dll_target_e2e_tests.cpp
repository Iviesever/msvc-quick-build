#include <windows.h>

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

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb,
    const fs::path& root,
    std::vector<std::string> arguments) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = std::move(arguments);
    spec.working_directory = root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch mqb: " + result.error().message);
    return std::move(*result);
}

[[nodiscard]] std::expected<int, std::string> call_export(
    const fs::path& dll,
    const char* symbol_name) {
    const HMODULE module = LoadLibraryW(dll.c_str());
    if (module == nullptr) {
        return std::unexpected("LoadLibraryW failed: " + std::to_string(GetLastError()));
    }
    const FARPROC symbol = GetProcAddress(module, symbol_name);
    if (symbol == nullptr) {
        const DWORD error = GetLastError();
        FreeLibrary(module);
        return std::unexpected(
            "GetProcAddress failed for '" + std::string{symbol_name}
            + "': " + std::to_string(error));
    }
    using ExportFunction = int (*)();
    const auto function = reinterpret_cast<ExportFunction>(symbol);
    const int value = function();
    FreeLibrary(module);
    return value;
}

[[nodiscard]] std::expected<int, std::string> call_answer(const fs::path& dll) {
    return call_export(dll, "mqb_answer");
}

[[nodiscard]] bool make_input_newer_than_output(
    const fs::path& input,
    const fs::path& output) {
    std::error_code error_code;
    const auto output_time = fs::last_write_time(output, error_code);
    if (error_code) return false;
    fs::last_write_time(input, output_time + std::chrono::seconds{2}, error_code);
    return !error_code;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_dll_target_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_dll_target_e2e_" + std::to_string(unique))};
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    write_text(tree.root / "plugin.cpp", R"cpp(extern "C" __declspec(dllexport) int mqb_answer() {
    return 42;
}
)cpp");

    const fs::path dll = tree.root / ".mqb" / "bin" / "plugin.dll";
    const fs::path import_library = tree.root / ".mqb" / "bin" / "plugin.lib";
    const fs::path export_file = tree.root / ".mqb" / "bin" / "plugin.exp";

    auto cold = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"plugin.cpp", "--no-discover", "--env", "vs", "--type", "dll",
         "--runtime", "MT", "-o", "plugin"});
    expect(cold.has_value(), "cold DLL invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "cold DLL target should build successfully");
        expect(cold->stdout_text.find("[compile] plugin.cpp") != std::string::npos,
               "cold DLL target should compile its TU");
        expect(cold->stdout_text.find("[link] plugin.dll") != std::string::npos,
               "cold DLL target should link a DLL");
    }
    expect(fs::is_regular_file(dll), "DLL output should exist at deterministic .mqb/bin path");
    expect(fs::is_regular_file(import_library), "exporting DLL should produce deterministic import library");
    expect(fs::is_regular_file(export_file), "exporting DLL should produce export side file");
    auto answer = call_answer(dll);
    expect(answer.has_value() && *answer == 42,
           "LoadLibrary/GetProcAddress should call exported function from generated DLL");

    auto warm = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"plugin.cpp", "--no-discover", "--env", "vs", "--type=dll",
         "--runtime", "MT", "-o", "plugin"});
    expect(warm.has_value(), "warm DLL invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm DLL target should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] plugin.cpp"),
               "unchanged DLL TU should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] plugin.dll"),
               "unchanged DLL should reuse link cache");
    }

    std::error_code error_code;
    fs::remove(import_library, error_code);
    expect(!error_code && !fs::exists(import_library),
           "test should remove generated import library");
    auto repaired = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"plugin.cpp", "--no-discover", "--env", "vs", "--type", "dll",
         "--runtime", "MT", "-o", "plugin"});
    expect(repaired.has_value(), "missing-import-library repair invocation should launch");
    if (repaired) {
        if (repaired->exit_code != 0) dump_failure(*repaired);
        expect(repaired->exit_code == 0, "missing import library should repair successfully");
        expect(contains_line(repaired->stdout_text, "[up-to-date] plugin.cpp"),
               "missing linker side output must not recompile a fresh TU");
        expect(repaired->stdout_text.find("[link] plugin.dll") != std::string::npos
                   && repaired->stdout_text.find("missing output") != std::string::npos,
               "missing import library should invalidate link freshness with missing-output reason");
    }
    expect(fs::is_regular_file(import_library), "repair relink should recreate import library");

    write_text(tree.root / "plugin.cpp", R"cpp(extern "C" __declspec(dllexport) int mqb_answer() {
    return 43;
}
)cpp");
    auto mutated = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"plugin.cpp", "--no-discover", "--env", "vs", "--type", "dll",
         "--runtime", "MT", "-o", "plugin"});
    expect(mutated.has_value(), "mutated DLL invocation should launch");
    if (mutated) {
        if (mutated->exit_code != 0) dump_failure(*mutated);
        expect(mutated->exit_code == 0, "mutated DLL target should rebuild");
        expect(mutated->stdout_text.find("[compile] plugin.cpp") != std::string::npos,
               "mutated DLL source should recompile");
        expect(mutated->stdout_text.find("[link] plugin.dll") != std::string::npos,
               "fresh DLL object should relink DLL");
    }
    auto mutated_answer = call_answer(dll);
    expect(mutated_answer.has_value() && *mutated_answer == 43,
           "relinked DLL should expose mutated exported behavior");

    // Native /DEF is a linker file input: the raw linker argv remains
    // authoritative, while MQB must separately track the definition file's
    // freshness and invalidate stale incremental-link state when it changes.
    const fs::path def_root = tree.root / "def-input";
    const fs::path def_source = def_root / "plugin.cpp";
    const fs::path def_file = def_root / "exports.def";
    const fs::path def_dll = def_root / ".mqb" / "bin" / "def_plugin.dll";
    write_text(def_source, R"cpp(extern "C" int def_one() { return 101; }
extern "C" int def_two() { return 202; }
)cpp");
    write_text(def_file, "EXPORTS\n    def_one\n");

    const std::vector<std::string> def_arguments{
        "plugin.cpp", "--no-discover", "--env", "vs", "--type", "dll",
        "--runtime", "MT", "-o", "def_plugin", "/link", "/DEF:exports.def"};
    auto def_cold = run_mqb(
        runner,
        mqb_executable,
        def_root,
        def_arguments);
    expect(def_cold.has_value(), "cold /DEF DLL invocation should launch");
    if (def_cold) {
        if (def_cold->exit_code != 0) dump_failure(*def_cold);
        expect(def_cold->exit_code == 0, "cold /DEF DLL should build successfully");
        expect(def_cold->stdout_text.find("[compile] plugin.cpp") != std::string::npos,
               "cold /DEF DLL should compile its TU");
        expect(def_cold->stdout_text.find("[link] def_plugin.dll") != std::string::npos,
               "cold /DEF DLL should link");
    }
    auto def_one = call_export(def_dll, "def_one");
    expect(def_one.has_value() && *def_one == 101,
           "initial module-definition file should export def_one");
    expect(!call_export(def_dll, "def_two").has_value(),
           "initial module-definition file should not export def_two");

    auto def_warm = run_mqb(
        runner,
        mqb_executable,
        def_root,
        def_arguments);
    expect(def_warm.has_value(), "warm /DEF DLL invocation should launch");
    if (def_warm) {
        if (def_warm->exit_code != 0) dump_failure(*def_warm);
        expect(def_warm->exit_code == 0, "warm /DEF DLL should succeed");
        expect(contains_line(def_warm->stdout_text, "[up-to-date] plugin.cpp"),
               "unchanged /DEF DLL TU should reuse compile cache");
        expect(contains_line(def_warm->stdout_text, "[up-to-date] def_plugin.dll"),
               "unchanged /DEF input should reuse link cache");
        expect(def_warm->stdout_text.find("[link] def_plugin.dll") == std::string::npos,
               "unchanged /DEF input must not trigger an unnecessary relink");
    }

    write_text(def_file, "EXPORTS\n    def_two\n");
    expect(make_input_newer_than_output(def_file, def_dll),
           "test should make mutated definition file newer than DLL output");
    auto def_mutated = run_mqb(
        runner,
        mqb_executable,
        def_root,
        def_arguments);
    expect(def_mutated.has_value(), "mutated /DEF DLL invocation should launch");
    if (def_mutated) {
        if (def_mutated->exit_code != 0) dump_failure(*def_mutated);
        expect(def_mutated->exit_code == 0, "mutated /DEF DLL should relink successfully");
        expect(contains_line(def_mutated->stdout_text, "[up-to-date] plugin.cpp"),
               "/DEF-only mutation must not recompile the source TU");
        expect(def_mutated->stdout_text.find("[link] def_plugin.dll") != std::string::npos,
               "/DEF-only mutation should invalidate link freshness");
    }
    expect(!call_export(def_dll, "def_one").has_value(),
           "full relink after /DEF mutation should remove the obsolete export");
    auto def_two = call_export(def_dll, "def_two");
    expect(def_two.has_value() && *def_two == 202,
           "full relink after /DEF mutation should publish the replacement export");

    // Config/profile native path-bearing arguments are resolved relative to
    // project root, not the child invocation directory.
    const fs::path config_root = tree.root / "def-config";
    const fs::path config_child = config_root / "child";
    fs::create_directories(config_child);
    write_text(config_root / "plugin.cpp", R"cpp(extern "C" int config_def() { return 303; }
)cpp");
    write_text(config_root / "exports.def", "EXPORTS\n    config_def\n");
    write_text(
        config_root / "mqb.json",
        R"json({
  "version": 1,
  "build": {
    "linker_args": ["/DEF:exports.def"]
  }
})json");

    auto config_def_build = run_mqb(
        runner,
        mqb_executable,
        config_child,
        {"../plugin.cpp", "--no-discover", "--env", "vs", "--type", "dll",
         "--runtime", "MT", "-o", "config_def"});
    expect(config_def_build.has_value(), "config-relative /DEF build should launch");
    if (config_def_build) {
        if (config_def_build->exit_code != 0) dump_failure(*config_def_build);
        expect(config_def_build->exit_code == 0,
               "mqb.json /DEF should resolve relative to project root from a child invocation directory");
    }
    const fs::path config_dll = config_root / ".mqb" / "bin" / "config_def.dll";
    auto config_export = call_export(config_dll, "config_def");
    expect(config_export.has_value() && *config_export == 303,
           "config-relative /DEF should control the real DLL export table");

    auto duplicate_def = run_mqb(
        runner,
        mqb_executable,
        config_child,
        {"../plugin.cpp", "--no-discover", "--env", "vs", "--type", "dll",
         "--runtime", "MT", "-o", "duplicate_def", "/link", "/DEF:other.def"});
    expect(duplicate_def.has_value(), "duplicate config/CLI /DEF validation should launch");
    if (duplicate_def) {
        expect(duplicate_def->exit_code == 2,
               "a second /DEF introduced by CLI should fail before LINK.exe");
        expect(duplicate_def->stderr_text.find("second native MSVC /DEF") != std::string::npos,
               "duplicate /DEF failure should explain the single-definition-file contract");
    }

    auto invalid_run = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"plugin.cpp", "--type", "dll", "--run"});
    expect(invalid_run.has_value(), "DLL --run validation invocation should launch");
    if (invalid_run) {
        expect(invalid_run->exit_code == 2,
               "DLL --run should fail at CLI/orchestration validation boundary");
        expect(invalid_run->stderr_text.find("--run is only valid for executable targets") != std::string::npos,
               "DLL --run should explain executable-only run semantics");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_dll_target_e2e_tests passed\n";
    return 0;
}
