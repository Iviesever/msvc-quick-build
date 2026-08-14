#include "ProjectConfigSchema.hpp"

#include <algorithm>
#include <charconv>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mqb::config::detail {
namespace {

namespace fs = std::filesystem;
using JsonObject = json::Object;
using JsonValue = json::Value;

[[nodiscard]] Error make_error(
    const ErrorCode code,
    const fs::path& path,
    const std::size_t line,
    const std::size_t column,
    std::string message) {
    return Error{code, path, line, column, std::move(message)};
}

[[nodiscard]] Error schema_error(
    const fs::path& path,
    const JsonValue& value,
    std::string message) {
    return make_error(
        ErrorCode::schema_error,
        path,
        value.line,
        value.column,
        std::move(message));
}

[[nodiscard]] std::expected<const JsonObject*, Error> require_object(
    const fs::path& path,
    const JsonValue& value,
    const std::string_view name) {
    if (value.kind != json::Kind::object) {
        return std::unexpected(schema_error(
            path,
            value,
            std::string{name} + " must be a JSON object"));
    }
    return &value.object;
}

[[nodiscard]] std::expected<std::string, Error> require_string(
    const fs::path& path,
    const JsonValue& value,
    const std::string_view name) {
    if (value.kind != json::Kind::string) {
        return std::unexpected(schema_error(
            path,
            value,
            std::string{name} + " must be a JSON string"));
    }
    if (value.scalar.empty()) {
        return std::unexpected(schema_error(
            path,
            value,
            std::string{name} + " must not be empty"));
    }
    return value.scalar;
}

[[nodiscard]] std::expected<bool, Error> require_bool(
    const fs::path& path,
    const JsonValue& value,
    const std::string_view name) {
    if (value.kind != json::Kind::boolean) {
        return std::unexpected(schema_error(
            path,
            value,
            std::string{name} + " must be a JSON boolean"));
    }
    return value.boolean;
}

[[nodiscard]] std::expected<std::vector<std::string>, Error> require_strings(
    const fs::path& path,
    const JsonValue& value,
    const std::string_view name) {
    if (value.kind != json::Kind::array) {
        return std::unexpected(schema_error(
            path,
            value,
            std::string{name} + " must be a JSON array"));
    }

    std::vector<std::string> result;
    result.reserve(value.array.size());
    for (const auto& element : value.array) {
        auto text = require_string(path, element, name);
        if (!text) {
            return std::unexpected(text.error());
        }
        result.push_back(std::move(*text));
    }
    return result;
}

[[nodiscard]] fs::path resolve_path(
    const fs::path& root,
    const std::string& text) {
    fs::path value = fs::u8path(text);
    return (value.is_absolute() ? value : root / value).lexically_normal();
}

[[nodiscard]] std::expected<std::vector<fs::path>, Error> require_paths(
    const fs::path& file,
    const fs::path& root,
    const JsonValue& value,
    const std::string_view name) {
    auto strings = require_strings(file, value, name);
    if (!strings) {
        return std::unexpected(strings.error());
    }

    std::vector<fs::path> result;
    result.reserve(strings->size());
    for (const auto& text : *strings) {
        result.push_back(resolve_path(root, text));
    }
    return result;
}

[[nodiscard]] std::expected<void, Error> reject_unknown(
    const fs::path& file,
    const JsonObject& object,
    const std::vector<std::string_view>& allowed,
    const std::string_view scope) {
    for (const auto& [key, value] : object) {
        if (std::ranges::find(allowed, std::string_view{key}) == allowed.end()) {
            return std::unexpected(schema_error(
                file,
                value,
                "unknown " + std::string{scope} + " field '" + key + "'"));
        }
    }
    return {};
}

[[nodiscard]] std::optional<BuildConfiguration> configuration(
    const std::string_view value) {
    if (value == "debug") return BuildConfiguration::debug;
    if (value == "release") return BuildConfiguration::release;
    return std::nullopt;
}

[[nodiscard]] std::optional<Architecture> architecture(
    const std::string_view value) {
    if (value == "x86") return Architecture::x86;
    if (value == "x64") return Architecture::x64;
    return std::nullopt;
}

[[nodiscard]] std::optional<CppStandard> standard(
    const std::string_view value) {
    if (value == "14" || value == "c++14") return CppStandard::cpp14;
    if (value == "17" || value == "c++17") return CppStandard::cpp17;
    if (value == "20" || value == "c++20") return CppStandard::cpp20;
    if (value == "23" || value == "c++23") return CppStandard::cpp23;
    if (value == "latest" || value == "c++latest") return CppStandard::latest;
    return std::nullopt;
}

[[nodiscard]] std::optional<RuntimeLibrary> runtime_library(
    const std::string_view value) {
    if (value == "MD" || value == "md") return RuntimeLibrary::md;
    if (value == "MDd" || value == "mdd") return RuntimeLibrary::mdd;
    if (value == "MT" || value == "mt") return RuntimeLibrary::mt;
    if (value == "MTd" || value == "mtd") return RuntimeLibrary::mtd;
    return std::nullopt;
}

[[nodiscard]] std::optional<LinkSubsystem> subsystem(
    const std::string_view value) {
    if (value == "console") return LinkSubsystem::console;
    if (value == "windows") return LinkSubsystem::windows;
    return std::nullopt;
}

[[nodiscard]] std::optional<TargetKind> target_kind(
    const std::string_view value) {
    if (value == "exe" || value == "executable") {
        return TargetKind::executable;
    }
    if (value == "dll" || value == "dynamic") {
        return TargetKind::dynamic_library;
    }
    if (value == "static" || value == "lib") {
        return TargetKind::static_library;
    }
    return std::nullopt;
}

[[nodiscard]] std::expected<void, Error> decode_build(
    const fs::path& file,
    const fs::path& root,
    const JsonValue& value,
    BuildOverrides& out) {
    auto object = require_object(file, value, "build");
    if (!object) return std::unexpected(object.error());

    auto known = reject_unknown(
        file,
        **object,
        {
            "configuration", "architecture", "standard", "runtime", "ltcg",
            "subsystem", "type", "entry", "output", "defines", "include_dirs",
            "library_dirs", "libraries", "compiler_args", "linker_args",
        },
        "build");
    if (!known) return std::unexpected(known.error());

    if (auto it = (**object).find("configuration"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.configuration");
        if (!text) return std::unexpected(text.error());
        auto parsed = configuration(*text);
        if (!parsed) {
            return std::unexpected(schema_error(
                file,
                it->second,
                "build.configuration must be 'debug' or 'release'"));
        }
        out.configuration = *parsed;
    }

    if (auto it = (**object).find("architecture"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.architecture");
        if (!text) return std::unexpected(text.error());
        auto parsed = architecture(*text);
        if (!parsed) {
            return std::unexpected(schema_error(
                file,
                it->second,
                "build.architecture must be 'x86' or 'x64'"));
        }
        out.architecture = *parsed;
    }

    if (auto it = (**object).find("standard"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.standard");
        if (!text) return std::unexpected(text.error());
        auto parsed = standard(*text);
        if (!parsed) {
            return std::unexpected(schema_error(
                file,
                it->second,
                "build.standard must be '14', '17', '20', '23', or 'latest'"));
        }
        out.standard = *parsed;
    }

    if (auto it = (**object).find("runtime"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.runtime");
        if (!text) return std::unexpected(text.error());
        auto parsed = runtime_library(*text);
        if (!parsed) {
            return std::unexpected(schema_error(
                file,
                it->second,
                "build.runtime must be 'MD', 'MDd', 'MT', or 'MTd'"));
        }
        out.runtime_library = *parsed;
    }

    if (auto it = (**object).find("ltcg"); it != (**object).end()) {
        auto enabled = require_bool(file, it->second, "build.ltcg");
        if (!enabled) return std::unexpected(enabled.error());
        out.link_time_code_generation = *enabled;
    }

    if (auto it = (**object).find("subsystem"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.subsystem");
        if (!text) return std::unexpected(text.error());
        auto parsed = subsystem(*text);
        if (!parsed) {
            return std::unexpected(schema_error(
                file,
                it->second,
                "build.subsystem must be 'console' or 'windows'"));
        }
        out.subsystem = *parsed;
    }

    if (auto it = (**object).find("type"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.type");
        if (!text) return std::unexpected(text.error());
        auto parsed = target_kind(*text);
        if (!parsed) {
            return std::unexpected(schema_error(
                file,
                it->second,
                "build.type must be 'exe', 'dll', or 'static'"));
        }
        out.target_kind = *parsed;
    }

    if (auto it = (**object).find("entry"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.entry");
        if (!text) return std::unexpected(text.error());
        out.entry = resolve_path(root, *text);
    }

    if (auto it = (**object).find("output"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.output");
        if (!text) return std::unexpected(text.error());
        out.output_name = std::move(*text);
    }

    if (auto it = (**object).find("defines"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.defines");
        if (!values) return std::unexpected(values.error());
        out.defines = std::move(*values);
    }

    if (auto it = (**object).find("include_dirs"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "build.include_dirs");
        if (!values) return std::unexpected(values.error());
        out.include_directories = std::move(*values);
    }

    if (auto it = (**object).find("library_dirs"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "build.library_dirs");
        if (!values) return std::unexpected(values.error());
        out.library_directories = std::move(*values);
    }

    if (auto it = (**object).find("libraries"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.libraries");
        if (!values) return std::unexpected(values.error());
        out.libraries = std::move(*values);
    }

    if (auto it = (**object).find("compiler_args"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.compiler_args");
        if (!values) return std::unexpected(values.error());
        out.compiler_arguments = std::move(*values);
    }

    if (auto it = (**object).find("linker_args"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.linker_args");
        if (!values) return std::unexpected(values.error());
        out.linker_arguments = std::move(*values);
    }

    return {};
}

[[nodiscard]] std::expected<void, Error> decode_discovery(
    const fs::path& file,
    const fs::path& root,
    const JsonValue& value,
    DiscoveryOverrides& out) {
    auto object = require_object(file, value, "discovery");
    if (!object) return std::unexpected(object.error());

    auto known = reject_unknown(
        file,
        **object,
        {"enabled", "exclude_dirs", "extra_sources", "exclude_sources"},
        "discovery");
    if (!known) return std::unexpected(known.error());

    if (auto it = (**object).find("enabled"); it != (**object).end()) {
        auto enabled = require_bool(file, it->second, "discovery.enabled");
        if (!enabled) return std::unexpected(enabled.error());
        out.enabled = *enabled;
    }

    if (auto it = (**object).find("exclude_dirs"); it != (**object).end()) {
        auto values = require_paths(
            file,
            root,
            it->second,
            "discovery.exclude_dirs");
        if (!values) return std::unexpected(values.error());
        out.exclude_directories = std::move(*values);
    }

    if (auto it = (**object).find("extra_sources"); it != (**object).end()) {
        auto values = require_paths(
            file,
            root,
            it->second,
            "discovery.extra_sources");
        if (!values) return std::unexpected(values.error());
        out.extra_sources = std::move(*values);
    }

    if (auto it = (**object).find("exclude_sources"); it != (**object).end()) {
        auto values = require_paths(
            file,
            root,
            it->second,
            "discovery.exclude_sources");
        if (!values) return std::unexpected(values.error());
        out.exclude_sources = std::move(*values);
    }

    return {};
}

[[nodiscard]] std::expected<void, Error> decode_modules(
    const fs::path& file,
    const fs::path& root,
    const JsonValue& value,
    ModuleOverrides& out) {
    auto object = require_object(file, value, "modules");
    if (!object) return std::unexpected(object.error());

    auto known = reject_unknown(file, **object, {"external"}, "modules");
    if (!known) return std::unexpected(known.error());

    const auto external_it = (**object).find("external");
    if (external_it == (**object).end()) return {};

    auto external = require_object(
        file,
        external_it->second,
        "modules.external");
    if (!external) return std::unexpected(external.error());

    out.external_providers.reserve((*external)->size());
    for (const auto& [logical_name, provider_value] : **external) {
        if (logical_name.empty()) {
            return std::unexpected(schema_error(
                file,
                provider_value,
                "modules.external logical module name must not be empty"));
        }

        auto path = require_string(
            file,
            provider_value,
            "modules.external provider IFC");
        if (!path) return std::unexpected(path.error());

        out.external_providers.push_back(ExternalModuleProvider{
            .logical_name = logical_name,
            .interface_file = resolve_path(root, *path),
        });
    }
    return {};
}

} // namespace

std::expected<ProjectConfig, Error> decode_project_config(
    const fs::path& file,
    const JsonValue& root) {
    auto object = require_object(file, root, "project config root");
    if (!object) return std::unexpected(object.error());

    auto known = reject_unknown(
        file,
        **object,
        {"version", "build", "discovery", "modules"},
        "root");
    if (!known) return std::unexpected(known.error());

    const auto version_it = (**object).find("version");
    if (version_it == (**object).end()) {
        return std::unexpected(schema_error(
            file,
            root,
            "project config requires integer field 'version'"));
    }

    if (version_it->second.kind != json::Kind::number) {
        return std::unexpected(schema_error(
            file,
            version_it->second,
            "version must be integer 1"));
    }

    int version = 0;
    const auto& version_text = version_it->second.scalar;
    const auto [end, err] = std::from_chars(
        version_text.data(),
        version_text.data() + version_text.size(),
        version);
    if (err != std::errc{} || end != version_text.data() + version_text.size()) {
        return std::unexpected(schema_error(
            file,
            version_it->second,
            "version must be integer 1"));
    }

    if (version != 1) {
        return std::unexpected(make_error(
            ErrorCode::unsupported_version,
            file,
            version_it->second.line,
            version_it->second.column,
            "unsupported mqb.json version " + std::to_string(version)
                + " (expected 1)"));
    }

    ProjectConfig config;
    config.version = version;
    config.file = file;
    config.project_root = file.parent_path().lexically_normal();

    if (auto it = (**object).find("build"); it != (**object).end()) {
        auto decoded = decode_build(
            file,
            config.project_root,
            it->second,
            config.build);
        if (!decoded) return std::unexpected(decoded.error());
    }

    if (auto it = (**object).find("discovery"); it != (**object).end()) {
        auto decoded = decode_discovery(
            file,
            config.project_root,
            it->second,
            config.discovery);
        if (!decoded) return std::unexpected(decoded.error());
    }

    if (auto it = (**object).find("modules"); it != (**object).end()) {
        auto decoded = decode_modules(
            file,
            config.project_root,
            it->second,
            config.modules);
        if (!decoded) return std::unexpected(decoded.error());
    }

    return config;
}

} // namespace mqb::config::detail
