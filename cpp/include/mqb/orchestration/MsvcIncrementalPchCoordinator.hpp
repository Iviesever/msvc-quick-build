#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"

namespace mqb::orchestration {

enum class IncrementalPchErrorCode {
    invalid_request,
    header_missing,
    synthetic_source_failed,
    compile_failed,
};

struct IncrementalPchError {
    IncrementalPchErrorCode code{IncrementalPchErrorCode::invalid_request};
    std::string message;
    std::optional<IncrementalCompileError> compile_error;
};

struct IncrementalPchRequest {
    std::filesystem::path header;
    PrecompiledHeaderArtifacts artifacts;
    CompilerOptions compiler_options;
    std::filesystem::path working_directory;
};

struct IncrementalPchResult {
    IncrementalCompileResult compile;
};

class MsvcIncrementalPchCoordinator {
public:
    explicit MsvcIncrementalPchCoordinator(
        MsvcIncrementalCompileCoordinator& compile_coordinator)
        : compile_coordinator_(compile_coordinator) {}

    [[nodiscard]] std::expected<IncrementalPchResult, IncrementalPchError>
    run(const IncrementalPchRequest& request) const;

private:
    MsvcIncrementalCompileCoordinator& compile_coordinator_;
};

} // namespace mqb::orchestration
