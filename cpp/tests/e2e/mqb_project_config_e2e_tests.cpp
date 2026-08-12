#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
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

[[nodiscard]] std::expected<void, std::string> build_static_library(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const mqb::msvc::MsvcToolchain& toolchain,
    const fs::path& project_root,
    const fs::path& library_directory) {
    fs::create_directories(library_directory);
    const fs::path source = library_directory / "math_library.cpp";
    const fs::path object = library_directory / "math_library.obj";
    const fs::path library = library_directory / "math.lib";
    write_text(source, "int library_value() { return 100; }\n");

    mqb::process::ProcessSpec compile;
    compile.executable = toolchain.identity.compiler;
    compile.arguments = {
        "/nologo", "/c", "/Zl",
        "/Fo:" + path_text(object),
        path_text(source),
    };
    compile.working_directory = project_root;
    compile.environment = toolchain.environment;
    compile.inherit_environment = true;
    compile.capture_stdout = true;
    compile.capture_stderr = true;
    auto compiled = runner.run(compile);
    if (!compiled) return std::unexpected("failed to launch cl.exe: " + compiled.error().message);
    if (compiled->exit_code != 0) {
        dump_failure(*compiled);
        return std::unexpected("cl.exe failed while building config E2E library");
    }

    mqb::process::ProcessSpec archive;
    archive.executable = toolchain.librarian;
    archive.arguments = {"/NOLOGO", "/OUT:" + path_text(library), path_text(object)};
    archive.working_directory = project_root;
    archive.environment = toolchain.environment;
    archive.inherit_environment = true;
    archive.capture_stdout = true;
    archive.capture_stderr = true;
    auto archived = runner.run(archive);
    if (!archived) return std::unexpected("failed to launch lib.exe: " + archived.error().message);
    if (archived->exit_code != 0) {
        dump_failure(*archived);
        return std::unexpected("lib.exe failed while building config E2E library");
    }
    if (!fs::is_regular_file(library)) return std::unexpected("math.lib was not produced");
    return {};
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_mqb(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb,
    const fs::path& working_directory,
    const std::vector<std::string>& extra_arguments) {
    mqb::process::ProcessSpec spec;
    spec.executable = mqb;
    spec.arguments = {"../../main.cpp", "--env", "vs", "--discover"};
    spec.arguments.insert(spec.arguments.end(), extra_arguments.begin(), extra_arguments.end());
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch mqb: " + result.error().message);
    return std::move(*result);
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_executable(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& project_root) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.working_directory = project_root;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) return std::unexpected("failed to launch generated executable: " + result.error().message);
    return std::move(*result);
}

[[nodiscard]] std::string config_text(int config_value) {
    return std::string{R"json({
  "version": 1,
  "build": {
    "configuration": "release",
    "standard": "23",
    "output": "config_product",
    "defines": ["CONFIG_VALUE=)json"}
        + std::to_string(config_value)
        + R"json("],
    "include_dirs": ["include"],
    "library_dirs": ["vendor libs"],
    "libraries": ["math"]
  },
  "discovery": {
    "enabled": false,
    "exclude_dirs": ["tests"],
    "extra_sources": ["manual.cpp"],
    "exclude_sources": ["legacy.cpp"]
  }
})json";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_project_config_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_project_config_e2e_" + std::to_string(unique)),
    };
    const fs::path nested_working_directory = tree.root / "nested" / "work";
    const fs::path include_dir = tree.root / "include";
    const fs::path library_dir = tree.root / "vendor libs";
    fs::create_directories(nested_working_directory);

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions discovery;
    discovery.target_architecture = mqb::Architecture::x64;
    discovery.host_architecture = mqb::Architecture::x64;
    discovery.preference = mqb::msvc::ToolchainPreference::visual_studio;
    auto toolchain = locator.discover(discovery);
    expect(toolchain.has_value(), "VS2026 toolchain should be discoverable for config E2E");
    if (!toolchain) {
        std::cerr << toolchain.error().message << '\n';
        return 1;
    }
    auto library = build_static_library(runner, *toolchain, tree.root, library_dir);
    expect(library.has_value(), "static library fixture should build");
    if (!library) {
        std::cerr << library.error() << '\n';
        return 1;
    }

    write_text(include_dir / "value.hpp", "#pragma once\nint value();\n");
    write_text(tree.root / "src" / "value.cpp",
               "#include \"value.hpp\"\nint value() { return 1; }\n");
    write_text(tree.root / "legacy.hpp", "#pragma once\nint legacy();\n");
    write_text(tree.root / "private.hpp", "#pragma once\nint private_value();\n");
    write_text(tree.root / "legacy.cpp",
               "#include \"legacy.hpp\"\n#include \"private.hpp\"\nint legacy() { return private_value(); }\n");
    write_text(tree.root / "private.cpp",
               "#include \"private.hpp\"\nint private_value() { return 50; }\n");
    write_text(tree.root / "manual.cpp", "int manual_value() { return 3; }\n");
    write_text(tree.root / "tests" / "test_helper.cpp",
               "#include \"value.hpp\"\nint test_helper() { return value(); }\n");
    write_text(tree.root / "main.cpp", R"cpp(#include <cstdio>
#include "value.hpp"
#include "legacy.hpp"
#ifndef CONFIG_VALUE
#error CONFIG_VALUE must come from mqb.json
#endif
#ifndef CLI_VALUE
#define CLI_VALUE 0
#endif
int manual_value();
int library_value();
int main() {
#ifdef _DEBUG
    constexpr int debug_bonus = 1000;
#else
    constexpr int debug_bonus = 0;
#endif
    std::printf("config-e2e=%d\n", value() + manual_value() + library_value() + CONFIG_VALUE + CLI_VALUE + debug_bonus);
    return 0;
}
)cpp");
    write_text(tree.root / "mqb.json", config_text(5));

    auto cold = run_mqb(runner, mqb_executable, nested_working_directory, {"--verbose"});
    expect(cold.has_value(), "config-driven cold invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "config-driven cold build should succeed");
        expect(cold->stdout_text.find("[discover] 3 translation units") != std::string::npos,
               "CLI --discover should override config discovery.enabled=false");
        expect(cold->stdout_text.find("config:  ") != std::string::npos,
               "verbose output should report the loaded project config");
        expect(cold->stdout_text.find("[link] config_product.exe") != std::string::npos,
               "config output name should drive target identity");
    }

    const fs::path config_exe = tree.root / ".mqb" / "bin" / "config_product.exe";
    expect(fs::is_regular_file(config_exe),
           "config-root artifact layout should be used even when invoked from nested directory");
    expect(fs::is_regular_file(tree.root / ".mqb" / "obj" / "main.cpp.obj"),
           "entry object should be rooted at config project root");
    expect(fs::is_regular_file(tree.root / ".mqb" / "obj" / "src" / "value.cpp.obj"),
           "include-connected source should be discovered from config root");
    expect(fs::is_regular_file(tree.root / ".mqb" / "obj" / "manual.cpp.obj"),
           "config extra_sources should add disconnected TU");
    expect(!fs::exists(tree.root / ".mqb" / "obj" / "legacy.cpp.obj"),
           "config exclude_sources should remove exact TU");
    expect(!fs::exists(tree.root / ".mqb" / "obj" / "private.cpp.obj"),
           "excluded source should block traversal into private subgraph");
    expect(!fs::exists(tree.root / ".mqb" / "obj" / "tests" / "test_helper.cpp.obj"),
           "config exclude_dirs should prune test subtree");

    auto cold_run = run_executable(runner, config_exe, tree.root);
    expect(cold_run.has_value() && cold_run->exit_code == 0,
           "config-built executable should run successfully");
    if (cold_run) {
        expect(cold_run->stdout_text.find("config-e2e=109") != std::string::npos,
               "config define/include/library settings should affect executable behavior");
    }

    auto warm = run_mqb(runner, mqb_executable, nested_working_directory, {});
    expect(warm.has_value(), "warm config invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm config build should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"),
               "warm config build should reuse entry TU");
        expect(contains_line(warm->stdout_text, "[up-to-date] src/value.cpp"),
               "warm config build should reuse discovered TU");
        expect(contains_line(warm->stdout_text, "[up-to-date] manual.cpp"),
               "warm config build should reuse config extra TU");
        expect(contains_line(warm->stdout_text, "[up-to-date] config_product.exe"),
               "warm config build should reuse linked target");
    }

    write_text(tree.root / "mqb.json", config_text(6));
    auto config_changed = run_mqb(runner, mqb_executable, nested_working_directory, {});
    expect(config_changed.has_value(), "config-only change invocation should launch");
    if (config_changed) {
        if (config_changed->exit_code != 0) dump_failure(*config_changed);
        expect(config_changed->exit_code == 0, "config-only changed build should succeed");
        expect(config_changed->stdout_text.find("compiler options changed") != std::string::npos,
               "config define change should invalidate compile recipe without source edits");
        expect(config_changed->stdout_text.find("[link] config_product.exe") != std::string::npos,
               "fresh compiles from config change should force relink");
    }
    auto config_changed_run = run_executable(runner, config_exe, tree.root);
    if (config_changed_run) {
        expect(config_changed_run->stdout_text.find("config-e2e=110") != std::string::npos,
               "config-only define change should alter executable behavior");
    } else {
        expect(false, "config-changed executable should launch");
    }

    auto cli_override = run_mqb(
        runner,
        mqb_executable,
        nested_working_directory,
        {"--debug", "-DCLI_VALUE=10", "-o", "cli_product"});
    expect(cli_override.has_value(), "CLI override invocation should launch");
    if (cli_override) {
        if (cli_override->exit_code != 0) dump_failure(*cli_override);
        expect(cli_override->exit_code == 0, "CLI override build should succeed");
        expect(cli_override->stdout_text.find("compiler options changed") != std::string::npos,
               "CLI --debug should override config release and invalidate compile recipe");
        expect(cli_override->stdout_text.find("[link] cli_product.exe") != std::string::npos,
               "CLI output name should override config output name");
    }
    const fs::path cli_exe = tree.root / ".mqb" / "bin" / "cli_product.exe";
    auto cli_run = run_executable(runner, cli_exe, tree.root);
    expect(cli_run.has_value() && cli_run->exit_code == 0,
           "CLI-override executable should launch");
    if (cli_run) {
        expect(cli_run->stdout_text.find("config-e2e=1120") != std::string::npos,
               "CLI debug/define overrides should win while retaining config include/library inputs");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_project_config_e2e_tests passed\n";
    return 0;
}
