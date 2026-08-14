#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/config/ProjectConfig.hpp"

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
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_project_config_" + std::to_string(unique)),
    };
    const fs::path nested = tree.root / "src" / "feature";
    fs::create_directories(nested);
    const fs::path config_file = tree.root / "mqb.json";

    write_text(config_file, R"json({
  "version": 1,
  "build": {
    "configuration": "release",
    "architecture": "x86",
    "standard": "latest",
    "type": "static",
    "entry": "src/main.cpp",
    "output": "demo",
    "defines": ["ONE=1", "TEXT=\u6d4b\u8bd5"],
    "include_dirs": ["include", "unicode/\u6d4b\u8bd5"],
    "library_dirs": ["vendor libs"],
    "libraries": ["math", "codec.lib"]
  },
  "discovery": {
    "enabled": false,
    "exclude_dirs": ["tests"],
    "extra_sources": ["src/manual.cpp"],
    "exclude_sources": ["src/legacy.cpp"]
  },
  "modules": {
    "external": {
      "vendor.math": "prebuilt/vendor.math.ifc",
      "vendor.text": "prebuilt/vendor text.ifc"
    }
  },
  "profiles": {
    "dev": {
      "build": {
        "configuration": "debug",
        "standard": "20",
        "runtime": "MDd",
        "defines": ["PROFILE_DEV=1"],
        "include_dirs": ["profiles/dev/include"],
        "compiler_args": ["/W4"]
      },
      "discovery": {
        "enabled": true,
        "extra_sources": ["profiles/dev/extra.cpp"]
      },
      "modules": {
        "external": {
          "vendor.math": "profiles/dev/vendor.math.ifc"
        }
      }
    },
    "ship": {
      "build": {
        "configuration": "release",
        "ltcg": true,
        "compiler_args": ["/O2"]
      }
    }
  }
})json");

    auto found = mqb::config::ProjectConfigLoader::find_upwards(nested);
    expect(found.has_value(), "upward config lookup should succeed");
    if (found) {
        expect(found->has_value(), "nested directory should find root mqb.json");
        if (*found) expect((**found).lexically_normal() == config_file.lexically_normal(),
                           "upward lookup should return exact root config path");
    }

    auto loaded = mqb::config::ProjectConfigLoader::load(config_file);
    expect(loaded.has_value(), "full version-1 config with profiles should load");
    if (loaded) {
        expect(loaded->version == 1, "config version should be retained");
        expect(loaded->project_root == tree.root.lexically_normal(),
               "project root should be the config file directory");
        expect(loaded->build.configuration == mqb::BuildConfiguration::release,
               "release override should decode");
        expect(loaded->build.architecture == mqb::Architecture::x86,
               "x86 override should decode");
        expect(loaded->build.standard == mqb::CppStandard::latest,
               "latest standard override should decode");
        expect(loaded->build.target_kind == mqb::TargetKind::static_library,
               "static target kind should decode");
        expect(loaded->build.entry
                   && *loaded->build.entry == (tree.root / "src/main.cpp").lexically_normal(),
               "build.entry should resolve relative to mqb.json");
        expect(loaded->build.output_name && *loaded->build.output_name == "demo",
               "output override should decode");
        expect(loaded->build.defines.size() == 2,
               "define array should preserve all entries");
        if (loaded->build.defines.size() == 2) {
            const std::string expected_unicode = std::string{"TEXT="} + "\xE6\xB5\x8B\xE8\xAF\x95";
            expect(loaded->build.defines[1] == expected_unicode,
                   "Unicode escapes should decode to UTF-8");
        }
        expect(loaded->build.include_directories.size() == 2,
               "include path array should decode");
        if (loaded->build.include_directories.size() == 2) {
            expect(loaded->build.include_directories[0] == (tree.root / "include").lexically_normal(),
                   "relative include path should resolve from config directory");
            const fs::path expected_unicode_path = fs::u8path(
                std::string{"unicode/"} + "\xE6\xB5\x8B\xE8\xAF\x95");
            expect(loaded->build.include_directories[1]
                       == (tree.root / expected_unicode_path).lexically_normal(),
                   "Unicode config path should resolve from config directory");
        }
        expect(loaded->build.library_directories.size() == 1
                   && loaded->build.library_directories[0]
                       == (tree.root / "vendor libs").lexically_normal(),
               "library path should resolve from config directory");
        expect(loaded->build.libraries.size() == 2
                   && loaded->build.libraries[0] == "math"
                   && loaded->build.libraries[1] == "codec.lib",
               "library order should be preserved");
        expect(loaded->discovery.enabled && !*loaded->discovery.enabled,
               "discovery enabled override should decode false");
        expect(loaded->discovery.exclude_directories.size() == 1
                   && loaded->discovery.exclude_directories[0]
                       == (tree.root / "tests").lexically_normal(),
               "discovery exclude directory should resolve from config directory");
        expect(loaded->discovery.extra_sources.size() == 1
                   && loaded->discovery.extra_sources[0]
                       == (tree.root / "src/manual.cpp").lexically_normal(),
               "extra source should resolve from config directory");
        expect(loaded->discovery.exclude_sources.size() == 1
                   && loaded->discovery.exclude_sources[0]
                       == (tree.root / "src/legacy.cpp").lexically_normal(),
               "excluded source should resolve from config directory");
        expect(loaded->modules.external_providers.size() == 2,
               "modules.external should decode every logical-name to IFC mapping");
        if (loaded->modules.external_providers.size() == 2) {
            expect(loaded->modules.external_providers[0].logical_name == "vendor.math"
                       && loaded->modules.external_providers[0].interface_file
                           == (tree.root / "prebuilt/vendor.math.ifc").lexically_normal(),
                   "external module IFC path should resolve relative to mqb.json");
            expect(loaded->modules.external_providers[1].logical_name == "vendor.text"
                       && loaded->modules.external_providers[1].interface_file
                           == (tree.root / "prebuilt/vendor text.ifc").lexically_normal(),
                   "external provider mapping should preserve names and spaces in config-relative paths");
        }

        expect(loaded->profiles.size() == 2,
               "profiles object should decode every named profile");
        const auto dev = loaded->profiles.find("dev");
        expect(dev != loaded->profiles.end(), "dev profile should be addressable by name");
        if (dev != loaded->profiles.end()) {
            expect(dev->second.build.configuration == mqb::BuildConfiguration::debug,
                   "profile build scalar should decode");
            expect(dev->second.build.standard == mqb::CppStandard::cpp20,
                   "profile C++ standard should decode");
            expect(dev->second.build.runtime_library == mqb::RuntimeLibrary::mdd,
                   "profile runtime should decode");
            expect(!dev->second.build.entry,
                   "profile build layer should never acquire project entry identity");
            expect(dev->second.build.include_directories.size() == 1
                       && dev->second.build.include_directories[0]
                           == (tree.root / "profiles/dev/include").lexically_normal(),
                   "profile paths should resolve relative to mqb.json");
            expect(dev->second.discovery.enabled && *dev->second.discovery.enabled,
                   "profile discovery scalar should decode");
            expect(dev->second.discovery.extra_sources.size() == 1
                       && dev->second.discovery.extra_sources[0]
                           == (tree.root / "profiles/dev/extra.cpp").lexically_normal(),
                   "profile discovery paths should resolve relative to mqb.json");
            expect(dev->second.modules.external_providers.size() == 1
                       && dev->second.modules.external_providers[0].logical_name == "vendor.math"
                       && dev->second.modules.external_providers[0].interface_file
                           == (tree.root / "profiles/dev/vendor.math.ifc").lexically_normal(),
                   "profile module providers should decode with config-relative IFC paths");
        }
    }

    write_text(config_file, R"json({"version":1,"build":{"type":"lib"}})json");
    auto static_alias = mqb::config::ProjectConfigLoader::load(config_file);
    expect(static_alias.has_value()
               && static_alias->build.target_kind == mqb::TargetKind::static_library,
           "lib alias should decode to static target kind");

    write_text(config_file, R"json({"version":1})json");
    auto minimal = mqb::config::ProjectConfigLoader::load(config_file);
    expect(minimal.has_value(), "minimal version-only config should load");
    if (minimal) {
        expect(!minimal->build.configuration && !minimal->build.architecture
                   && !minimal->build.standard && !minimal->build.target_kind
                   && !minimal->build.entry && !minimal->build.output_name,
               "missing build scalar/path fields must remain unset rather than receiving defaults");
        expect(minimal->build.defines.empty() && minimal->build.include_directories.empty()
                   && minimal->build.library_directories.empty() && minimal->build.libraries.empty(),
               "missing build list fields must remain empty overrides");
        expect(!minimal->discovery.enabled,
               "missing discovery enabled field must remain unset");
        expect(minimal->modules.external_providers.empty(),
               "missing modules policy must remain an empty override");
        expect(minimal->profiles.empty(),
               "missing profiles object should remain an empty named-profile registry");
    }

    write_text(config_file, R"json({"version":1,"build":{"entry":""}})json");
    auto empty_entry = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!empty_entry && empty_entry.error().code == mqb::config::ErrorCode::schema_error,
           "build.entry must be a non-empty string");

    write_text(config_file, R"json({"version":1,"profiles":{"bad":{"build":{"entry":"other.cpp"}}}})json");
    auto profile_entry = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!profile_entry && profile_entry.error().code == mqb::config::ErrorCode::schema_error,
           "profile build.entry should be rejected as project identity rather than build policy");
    if (!profile_entry) {
        expect(profile_entry.error().message.find("build.entry is not allowed") != std::string::npos,
               "profile entry rejection should explain the project-identity boundary");
    }

    write_text(config_file, R"json({"version":1,"profiles":{"bad":{"surprise":true}}})json");
    auto bad_profile_field = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!bad_profile_field && bad_profile_field.error().code == mqb::config::ErrorCode::schema_error,
           "unknown profile fields should be rejected by strict schema");

    write_text(config_file, R"json({"version":1,"modules":{"external":{"vendor.math":7}}})json");
    auto bad_provider_type = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!bad_provider_type && bad_provider_type.error().code == mqb::config::ErrorCode::schema_error,
           "external module provider IFC must be a non-empty JSON string");

    write_text(config_file, R"json({"version":1,"modules":{"surprise":{}}})json");
    auto bad_module_field = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!bad_module_field && bad_module_field.error().code == mqb::config::ErrorCode::schema_error,
           "unknown modules fields should be rejected by strict schema");

    write_text(config_file, R"json({"version":2})json");
    auto unsupported = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!unsupported, "unsupported config version should fail");
    if (!unsupported) {
        expect(unsupported.error().code == mqb::config::ErrorCode::unsupported_version,
               "unsupported config version should report dedicated error code");
    }

    write_text(config_file, R"json({"version":1,"surprise":true})json");
    auto unknown = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!unknown && unknown.error().code == mqb::config::ErrorCode::schema_error,
           "unknown root fields should be rejected by strict schema");

    write_text(config_file, R"json({"version":1,"build":{"architecture":"arm64"}})json");
    auto bad_enum = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!bad_enum && bad_enum.error().code == mqb::config::ErrorCode::schema_error,
           "invalid enum value should be rejected");

    write_text(config_file, R"json({"version":1,"discovery":{"enabled":"yes"}})json");
    auto bad_type = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!bad_type && bad_type.error().code == mqb::config::ErrorCode::schema_error,
           "wrong field type should be rejected");

    write_text(config_file, R"json({"version":1,"version":1})json");
    auto duplicate = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!duplicate && duplicate.error().code == mqb::config::ErrorCode::parse_error,
           "duplicate JSON keys should be rejected");

    write_text(config_file, "{\n  \"version\": 1,\n  \"build\": [\n}\n");
    auto malformed = mqb::config::ProjectConfigLoader::load(config_file);
    expect(!malformed && malformed.error().code == mqb::config::ErrorCode::parse_error,
           "malformed JSON should report parse error");
    if (!malformed) {
        expect(malformed.error().line >= 3,
               "parse errors should retain useful line information");
    }

    std::error_code ignored;
    fs::remove(config_file, ignored);
    auto missing = mqb::config::ProjectConfigLoader::find_upwards(nested);
    expect(missing.has_value() && !missing->has_value(),
           "upward lookup should return empty optional when no config exists");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_project_config_tests passed\n";
    return 0;
}
