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

[[nodiscard]] std::expected<int, std::string> call_answer(const fs::path& dll) {
    const HMODULE module = LoadLibraryW(dll.c_str());
    if (module == nullptr) {
        return std::unexpected("LoadLibraryW failed: " + std::to_string(GetLastError()));
    }
    const FARPROC symbol = GetProcAddress(module, "mqb_answer");
    if (symbol == nullptr) {
        const DWORD error = GetLastError();
        FreeLibrary(module);
        return std::unexpected("GetProcAddress failed: " + std::to_string(error));
    }
    using AnswerFunction = int (*)();
    const auto answer = reinterpret_cast<AnswerFunction>(symbol);
    const int value = answer();
    FreeLibrary(module);
    return value;
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
        {"plugin.cpp", "--no-discover", "--env", "vs", "-type", "dll",
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
