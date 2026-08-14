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

[[nodiscard]] std::vector<std::string> raw_policy(int value, const fs::path& map_file) {
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
#include <windows.h>
#ifndef POLICY_VALUE
#error POLICY_VALUE must be supplied by raw build policy
#endif
int main() {
    std::printf("policy=%d\n", POLICY_VALUE);
    return 0;
}
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return POLICY_VALUE > 0 ? 0 : 1;
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
    expect(cold_run.has_value() && cold_run->exit_code == 0, "cold raw-policy executable should run");
    if (cold_run) expect(cold_run->stdout_text.find("policy=1") != std::string::npos, "raw compiler define should affect executable behavior");

    auto warm = run_mqb(runner, mqb_executable, tree.root, raw_policy(1, map_file));
    expect(warm.has_value(), "warm raw-policy invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm raw-policy build should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] main.cpp"), "unchanged raw compiler policy should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] policy.exe"), "unchanged raw linker policy should reuse link cache");
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
    expect(compiler_changed_run.has_value() && compiler_changed_run->exit_code == 0, "compiler-policy changed executable should run");
    if (compiler_changed_run) expect(compiler_changed_run->stdout_text.find("policy=2") != std::string::npos, "changed raw compiler argument should affect executable behavior");

    std::error_code remove_error;
    fs::remove(map_file, remove_error);
    expect(!remove_error && !fs::exists(map_file), "test should remove the first map artifact before linker-only rebuild");

    auto linker_policy = raw_policy(2, map_file);
    linker_policy.emplace_back("--linker-arg");
    linker_policy.emplace_back("/INCREMENTAL:NO");
    auto linker_changed = run_mqb(runner, mqb_executable, tree.root, linker_policy);
    expect(linker_changed.has_value(), "linker-policy change invocation should launch");
    if (linker_changed) {
        if (linker_changed->exit_code != 0) dump_failure(*linker_changed);
        expect(linker_changed->exit_code == 0, "linker-policy changed build should succeed");
        expect(contains_line(linker_changed->stdout_text, "[up-to-date] main.cpp"), "linker-only policy change must not recompile the TU");
        expect(linker_changed->stdout_text.find("[link] policy.exe") != std::string::npos
                   && linker_changed->stdout_text.find("linker options changed") != std::string::npos,
               "raw linker argument change should invalidate link recipe identity only");
    }
    expect(fs::is_regular_file(map_file), "linker-only rebuild should execute raw /MAP policy and recreate its side artifact");

    auto runtime_policy = raw_policy(2, map_file);
    runtime_policy.emplace_back("--runtime");
    runtime_policy.emplace_back("MT");
    auto runtime_changed = run_mqb(runner, mqb_executable, tree.root, runtime_policy);
    expect(runtime_changed.has_value(), "runtime-policy change invocation should launch");
    if (runtime_changed) {
        if (runtime_changed->exit_code != 0) dump_failure(*runtime_changed);
        expect(runtime_changed->exit_code == 0, "typed runtime changed build should succeed");
        expect(runtime_changed->stdout_text.find("[compile] main.cpp") != std::string::npos
                   && runtime_changed->stdout_text.find("compiler options changed") != std::string::npos,
               "typed runtime change should invalidate compile recipe identity");
        expect(runtime_changed->stdout_text.find("[link] policy.exe") != std::string::npos, "typed runtime change should relink after recompilation");
    }

    auto runtime_warm = run_mqb(runner, mqb_executable, tree.root, runtime_policy);
    expect(runtime_warm.has_value(), "warm typed-runtime invocation should launch");
    if (runtime_warm) {
        if (runtime_warm->exit_code != 0) dump_failure(*runtime_warm);
        expect(runtime_warm->exit_code == 0, "warm typed-runtime build should succeed");
        expect(contains_line(runtime_warm->stdout_text, "[up-to-date] main.cpp"), "unchanged typed runtime should reuse compile cache");
        expect(contains_line(runtime_warm->stdout_text, "[up-to-date] policy.exe"), "unchanged typed runtime should reuse link cache");
    }

    auto windows_policy = runtime_policy;
    windows_policy.emplace_back("--subsystem");
    windows_policy.emplace_back("windows");
    auto subsystem_changed = run_mqb(runner, mqb_executable, tree.root, windows_policy);
    expect(subsystem_changed.has_value(), "subsystem-policy change invocation should launch");
    if (subsystem_changed) {
        if (subsystem_changed->exit_code != 0) dump_failure(*subsystem_changed);
        expect(subsystem_changed->exit_code == 0, "Windows-subsystem target should link successfully");
        expect(contains_line(subsystem_changed->stdout_text, "[up-to-date] main.cpp"), "subsystem-only change must not recompile the TU");
        expect(subsystem_changed->stdout_text.find("[link] policy.exe") != std::string::npos
                   && subsystem_changed->stdout_text.find("linker options changed") != std::string::npos,
               "typed subsystem change should invalidate link recipe identity only");
    }

    auto subsystem_warm = run_mqb(runner, mqb_executable, tree.root, windows_policy);
    expect(subsystem_warm.has_value(), "warm Windows-subsystem invocation should launch");
    if (subsystem_warm) {
        if (subsystem_warm->exit_code != 0) dump_failure(*subsystem_warm);
        expect(subsystem_warm->exit_code == 0, "warm Windows-subsystem build should succeed");
        expect(contains_line(subsystem_warm->stdout_text, "[up-to-date] main.cpp"), "unchanged subsystem should keep compile cache warm");
        expect(contains_line(subsystem_warm->stdout_text, "[up-to-date] policy.exe"), "unchanged subsystem should keep link cache warm");
    }

    auto ltcg_policy = windows_policy;
    ltcg_policy.emplace_back("--ltcg");
    auto ltcg_changed = run_mqb(runner, mqb_executable, tree.root, ltcg_policy);
    expect(ltcg_changed.has_value(), "typed-LTCG invocation should launch");
    if (ltcg_changed) {
        if (ltcg_changed->exit_code != 0) dump_failure(*ltcg_changed);
        expect(ltcg_changed->exit_code == 0, "typed LTCG executable build should succeed");
        expect(ltcg_changed->stdout_text.find("[compile] main.cpp") != std::string::npos
                   && ltcg_changed->stdout_text.find("compiler options changed") != std::string::npos,
               "enabling typed LTCG should invalidate compile identity for /GL");
        expect(ltcg_changed->stdout_text.find("[link] policy.exe") != std::string::npos, "enabling typed LTCG should relink with /LTCG");
    }

    auto ltcg_warm = run_mqb(runner, mqb_executable, tree.root, ltcg_policy);
    expect(ltcg_warm.has_value(), "warm typed-LTCG invocation should launch");
    if (ltcg_warm) {
        if (ltcg_warm->exit_code != 0) dump_failure(*ltcg_warm);
        expect(ltcg_warm->exit_code == 0, "warm typed LTCG executable build should succeed");
        expect(contains_line(ltcg_warm->stdout_text, "[up-to-date] main.cpp"), "unchanged typed LTCG should reuse /GL compile cache");
        expect(contains_line(ltcg_warm->stdout_text, "[up-to-date] policy.exe"), "unchanged typed LTCG should reuse /LTCG link cache");
    }

    // Native semantic switches are normalized into the same typed build model.
    const fs::path native_semantic_executable = tree.root / ".mqb" / "bin" / "native_semantic.exe";
    auto native_semantic = run_mqb(
        runner, mqb_executable, tree.root,
        {"-o", "native_semantic",
         "--compiler-arg", "/DPOLICY_VALUE=4",
         "--compiler-arg", "/std:c++20",
         "--compiler-arg", "/MT",
         "--linker-arg", "/SUBSYSTEM:CONSOLE"});
    expect(native_semantic.has_value(), "native semantic policy invocation should launch");
    if (native_semantic) {
        if (native_semantic->exit_code != 0) dump_failure(*native_semantic);
        expect(native_semantic->exit_code == 0, "native /std, CRT, and subsystem semantics should normalize and build");
    }
    auto native_semantic_run = run_executable(runner, native_semantic_executable, tree.root);
    expect(native_semantic_run.has_value() && native_semantic_run->exit_code == 0,
           "native semantic policy executable should run");
    if (native_semantic_run) {
        expect(native_semantic_run->stdout_text.find("policy=4") != std::string::npos,
               "native semantic normalization must preserve safe passthrough compiler policy");
    }

    auto semantic_conflict = run_mqb(
        runner, mqb_executable, tree.root,
        {"--runtime", "MT", "--compiler-arg", "/MD", "--compiler-arg", "/DPOLICY_VALUE=1"});
    expect(semantic_conflict.has_value(), "same-layer semantic conflict invocation should launch MQB");
    if (semantic_conflict) {
        expect(semantic_conflict->exit_code == 2, "typed/native same-layer runtime conflict should fail before compilation");
        expect(semantic_conflict->stderr_text.find("conflicting typed and native MSVC values for runtime library") != std::string::npos,
               "same-layer semantic conflict should produce an ownership-aware diagnostic");
    }

    auto owned_escape = run_mqb(
        runner, mqb_executable, tree.root,
        {"--compiler-arg", "/Foescape.obj", "--compiler-arg", "/DPOLICY_VALUE=1"});
    expect(owned_escape.has_value(), "MQB-owned escape invocation should launch MQB");
    if (owned_escape) {
        expect(owned_escape->exit_code == 2, "MQB-owned /Fo escape should fail before cl.exe");
        expect(owned_escape->stderr_text.find("MQB-owned") != std::string::npos,
               "owned structural rejection should explain MQB ownership");
    }

    const fs::path config_map = tree.root / "config-policy.map";
    const std::string config = std::string{R"json({
  "version": 1,
  "build": {
    "standard": "17",
    "ltcg": true,
    "output": "config_policy",
    "compiler_args": ["/DPOLICY_VALUE=3", "/std:c++17", "/GL"],
    "linker_args": ["/LTCG", "/MAP:)json"}
        + path_text(config_map)
        + R"json("]
  }
})json";
    write_text(tree.root / "mqb.json", config);

    auto config_cold = run_mqb(runner, mqb_executable, tree.root, {});
    expect(config_cold.has_value(), "config build-policy invocation should launch");
    if (config_cold) {
        if (config_cold->exit_code != 0) dump_failure(*config_cold);
        expect(config_cold->exit_code == 0, "matching typed/native config semantics should normalize and build");
        expect(config_cold->stdout_text.find("[compile] main.cpp") != std::string::npos,
               "config standard/compiler/LTCG policy should produce a fresh compile");
        expect(config_cold->stdout_text.find("[link] config_policy.exe") != std::string::npos,
               "config linker/LTCG policy should produce the configured target");
    }
    expect(fs::is_regular_file(config_map), "mqb.json safe linker_args should reach link.exe");

    const fs::path config_executable = tree.root / ".mqb" / "bin" / "config_policy.exe";
    auto config_run = run_executable(runner, config_executable, tree.root);
    expect(config_run.has_value() && config_run->exit_code == 0, "config build-policy executable should run");
    if (config_run) expect(config_run->stdout_text.find("policy=3") != std::string::npos, "mqb.json compiler_args should affect executable behavior");

    auto config_warm = run_mqb(runner, mqb_executable, tree.root, {});
    expect(config_warm.has_value(), "warm config build-policy invocation should launch");
    if (config_warm) {
        if (config_warm->exit_code != 0) dump_failure(*config_warm);
        expect(config_warm->exit_code == 0, "warm config build-policy target should succeed");
        expect(contains_line(config_warm->stdout_text, "[up-to-date] main.cpp"), "unchanged config compiler/LTCG policy should be reusable");
        expect(contains_line(config_warm->stdout_text, "[up-to-date] config_policy.exe"), "unchanged config linker/LTCG policy should be reusable");
    }

    auto config_cli_disable = run_mqb(runner, mqb_executable, tree.root, {"--no-ltcg"});
    expect(config_cli_disable.has_value(), "CLI LTCG-disable override invocation should launch");
    if (config_cli_disable) {
        if (config_cli_disable->exit_code != 0) dump_failure(*config_cli_disable);
        expect(config_cli_disable->exit_code == 0, "--no-ltcg should override normalized build.ltcg=true and remain buildable");
        expect(config_cli_disable->stdout_text.find("[compile] main.cpp") != std::string::npos
                   && config_cli_disable->stdout_text.find("compiler options changed") != std::string::npos,
               "CLI disablement should invalidate the config-driven /GL compile identity");
        expect(config_cli_disable->stdout_text.find("[link] config_policy.exe") != std::string::npos,
               "CLI disablement should relink without typed /LTCG");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_build_policy_e2e_tests passed\n";
    return 0;
}
