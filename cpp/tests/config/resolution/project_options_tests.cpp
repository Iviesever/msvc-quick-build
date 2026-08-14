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
        const auto effective = mqb::config::resolve_project_options(nullptr, nullptr, cli);
        expect(effective.configuration == mqb::BuildConfiguration::debug,
               "built-in configuration default should be debug");
        expect(effective.architecture == mqb::Architecture::x64,
               "built-in architecture default should be x64");
        expect(effective.standard == mqb::CppStandard::cpp23,
               "built-in language default should be C++23");
        expect(effective.target_kind == mqb::TargetKind::executable,
               "built-in target kind should be executable");
        expect(effective.discovery_enabled,
               "smart discovery should be enabled by default");
        expect(!effective.output_name,
               "output should remain unset by default");
        expect(effective.external_module_providers.empty(),
               "external module provider registry should be empty by default");
    }

    mqb::config::ProjectConfig project;
    project.build.configuration = mqb::BuildConfiguration::release;
    project.build.architecture = mqb::Architecture::x86;
    project.build.standard = mqb::CppStandard::latest;
    project.build.target_kind = mqb::TargetKind::static_library;
    project.build.output_name = "from-config";
    project.build.defines = {"CONFIG_A=1", "SHARED=config"};
    project.build.include_directories = {fs::path{"config/include"}};
    project.build.library_directories = {fs::path{"config/lib"}};
    project.build.libraries = {"configlib"};
    project.discovery.enabled = false;
    project.discovery.exclude_directories = {fs::path{"tests"}};
    project.discovery.extra_sources = {fs::path{"src/config-extra.cpp"}};
    project.discovery.exclude_sources = {fs::path{"src/config-old.cpp"}};
    project.modules.external_providers = {
        mqb::ExternalModuleProvider{
            .logical_name = "vendor.math",
            .interface_file = fs::path{"config/vendor.math.ifc"},
        },
        mqb::ExternalModuleProvider{
            .logical_name = "config.only",
            .interface_file = fs::path{"config/config.only.ifc"},
        },
    };

    {
        const mqb::config::ProjectOverrides cli;
        const auto effective = mqb::config::resolve_project_options(&project, nullptr, cli);
        expect(effective.configuration == mqb::BuildConfiguration::release,
               "project config should override built-in configuration");
        expect(effective.architecture == mqb::Architecture::x86,
               "project config should override built-in architecture");
        expect(effective.standard == mqb::CppStandard::latest,
               "project config should override built-in standard");
        expect(effective.target_kind == mqb::TargetKind::static_library,
               "project config should override built-in target kind");
        expect(effective.output_name && *effective.output_name == "from-config",
               "project output should override built-in unset output");
        expect(!effective.discovery_enabled,
               "project config should disable discovery");
        expect(effective.defines == project.build.defines,
               "project list values should populate effective options");
        expect(effective.external_module_providers.size() == 2
                   && effective.external_module_providers[0].logical_name == "vendor.math"
                   && effective.external_module_providers[1].logical_name == "config.only",
               "project external module registry should populate effective provider policy");
    }

    mqb::config::ProjectProfile profile;
    profile.build.configuration = mqb::BuildConfiguration::debug;
    profile.build.standard = mqb::CppStandard::cpp20;
    profile.build.output_name = "from-profile";
    profile.build.defines = {"PROFILE_B=2", "SHARED=profile"};
    profile.build.include_directories = {fs::path{"profile/include"}};
    profile.build.libraries = {"profilelib"};
    profile.discovery.enabled = true;
    profile.discovery.exclude_directories = {fs::path{"profile-tests"}};
    profile.modules.external_providers = {
        mqb::ExternalModuleProvider{
            .logical_name = "vendor.math",
            .interface_file = fs::path{"profile/vendor.math.ifc"},
        },
        mqb::ExternalModuleProvider{
            .logical_name = "profile.only",
            .interface_file = fs::path{"profile/profile.only.ifc"},
        },
    };

    {
        const mqb::config::ProjectOverrides cli;
        const auto effective = mqb::config::resolve_project_options(&project, &profile, cli);
        expect(effective.configuration == mqb::BuildConfiguration::debug,
               "profile scalar should override base project config");
        expect(effective.architecture == mqb::Architecture::x86,
               "base scalar should remain when profile leaves it unset");
        expect(effective.standard == mqb::CppStandard::cpp20,
               "profile standard should override base standard");
        expect(effective.target_kind == mqb::TargetKind::static_library,
               "base target kind should remain when profile leaves it unset");
        expect(effective.output_name && *effective.output_name == "from-profile",
               "profile output should override base output");
        expect(effective.discovery_enabled,
               "profile discovery scalar should override base discovery scalar");
        expect(effective.defines.size() == 4
                   && effective.defines[0] == "CONFIG_A=1"
                   && effective.defines[1] == "SHARED=config"
                   && effective.defines[2] == "PROFILE_B=2"
                   && effective.defines[3] == "SHARED=profile",
               "profile list values should append after base config lists");
        expect(effective.include_directories.size() == 2
                   && effective.include_directories[1] == fs::path{"profile/include"},
               "profile paths should append after base paths");
        expect(effective.libraries.size() == 2
                   && effective.libraries[0] == "configlib"
                   && effective.libraries[1] == "profilelib",
               "profile libraries should append after base libraries");
        expect(effective.external_module_providers.size() == 3,
               "profile modules should replace by logical name and append profile-only providers");
        if (effective.external_module_providers.size() == 3) {
            expect(effective.external_module_providers[0].logical_name == "vendor.math"
                       && effective.external_module_providers[0].interface_file
                           == fs::path{"profile/vendor.math.ifc"},
                   "profile provider should replace matching base provider in place");
            expect(effective.external_module_providers[1].logical_name == "config.only",
                   "base-only provider should remain after profile overlay");
            expect(effective.external_module_providers[2].logical_name == "profile.only",
                   "profile-only provider should append deterministically");
        }
    }

    {
        mqb::config::ProjectOverrides cli;
        cli.build.configuration = mqb::BuildConfiguration::release;
        cli.build.architecture = mqb::Architecture::x64;
        cli.build.standard = mqb::CppStandard::cpp23;
        cli.build.target_kind = mqb::TargetKind::dynamic_library;
        cli.build.output_name = "from-cli";
        cli.build.defines = {"CLI_C=3", "SHARED=cli"};
        cli.build.include_directories = {fs::path{"cli/include"}};
        cli.build.library_directories = {fs::path{"cli/lib"}};
        cli.build.libraries = {"clilib"};
        cli.discovery.enabled = false;
        cli.discovery.exclude_directories = {fs::path{"cli-tests"}};
        cli.discovery.extra_sources = {fs::path{"src/cli-extra.cpp"}};
        cli.discovery.exclude_sources = {fs::path{"src/cli-old.cpp"}};
        cli.modules.external_providers = {
            mqb::ExternalModuleProvider{
                .logical_name = "vendor.math",
                .interface_file = fs::path{"cli/vendor.math.ifc"},
            },
            mqb::ExternalModuleProvider{
                .logical_name = "cli.only",
                .interface_file = fs::path{"cli/cli.only.ifc"},
            },
        };

        const auto effective = mqb::config::resolve_project_options(&project, &profile, cli);
        expect(effective.configuration == mqb::BuildConfiguration::release,
               "CLI configuration should override selected profile");
        expect(effective.architecture == mqb::Architecture::x64,
               "CLI architecture should override base config");
        expect(effective.standard == mqb::CppStandard::cpp23,
               "CLI standard should override selected profile");
        expect(effective.target_kind == mqb::TargetKind::dynamic_library,
               "CLI target kind should override base project config");
        expect(effective.output_name && *effective.output_name == "from-cli",
               "CLI output should override selected profile");
        expect(!effective.discovery_enabled,
               "CLI discovery override should override selected profile");
        expect(effective.defines.size() == 6
                   && effective.defines[0] == "CONFIG_A=1"
                   && effective.defines[2] == "PROFILE_B=2"
                   && effective.defines[4] == "CLI_C=3"
                   && effective.defines[5] == "SHARED=cli",
               "list precedence should preserve base then profile then CLI order");
        expect(effective.include_directories.size() == 3
                   && effective.include_directories[0] == fs::path{"config/include"}
                   && effective.include_directories[1] == fs::path{"profile/include"}
                   && effective.include_directories[2] == fs::path{"cli/include"},
               "include path precedence should preserve base then profile then CLI order");
        expect(effective.library_directories.size() == 2
                   && effective.library_directories[0] == fs::path{"config/lib"}
                   && effective.library_directories[1] == fs::path{"cli/lib"},
               "unset profile library paths should not disturb base plus CLI order");
        expect(effective.libraries.size() == 3
                   && effective.libraries[0] == "configlib"
                   && effective.libraries[1] == "profilelib"
                   && effective.libraries[2] == "clilib",
               "libraries should append through all three layers");
        expect(effective.discovery_exclude_directories.size() == 3,
               "discovery exclude directories should be additive across all layers");
        expect(effective.discovery_extra_sources.size() == 2,
               "discovery extra sources should append base then CLI when profile is empty");
        expect(effective.discovery_exclude_sources.size() == 2,
               "discovery excluded sources should append base then CLI when profile is empty");
        expect(effective.external_module_providers.size() == 4,
               "CLI providers should replace the selected profile provider and keep base/profile-only providers");
        if (effective.external_module_providers.size() == 4) {
            expect(effective.external_module_providers[0].logical_name == "vendor.math"
                       && effective.external_module_providers[0].interface_file
                           == fs::path{"cli/vendor.math.ifc"},
                   "CLI provider should replace the matching profile provider in-place");
            expect(effective.external_module_providers[1].logical_name == "config.only",
                   "config-only provider should remain after profile and CLI overlays");
            expect(effective.external_module_providers[2].logical_name == "profile.only",
                   "profile-only provider should remain after CLI overlay");
            expect(effective.external_module_providers[3].logical_name == "cli.only",
                   "CLI-only provider should append deterministically");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_project_options_tests passed\n";
    return 0;
}
