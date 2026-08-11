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

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: mqb_static_library_e2e_tests <mqb-executable>\n";
        return 2;
    }

    const fs::path mqb_executable{argv[1]};
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_static_library_e2e_" + std::to_string(unique))};
    fs::create_directories(tree.root);
    mqb::platform::windows::WindowsProcessRunner runner;

    write_text(tree.root / "math.cpp", R"cpp(extern "C" int mqb_answer() {
    return 42;
}
)cpp");
    write_text(tree.root / "obsolete.cpp", R"cpp(extern "C" int mqb_obsolete() {
    return 7;
}
)cpp");
    write_text(tree.root / "consumer.cpp", R"cpp(extern "C" int mqb_answer();
int main() {
    return mqb_answer();
}
)cpp");
    write_text(tree.root / "obsolete_consumer.cpp", R"cpp(extern "C" int mqb_obsolete();
int main() {
    return mqb_obsolete();
}
)cpp");

    const fs::path library = tree.root / ".mqb" / "bin" / "math.lib";
    const fs::path consumer = tree.root / ".mqb" / "bin" / "consumer.exe";
    const fs::path obsolete_consumer = tree.root / ".mqb" / "bin" / "obsolete_consumer.exe";

    auto cold = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "obsolete.cpp", "--env", "vs", "--type", "static", "-o", "math"});
    expect(cold.has_value(), "cold static-library invocation should launch");
    if (cold) {
        if (cold->exit_code != 0) dump_failure(*cold);
        expect(cold->exit_code == 0, "cold static-library target should succeed");
        expect(cold->stdout_text.find("[compile] math.cpp") != std::string::npos
                   && cold->stdout_text.find("[compile] obsolete.cpp") != std::string::npos,
               "cold static target should compile its object set");
        expect(cold->stdout_text.find("[archive] math.lib") != std::string::npos,
               "cold static target should archive with lib.exe");
    }
    expect(fs::is_regular_file(library), "static target should produce .mqb/bin/math.lib");

    auto consumer_build = run_process(
        runner, mqb_executable, tree.root,
        {"consumer.cpp", "--no-discover", "--env", "vs", "-L", ".mqb/bin", "-l", "math", "-o", "consumer"});
    expect(consumer_build.has_value(), "consumer build should launch");
    if (consumer_build) {
        if (consumer_build->exit_code != 0) dump_failure(*consumer_build);
        expect(consumer_build->exit_code == 0, "consumer should link against generated static library");
    }
    expect(fs::is_regular_file(consumer), "consumer executable should exist");
    auto consumer_run = run_process(runner, consumer, tree.root);
    expect(consumer_run.has_value() && consumer_run->exit_code == 42,
           "consumer should execute symbol from generated static library");

    auto obsolete_consumer_build = run_process(
        runner, mqb_executable, tree.root,
        {"obsolete_consumer.cpp", "--no-discover", "--env", "vs", "-L", ".mqb/bin", "-l", "math", "-o", "obsolete_consumer"});
    expect(obsolete_consumer_build.has_value(), "obsolete-symbol consumer build should launch");
    if (obsolete_consumer_build) {
        if (obsolete_consumer_build->exit_code != 0) dump_failure(*obsolete_consumer_build);
        expect(obsolete_consumer_build->exit_code == 0,
               "initial archive should expose the second object member");
    }
    auto obsolete_consumer_run = run_process(runner, obsolete_consumer, tree.root);
    expect(obsolete_consumer_run.has_value() && obsolete_consumer_run->exit_code == 7,
           "initial archive should execute symbol from its second object member");

    auto warm = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "obsolete.cpp", "--env", "vs", "-type", "static", "-o", "math"});
    expect(warm.has_value(), "warm static-library invocation should launch");
    if (warm) {
        if (warm->exit_code != 0) dump_failure(*warm);
        expect(warm->exit_code == 0, "warm static target should succeed");
        expect(contains_line(warm->stdout_text, "[up-to-date] math.cpp")
                   && contains_line(warm->stdout_text, "[up-to-date] obsolete.cpp"),
               "warm static target should reuse compile cache");
        expect(contains_line(warm->stdout_text, "[up-to-date] math.lib"),
               "warm static target should reuse archive cache");
    }

    std::error_code ec;
    fs::remove(library, ec);
    expect(!ec && !fs::exists(library), "test should remove static archive");
    auto repair = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "obsolete.cpp", "--env", "vs", "--type=static", "-o", "math"});
    expect(repair.has_value(), "missing-archive repair invocation should launch");
    if (repair) {
        if (repair->exit_code != 0) dump_failure(*repair);
        expect(repair->exit_code == 0, "missing static archive should repair");
        expect(contains_line(repair->stdout_text, "[up-to-date] math.cpp")
                   && contains_line(repair->stdout_text, "[up-to-date] obsolete.cpp"),
               "missing archive should not recompile fresh sources");
        expect(repair->stdout_text.find("[archive] math.lib") != std::string::npos
                   && repair->stdout_text.find("missing output") != std::string::npos,
               "missing archive should invalidate archive freshness");
    }
    expect(fs::is_regular_file(library), "repair should recreate static archive");

    write_text(tree.root / "math.cpp", R"cpp(extern "C" int mqb_answer() {
    return 43;
}
)cpp");
    auto mutated = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "obsolete.cpp", "--env", "vs", "--type", "static", "-o", "math"});
    expect(mutated.has_value(), "mutated static-library invocation should launch");
    if (mutated) {
        if (mutated->exit_code != 0) dump_failure(*mutated);
        expect(mutated->exit_code == 0, "mutated static library should rebuild");
        expect(mutated->stdout_text.find("[compile] math.cpp") != std::string::npos,
               "mutated source should recompile");
        expect(mutated->stdout_text.find("[archive] math.lib") != std::string::npos,
               "fresh object should rearchive static target");
    }

    auto consumer_relink = run_process(
        runner, mqb_executable, tree.root,
        {"consumer.cpp", "--no-discover", "--env", "vs", "-L", ".mqb/bin", "-l", "math", "-o", "consumer"});
    expect(consumer_relink.has_value(), "consumer relink should launch");
    if (consumer_relink) {
        if (consumer_relink->exit_code != 0) dump_failure(*consumer_relink);
        expect(consumer_relink->exit_code == 0, "consumer should relink after static library mutation");
        expect(contains_line(consumer_relink->stdout_text, "[up-to-date] consumer.cpp"),
               "library mutation should not recompile fresh consumer source");
        expect(consumer_relink->stdout_text.find("[link] consumer.exe") != std::string::npos,
               "newer static library should invalidate consumer link freshness");
    }
    auto consumer_run_after_mutation = run_process(runner, consumer, tree.root);
    expect(consumer_run_after_mutation.has_value() && consumer_run_after_mutation->exit_code == 43,
           "consumer should observe mutated static-library behavior");

    auto shrunk = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "--no-discover", "--env", "vs", "--type", "static", "-o", "math"});
    expect(shrunk.has_value(), "source-set shrink invocation should launch");
    if (shrunk) {
        if (shrunk->exit_code != 0) dump_failure(*shrunk);
        expect(shrunk->exit_code == 0, "source-set shrink should rebuild archive successfully");
        expect(contains_line(shrunk->stdout_text, "[up-to-date] math.cpp"),
               "source-set shrink should reuse the remaining fresh object");
        expect(shrunk->stdout_text.find("[archive] math.lib") != std::string::npos
                   && shrunk->stdout_text.find("archive inputs changed") != std::string::npos,
               "source-set shrink should invalidate archive input identity");
    }

    ec.clear();
    fs::remove(obsolete_consumer, ec);
    expect(!ec && !fs::exists(obsolete_consumer),
           "test should remove old obsolete-symbol consumer output");
    auto stale_member_probe = run_process(
        runner, mqb_executable, tree.root,
        {"obsolete_consumer.cpp", "--no-discover", "--env", "vs", "-L", ".mqb/bin", "-l", "math", "-o", "obsolete_consumer"});
    expect(stale_member_probe.has_value(), "stale-member probe should launch");
    if (stale_member_probe) {
        expect(stale_member_probe->exit_code == 5,
               "shrunk archive must not retain an object member removed from the current source set");
        if (stale_member_probe->exit_code == 0) dump_failure(*stale_member_probe);
    }

    write_text(tree.root / "mqb.json", R"json({
  "version": 1,
  "build": {
    "type": "static",
    "output": "config_math"
  },
  "discovery": {
    "enabled": false
  }
})json");
    auto config_static = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "--no-discover", "--env", "vs"});
    expect(config_static.has_value(), "config-only static target invocation should launch");
    if (config_static) {
        if (config_static->exit_code != 0) dump_failure(*config_static);
        expect(config_static->exit_code == 0,
               "mqb.json build.type=static should build without a CLI target-kind flag");
        expect(config_static->stdout_text.find("[archive] config_math.lib") != std::string::npos,
               "config target kind and output should route to the librarian pipeline");
    }
    expect(fs::is_regular_file(tree.root / ".mqb" / "bin" / "config_math.lib"),
           "config-only static target should produce the configured .lib output");

    auto cli_type_override = run_process(
        runner, mqb_executable, tree.root,
        {"consumer.cpp", "--no-discover", "--env", "vs", "--type", "exe",
         "-L", ".mqb/bin", "-l", "math", "-o", "config_override"});
    expect(cli_type_override.has_value(), "CLI target-kind override invocation should launch");
    if (cli_type_override) {
        if (cli_type_override->exit_code != 0) dump_failure(*cli_type_override);
        expect(cli_type_override->exit_code == 0,
               "CLI --type exe should override mqb.json build.type=static");
        expect(cli_type_override->stdout_text.find("[link] config_override.exe") != std::string::npos,
               "CLI target-kind override should route back to the linker pipeline");
    }
    auto override_run = run_process(
        runner,
        tree.root / ".mqb" / "bin" / "config_override.exe",
        tree.root);
    expect(override_run.has_value() && override_run->exit_code == 43,
           "CLI-overridden executable should consume the existing static library");

    auto invalid_policy = run_process(
        runner, mqb_executable, tree.root,
        {"math.cpp", "--type", "static", "--subsystem", "console"});
    expect(invalid_policy.has_value() && invalid_policy->exit_code == 2,
           "static target should reject explicit linker-only policy");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_static_library_e2e_tests passed\n";
    return 0;
}
