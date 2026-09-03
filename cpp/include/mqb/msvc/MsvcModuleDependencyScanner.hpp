#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/modules/P1689.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

struct ModuleScanInvocation {
    std::filesystem::path source;
    std::filesystem::path output_file;
    CompilerOptions options;
    TranslationUnitKind kind{TranslationUnitKind::source};
    std::optional<std::filesystem::path> working_directory;
};

// Pure, inspectable representation of the exact cl.exe /scanDependencies
// contract. Construction does not prepare output directories, remove stale
// metadata, write cache state, or launch a process.
struct MsvcModuleScanRecipe {
    ToolchainIdentity toolchain;
    ModuleScanInvocation invocation;
    process::ProcessSpec process;
};

struct ModuleScanResult {
    process::ProcessResult process;
    modules::P1689Document dependencies;
    bool reused{false};
};

enum class ModuleScanErrorCode {
    invalid_request,
    output_prepare_failed,
    stale_output_remove_failed,
    process_failed,
    scan_failed,
    output_missing,
    output_read_failed,
    dependency_metadata_failed,
};

struct ModuleScanError {
    ModuleScanErrorCode code{ModuleScanErrorCode::invalid_request};
    std::string message;
    std::optional<process::ProcessError> process_error;
    std::optional<process::ProcessResult> process_result;
    std::optional<modules::P1689Error> dependency_error;
};

class MsvcModuleDependencyScanner {
public:
    MsvcModuleDependencyScanner(
        const MsvcToolchain& toolchain,
        process::ProcessRunner& runner)
        : toolchain_(toolchain), runner_(runner) {}

    [[nodiscard]] static std::expected<std::vector<std::string>, ModuleScanError>
    build_arguments(const ModuleScanInvocation& invocation);

    [[nodiscard]] static std::expected<MsvcModuleScanRecipe, ModuleScanError>
    build_recipe(
        const MsvcToolchain& toolchain,
        const ModuleScanInvocation& invocation);

    [[nodiscard]] std::expected<ModuleScanResult, ModuleScanError>
    execute_recipe(const MsvcModuleScanRecipe& recipe) const;

    [[nodiscard]] std::expected<ModuleScanResult, ModuleScanError>
    scan(const ModuleScanInvocation& invocation) const;

    [[nodiscard]] const MsvcToolchain& toolchain() const noexcept {
        return toolchain_;
    }

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
