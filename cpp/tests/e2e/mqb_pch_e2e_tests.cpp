#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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

void rewrite_after_tick(const fs::path& path, const std::string_view text) {
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    write_text(path, text);
}

void dump_failure(const mqb::process::ProcessResult& result) {
    std::cerr << "exit: " << result.exit_code << '\n'
              << "stdout:\n" << result.stdout_text << '\n'
              << "stderr:\n" << result.stderr_text << '\n';
}

[[nodiscard]] std::expected<mqb::process::ProcessResult, std::string>
run_process(
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
        return std::unexpected("failed to launch process: " + result.error().message);
    }
    return std::move(*result);
}

[[nodiscard]] bool contains(const mqb::process::ProcessResult& result, const std::string_view text) {
    return result.stdout_text.find(text) != std::string::npos
        || result.stderr_text.find(text) != std::string::npos;
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

[[nodiscard]] std::vector<std::string> build_args() {
    return {
        "build", "main.cpp", "worker.cpp",
        "--env", "vs", "--no-discover", "--debug",
        "--pch", "include/pch.hpp",
        "-o", "pch_app",
    };
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_pch_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_pch_e2e_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path pch_header = tree.root / "include/pch.hpp";
    write_text(
        pch_header,
        "#pragma once\n"
        "struct PchValue { int value; };\n"
        "inline constexpr int pch_bias = 10;\n");
    write_text(
        tree.root / "worker.cpp",
        "int worker_value() { PchValue v{32}; return v.value + pch_bias; }\n");
    write_text(
        tree.root / "main.cpp",
        "#include <cstdio>\n"
        "int worker_value();\n"
        "int main() { std::printf(\"pch=%d\\n\", worker_value()); return worker_value() == 42 ? 0 : 1; }\n");

    mqb::platform::windows::WindowsProcessRunner runner;

    auto cold = run_process(runner, mqb_executable, tree.root, build_args());
    expect(cold.has_value(), "cold PCH build should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "cold PCH build should succeed");
        expect(contains(*cold, "[pch]"), "cold build should report PCH creation");
    }

    const fs::path pch_root = tree.root / ".mqb/pch/pch_app/debug/x64";
    const fs::path pch_file = pch_root / "project.pch";
    const fs::path creator_object = pch_root / "creator.obj";
    const fs::path executable = tree.root / ".mqb/bin/pch_app.exe";
    expect(fs::is_regular_file(pch_file), "cold build should produce MQB-owned .pch");
    expect(fs::is_regular_file(creator_object), "cold build should retain the paired PCH creator object");
    expect(fs::is_regular_file(executable), "cold PCH build should link executable");

    auto run = run_process(runner, executable, tree.root);
    expect(run.has_value(), "PCH executable should launch");
    if (run) {
        if (run->exit_code != 0) dump_failure(*run);
        expect(run->exit_code == 0, "PCH executable should succeed");
        expect(run->stdout_text.find("pch=42") != std::string::npos,
               "forced PCH include should provide declarations to ordinary sources");
    }

    auto warm = run_process(runner, mqb_executable, tree.root, build_args());
    expect(warm.has_value(), "warm PCH build should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm PCH build should succeed");
        expect(contains(*warm, "[up-to-date] pch"), "warm build should reuse PCH cache");
        expect(contains(*warm, "[up-to-date] main.cpp"), "warm build should reuse main object");
        expect(contains(*warm, "[up-to-date] worker.cpp"), "warm build should reuse worker object");
        expect(contains(*warm, "[up-to-date] pch_app.exe"), "warm build should reuse linked target");
    }

    rewrite_after_tick(
        tree.root / "main.cpp",
        "#include <cstdio>\n"
        "int worker_value();\n"
        "int main() { const int value = worker_value(); std::printf(\"pch=%d\\n\", value); return value == 42 ? 0 : 1; }\n");
    auto source_changed = run_process(runner, mqb_executable, tree.root, build_args());
    expect(source_changed.has_value(), "source-only PCH build should launch");
    if (source_changed) {
        if (source_changed->exit_code != 0) dump_failure(*source_changed);
        expect(source_changed->exit_code == 0, "source-only change should build successfully");
        expect(contains(*source_changed, "[up-to-date] pch"),
               "ordinary source change must not rebuild the PCH creator");
        expect(contains(*source_changed, "[compile] main.cpp"),
               "changed ordinary source should rebuild its object");
    }

    rewrite_after_tick(
        pch_header,
        "#pragma once\n"
        "struct PchValue { int value; };\n"
        "inline constexpr int pch_bias = 11;\n");
    auto header_changed = run_process(runner, mqb_executable, tree.root, build_args());
    expect(header_changed.has_value(), "header-changed PCH build should launch");
    if (header_changed) {
        if (header_changed->exit_code != 0) dump_failure(*header_changed);
        expect(header_changed->exit_code == 0, "header change should rebuild PCH target successfully");
        expect(contains(*header_changed, "[pch]"), "PCH header change should rebuild the creator");
        expect(contains(*header_changed, "[compile] main.cpp")
                   && contains(*header_changed, "[compile] worker.cpp"),
               "PCH rebuild should invalidate all ordinary PCH consumers");
        expect(contains(*header_changed, "[link] pch_app.exe"),
               "PCH creator rebuild should force downstream relink");
    }

    std::error_code remove_error;
    fs::remove(pch_file, remove_error);
    expect(!remove_error, "test should be able to remove the PCH artifact");
    auto repaired = run_process(runner, mqb_executable, tree.root, build_args());
    expect(repaired.has_value(), "missing-PCH repair build should launch");
    if (repaired) {
        if (repaired->exit_code != 0) dump_failure(*repaired);
        expect(repaired->exit_code == 0, "missing PCH artifact should be repairable");
        expect(contains(*repaired, "[pch]"), "missing .pch should invalidate creator cache");
        expect(fs::is_regular_file(pch_file), "repair should restore the owned .pch artifact");
    }

    // Final Closure #4: smart discovery must observe the effective first-class
    // PCH as compiler-visible forced-include semantics before source selection.
    {
        TempTree discovery_tree{
            .root = fs::temp_directory_path()
                / ("mqb_pch_discovery_e2e_" + std::to_string(unique)),
        };
        fs::create_directories(discovery_tree.root);

        write_text(
            discovery_tree.root / "pch.hpp",
            "#pragma once\n"
            "#include \"widget.hpp\"\n"
            "#include \"nested/root.hpp\"\n");
        write_text(discovery_tree.root / "widget.hpp", "#pragma once\nint widget_value();\n");
        write_text(discovery_tree.root / "widget.cpp", "int widget_value() { return 20; }\n");
        write_text(
            discovery_tree.root / "nested/root.hpp",
            "#pragma once\n"
            "#include \"impl/deep.hpp\"\n");
        write_text(
            discovery_tree.root / "nested/impl/deep.hpp",
            "#pragma once\nint deep_value();\n");
        write_text(
            discovery_tree.root / "nested/impl/deep.cpp",
            "int deep_value() { return 22; }\n");
        write_text(
            discovery_tree.root / "unrelated.cpp",
            "int unrelated_pch_discovery_value() { return 99; }\n");
        write_text(
            discovery_tree.root / "main.cpp",
            "int main() { return widget_value() + deep_value() == 42 ? 0 : 1; }\n");

        auto pch_only_reachable = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "main.cpp", "--env", "vs", "--debug",
             "--pch", "pch.hpp", "--verbose", "-o", "pch_discovery"});
        expect(pch_only_reachable.has_value(),
               "PCH-aware smart-discovery build should launch");
        if (pch_only_reachable) {
            if (pch_only_reachable->exit_code != 0) dump_failure(*pch_only_reachable);
            expect(pch_only_reachable->exit_code == 0,
                   "PCH-only reachable implementation sources should build successfully");
            expect(contains(*pch_only_reachable, "[discover] 3 translation units"),
                   "PCH root plus nested include should select exactly main, widget, and deep TUs");
            expect(contains(*pch_only_reachable, "widget.cpp"),
                   "direct PCH-only header ownership should select widget.cpp");
            expect(contains(*pch_only_reachable, "deep.cpp"),
                   "nested include reachable only through PCH should select deep.cpp");
            expect(!contains(*pch_only_reachable, "unrelated.cpp"),
                   "PCH discovery root must not turn the indexed project into a global source hub");
        }

        write_text(discovery_tree.root / "alpha.hpp", "#pragma once\nint alpha_value();\n");
        write_text(discovery_tree.root / "alpha.cpp", "int alpha_value() { return 1; }\n");
        write_text(discovery_tree.root / "beta.hpp", "#pragma once\nint beta_value();\n");
        write_text(discovery_tree.root / "beta.cpp", "int beta_value() { return 2; }\n");
        write_text(discovery_tree.root / "raw.hpp", "#pragma once\nint raw_value();\n");
        write_text(discovery_tree.root / "raw.cpp", "int raw_value() { return 3; }\n");
        write_text(
            discovery_tree.root / "base_pch.hpp",
            "#pragma once\n#include \"alpha.hpp\"\n");
        write_text(
            discovery_tree.root / "profile_pch.hpp",
            "#pragma once\n#include \"beta.hpp\"\n");
        write_text(discovery_tree.root / "identity_main.cpp", "int main() { return 0; }\n");
        write_text(
            discovery_tree.root / "mqb.json",
            R"json({
  "version": 1,
  "build": {
    "pch": "base_pch.hpp"
  },
  "profiles": {
    "alternate": {
      "build": {
        "pch": "profile_pch.hpp"
      }
    }
  }
})json");

        auto base_discovery = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "identity_main.cpp", "--env", "vs", "--debug",
             "--verbose", "-o", "pch_discovery_base"});
        expect(base_discovery.has_value(), "base PCH discovery build should launch");
        if (base_discovery) {
            if (base_discovery->exit_code != 0) dump_failure(*base_discovery);
            expect(base_discovery->exit_code == 0, "base PCH discovery build should succeed");
            expect(contains(*base_discovery, "[discover] 2 translation units")
                       && contains(*base_discovery, "alpha.cpp")
                       && !contains(*base_discovery, "beta.cpp"),
                   "base PCH should project alpha.hpp ownership, and only alpha.cpp, into discovery");
        }

        rewrite_after_tick(
            discovery_tree.root / "base_pch.hpp",
            "#pragma once\n#include \"beta.hpp\"\n");
        auto changed_pch_discovery = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "identity_main.cpp", "--env", "vs", "--debug",
             "--verbose", "-o", "pch_discovery_changed"});
        expect(changed_pch_discovery.has_value(), "changed PCH discovery build should launch");
        if (changed_pch_discovery) {
            if (changed_pch_discovery->exit_code != 0) dump_failure(*changed_pch_discovery);
            expect(changed_pch_discovery->exit_code == 0,
                   "changing PCH include graph should rediscover successfully");
            expect(contains(*changed_pch_discovery, "beta.cpp")
                       && !contains(*changed_pch_discovery, "alpha.cpp"),
                   "PCH content changes must alter selected source closure");
        }

        rewrite_after_tick(
            discovery_tree.root / "base_pch.hpp",
            "#pragma once\n#include \"alpha.hpp\"\n");
        auto resealed_base_discovery = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "identity_main.cpp", "--env", "vs", "--debug",
             "--verbose", "-o", "pch_discovery_resealed_base"});
        expect(resealed_base_discovery.has_value(), "resealed base PCH discovery should launch");
        if (resealed_base_discovery) {
            if (resealed_base_discovery->exit_code != 0) dump_failure(*resealed_base_discovery);
            expect(resealed_base_discovery->exit_code == 0
                       && contains(*resealed_base_discovery, "alpha.cpp")
                       && !contains(*resealed_base_discovery, "beta.cpp"),
                   "base PCH closure should be resealed before path-identity regression check");
        }

        auto profile_discovery = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "identity_main.cpp", "--env", "vs", "--debug",
             "--profile", "alternate", "--verbose", "-o", "pch_discovery_profile"});
        expect(profile_discovery.has_value(), "profile PCH discovery build should launch");
        if (profile_discovery) {
            if (profile_discovery->exit_code != 0) dump_failure(*profile_discovery);
            expect(profile_discovery->exit_code == 0, "profile PCH discovery build should succeed");
            expect(contains(*profile_discovery, "beta.cpp")
                       && !contains(*profile_discovery, "alpha.cpp"),
                   "profile PCH overlay must change discovery and invalidate base-PCH cache identity");
        }

        auto no_pch_discovery = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "identity_main.cpp", "--env", "vs", "--debug",
             "--profile", "alternate", "--no-pch", "--verbose",
             "-o", "pch_discovery_off"});
        expect(no_pch_discovery.has_value(), "--no-pch discovery build should launch");
        if (no_pch_discovery) {
            if (no_pch_discovery->exit_code != 0) dump_failure(*no_pch_discovery);
            expect(no_pch_discovery->exit_code == 0, "--no-pch discovery build should succeed");
            expect(contains(*no_pch_discovery, "[discover] 1 translation units")
                       && !contains(*no_pch_discovery, "alpha.cpp")
                       && !contains(*no_pch_discovery, "beta.cpp"),
                   "CLI --no-pch must remove the inherited/profile forced discovery root");
        }

        auto raw_and_pch_discovery = run_process(
            runner,
            mqb_executable,
            discovery_tree.root,
            {"build", "identity_main.cpp", "--env", "vs", "--debug",
             "--pch", "base_pch.hpp", "/FIraw.hpp", "--verbose",
             "-o", "pch_discovery_raw_and_typed"});
        expect(raw_and_pch_discovery.has_value(), "raw /FI + typed PCH discovery build should launch");
        if (raw_and_pch_discovery) {
            if (raw_and_pch_discovery->exit_code != 0) dump_failure(*raw_and_pch_discovery);
            expect(raw_and_pch_discovery->exit_code == 0,
                   "raw /FI and first-class PCH should coexist through smart discovery");
            expect(contains(*raw_and_pch_discovery, "[discover] 3 translation units")
                       && contains(*raw_and_pch_discovery, "alpha.cpp")
                       && contains(*raw_and_pch_discovery, "raw.cpp"),
                   "discovery must union raw /FI and MQB-owned PCH forced roots");
        }
    }

    {
        TempTree ordinary_tree{
            .root = fs::temp_directory_path()
                / ("mqb_no_pch_discovery_e2e_" + std::to_string(unique)),
        };
        fs::create_directories(ordinary_tree.root);
        write_text(
            ordinary_tree.root / "main.cpp",
            "#include \"ordinary.hpp\"\nint main() { return ordinary_value() == 7 ? 0 : 1; }\n");
        write_text(ordinary_tree.root / "ordinary.hpp", "#pragma once\nint ordinary_value();\n");
        write_text(ordinary_tree.root / "ordinary.cpp", "int ordinary_value() { return 7; }\n");
        write_text(ordinary_tree.root / "unrelated.cpp", "int unrelated_value() { return 0; }\n");

        auto ordinary_discovery = run_process(
            runner,
            mqb_executable,
            ordinary_tree.root,
            {"build", "main.cpp", "--env", "vs", "--debug",
             "--verbose", "-o", "ordinary_discovery"});
        expect(ordinary_discovery.has_value(), "ordinary no-PCH discovery build should launch");
        if (ordinary_discovery) {
            if (ordinary_discovery->exit_code != 0) dump_failure(*ordinary_discovery);
            expect(ordinary_discovery->exit_code == 0, "ordinary no-PCH discovery should remain valid");
            expect(contains(*ordinary_discovery, "[discover] 2 translation units")
                       && contains(*ordinary_discovery, "ordinary.cpp")
                       && !contains(*ordinary_discovery, "unrelated.cpp"),
                   "ordinary smart-discovery behavior must remain unchanged without PCH");
        }
    }

    // Exercise the project/profile/CLI scalar precedence through the real
    // candidate binary from a nested invocation directory. Config/profile PCH
    // paths must remain config-relative while CLI PCH paths remain invocation-relative.
    write_text(tree.root / "include/base.hpp", "#pragma once\ninline constexpr int base_marker = 1;\n");
    write_text(tree.root / "include/profile.hpp", "#pragma once\ninline constexpr int profile_marker = 2;\n");
    write_text(tree.root / "include/cli.hpp", "#pragma once\ninline constexpr int cli_marker = 3;\n");
    write_text(
        tree.root / "config_main.cpp",
        "#include \"include/base.hpp\"\n"
        "int main() { return base_marker == 1 ? 0 : 1; }\n");
    write_text(
        tree.root / "mqb.json",
        R"json({
  "version": 1,
  "build": {
    "pch": "include/base.hpp"
  },
  "profiles": {
    "profile": {
      "build": {
        "pch": "include/profile.hpp"
      }
    },
    "off": {
      "build": {
        "pch": false
      }
    }
  }
})json");
    const fs::path nested = tree.root / "nested/work";
    fs::create_directories(nested);

    auto config_base = run_process(
        runner,
        mqb_executable,
        nested,
        {"build", "../../config_main.cpp", "--env", "vs", "--no-discover",
         "-o", "pch_config_base"});
    expect(config_base.has_value(), "base-config PCH build should launch from nested directory");
    if (config_base) {
        if (config_base->exit_code != 0) dump_failure(*config_base);
        expect(config_base->exit_code == 0, "base-config PCH build should succeed");
        expect(contains(*config_base, "[pch]") && contains(*config_base, "base.hpp"),
               "base build.pch should resolve relative to mqb.json and create that PCH");
    }

    auto profile_override = run_process(
        runner,
        mqb_executable,
        nested,
        {"build", "../../config_main.cpp", "--env", "vs", "--no-discover",
         "--profile", "profile", "-o", "pch_config_profile"});
    expect(profile_override.has_value(), "profile PCH override build should launch");
    if (profile_override) {
        if (profile_override->exit_code != 0) dump_failure(*profile_override);
        expect(profile_override->exit_code == 0, "profile PCH override should build successfully");
        expect(contains(*profile_override, "[pch]") && contains(*profile_override, "profile.hpp"),
               "selected profile PCH should override base PCH using config-relative path semantics");
    }

    auto profile_disable = run_process(
        runner,
        mqb_executable,
        nested,
        {"build", "../../config_main.cpp", "--env", "vs", "--no-discover",
         "--profile", "off", "-o", "pch_config_profile_off"});
    expect(profile_disable.has_value(), "profile PCH disable build should launch");
    if (profile_disable) {
        if (profile_disable->exit_code != 0) dump_failure(*profile_disable);
        expect(profile_disable->exit_code == 0, "profile pch:false should disable base PCH and still build");
        expect(!contains(*profile_disable, "[pch]") && !contains(*profile_disable, "[up-to-date] pch"),
               "profile pch:false should suppress PCH orchestration inherited from base config");
    }

    auto cli_override = run_process(
        runner,
        mqb_executable,
        nested,
        {"build", "../../config_main.cpp", "--env", "vs", "--no-discover",
         "--profile", "profile", "--pch", "../../include/cli.hpp",
         "-o", "pch_config_cli"});
    expect(cli_override.has_value(), "CLI PCH override build should launch");
    if (cli_override) {
        if (cli_override->exit_code != 0) dump_failure(*cli_override);
        expect(cli_override->exit_code == 0, "CLI PCH override should build successfully");
        expect(contains(*cli_override, "[pch]") && contains(*cli_override, "cli.hpp"),
               "CLI --pch should override selected profile and resolve relative to invocation directory");
    }

    auto cli_disable = run_process(
        runner,
        mqb_executable,
        nested,
        {"build", "../../config_main.cpp", "--env", "vs", "--no-discover",
         "--profile", "profile", "--no-pch", "-o", "pch_config_cli_off"});
    expect(cli_disable.has_value(), "CLI PCH disable build should launch");
    if (cli_disable) {
        if (cli_disable->exit_code != 0) dump_failure(*cli_disable);
        expect(cli_disable->exit_code == 0, "CLI --no-pch should override selected profile and build");
        expect(!contains(*cli_disable, "[pch]") && !contains(*cli_disable, "[up-to-date] pch"),
               "CLI --no-pch should suppress profile/base PCH orchestration");
    }

    write_text(tree.root / "plain.c", "int main(void) { return 0; }\n");
    auto c_rejected = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"build", "plain.c", "--env", "vs", "--no-discover", "--pch", "include/pch.hpp"});
    expect(c_rejected.has_value(), "C + PCH rejection should launch candidate MQB");
    if (c_rejected) {
        expect(c_rejected->exit_code == 2, "C + first-class PCH should fail closed");
        expect(contains(*c_rejected, "ordinary C++ source set"),
               "C + PCH rejection should explain the current language boundary");
    }

    write_text(tree.root / "math.ixx", "export module math; export int answer() { return 42; }\n");
    auto module_rejected = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"build", "math.ixx", "--env", "vs", "--no-discover", "--std", "20", "--pch", "include/pch.hpp"});
    expect(module_rejected.has_value(), "module + PCH rejection should launch candidate MQB");
    if (module_rejected) {
        expect(module_rejected->exit_code == 2,
               "Modules/Header Unit pipeline + first-class PCH should fail closed");
        expect(contains(*module_rejected, "Modules/Header Unit pipeline"),
               "module + PCH rejection should explain the current pipeline boundary");
    }

    write_text(
        tree.root / "static_source.cpp",
        "int static_value() { PchValue v{1}; return v.value + pch_bias; }\n");
    auto static_build = run_process(
        runner,
        mqb_executable,
        tree.root,
        {"build", "static_source.cpp", "--env", "vs", "--no-discover", "--debug",
         "--type", "static", "--pch", "include/pch.hpp", "-o", "pch_static"});
    expect(static_build.has_value(), "static PCH build should launch");
    if (static_build) {
        if (static_build->exit_code != 0) dump_failure(*static_build);
        expect(static_build->exit_code == 0, "ordinary C++ static target should support first-class PCH");
        expect(fs::is_regular_file(tree.root / ".mqb/bin/pch_static.lib"),
               "static PCH build should produce archive containing the synthetic creator object");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_pch_e2e_tests passed\n";
    return 0;
}