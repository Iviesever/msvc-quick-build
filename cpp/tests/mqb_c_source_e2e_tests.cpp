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

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_c_source_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_c_source_e2e_" + std::to_string(unique))};
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    // Pure-C smart-discovery fixture. `class` is a valid C identifier but a C++
    // keyword, so successful compilation proves the .c file stayed in C mode.
    write_text(tree.root / "shared.h", R"c(#pragma once
int c_answer(void);
)c");
    write_text(tree.root / "helper.c", R"c(#include "shared.h"
int class = 42;
int c_answer(void) { return class; }
)c");
    write_text(tree.root / "main.c", R"c(#include <stdio.h>
#include "shared.h"
int main(void) {
    printf("c-answer=%d\n", c_answer());
    return c_answer() >= 40 ? 0 : 1;
}
)c");

    auto c_cold = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"main.c", "--env", "vs", "--std", "latest", "--run", "-o", "pure_c"});
    expect(c_cold.has_value(), "pure-C smart-discovery invocation should launch");
    if (c_cold) {
        if (c_cold->exit_code != 0) dump_failure(*c_cold);
        expect(c_cold->exit_code == 0, "pure-C smart-discovery build/run should succeed");
        expect(c_cold->stdout_text.find("[discover] 2 translation units") != std::string::npos,
               "single C entry should smart-discover the connected helper.c");
        expect(c_cold->stdout_text.find("[compile] main.c") != std::string::npos
                   && c_cold->stdout_text.find("[compile] helper.c") != std::string::npos,
               "cold pure-C target should compile both discovered C TUs");
        expect(c_cold->stdout_text.find("[link] pure_c.exe") != std::string::npos,
               "cold pure-C target should link");
        expect(c_cold->stdout_text.find("c-answer=42") != std::string::npos,
               "pure-C executable should run and expose the helper result");
    }

    auto c_warm = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"main.c", "--env", "vs", "--std", "latest", "--run", "-o", "pure_c"});
    expect(c_warm.has_value(), "warm pure-C invocation should launch");
    if (c_warm) {
        if (c_warm->exit_code != 0) dump_failure(*c_warm);
        expect(c_warm->exit_code == 0, "warm pure-C target should succeed");
        expect(contains_line(c_warm->stdout_text, "[up-to-date] main.c")
                   && contains_line(c_warm->stdout_text, "[up-to-date] helper.c"),
               "unchanged pure-C TUs should reuse compile caches");
        expect(contains_line(c_warm->stdout_text, "[up-to-date] pure_c.exe"),
               "unchanged pure-C target should reuse link cache");
    }

    write_text(tree.root / "helper.c", R"c(#include "shared.h"
int class = 43;
int c_answer(void) { return class; }
)c");
    auto c_mutated = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"main.c", "--env", "vs", "--std", "latest", "--run", "-o", "pure_c"});
    expect(c_mutated.has_value(), "mutated pure-C invocation should launch");
    if (c_mutated) {
        if (c_mutated->exit_code != 0) dump_failure(*c_mutated);
        expect(c_mutated->exit_code == 0, "mutated pure-C target should succeed");
        expect(contains_line(c_mutated->stdout_text, "[up-to-date] main.c"),
               "unmodified C entry should remain compile-cache fresh");
        expect(c_mutated->stdout_text.find("[compile] helper.c") != std::string::npos,
               "mutated C helper should rebuild");
        expect(c_mutated->stdout_text.find("[link] pure_c.exe") != std::string::npos,
               "fresh C object should relink the target");
        expect(c_mutated->stdout_text.find("c-answer=43") != std::string::npos,
               "mutated pure-C executable should run with the new value");
    }

    // Explicit mixed-language fixture. The shared header exports a C ABI to the
    // C++ TU while helper_mixed.c retains syntax that cannot compile as C++.
    write_text(tree.root / "mixed_api.h", R"c(#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int mixed_c_answer(void);
#ifdef __cplusplus
}
#endif
)c");
    write_text(tree.root / "helper_mixed.c", R"c(#include "mixed_api.h"
int class = 7;
int mixed_c_answer(void) { return class; }
)c");
    write_text(tree.root / "main.cpp", R"cpp(#include <cstdio>
#include "mixed_api.h"
int main() {
    std::printf("mixed-answer=%d\n", mixed_c_answer());
    return mixed_c_answer() == 7 ? 0 : 1;
}
)cpp");

    auto mixed_cold = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "helper_mixed.c", "--env", "vs", "--std", "17", "--run", "-o", "mixed"});
    expect(mixed_cold.has_value(), "mixed C/C++ invocation should launch");
    if (mixed_cold) {
        if (mixed_cold->exit_code != 0) dump_failure(*mixed_cold);
        expect(mixed_cold->exit_code == 0, "mixed C/C++ build/run should succeed");
        expect(mixed_cold->stdout_text.find("[compile] main.cpp") != std::string::npos
                   && mixed_cold->stdout_text.find("[compile] helper_mixed.c") != std::string::npos,
               "mixed target should compile one C++ and one C TU");
        expect(mixed_cold->stdout_text.find("[link] mixed.exe") != std::string::npos,
               "mixed target should link normally");
        expect(mixed_cold->stdout_text.find("mixed-answer=7") != std::string::npos,
               "mixed executable should preserve the C ABI and run");
    }

    auto mixed_warm = run_mqb(
        runner,
        mqb_executable,
        tree.root,
        {"main.cpp", "helper_mixed.c", "--env", "vs", "--std", "17", "--run", "-o", "mixed"});
    expect(mixed_warm.has_value(), "warm mixed-language invocation should launch");
    if (mixed_warm) {
        if (mixed_warm->exit_code != 0) dump_failure(*mixed_warm);
        expect(mixed_warm->exit_code == 0, "warm mixed-language target should succeed");
        expect(contains_line(mixed_warm->stdout_text, "[up-to-date] main.cpp")
                   && contains_line(mixed_warm->stdout_text, "[up-to-date] helper_mixed.c"),
               "unchanged mixed-language TUs should reuse their compile caches");
        expect(contains_line(mixed_warm->stdout_text, "[up-to-date] mixed.exe"),
               "unchanged mixed-language target should reuse link cache");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_c_source_e2e_tests passed\n";
    return 0;
}
