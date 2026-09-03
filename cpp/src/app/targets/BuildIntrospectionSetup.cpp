#include "BuildIntrospectionSetup.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Diagnostics.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/msvc/MsvcParameterCapabilities.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] BuildIntrospectionError failure(
    std::string message,
    const int exit_code = 2) {
    return BuildIntrospectionError{
        .exit_code = exit_code,
        .message = std::move(message),
    };
}

[[nodiscard]] BuildIntrospectionError config_failure(ProjectSetupError error) {
    return BuildIntrospectionError{
        .exit_code = 2,
        .message = std::move(error.message),
        .config_error = std::move(error.config_error),
    };
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

[[nodiscard]] bool is_module_interface_source(const fs::path& path) {
    const auto kind = mqb::classify_translation_unit_path(path);
    return kind && *kind == mqb::TranslationUnitKind::module_interface;
}

[[nodiscard]] std::expected<void, BuildIntrospectionError>
validate_compiler_capabilities(
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
        return std::unexpected(failure(std::move(message)));
    }
    return {};
}

} // namespace

mqb::CompilerOptions BuildIntrospectionContext::consumer_compiler_options() const {
    mqb::CompilerOptions result = compiler_options;
    if (pch_artifacts && project.effective.precompiled_header) {
        result.precompiled_header = mqb::PrecompiledHeaderBinding{
            .header = project.effective.precompiled_header->lexically_normal(),
            .artifact = pch_artifacts->precompiled_header.lexically_normal(),
            .role = mqb::PrecompiledHeaderRole::use,
        };
    }
    return result;
}

std::expected<BuildIntrospectionContext, BuildIntrospectionError>
prepare_build_introspection(
    mqb::cli::Options options,
    mqb::process::ProcessRunner& runner,
    const BuildIntrospectionPolicy policy) {
    auto invocation = resolve_invocation(options);
    if (!invocation) {
        return std::unexpected(failure(invocation.error()));
    }

    auto project = prepare_project(options, invocation->directory);
    if (!project) {
        return std::unexpected(config_failure(std::move(project.error())));
    }

    if (invocation->requested_sources.empty()) {
        auto entry = resolve_default_entry(
            project->project_root,
            project->config ? project->config->build.entry : std::nullopt);
        if (!entry) {
            return std::unexpected(failure(entry.error()));
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
            return std::unexpected(failure(forced_includes.error().message));
        }
        if (project->effective.precompiled_header) {
            forced_includes->push_back(
                project->effective.precompiled_header->lexically_normal());
        }

        const fs::path& entry = requested_sources.front();
        const bool project_scoped = inside_project(project->project_root, entry);
        const fs::path discovery_root = project_scoped
            ? project->project_root
            : entry.parent_path();
        mqb::discovery::Request discovery_request{
            .project_root = discovery_root,
            .entry = entry,
            .include_directories = options.discovery_include_directories,
            .forced_includes = std::move(*forced_includes),
            .persistent_cache = policy.persistent_discovery_cache,
        };
        if (project_scoped) {
            discovery_request.excluded_directories = project->effective.discovery_exclude_directories;
            discovery_request.extra_sources = project->effective.discovery_extra_sources;
            discovery_request.excluded_sources = project->effective.discovery_exclude_sources;
        }
        auto discovered = mqb::discovery::SourceDiscovery::discover(discovery_request);
        if (!discovered) {
            return std::unexpected(failure(
                "source discovery failed: " + discovered.error().message));
        }
        for (const auto& warning : discovered->warnings) {
            diagnostics::print_warning("source discovery: " + warning.message);
        }
        discovery_requires_module_pipeline = discovered->requires_module_pipeline;
        sources = std::move(discovered->sources);
    }
    options.build.sources = sources;

    auto layout = mqb::ProjectArtifactLayout::create(project->project_root);
    if (!layout) {
        return std::unexpected(failure(layout.error().message));
    }

    std::vector<mqb::orchestration::TargetSourceRequest> target_sources;
    target_sources.reserve(sources.size());
    for (const auto& source : sources) {
        auto artifacts = layout->for_source(source);
        if (!artifacts) {
            return std::unexpected(failure(artifacts.error().message));
        }
        target_sources.push_back(mqb::orchestration::TargetSourceRequest{
            .source = source,
            .artifacts = std::move(*artifacts),
        });
    }

    const std::string target_name = options.build.output_name.value_or(
        diagnostics::path_text(requested_sources.front().stem()));
    auto target_artifacts = layout->for_target(target_name, options.build.target_kind);
    if (!target_artifacts) {
        return std::unexpected(failure(target_artifacts.error().message));
    }

    add_portable_root_if_missing(
        options.portable_roots,
        project->project_root / "portable_msvc");
    for (const auto& source : sources) {
        add_portable_root_if_missing(
            options.portable_roots,
            source.parent_path() / "portable_msvc");
    }

    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions toolchain_discovery;
    toolchain_discovery.target_architecture = options.build.architecture;
    toolchain_discovery.host_architecture = mqb::Architecture::x64;
    toolchain_discovery.preference = options.toolchain_preference;
    toolchain_discovery.portable_roots = options.portable_roots;
    if (policy.persistent_toolchain_cache) {
        std::string cache_name = "vs-";
        cache_name += mqb::to_string(options.build.architecture);
        cache_name += ".cache";
        toolchain_discovery.cache_file = layout->artifact_root()
            / "cache" / "toolchain" / cache_name;
    } else {
        // An explicitly empty cache path disables Visual Studio discovery reuse.
        // nullopt would delegate back to the locator's current-directory .mqb cache.
        toolchain_discovery.cache_file = fs::path{};
    }

    auto toolchain = locator.discover(toolchain_discovery);
    if (!toolchain) {
        return std::unexpected(failure(toolchain.error().message, 3));
    }

    auto capabilities = validate_compiler_capabilities(
        options.compiler_arguments,
        toolchain->identity.version);
    if (!capabilities) {
        return std::unexpected(capabilities.error());
    }

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = options.build.configuration;
    compiler_options.architecture = options.build.architecture;
    compiler_options.standard = options.build.standard;
    compiler_options.runtime_library = options.runtime_override;
    compiler_options.link_time_code_generation = project->effective.link_time_code_generation;
    compiler_options.defines = options.defines;
    compiler_options.include_directories = options.include_directories;
    compiler_options.additional_arguments = options.compiler_arguments;
    compiler_options.external_module_providers = options.external_module_providers;

    const bool module_target = !compiler_options.external_module_providers.empty()
        || discovery_requires_module_pipeline
        || std::any_of(
            target_sources.begin(),
            target_sources.end(),
            [](const mqb::orchestration::TargetSourceRequest& source) {
                return is_module_interface_source(source.source);
            });

    std::optional<mqb::PrecompiledHeaderArtifacts> pch_artifacts;
    if (project->effective.precompiled_header) {
        if (module_target) {
            return std::unexpected(failure(
                "first-class PCH is not yet supported with the Modules/Header Unit pipeline"));
        }
        const auto c_source = std::find_if(
            target_sources.begin(),
            target_sources.end(),
            [](const mqb::orchestration::TargetSourceRequest& source) {
                return mqb::is_c_translation_unit_path(source.source);
            });
        if (c_source != target_sources.end()) {
            return std::unexpected(failure(
                "first-class PCH currently requires an ordinary C++ source set"));
        }
        auto allocated = layout->for_precompiled_header(
            target_name,
            options.build.configuration,
            options.build.architecture);
        if (!allocated) {
            return std::unexpected(failure(allocated.error().message));
        }
        pch_artifacts = std::move(*allocated);
    }

    return BuildIntrospectionContext{
        .options = std::move(options),
        .invocation = std::move(*invocation),
        .project = std::move(*project),
        .layout = std::move(*layout),
        .target_sources = std::move(target_sources),
        .target_name = target_name,
        .target_artifacts = std::move(*target_artifacts),
        .toolchain = std::move(*toolchain),
        .compiler_options = std::move(compiler_options),
        .pch_artifacts = std::move(pch_artifacts),
        .module_target = module_target,
    };
}

} // namespace mqb::app
