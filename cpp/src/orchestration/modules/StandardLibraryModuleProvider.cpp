#include "StandardLibraryModuleProvider.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

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

[[nodiscard]] std::optional<fs::path> working_directory_for(
    const fs::path& requested,
    const fs::path& source) {
    if (!requested.empty()) return requested;
    if (!source.parent_path().empty()) return source.parent_path();
    return std::nullopt;
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

std::expected<void, IncrementalModuleTargetError>
inject_standard_library_module_providers(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner,
    ModuleTargetArtifactRegistry& artifacts,
    std::vector<modules::ScannedModuleUnit>& scanned_units,
    std::vector<ModuleCompileSourceRequest>& compile_sources) {
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

        msvc::ModuleScanInvocation invocation{
            .source = *source,
            .output_file = source_artifacts->module_dependencies,
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
            .artifacts = std::move(*source_artifacts),
            .kind = TranslationUnitKind::module_interface,
        });
        injected_standard_modules.emplace(std::move(pending->logical_name));
    }

    return {};
}

} // namespace mqb::orchestration::detail
