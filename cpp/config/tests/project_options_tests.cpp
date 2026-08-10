#include <filesystem>
#include <iostream>
#include <string_view>

#include "mqb/config/ProjectOptions.hpp"

namespace {
int failures = 0;
void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
} // namespace

int main() {
    namespace fs = std::filesystem;

    {
        const mqb::config::ProjectOverrides cli;
        const auto effective = mqb::config::resolve_project_options(nullptr, cli);
        expect(effective.configuration == mqb::BuildConfiguration::debug,
               "built-in configuration default should be debug");
        expect(effective.architecture == mqb::Architecture::x64,
               "built-in architecture default should be x64");
        expect(effective.standard == mqb::CppStandard::cpp23,
               "built-in language default should be C++23");
        expect(effective.discovery_enabled,
               "smart discovery should be enabled by default");
        expect(!effective.output_name,
               "output should remain unset by default");
    }

    mqb::config::ProjectConfig project;
    project.build.configuration = mqb::BuildConfiguration::release;
    project.build.architecture = mqb::Architecture::x86;
    project.build.standard = mqb::CppStandard::latest;
    project.build.output_name = "from-config";
    project.build.defines = {"CONFIG_A=1", "SHARED=config"};
    project.build.include_directories = {fs::path{"config/include"}};
    project.build.library_directories = {fs::path{"config/lib"}};
    project.build.libraries = {"configlib"};
    project.discovery.enabled = false;
    project.discovery.exclude_directories = {fs::path{"tests"}};
    project.discovery.extra_sources = {fs::path{"src/config-extra.cpp"}};
    project.discovery.exclude_sources = {fs::path{"src/config-old.cpp"}};

    {
        const mqb::config::ProjectOverrides cli;
        const auto effective = mqb::config::resolve_project_options(&project, cli);
        expect(effective.configuration == mqb::BuildConfiguration::release,
               "project config should override built-in configuration");
        expect(effective.architecture == mqb::Architecture::x86,
               "project config should override built-in architecture");
        expect(effective.standard == mqb::CppStandard::latest,
               "project config should override built-in standard");
        expect(effective.output_name && *effective.output_name == "from-config",
               "project output should override built-in unset output");
        expect(!effective.discovery_enabled,
               "project config should disable discovery");
        expect(effective.defines == project.build.defines,
               "project list values should populate effective options");
    }

    {
        mqb::config::ProjectOverrides cli;
        cli.build.configuration = mqb::BuildConfiguration::debug;
        cli.build.architecture = mqb::Architecture::x64;
        cli.build.standard = mqb::CppStandard::cpp20;
        cli.build.output_name = "from-cli";
        cli.build.defines = {"CLI_B=2", "SHARED=cli"};
        cli.build.include_directories = {fs::path{"cli/include"}};
        cli.build.library_directories = {fs::path{"cli/lib"}};
        cli.build.libraries = {"clilib"};
        cli.discovery.enabled = true;
        cli.discovery.exclude_directories = {fs::path{"cli-tests"}};
        cli.discovery.extra_sources = {fs::path{"src/cli-extra.cpp"}};
        cli.discovery.exclude_sources = {fs::path{"src/cli-old.cpp"}};

        const auto effective = mqb::config::resolve_project_options(&project, cli);
        expect(effective.configuration == mqb::BuildConfiguration::debug,
               "CLI configuration should override project config");
        expect(effective.architecture == mqb::Architecture::x64,
               "CLI architecture should override project config");
        expect(effective.standard == mqb::CppStandard::cpp20,
               "CLI standard should override project config");
        expect(effective.output_name && *effective.output_name == "from-cli",
               "CLI output should override project config");
        expect(effective.discovery_enabled,
               "CLI discovery override should override project config");
        expect(effective.defines.size() == 4
                   && effective.defines[0] == "CONFIG_A=1"
                   && effective.defines[1] == "SHARED=config"
                   && effective.defines[2] == "CLI_B=2"
                   && effective.defines[3] == "SHARED=cli",
               "CLI list values should append after config list values in stable order");
        expect(effective.include_directories.size() == 2
                   && effective.include_directories[0] == fs::path{"config/include"}
                   && effective.include_directories[1] == fs::path{"cli/include"},
               "include path precedence should preserve config then CLI order");
        expect(effective.library_directories.size() == 2
                   && effective.library_directories[0] == fs::path{"config/lib"}
                   && effective.library_directories[1] == fs::path{"cli/lib"},
               "library path lists should append CLI after config");
        expect(effective.libraries.size() == 2
                   && effective.libraries[0] == "configlib"
                   && effective.libraries[1] == "clilib",
               "libraries should append CLI after config");
        expect(effective.discovery_exclude_directories.size() == 2,
               "discovery exclude directories should be additive");
        expect(effective.discovery_extra_sources.size() == 2,
               "discovery extra sources should be additive");
        expect(effective.discovery_exclude_sources.size() == 2,
               "discovery excluded sources should be additive");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_project_options_tests passed\n";
    return 0;
}
