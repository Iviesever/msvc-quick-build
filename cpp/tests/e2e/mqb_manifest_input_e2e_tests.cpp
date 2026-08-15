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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

std::string manifest_text(std::string_view marker) {
    return "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>\n"
           "<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>\n"
           "  <file name='" + std::string{marker} + "'/>\n"
           "</assembly>\n";
}

bool contains_line(std::string_view output, std::string_view expected) {
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

std::expected<mqb::process::ProcessResult, std::string> run_mqb(
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

void make_newer(const fs::path& input, const fs::path& output) {
    std::error_code error;
    const auto output_time = fs::last_write_time(output, error);
    expect(!error, "should read linked output timestamp");
    if (error) return;
    fs::last_write_time(input, output_time + std::chrono::seconds{2}, error);
    expect(!error, "should advance manifest input timestamp");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_manifest_input_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_manifest_input_e2e_" + std::to_string(unique))};
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    write_text(tree.root / "main.cpp", "int main() { return 0; }\n");
    write_text(tree.root / "first.manifest", manifest_text("mqb-first-v1.dll"));
    write_text(tree.root / "second.manifest", manifest_text("mqb-second-v1.dll"));

    const std::vector<std::string> arguments{
        "main.cpp", "--no-discover", "--env", "vs", "--runtime", "MT", "-o", "manifested",
        "/link", "/MANIFEST:EMBED", "/MANIFESTINPUT:first.manifest", "/MANIFESTINPUT:second.manifest"};
    const fs::path output = tree.root / ".mqb" / "bin" / "manifested.exe";

    auto cold = run_mqb(runner, mqb_executable, tree.root, arguments);
    expect(cold.has_value(), "cold cumulative manifest invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "real LINK should merge two /MANIFESTINPUT files");
        expect(cold->stdout_text.find("[link] manifested.exe") != std::string::npos,
               "cold cumulative manifest target should link");
    }
    expect(fs::is_regular_file(output), "cumulative manifest target should produce executable");

    auto warm = run_mqb(runner, mqb_executable, tree.root, arguments);
    expect(warm.has_value(), "warm cumulative manifest invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm cumulative manifest target should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] manifested.exe"),
               "unchanged manifest inputs should reuse link cache");
        expect(warm->stdout_text.find("[link] manifested.exe") == std::string::npos,
               "unchanged manifest inputs should remain zero-LINK");
    }

    write_text(tree.root / "first.manifest", manifest_text("mqb-first-v2.dll"));
    make_newer(tree.root / "first.manifest", output);
    auto first_changed = run_mqb(runner, mqb_executable, tree.root, arguments);
    expect(first_changed.has_value(), "first manifest mutation should launch");
    if (first_changed) {
        if (first_changed->exit_code != 0) dump_failure(*first_changed);
        expect(first_changed->exit_code == 0, "first manifest mutation should relink successfully");
        expect(contains_line(first_changed->stdout_text, "[up-to-date] main.cpp"),
               "manifest-only mutation must not recompile source TU");
        expect(first_changed->stdout_text.find("[link] manifested.exe") != std::string::npos,
               "first cumulative input must participate in freshness");
    }

    write_text(tree.root / "second.manifest", manifest_text("mqb-second-v2.dll"));
    make_newer(tree.root / "second.manifest", output);
    auto second_changed = run_mqb(runner, mqb_executable, tree.root, arguments);
    expect(second_changed.has_value(), "second manifest mutation should launch");
    if (second_changed) {
        if (second_changed->exit_code != 0) dump_failure(*second_changed);
        expect(second_changed->exit_code == 0, "second manifest mutation should relink successfully");
        expect(contains_line(second_changed->stdout_text, "[up-to-date] main.cpp"),
               "second manifest-only mutation must not recompile source TU");
        expect(second_changed->stdout_text.find("[link] manifested.exe") != std::string::npos,
               "second cumulative input must participate in freshness");
    }

    const fs::path project = tree.root / "layered-project";
    const fs::path child = project / "child";
    fs::create_directories(child);
    write_text(project / "main.cpp", "int main() { return 0; }\n");
    write_text(project / "config.manifest", manifest_text("mqb-config-v1.dll"));
    write_text(child / "cli.manifest", manifest_text("mqb-cli-v1.dll"));
    write_text(project / "mqb.json", R"json({
  "version": 1,
  "build": {
    "entry": "main.cpp",
    "type": "exe",
    "runtime": "MT",
    "output": "layered-manifest",
    "linker_args": ["/MANIFEST:EMBED", "/MANIFESTINPUT:config.manifest"]
  },
  "discovery": {"enabled": false}
}
)json");

    const std::vector<std::string> layered_args{
        "build", "--env", "vs", "/link", "/MANIFESTINPUT:cli.manifest"};
    const fs::path layered_output = project / ".mqb" / "bin" / "layered-manifest.exe";
    auto layered = run_mqb(runner, mqb_executable, child, layered_args);
    expect(layered.has_value(), "cross-layer cumulative manifest invocation should launch");
    if (layered) {
        if (layered->exit_code != 0) dump_failure(*layered);
        expect(layered->exit_code == 0,
               "project-root config input plus invocation-relative CLI input should both reach LINK");
    }
    expect(fs::is_regular_file(layered_output), "cross-layer manifest target should produce executable");

    auto layered_warm = run_mqb(runner, mqb_executable, child, layered_args);
    expect(layered_warm.has_value(), "cross-layer warm invocation should launch");
    if (layered_warm) {
        if (layered_warm->exit_code != 0) dump_failure(*layered_warm);
        expect(layered_warm->exit_code == 0, "cross-layer warm build should succeed");
        expect(contains_line(layered_warm->stdout_text, "[up-to-date] layered-manifest.exe"),
               "unchanged cross-layer manifest inputs should reuse link cache");
    }

    write_text(project / "config.manifest", manifest_text("mqb-config-v2.dll"));
    make_newer(project / "config.manifest", layered_output);
    auto config_changed = run_mqb(runner, mqb_executable, child, layered_args);
    expect(config_changed.has_value(), "config manifest mutation should launch");
    if (config_changed) {
        if (config_changed->exit_code != 0) dump_failure(*config_changed);
        expect(config_changed->exit_code == 0, "config manifest mutation should relink successfully");
        expect(config_changed->stdout_text.find("[link] layered-manifest.exe") != std::string::npos,
               "config MANIFESTINPUT must remain cumulative across CLI overlay");
    }

    write_text(child / "cli.manifest", manifest_text("mqb-cli-v2.dll"));
    make_newer(child / "cli.manifest", layered_output);
    auto cli_changed = run_mqb(runner, mqb_executable, child, layered_args);
    expect(cli_changed.has_value(), "CLI manifest mutation should launch");
    if (cli_changed) {
        if (cli_changed->exit_code != 0) dump_failure(*cli_changed);
        expect(cli_changed->exit_code == 0, "CLI manifest mutation should relink successfully");
        expect(cli_changed->stdout_text.find("[link] layered-manifest.exe") != std::string::npos,
               "CLI MANIFESTINPUT must participate in cumulative freshness");
    }

    auto missing_embed = run_mqb(
        runner, mqb_executable, tree.root,
        {"main.cpp", "--no-discover", "--env", "vs", "-o", "missing-embed",
         "/link", "/MANIFESTINPUT:first.manifest"});
    expect(missing_embed.has_value(), "missing EMBED validation should launch");
    if (missing_embed) {
        expect(missing_embed->exit_code != 0, "/MANIFESTINPUT without EMBED must fail");
        expect(missing_embed->stderr_text.find("/MANIFESTINPUT requires the final linker manifest mode to be /MANIFEST:EMBED") != std::string::npos,
               "missing EMBED should report final manifest-mode requirement");
        expect(missing_embed->stdout_text.find("[link] missing-embed.exe") == std::string::npos,
               "missing EMBED must reject before LINK");
    }

    auto final_no = run_mqb(
        runner, mqb_executable, tree.root,
        {"main.cpp", "--no-discover", "--env", "vs", "-o", "final-no", "/link",
         "/MANIFEST:EMBED", "/MANIFESTINPUT:first.manifest", "/MANIFEST:NO"});
    expect(final_no.has_value(), "final MANIFEST:NO validation should launch");
    if (final_no) {
        expect(final_no->exit_code != 0, "later /MANIFEST:NO must override EMBED and invalidate MANIFESTINPUT");
        expect(final_no->stderr_text.find("/MANIFESTINPUT requires the final linker manifest mode to be /MANIFEST:EMBED") != std::string::npos,
               "final MANIFEST:NO should report final manifest-mode requirement");
        expect(final_no->stdout_text.find("[link] final-no.exe") == std::string::npos,
               "final MANIFEST:NO must reject before LINK");
    }

    auto empty = run_mqb(
        runner, mqb_executable, tree.root,
        {"main.cpp", "--no-discover", "--env", "vs", "/link", "/MANIFEST:EMBED", "/MANIFESTINPUT:"});
    expect(empty.has_value(), "empty MANIFESTINPUT validation should launch");
    if (empty) {
        expect(empty->exit_code != 0, "empty /MANIFESTINPUT must fail before LINK");
        expect(empty->stderr_text.find("/MANIFESTINPUT requires a manifest file path") != std::string::npos,
               "empty /MANIFESTINPUT should report required file operand");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_manifest_input_e2e_tests passed\n";
    return 0;
}
