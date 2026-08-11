#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
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
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::string normalize_newlines(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const char ch : text) {
        if (ch != '\r') normalized.push_back(ch);
    }
    return normalized;
}

void dump_process(const std::string_view label, const mqb::process::ProcessResult& result) {
    std::cerr << label << " exit: " << result.exit_code << '\n'
              << label << " stdout:\n" << result.stdout_text << '\n'
              << label << " stderr:\n" << result.stderr_text << '\n';
}

[[nodiscard]] std::expected<void, std::string> copy_fixture(
    const fs::path& source,
    const fs::path& destination) {
    std::error_code ec;
    if (!fs::is_directory(source, ec) || ec) {
        return std::unexpected("fixture directory does not exist: " + path_text(source));
    }
    fs::create_directories(destination, ec);
    if (ec) {
        return std::unexpected("failed to create parity sandbox: " + ec.message());
    }

    for (fs::recursive_directory_iterator it{source, ec}, end; it != end; it.increment(ec)) {
        if (ec) {
            return std::unexpected("failed while enumerating parity fixture: " + ec.message());
        }
        const fs::path relative = it->path().lexically_relative(source);
        const fs::path target = destination / relative;
        if (it->is_directory(ec)) {
            if (ec) return std::unexpected("failed to inspect fixture directory: " + ec.message());
            fs::create_directories(target, ec);
            if (ec) return std::unexpected("failed to copy fixture directory: " + ec.message());
        } else if (it->is_regular_file(ec)) {
            if (ec) return std::unexpected("failed to inspect fixture file: " + ec.message());
            fs::create_directories(target.parent_path(), ec);
            if (ec) return std::unexpected("failed to prepare fixture target directory: " + ec.message());
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec) return std::unexpected("failed to copy parity fixture file: " + ec.message());
        }
    }
    return {};
}

[[nodiscard]] fs::path powershell_executable() {
    if (const char* system_root = std::getenv("SystemRoot"); system_root != nullptr) {
        const fs::path candidate = fs::path{system_root}
            / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe";
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && !ec) return candidate;
    }
    return fs::path{"powershell.exe"};
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string> run_process(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& executable,
    const fs::path& working_directory,
    std::vector<std::string> arguments = {}) {
    mqb::process::ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = std::move(arguments);
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected(
            "failed to launch '" + path_text(executable) + "': " + result.error().message);
    }
    return std::move(*result);
}

struct Scenario {
    std::string name;
    fs::path fixture;
    std::vector<std::string> sources;
    std::string output_name;
    std::vector<std::string> powershell_options;
    std::vector<std::string> cpp_options;
    std::string expected_stdout;
};

[[nodiscard]] std::vector<std::string> powershell_build_arguments(
    const fs::path& build_script,
    const Scenario& scenario) {
    std::vector<std::string> arguments{
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        path_text(build_script),
    };
    arguments.insert(arguments.end(), scenario.sources.begin(), scenario.sources.end());
    // Legacy -env is a persistent preference command that exits immediately;
    // it is not equivalent to native --env. A clean installed-MSVC CI runner
    // therefore lets the Golden Reference auto-detect Visual Studio.
    arguments.emplace_back("-o");
    arguments.push_back(scenario.output_name);
    arguments.insert(
        arguments.end(), scenario.powershell_options.begin(), scenario.powershell_options.end());
    return arguments;
}

[[nodiscard]] std::vector<std::string> cpp_build_arguments(const Scenario& scenario) {
    std::vector<std::string> arguments = scenario.sources;
    arguments.emplace_back("--env");
    arguments.emplace_back("vs");
    arguments.emplace_back("-o");
    arguments.push_back(scenario.output_name);
    arguments.insert(arguments.end(), scenario.cpp_options.begin(), scenario.cpp_options.end());
    return arguments;
}

void run_scenario(
    mqb::platform::windows::WindowsProcessRunner& runner,
    const fs::path& mqb_executable,
    const fs::path& build_script,
    const fs::path& fixtures_root,
    const fs::path& temp_root,
    const Scenario& scenario) {
    const fs::path fixture = fixtures_root / scenario.fixture;
    const fs::path scenario_root = temp_root / scenario.name;
    const fs::path powershell_root = scenario_root / "powershell";
    const fs::path cpp_root = scenario_root / "cpp";

    auto copied_ps = copy_fixture(fixture, powershell_root);
    expect(copied_ps.has_value(), scenario.name + ": PowerShell fixture copy should succeed");
    if (!copied_ps) {
        std::cerr << copied_ps.error() << '\n';
        return;
    }
    auto copied_cpp = copy_fixture(fixture, cpp_root);
    expect(copied_cpp.has_value(), scenario.name + ": C++ fixture copy should succeed");
    if (!copied_cpp) {
        std::cerr << copied_cpp.error() << '\n';
        return;
    }

    auto powershell_build = run_process(
        runner,
        powershell_executable(),
        powershell_root,
        powershell_build_arguments(build_script, scenario));
    expect(powershell_build.has_value(), scenario.name + ": PowerShell build should launch");
    if (!powershell_build) {
        std::cerr << powershell_build.error() << '\n';
        return;
    }
    if (powershell_build->exit_code != 0) dump_process("PowerShell build", *powershell_build);
    expect(powershell_build->exit_code == 0, scenario.name + ": PowerShell build should succeed");

    auto cpp_build = run_process(
        runner,
        mqb_executable,
        cpp_root,
        cpp_build_arguments(scenario));
    expect(cpp_build.has_value(), scenario.name + ": C++ build should launch");
    if (!cpp_build) {
        std::cerr << cpp_build.error() << '\n';
        return;
    }
    if (cpp_build->exit_code != 0) dump_process("C++ build", *cpp_build);
    expect(cpp_build->exit_code == 0, scenario.name + ": C++ build should succeed");

    if (powershell_build->exit_code != 0 || cpp_build->exit_code != 0) return;

    const fs::path powershell_output = powershell_root / (scenario.output_name + ".exe");
    const fs::path cpp_output = cpp_root / ".mqb" / "bin" / (scenario.output_name + ".exe");
    std::error_code ec;
    expect(fs::is_regular_file(powershell_output, ec) && !ec,
           scenario.name + ": PowerShell executable should exist");
    ec.clear();
    expect(fs::is_regular_file(cpp_output, ec) && !ec,
           scenario.name + ": C++ executable should exist");
    if (!fs::is_regular_file(powershell_output) || !fs::is_regular_file(cpp_output)) return;

    auto powershell_run = run_process(runner, powershell_output, powershell_root);
    expect(powershell_run.has_value(), scenario.name + ": PowerShell-built executable should launch");
    if (!powershell_run) {
        std::cerr << powershell_run.error() << '\n';
        return;
    }
    auto cpp_run = run_process(runner, cpp_output, cpp_root);
    expect(cpp_run.has_value(), scenario.name + ": C++-built executable should launch");
    if (!cpp_run) {
        std::cerr << cpp_run.error() << '\n';
        return;
    }

    if (powershell_run->exit_code != 0) dump_process("PowerShell program", *powershell_run);
    if (cpp_run->exit_code != 0) dump_process("C++ program", *cpp_run);
    expect(powershell_run->exit_code == 0, scenario.name + ": PowerShell-built program should succeed");
    expect(cpp_run->exit_code == 0, scenario.name + ": C++-built program should succeed");

    const std::string powershell_stdout = normalize_newlines(powershell_run->stdout_text);
    const std::string cpp_stdout = normalize_newlines(cpp_run->stdout_text);
    expect(powershell_stdout == scenario.expected_stdout,
           scenario.name + ": PowerShell program should satisfy fixture contract");
    expect(cpp_stdout == scenario.expected_stdout,
           scenario.name + ": C++ program should satisfy fixture contract");
    expect(powershell_stdout == cpp_stdout,
           scenario.name + ": PowerShell and C++ observable stdout should match");
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "usage: mqb_parity_e2e_tests <mqb-executable> <build.ps1> <fixtures-root>\n";
        return 2;
    }

    const fs::path mqb_executable = fs::absolute(fs::path{argv[1]}).lexically_normal();
    const fs::path build_script = fs::absolute(fs::path{argv[2]}).lexically_normal();
    const fs::path fixtures_root = fs::absolute(fs::path{argv[3]}).lexically_normal();

    std::error_code ec;
    if (!fs::is_regular_file(mqb_executable, ec) || ec) {
        std::cerr << "mqb executable does not exist: " << path_text(mqb_executable) << '\n';
        return 2;
    }
    ec.clear();
    if (!fs::is_regular_file(build_script, ec) || ec) {
        std::cerr << "PowerShell Golden Reference does not exist: " << path_text(build_script) << '\n';
        return 2;
    }
    ec.clear();
    if (!fs::is_directory(fixtures_root, ec) || ec) {
        std::cerr << "parity fixture root does not exist: " << path_text(fixtures_root) << '\n';
        return 2;
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_shared_parity_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    mqb::platform::windows::WindowsProcessRunner runner;
    const std::vector<Scenario> scenarios{
        Scenario{
            .name = "single",
            .fixture = "single",
            .sources = {"main.cpp"},
            .output_name = "parity_single",
            .expected_stdout = "single=17\n",
        },
        Scenario{
            .name = "multi",
            .fixture = "multi",
            .sources = {"main.cpp", "value.cpp"},
            .output_name = "parity_multi",
            .expected_stdout = "multi=42\n",
        },
        Scenario{
            .name = "configuration_debug",
            .fixture = "configuration",
            .sources = {"main.cpp"},
            .output_name = "parity_debug",
            .powershell_options = {"-config", "debug"},
            .cpp_options = {"--config", "debug"},
            .expected_stdout = "config=debug\n",
        },
        Scenario{
            .name = "configuration_release",
            .fixture = "configuration",
            .sources = {"main.cpp"},
            .output_name = "parity_release",
            .powershell_options = {"-config", "release"},
            .cpp_options = {"--config", "release"},
            .expected_stdout = "config=release\n",
        },
    };

    for (const auto& scenario : scenarios) {
        run_scenario(
            runner,
            mqb_executable,
            build_script,
            fixtures_root,
            tree.root,
            scenario);
    }

    if (failures != 0) {
        std::cerr << failures << " parity assertion(s) failed\n";
        return 1;
    }
    std::cout << "mqb_parity_e2e_tests passed (" << scenarios.size() << " shared scenarios)\n";
    return 0;
}
