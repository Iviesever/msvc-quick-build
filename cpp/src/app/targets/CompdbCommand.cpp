#include "CompdbCommand.hpp"

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Cli.hpp"
#include "Diagnostics.hpp"
#include "Invocation.hpp"
#include "ProjectSetup.hpp"
#include "mqb/core/Artifact.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcParameterCapabilities.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

struct ParsedCompdbOptions {
    mqb::cli::Options build;
    std::optional<fs::path> output;
};

struct CompilationDatabaseEntry {
    fs::path directory;
    fs::path file;
    fs::path output;
    std::vector<std::string> arguments;
};

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] bool is_module_interface_source(const fs::path& path) {
    const auto kind = mqb::classify_translation_unit_path(path);
    return kind && *kind == mqb::TranslationUnitKind::module_interface;
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream escaped;
    escaped << std::hex << std::uppercase;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20u) {
                escaped << "\\u00"
                        << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(ch);
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

[[nodiscard]] fs::path absolute_from(
    const fs::path& base,
    const fs::path& path) {
    return path.is_absolute()
        ? path.lexically_normal()
        : (base / path).lexically_normal();
}

[[nodiscard]] bool inside_project(
    const fs::path& project_root,
    const fs::path& path) {
    return mqb::platform::windows::path_identity_contains(project_root, path);
}

void add_portable_root_if_missing(
    std::vector<fs::path>& roots,
    const fs::path& candidate) {
    if (candidate.empty()) return;
    const fs::path normalized = candidate.lexically_normal();
    const std::string candidate_key = mqb::platform::windows::path_identity_key(normalized);
    const bool present = std::any_of(
        roots.begin(),
        roots.end(),
        [&](const fs::path& root) {
            return mqb::platform::windows::path_identity_key(root) == candidate_key;
        });
    if (!present) roots.push_back(normalized);
}

[[nodiscard]] bool validate_compiler_capabilities(
    const std::vector<std::string>& arguments,
    const std::string_view vc_tools_version) {
    for (const auto& argument : arguments) {
        const auto capability = mqb::msvc::MsvcParameterCapabilities::inspect(
            mqb::msvc::ParameterTool::compiler,
            argument,
            vc_tools_version);
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::active) continue;

        std::string message = "MSVC compiler option '" + argument + "' is ";
        message += mqb::msvc::to_string(capability.lifecycle);
        message += " for toolset ";
        message += vc_tools_version;
        if (!capability.guidance.empty()) {
            message += ": ";
            message += capability.guidance;
        }
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::deprecated) {
            diagnostics::print_warning(message);
            continue;
        }
        diagnostics::print_error(message);
        return false;
    }
    return true;
}

[[nodiscard]] std::expected<ParsedCompdbOptions, std::string>
parse_compdb_arguments(const std::span<const std::string_view> arguments) {
    std::vector<std::string_view> forwarded;
    forwarded.reserve(arguments.size() + 1u);
    forwarded.emplace_back("build");

    std::optional<fs::path> output;
    bool native_tail = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (!native_tail && (argument == "/link" || argument == "-link"
                || argument == "/lib" || argument == "/LIB")) {
            native_tail = true;
            forwarded.push_back(argument);
            continue;
        }
        if (!native_tail && (argument == "-o" || argument == "--output")) {
            if (output) {
                return std::unexpected("mqb compdb --output may be specified only once");
            }
            if (index + 1 >= arguments.size() || arguments[index + 1].empty()) {
                return std::unexpected("mqb compdb --output requires a path");
            }
            output = path_from_utf8(arguments[++index]);
            continue;
        }
        if (!native_tail && argument.starts_with("--output=")) {
            if (output) {
                return std::unexpected("mqb compdb --output may be specified only once");
            }
            const std::string_view value = argument.substr(std::string_view{"--output="}.size());
            if (value.empty()) {
                return std::unexpected("mqb compdb --output requires a path");
            }
            output = path_from_utf8(value);
            continue;
        }
        forwarded.push_back(argument);
    }

    auto parsed = mqb::cli::parse_arguments(forwarded);
    if (!parsed) return std::unexpected(parsed.error().message);
    return ParsedCompdbOptions{
        .build = std::move(*parsed),
        .output = std::move(output),
    };
}

[[nodiscard]] std::string render_compilation_database(
    const std::vector<CompilationDatabaseEntry>& entries) {
    std::ostringstream output;
    output << "[\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        output << "  {\n"
               << "    \"directory\": \"" << json_escape(path_to_utf8(entry.directory)) << "\",\n"
               << "    \"file\": \"" << json_escape(path_to_utf8(entry.file)) << "\",\n"
               << "    \"arguments\": [";
        for (std::size_t argument_index = 0;
             argument_index < entry.arguments.size();
             ++argument_index) {
            if (argument_index != 0) output << ", ";
            output << "\"" << json_escape(entry.arguments[argument_index]) << "\"";
        }
        output << "],\n"
               << "    \"output\": \"" << json_escape(path_to_utf8(entry.output)) << "\"\n"
               << "  }";
        if (index + 1 != entries.size()) output << ',';
        output << '\n';
    }
    output << "]\n";
    return output.str();
}

[[nodiscard]] std::expected<void, std::string>
write_database_file(
    const fs::path& output,
    const std::string_view content) {
    std::error_code error_code;
    const fs::path parent = output.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, error_code);
        if (error_code) {
            return std::unexpected("failed to create compilation database output directory");
        }
    }

    fs::path temporary = output;
    temporary += ".tmp-";
    temporary += std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());

    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) {
            return std::unexpected("failed to create temporary compilation database file");
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected("failed to write compilation database file");
        }
    }

    fs::rename(temporary, output, error_code);
    if (error_code) {
        error_code.clear();
        fs::remove(output, error_code);
        if (error_code) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected("failed to replace existing compilation database");
        }
        fs::rename(temporary, output, error_code);
    }
    if (error_code) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected("failed to publish compilation database");
    }
    return {};
}

} // namespace

int run_compdb_command(const std::span<const std::string_view> arguments) {
    auto parsed = parse_compdb_arguments(arguments);
    if (!parsed) {
        diagnostics::print_error(parsed.error());
        return 2;
    }
    auto options = std::move(parsed->build);
    if (options.show_help) {
        std::cout << compdb_usage();
        return 0;
    }

    auto invocation = resolve_invocation(options);
    if (!invocation) {
        diagnostics::print_error(invocation.error());
        return 2;
    }

    auto project = prepare_project(options, invocation->directory);
    if (!project) {
        if (project.error().config_error) {
            diagnostics::print_config_error(*project.error().config_error);
        } else {
            diagnostics::print_error(project.error().message);
        }
        return 2;
    }

    auto& project_config = project->config;
    auto& effective = project->effective;
    const fs::path& project_root = project->project_root;

    if (invocation->requested_sources.empty()) {
        auto entry = resolve_default_entry(
            project_root,
            project_config ? project_config->build.entry : std::nullopt);
        if (!entry) {
            diagnostics::print_error(entry.error());
            return 2;
        }
        invocation->requested_sources.push_back(std::move(*entry));
    }

    const auto& requested_sources = invocation->requested_sources;
    std::vector<fs::path> sources = requested_sources;
    bool discovery_requires_module_pipeline = false;
    if (options.discover_sources
        && requested_sources.size() == 1
        && !is_module_interface_source(requested_sources.front())) {
        auto forced_includes = mqb::msvc::MsvcParameterEngine::forced_includes(
            options.compiler_arguments);
        if (!forced_includes) {
            diagnostics::print_error(forced_includes.error().message);
            return 2;
        }
        if (effective.precompiled_header) {
            forced_includes->push_back(effective.precompiled_header->lexically_normal());
        }

        const fs::path& entry = requested_sources.front();
        const bool project_scoped = inside_project(project_root, entry);
        const fs::path discovery_root = project_scoped
            ? project_root
            : entry.parent_path();
        mqb::discovery::Request discovery_request{
            .project_root = discovery_root,
            .entry = entry,
            .include_directories = options.discovery_include_directories,
            .forced_includes = std::move(*forced_includes),
        };
        if (project_scoped) {
            discovery_request.excluded_directories = effective.discovery_exclude_directories;
            discovery_request.extra_sources = effective.discovery_extra_sources;
            discovery_request.excluded_sources = effective.discovery_exclude_sources;
        }
        auto discovered = mqb::discovery::SourceDiscovery::discover(discovery_request);
        if (!discovered) {
            diagnostics::print_error("source discovery failed: " + discovered.error().message);
            return 2;
        }
        for (const auto& warning : discovered->warnings) {
            diagnostics::print_warning("source discovery: " + warning.message);
        }
        discovery_requires_module_pipeline = discovered->requires_module_pipeline;
        sources = std::move(discovered->sources);
    }
    options.build.sources = sources;

    auto layout = mqb::ProjectArtifactLayout::create(project_root);
    if (!layout) {
        diagnostics::print_error(layout.error().message);
        return 2;
    }

    std::vector<mqb::orchestration::TargetSourceRequest> target_sources;
    target_sources.reserve(sources.size());
    for (const auto& source : sources) {
        auto artifacts = layout->for_source(source);
        if (!artifacts) {
            diagnostics::print_error(artifacts.error().message);
            return 2;
        }
        target_sources.push_back(mqb::orchestration::TargetSourceRequest{
            .source = source,
            .artifacts = std::move(*artifacts),
        });
    }

    add_portable_root_if_missing(options.portable_roots, project_root / "portable_msvc");
    for (const auto& source : sources) {
        add_portable_root_if_missing(options.portable_roots, source.parent_path() / "portable_msvc");
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions toolchain_discovery;
    toolchain_discovery.target_architecture = options.build.architecture;
    toolchain_discovery.host_architecture = mqb::Architecture::x64;
    toolchain_discovery.preference = options.toolchain_preference;
    toolchain_discovery.portable_roots = options.portable_roots;
    std::string toolchain_cache_name = "vs-";
    toolchain_cache_name += mqb::to_string(options.build.architecture);
    toolchain_cache_name += ".cache";
    toolchain_discovery.cache_file = layout->artifact_root()
        / "cache"
        / "toolchain"
        / toolchain_cache_name;

    auto toolchain = locator.discover(toolchain_discovery);
    if (!toolchain) {
        diagnostics::print_error(toolchain.error().message);
        return 3;
    }
    if (!validate_compiler_capabilities(
            options.compiler_arguments,
            toolchain->identity.version)) {
        return 2;
    }

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = options.build.configuration;
    compiler_options.architecture = options.build.architecture;
    compiler_options.standard = options.build.standard;
    compiler_options.runtime_library = options.runtime_override;
    compiler_options.link_time_code_generation = effective.link_time_code_generation;
    compiler_options.defines = std::move(options.defines);
    compiler_options.include_directories = std::move(options.include_directories);
    compiler_options.additional_arguments = std::move(options.compiler_arguments);
    compiler_options.external_module_providers = options.external_module_providers;

    const bool module_target = !compiler_options.external_module_providers.empty()
        || discovery_requires_module_pipeline
        || std::any_of(
            target_sources.begin(),
            target_sources.end(),
            [](const mqb::orchestration::TargetSourceRequest& source) {
                return is_module_interface_source(source.source);
            });
    if (module_target) {
        diagnostics::print_error(
            "mqb compdb currently supports ordinary C/C++ targets only; "
            "Modules/Header Units require P1689 graph materialization and are intentionally fail-closed");
        return 2;
    }

    const std::string target_name = options.build.output_name.value_or(
        diagnostics::path_text(requested_sources.front().stem()));
    if (effective.precompiled_header) {
        const auto c_source = std::find_if(
            target_sources.begin(),
            target_sources.end(),
            [](const mqb::orchestration::TargetSourceRequest& source) {
                return mqb::is_c_translation_unit_path(source.source);
            });
        if (c_source != target_sources.end()) {
            diagnostics::print_error(
                "first-class PCH currently requires an ordinary C++ source set");
            return 2;
        }
        auto pch = layout->for_precompiled_header(
            target_name,
            options.build.configuration,
            options.build.architecture);
        if (!pch) {
            diagnostics::print_error(pch.error().message);
            return 2;
        }
        compiler_options.precompiled_header = mqb::PrecompiledHeaderBinding{
            .header = effective.precompiled_header->lexically_normal(),
            .artifact = pch->precompiled_header.lexically_normal(),
            .role = mqb::PrecompiledHeaderRole::use,
        };
    }

    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    std::vector<CompilationDatabaseEntry> entries;
    entries.reserve(target_sources.size());
    for (const auto& source : target_sources) {
        const auto kind = mqb::classify_translation_unit_path(source.source);
        if (!kind || *kind != mqb::TranslationUnitKind::source) {
            diagnostics::print_error(
                "mqb compdb encountered a non-ordinary translation unit after ordinary-target validation");
            return 2;
        }

        mqb::TranslationUnit unit;
        unit.source = source.source;
        unit.kind = *kind;
        unit.outputs = {
            mqb::Artifact{source.artifacts.object, mqb::ArtifactKind::object},
        };
        const mqb::msvc::CompileExecutionRequest request{
            .unit = std::move(unit),
            .options = compiler_options,
            .source_dependencies_file = source.artifacts.dependencies,
            .working_directory = project_root,
        };
        auto recipe = executor.build_recipe(request);
        if (!recipe) {
            diagnostics::print_error(
                "failed to model compile recipe: " + recipe.error().message);
            return 2;
        }

        CompilationDatabaseEntry entry;
        entry.directory = absolute_from(invocation->directory, project_root);
        entry.file = absolute_from(invocation->directory, source.source);
        entry.output = absolute_from(project_root, source.artifacts.object);
        entry.arguments.reserve(recipe->process.arguments.size() + 1u);
        entry.arguments.push_back(path_to_utf8(recipe->process.executable));
        entry.arguments.insert(
            entry.arguments.end(),
            recipe->process.arguments.begin(),
            recipe->process.arguments.end());
        entries.push_back(std::move(entry));
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const CompilationDatabaseEntry& left, const CompilationDatabaseEntry& right) {
            return mqb::platform::windows::path_identity_key(left.file)
                < mqb::platform::windows::path_identity_key(right.file);
        });

    const fs::path output = parsed->output
        ? absolute_from(invocation->directory, *parsed->output)
        : (project_root / "compile_commands.json").lexically_normal();
    const std::string content = render_compilation_database(entries);
    auto written = write_database_file(output, content);
    if (!written) {
        diagnostics::print_error(written.error());
        return 2;
    }

    std::cout << "[compdb] " << entries.size() << " translation units\n"
              << "output: " << path_to_utf8(output) << '\n';
    return 0;
}

std::string_view compdb_usage() noexcept {
    return R"(Usage:
  mqb compdb [entry.cpp] [options] [MSVC-compiler-options]
  mqb compdb [source...] [options] [MSVC-compiler-options]

Generate a deterministic compile_commands.json from MQB's exact MSVC compile recipes.
The command performs project/config resolution, source discovery, artifact layout and toolchain
discovery, but does not compile, link, archive, or run the target.

Options are the same compile-side options accepted by `mqb build`.
For this command only:
  -o, --output <path>      Compilation database path (default: <project>/compile_commands.json)

The first release supports ordinary C/C++ targets, including MQB-owned PCH consumer recipes.
Targets requiring the Modules/Header Units P1689 pipeline fail closed until graph-aware compdb
materialization is added in a follow-up iteration.
)";
}

} // namespace mqb::app
