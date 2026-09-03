#include "StandardLibraryModuleProvider.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ModuleTargetScanner.hpp"

namespace mqb::orchestration::detail {
namespace {

namespace fs = std::filesystem;

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

[[nodiscard]] bool standard_library_module_name(
    const std::string_view logical_name) noexcept {
    return logical_name == "std" || logical_name == "std.compat";
}

[[nodiscard]] std::optional<fs::path> standard_library_module_source(
    const msvc::MsvcToolchain& toolchain,
    const std::string_view logical_name) {
    if (logical_name == "std") return toolchain.standard_library_modules.std;
    if (logical_name == "std.compat") return toolchain.standard_library_modules.std_compat;
    return std::nullopt;
}

[[nodiscard]] bool named_requirement(
    const modules::RequiredModule& requirement) noexcept {
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
    const std::unordered_set<std::string>& visited) {
    for (const auto& unit : units) {
        for (const auto& requirement : unit.rule.required_modules) {
            if (!named_requirement(requirement)
                || !standard_library_module_name(requirement.logical_name)
                || visited.contains(requirement.logical_name)) {
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

[[nodiscard]] std::expected<ModuleCompileSourceRequest, IncrementalModuleTargetError>
resolve_standard_library_provider(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    const PendingStandardLibraryRequirement& pending) {
    if (request.compiler_options.standard != CppStandard::latest) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::standard_library_module_standard_unsupported,
            "MSVC standard-library module '" + pending.logical_name
                + "' requires --std latest",
            pending.consumer));
    }
    if (!request.artifact_layout) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::artifact_layout_missing,
            "standard-library module provider requires an artifact layout",
            pending.consumer));
    }

    auto source = standard_library_module_source(
        scanner.toolchain(),
        pending.logical_name);
    if (!source || source->empty() || !regular_file(*source)) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::standard_library_module_unavailable,
            "selected MSVC toolchain " + scanner.toolchain().identity.version
                + " does not provide standard-library module source '"
                + pending.logical_name + "'",
            pending.consumer));
    }

    if (auto registered = artifacts.add_standard_library_source_identity(*source);
        !registered) {
        return std::unexpected(std::move(registered.error()));
    }

    auto source_artifacts = request.artifact_layout->for_source(*source);
    if (!source_artifacts) {
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::artifact_layout_failed,
            "failed to assign artifacts to standard-library module provider",
            *source);
        error.artifact_layout_error = source_artifacts.error();
        return std::unexpected(std::move(error));
    }

    if (auto registered = artifacts.add_standard_library_artifacts(
            *source,
            *source_artifacts); !registered) {
        return std::unexpected(std::move(registered.error()));
    }

    return ModuleCompileSourceRequest{
        .source = *source,
        .artifacts = std::move(*source_artifacts),
        .kind = TranslationUnitKind::module_interface,
    };
}

[[nodiscard]] std::expected<modules::P1689Rule, IncrementalModuleTargetError>
validated_standard_library_rule(
    const fs::path& source,
    const std::string_view logical_name,
    const modules::P1689Document& dependencies) {
    if (dependencies.rules.size() != 1
        || !provides_named_interface(
            dependencies.rules.front(),
            logical_name)) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::invalid_scan_result,
            "selected MSVC standard-library module source did not provide expected interface '"
                + std::string{logical_name} + "'",
            source));
    }
    return dependencies.rules.front();
}

void append_reusable_standard_library_provider(
    const ModuleCompileSourceRequest& provider,
    modules::P1689Rule rule,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources) {
    scanned_units.push_back(modules::ScannedModuleUnit{
        .source = provider.source,
        .rule = std::move(rule),
        .toolchain_owned = true,
    });
    compile_sources.push_back(provider);
}

} // namespace

std::expected<bool, IncrementalModuleTargetError>
inspect_standard_library_module_providers(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources,
    std::vector<ModuleTargetScanInspection>& scan_inspections) {
    std::unordered_set<std::string> inspected_standard_modules;
    bool complete = true;

    while (auto pending = next_standard_library_requirement(
               scanned_units,
               inspected_standard_modules)) {
        auto provider = resolve_standard_library_provider(
            request,
            scanner,
            artifacts,
            *pending);
        if (!provider) return std::unexpected(std::move(provider.error()));

        auto inspection = inspect_module_source(
            *provider,
            request.compiler_options,
            request.working_directory,
            scanner);
        if (!inspection) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::scan_failed,
                "standard-library module dependency scan inspection failed",
                provider->source);
            error.scan_error = inspection.error();
            return std::unexpected(std::move(error));
        }

        inspected_standard_modules.emplace(pending->logical_name);
        if (inspection->reusable()) {
            auto rule = validated_standard_library_rule(
                provider->source,
                pending->logical_name,
                *inspection->dependencies);
            if (!rule) return std::unexpected(std::move(rule.error()));
            append_reusable_standard_library_provider(
                *provider,
                std::move(*rule),
                scanned_units,
                compile_sources);
        } else {
            complete = false;
        }

        scan_inspections.push_back(ModuleTargetScanInspection{
            .source = provider->source,
            .result = std::move(*inspection),
            .toolchain_owned = true,
        });
    }

    return complete;
}

std::expected<void, IncrementalModuleTargetError>
inject_standard_library_module_providers(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources) {
    std::unordered_set<std::string> injected_standard_modules;
    while (auto pending = next_standard_library_requirement(
               scanned_units,
               injected_standard_modules)) {
        auto provider = resolve_standard_library_provider(
            request,
            scanner,
            artifacts,
            *pending);
        if (!provider) return std::unexpected(std::move(provider.error()));

        auto scan = scan_module_source(
            *provider,
            request.compiler_options,
            request.working_directory,
            scanner);
        if (!scan) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::scan_failed,
                "standard-library module dependency scan failed",
                provider->source);
            error.scan_error = scan.error();
            return std::unexpected(std::move(error));
        }

        auto rule = validated_standard_library_rule(
            provider->source,
            pending->logical_name,
            scan->dependencies);
        if (!rule) return std::unexpected(std::move(rule.error()));
        append_reusable_standard_library_provider(
            *provider,
            std::move(*rule),
            scanned_units,
            compile_sources);
        injected_standard_modules.emplace(std::move(pending->logical_name));
    }

    return {};
}

} // namespace mqb::orchestration::detail
