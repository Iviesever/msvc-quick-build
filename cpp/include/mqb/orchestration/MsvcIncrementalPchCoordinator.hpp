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

struct IncrementalPchInspection {
    // Exact typed execution request projected by the PCH coordinator. Future
    // introspection consumers can feed this directly into build_recipe()
    // without reproducing /Yc, /Fp, /FI, artifact, or working-directory policy.
    msvc::CompileExecutionRequest compile_request;
    IncrementalCompileInspection compile;
    // The synthetic creator is MQB-owned writable state. Inspection reports
    // whether execution would have to materialize/repair it, but never writes it.
    bool creator_source_materialization_required{false};
};

struct IncrementalPchResult {
    IncrementalCompileResult compile;
};

class MsvcIncrementalPchCoordinator {
public:
    explicit MsvcIncrementalPchCoordinator(
        MsvcIncrementalCompileCoordinator& compile_coordinator)
        : compile_coordinator_(compile_coordinator) {}

    // Validate the PCH request, project its exact typed creator recipe request,
    // and inspect incremental freshness without creating directories, writing the
    // synthetic creator source, touching cache/output state, or launching cl.exe.
    [[nodiscard]] std::expected<IncrementalPchInspection, IncrementalPchError>
    inspect(const IncrementalPchRequest& request) const;

    [[nodiscard]] std::expected<IncrementalPchResult, IncrementalPchError>
    run(const IncrementalPchRequest& request) const;

private:
    MsvcIncrementalCompileCoordinator& compile_coordinator_;
};

} // namespace mqb::orchestration
