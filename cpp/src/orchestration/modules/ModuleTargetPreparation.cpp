#include "ModuleTargetPreparation.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/modules/ModuleDependencyGraph.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace mqb::orchestration::detail {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    std::string value = path.lexically_normal().generic_string();
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] IncrementalModuleTargetError failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    fs::path source = {}) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

[[nodiscard]] IncrementalModuleTargetError artifact_failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    const fs::path& source,
    const fs::path& artifact) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = source,
        .artifact = artifact,
    };
}

[[nodiscard]] std::optional<IncrementalModuleTargetError> claim_artifact(
    std::unordered_set<std::string>& claimed,
    const fs::path& source,
    const fs::path& artifact,
    const std::string_view role) {
    if (artifact.empty()) {
        return artifact_failure(
            IncrementalModuleTargetErrorCode::invalid_artifact,
            "module target " + std::string{role} + " artifact path is empty",
            source,
            artifact);
    }
    if (!claimed.emplace(windows_path_key(artifact)).second) {
        return artifact_failure(
            IncrementalModuleTargetErrorCode::artifact_collision,
            "module target " + std::string{role}
                + " artifact collides with another planned writable artifact",
            source,
            artifact);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<fs::path> working_directory_for(
    const fs::path& requested,
    const fs::path& source) {
    if (!requested.empty()) return requested;
    if (!source.parent_path().empty()) return source.parent_path();
    return std::nullopt;
}

[[nodiscard]] std::optional<HeaderUnitLookupMethod> core_lookup_method(
    const modules::LookupMethod lookup_method) {
    switch (lookup_method) {
    case modules::LookupMethod::include_angle:
        return HeaderUnitLookupMethod::angle;
    case modules::LookupMethod::include_quote:
        return HeaderUnitLookupMethod::quote;
    case modules::LookupMethod::by_name:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool standard_library_module_name(const std::string_view logical_name) noexcept {
    return logical_name == "std" || logical_name == "std.compat";
}

[[nodiscard]] std::optional<fs::path> standard_library_module_source(
    const msvc::MsvcToolchain& toolchain,
    const std::string_view logical_name) {
    if (logical_name == "std") return toolchain.standard_library_modules.std;
    if (logical_name == "std.compat") return toolchain.standard_library_modules.std_compat;
    return std::nullopt;
}

[[nodiscard]] bool named_requirement(const modules::RequiredModule& requirement) noexcept {
    return !requirement.unique_on_source_path
        && requirement.lookup_method == modules::LookupMethod::by_name;
}

struct PendingStandardLibraryRequirement {
    fs::path consumer;
    std::string logical_name;
};

[[nodiscard]] std::optional<PendingStandardLibraryRequirement>
next_standard_library_requirement(
    const std::vector<modules::ScannedModuleUnit>& units,
    const std::unordered_set<std::string>& injected) {
    for (const auto& unit : units) {
        for (const auto& requirement : unit.rule.required_modules) {
            if (!named_requirement(requirement)
                || !standard_library_module_name(requirement.logical_name)
                || injected.contains(requirement.logical_name)) {
                continue;
            }
            return PendingStandardLibraryRequirement{
                .consumer = unit.source,
                .logical_name = requirement.logical_name,
            };
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool provides_named_interface(
    const modules::P1689Rule& rule,
    const std::string_view logical_name) {
    return std::any_of(
        rule.provided_modules.begin(),
        rule.provided_modules.end(),
        [&](const modules::ProvidedModule& provided) {
            return !provided.unique_on_source_path
                && provided.is_interface
                && provided.logical_name == logical_name;
        });
}

[[nodiscard]] bool regular_file(const fs::path& path) {
    std::error_code error_code;
    return fs::is_regular_file(path, error_code) && !error_code;
}

} // namespace

std::expected<ModuleTargetPreparation, IncrementalModuleTargetError>
prepare_module_target(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner) {
    if (request.sources.empty()) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::no_sources,
            "module target requires at least one source"));
    }
    if (request.max_parallel_scans == 0 || request.max_parallel_compiles == 0) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::invalid_parallelism,
            "module target scan and compile parallelism must both be at least one"));
    }

    std::unordered_set<std::string> seen_sources;
    std::unordered_set<std::string> claimed_artifacts;
    seen_sources.reserve(request.sources.size() + 2u);
    claimed_artifacts.reserve(request.sources.size() * 5u + 18u);

    for (const auto& source : request.sources) {
        if (source.source.empty()) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::duplicate_source,
                "module target source path is empty",
                source.source));
        }
        if (!seen_sources.emplace(windows_path_key(source.source)).second) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::duplicate_source,
                "module target contains the same source more than once",
                source.source));
        }

        if (auto error = claim_artifact(
                claimed_artifacts, source.source, source.artifacts.object, "object")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                source.source,
                source.artifacts.dependencies,
                "source-dependency metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                source.source,
                source.artifacts.module_dependencies,
                "module-scan metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                source.source,
                source.artifacts.compile_cache,
                "compile-cache metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (source.kind == TranslationUnitKind::module_interface) {
            if (auto error = claim_artifact(
                    claimed_artifacts,
                    source.source,
                    source.artifacts.module_interface,
                    "IFC")) {
                return std::unexpected(std::move(*error));
            }
        }
    }

    if (auto error = claim_artifact(
            claimed_artifacts, {}, request.target.executable, "executable")) {
        return std::unexpected(std::move(*error));
    }
    if (auto error = claim_artifact(
            claimed_artifacts, {}, request.target.link_cache, "link-cache metadata")) {
        return std::unexpected(std::move(*error));
    }

    using ScanAttempt = std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>;
    std::vector<std::optional<ScanAttempt>> attempts(request.sources.size());

    const auto scheduled = BoundedWorkScheduler::run(
        request.sources.size(), request.max_parallel_scans,
        [&](const std::size_t index) {
            const auto& source = request.sources[index];
            msvc::ModuleScanInvocation invocation{
                .source = source.source,
                .output_file = source.artifacts.module_dependencies,
                .options = request.compiler_options,
                .kind = source.kind,
                .working_directory = working_directory_for(
                    request.working_directory,
                    source.source),
            };
            attempts[index].emplace(scanner.scan(invocation));
            return attempts[index]->has_value();
        });
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::scheduling_failed,
            "module scan scheduler failed: " + scheduled.error().message));
    }

    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (attempts[index] && !attempts[index]->has_value()) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::scan_failed,
                "module dependency scan failed",
                request.sources[index].source);
            error.scan_error = attempts[index]->error();
            return std::unexpected(std::move(error));
        }
    }

    ModuleTargetPreparation prepared;
    prepared.scans.reserve(request.sources.size());
    std::vector<modules::ScannedModuleUnit> scanned_units;
    scanned_units.reserve(request.sources.size() + 2u);
    std::vector<ModuleCompileSourceRequest> compile_sources = request.sources;
    compile_sources.reserve(request.sources.size() + 2u);

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (!attempts[index]) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::scheduling_failed,
                "module scan scheduler stopped without a recorded scan failure",
                request.sources[index].source));
        }
        auto scan = std::move(attempts[index]->value());
        if (scan.dependencies.rules.size() != 1) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::invalid_scan_result,
                "one-source module scan must produce exactly one P1689 rule",
                request.sources[index].source));
        }
        scanned_units.push_back(modules::ScannedModuleUnit{
            .source = request.sources[index].source,
            .rule = scan.dependencies.rules.front(),
        });
        prepared.scans.push_back(ModuleTargetScanResult{
            .source = request.sources[index].source,
            .result = std::move(scan),
        });
    }

    // P1689 from the user's selected TUs is the only trigger for standard-library
    // provider injection. Newly scanned toolchain providers may in turn require
    // another toolchain-owned standard module (std.compat -> std), so repeat to
    // a fixed point before handing the complete topology to the graph owner.
    std::unordered_set<std::string> injected_standard_modules;
    while (auto pending = next_standard_library_requirement(
               scanned_units,
               injected_standard_modules)) {
        if (request.compiler_options.standard != CppStandard::latest) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::standard_library_module_standard_unsupported,
                "MSVC standard-library module '" + pending->logical_name
                    + "' requires --std latest",
                pending->consumer));
        }
        if (!request.artifact_layout) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::artifact_layout_missing,
                "standard-library module provider requires an artifact layout",
                pending->consumer));
        }

        auto source = standard_library_module_source(
            scanner.toolchain(),
            pending->logical_name);
        if (!source || source->empty() || !regular_file(*source)) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::standard_library_module_unavailable,
                "selected MSVC toolchain " + scanner.toolchain().identity.version
                    + " does not provide standard-library module source '"
                    + pending->logical_name + "'",
                pending->consumer));
        }

        if (!seen_sources.emplace(windows_path_key(*source)).second) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::duplicate_source,
                "toolchain standard-library module source collides with another target source",
                *source));
        }

        auto artifacts = request.artifact_layout->for_source(*source);
        if (!artifacts) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::artifact_layout_failed,
                "failed to assign artifacts to standard-library module provider",
                *source);
            error.artifact_layout_error = artifacts.error();
            return std::unexpected(std::move(error));
        }

        if (auto error = claim_artifact(
                claimed_artifacts,
                *source,
                artifacts->object,
                "standard-library module object")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                *source,
                artifacts->dependencies,
                "standard-library source-dependency metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                *source,
                artifacts->module_dependencies,
                "standard-library module-scan metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                *source,
                artifacts->compile_cache,
                "standard-library compile-cache metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                *source,
                artifacts->module_interface,
                "standard-library IFC")) {
            return std::unexpected(std::move(*error));
        }

        msvc::ModuleScanInvocation invocation{
            .source = *source,
            .output_file = artifacts->module_dependencies,
            .options = request.compiler_options,
            .kind = TranslationUnitKind::module_interface,
            .working_directory = working_directory_for(
                request.working_directory,
                *source),
        };
        auto scan = scanner.scan(invocation);
        if (!scan) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::scan_failed,
                "standard-library module dependency scan failed",
                *source);
            error.scan_error = scan.error();
            return std::unexpected(std::move(error));
        }
        if (scan->dependencies.rules.size() != 1
            || !provides_named_interface(
                scan->dependencies.rules.front(),
                pending->logical_name)) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::invalid_scan_result,
                "selected MSVC standard-library module source did not provide expected interface '"
                    + pending->logical_name + "'",
                *source));
        }

        scanned_units.push_back(modules::ScannedModuleUnit{
            .source = *source,
            .rule = scan->dependencies.rules.front(),
            .toolchain_owned = true,
        });
        compile_sources.push_back(ModuleCompileSourceRequest{
            .source = *source,
            .artifacts = std::move(*artifacts),
            .kind = TranslationUnitKind::module_interface,
        });
        injected_standard_modules.emplace(std::move(pending->logical_name));
    }

    auto plan = modules::ModuleDependencyGraphBuilder::build(
        scanned_units,
        request.compiler_options.external_module_providers);
    if (!plan) {
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::graph_failed,
            "failed to build module dependency graph");
        error.graph_error = plan.error();
        return std::unexpected(std::move(error));
    }
    prepared.plan = *plan;

    std::vector<ModuleCompileHeaderUnitRequest> header_units;
    header_units.reserve(prepared.plan.header_units.size());
    if (!prepared.plan.header_units.empty() && !request.artifact_layout) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::artifact_layout_missing,
            "module target discovered project-local header units but no artifact layout was provided",
            prepared.plan.header_units.front().source));
    }

    for (const auto& header : prepared.plan.header_units) {
        const auto lookup = core_lookup_method(header.lookup_method);
        if (!lookup) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::invalid_header_unit,
                "resolved header-unit provider has a non-header lookup method",
                header.source));
        }
        auto artifacts = request.artifact_layout->for_source(header.source);
        if (!artifacts) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::artifact_layout_failed,
                "failed to assign artifacts to discovered header-unit provider",
                header.source);
            error.artifact_layout_error = artifacts.error();
            return std::unexpected(std::move(error));
        }

        // Header units are IFC-only producers: object and module-scan paths from
        // SourceArtifacts are reserved layout slots but are not writable inputs
        // to this target. Claim only what the producer actually writes.
        if (auto error = claim_artifact(
                claimed_artifacts,
                header.source,
                artifacts->dependencies,
                "header-unit source-dependency metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                header.source,
                artifacts->compile_cache,
                "header-unit compile-cache metadata")) {
            return std::unexpected(std::move(*error));
        }
        if (auto error = claim_artifact(
                claimed_artifacts,
                header.source,
                artifacts->module_interface,
                "header-unit IFC")) {
            return std::unexpected(std::move(*error));
        }

        header_units.push_back(ModuleCompileHeaderUnitRequest{
            .source = header.source,
            .header_name = header.header_name,
            .lookup_method = *lookup,
            .artifacts = std::move(*artifacts),
        });
    }

    prepared.compile_request = ModuleCompileWaveRequest{
        .sources = std::move(compile_sources),
        .header_units = std::move(header_units),
        .plan = prepared.plan,
        .compiler_options = request.compiler_options,
        .working_directory = request.working_directory,
        .max_parallel_compiles = request.max_parallel_compiles,
    };
    return prepared;
}

} // namespace mqb::orchestration::detail
