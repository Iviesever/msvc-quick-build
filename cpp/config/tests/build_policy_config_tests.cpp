#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

#include "mqb/config/ProjectConfig.hpp"
#include "mqb/config/ProjectOptions.hpp"

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

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};
} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_build_policy_config_" + std::to_string(unique))};
    const fs::path file = tree.root / "mqb.json";

    write_text(file, R"json({
  "version": 1,
  "build": {
    "standard": "17",
    "compiler_args": ["/W4", "/DCONFIG_POLICY=1"],
    "linker_args": ["/OPT:NOREF"]
  }
})json");

    auto loaded = mqb::config::ProjectConfigLoader::load(file);
    expect(loaded.has_value(), "build-policy config should load");
    if (loaded) {
        expect(loaded->build.standard == mqb::CppStandard::cpp17,
               "mqb.json should accept C++17");
        expect(loaded->build.compiler_arguments.size() == 2
                   && loaded->build.compiler_arguments[0] == "/W4"
                   && loaded->build.compiler_arguments[1] == "/DCONFIG_POLICY=1",
               "compiler_args should preserve config order");
        expect(loaded->build.linker_arguments.size() == 1
                   && loaded->build.linker_arguments[0] == "/OPT:NOREF",
               "linker_args should decode");

        mqb::config::ProjectOverrides cli;
        cli.build.standard = mqb::CppStandard::cpp14;
        cli.build.compiler_arguments = {"/WX"};
        cli.build.linker_arguments = {"/MAP:cli.map"};
        const auto effective = mqb::config::resolve_project_options(&*loaded, cli);
        expect(effective.standard == mqb::CppStandard::cpp14,
               "CLI scalar standard should override project config");
        expect(effective.compiler_arguments.size() == 3
                   && effective.compiler_arguments[0] == "/W4"
                   && effective.compiler_arguments[1] == "/DCONFIG_POLICY=1"
                   && effective.compiler_arguments[2] == "/WX",
               "CLI compiler arguments should append after config arguments");
        expect(effective.linker_arguments.size() == 2
                   && effective.linker_arguments[0] == "/OPT:NOREF"
                   && effective.linker_arguments[1] == "/MAP:cli.map",
               "CLI linker arguments should append after config arguments");
    }

    write_text(file, R"json({"version":1,"build":{"standard":"14"}})json");
    auto cpp14 = mqb::config::ProjectConfigLoader::load(file);
    expect(cpp14.has_value() && cpp14->build.standard == mqb::CppStandard::cpp14,
           "mqb.json should accept C++14");

    write_text(file, R"json({"version":1,"build":{"compiler_args":[""]}})json");
    auto empty = mqb::config::ProjectConfigLoader::load(file);
    expect(!empty && empty.error().code == mqb::config::ErrorCode::schema_error,
           "empty compiler_args entry should fail strict schema validation");

    write_text(file, R"json({"version":1,"build":{"linker_args":"/MAP"}})json");
    auto wrong_type = mqb::config::ProjectConfigLoader::load(file);
    expect(!wrong_type && wrong_type.error().code == mqb::config::ErrorCode::schema_error,
           "linker_args must be a JSON string array");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_build_policy_config_tests passed\n";
    return 0;
}
