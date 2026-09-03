#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/modules/P1689.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"

namespace mqb::orchestration {

struct IncrementalModuleScanRequest {
    std::filesystem::path source;
    std::filesystem::path module_dependencies_file;
    std::filesystem::path compile_cache_file;
    CompilerOptions options;
    TranslationUnitKind kind{TranslationUnitKind::source};
    std::optional<std::filesystem::path> working_directory;
};

enum class ModuleScanReason {
    missing_cache_entry,
    missing_scan_evidence,
    include_search_roots_changed,
    scan_signature_changed,
    source_changed,
    output_changed,
    dependency_changed,
    dependency_metadata_invalid,
};

[[nodiscard]] std::string_view to_string(ModuleScanReason reason) noexcept;

struct IncrementalModuleScanInspection {
    // The recipe is always available, including on reusable warm paths. This
    // lets introspection show the exact cl.exe scan contract without launching
    // it while keeping one source of truth for real execution.
    msvc::MsvcModuleScanRecipe recipe;
    std::vector<ModuleScanReason> reasons;
    std::optional<modules::P1689Document> dependencies;

    [[nodiscard]] bool reusable() const noexcept {
        return reasons.empty() && dependencies.has_value();
    }

    [[nodiscard]] bool scan_required() const noexcept {
        return !reusable();
    }
};

struct IncrementalModuleScanResult {
    IncrementalModuleScanInspection inspection;
    msvc::ModuleScanResult result;
    bool scanned{false};
};

class MsvcIncrementalModuleScanCoordinator {
public:
    explicit MsvcIncrementalModuleScanCoordinator(
        msvc::MsvcModuleDependencyScanner& scanner)
        : scanner_(scanner) {}

    // Inspect compile-cache scan evidence and parse reusable P1689 metadata
    // without creating directories, rewriting metadata, or launching cl.exe.
    [[nodiscard]] std::expected<IncrementalModuleScanInspection, msvc::ModuleScanError>
    inspect(const IncrementalModuleScanRequest& request) const;

    // Execute the exact recipe from inspect() only when evidence is not
    // reusable. Scan evidence remains sealed by the subsequent successful
    // compile, preserving the existing scan/compile freshness contract.
    [[nodiscard]] std::expected<IncrementalModuleScanResult, msvc::ModuleScanError>
    run(const IncrementalModuleScanRequest& request) const;

private:
    msvc::MsvcModuleDependencyScanner& scanner_;
};

} // namespace mqb::orchestration
