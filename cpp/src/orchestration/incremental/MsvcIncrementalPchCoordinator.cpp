#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;
constexpr std::string_view creator_source_text =
    "// MQB synthetic precompiled-header creator. Header injection is owned by /FI.\n";

struct PchInspectionState {
    IncrementalPchInspection inspection;
    // If a stale-but-timestamp-reusable creator source must be repaired, force
    // the execution recheck to compile even if the filesystem timestamp happens
    // to retain coarse equality after materialization. Public diagnostics still
    // report the semantic source_changed reason modeled by inspection.
    bool force_compile_after_materialization{false};
};

[[nodiscard]] IncrementalPchError failure(
    const IncrementalPchErrorCode code,
    std::string message) {
    return IncrementalPchError{
        .code = code,
        .message = std::move(message),
    };
}

[[nodiscard]] IncrementalPchError compile_failure(
    std::string message,
    IncrementalCompileError compile_error) {
    IncrementalPchError error = failure(
        IncrementalPchErrorCode::compile_failed,
        std::move(message));
    error.compile_error = std::move(compile_error);
    return error;
}

[[nodiscard]] bool regular_file(const fs::path& path) {
    std::error_code error_code;
    return fs::is_regular_file(path, error_code) && !error_code;
}

[[nodiscard]] bool creator_source_is_current(const fs::path& source) {
    if (!regular_file(source)) {
        return false;
    }

    std::ifstream input{source, std::ios::binary};
    if (!input) {
        return false;
    }
    const std::string existing{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    return existing == creator_source_text;
}

[[nodiscard]] std::expected<void, IncrementalPchError> ensure_parent(
    const fs::path& path) {
    if (path.parent_path().empty()) return {};
    std::error_code error_code;
    fs::create_directories(path.parent_path(), error_code);
    if (error_code) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::synthetic_source_failed,
            "failed to create PCH artifact directory: " + error_code.message()));
    }
    return {};
}

[[nodiscard]] std::expected<void, IncrementalPchError> write_creator_if_needed(
    const fs::path& source) {
    auto parent = ensure_parent(source);
    if (!parent) return std::unexpected(parent.error());

    if (creator_source_is_current(source)) {
        return {};
    }

    std::ofstream output{source, std::ios::binary | std::ios::trunc};
    if (!output) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::synthetic_source_failed,
            "failed to open synthetic PCH creator source for writing"));
    }
    output.write(creator_source_text.data(), static_cast<std::streamsize>(creator_source_text.size()));
    output.close();
    if (!output) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::synthetic_source_failed,
            "failed to write synthetic PCH creator source"));
    }
    return {};
}

void add_reason_once(
    std::vector<BuildReason>& reasons,
    const BuildReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

[[nodiscard]] std::expected<IncrementalCompileRequest, IncrementalPchError>
make_compile_request(const IncrementalPchRequest& request) {
    if (request.header.empty()) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::invalid_request,
            "PCH header path must not be empty"));
    }
    if (!regular_file(request.header)) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::header_missing,
            "PCH header does not exist or is not a regular file"));
    }
    if (request.artifacts.source.empty()
        || request.artifacts.object.empty()
        || request.artifacts.dependencies.empty()
        || request.artifacts.precompiled_header.empty()
        || request.artifacts.compile_cache.empty()) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::invalid_request,
            "PCH artifact layout contains an empty path"));
    }

    CompilerOptions creator_options = request.compiler_options;
    creator_options.precompiled_header = PrecompiledHeaderBinding{
        .header = request.header.lexically_normal(),
        .artifact = request.artifacts.precompiled_header.lexically_normal(),
        .role = PrecompiledHeaderRole::create,
    };

    TranslationUnit unit;
    unit.source = request.artifacts.source;
    unit.kind = TranslationUnitKind::source;
    unit.outputs = {
        Artifact{
            .path = request.artifacts.object,
            .kind = ArtifactKind::object,
        },
        Artifact{
            .path = request.artifacts.precompiled_header,
            .kind = ArtifactKind::precompiled_header,
        },
    };

    return IncrementalCompileRequest{
        .unit = std::move(unit),
        .options = std::move(creator_options),
        .cache_file = request.artifacts.compile_cache,
        .source_dependencies_file = request.artifacts.dependencies,
        .working_directory = request.working_directory,
    };
}

[[nodiscard]] std::expected<PchInspectionState, IncrementalPchError>
inspect_pch(
    const IncrementalPchRequest& request,
    MsvcIncrementalCompileCoordinator& compile_coordinator) {
    auto compile_request = make_compile_request(request);
    if (!compile_request) {
        return std::unexpected(compile_request.error());
    }

    PchInspectionState state;
    state.inspection.creator_source_materialization_required =
        !creator_source_is_current(request.artifacts.source);

    auto compile = compile_coordinator.inspect(*compile_request);
    if (!compile) {
        return std::unexpected(compile_failure(
            "failed to inspect precompiled-header creator compilation",
            compile.error()));
    }

    state.inspection.compile_request = *compile_request;
    state.inspection.compile = std::move(*compile);

    // Compile freshness normally observes the creator source by path + timestamp.
    // That is sufficient for a missing creator, but an MQB-owned creator with
    // stale/corrupt content could retain an old timestamp and otherwise look
    // reusable. Model the repair as source_changed without writing the file.
    if (state.inspection.creator_source_materialization_required
        && state.inspection.compile.validation.reusable()) {
        auto forced_request = *compile_request;
        forced_request.force_rebuild = true;
        auto forced = compile_coordinator.inspect(forced_request);
        if (!forced) {
            return std::unexpected(compile_failure(
                "failed to plan precompiled-header creator repair",
                forced.error()));
        }
        state.inspection.compile.plan = std::move(forced->plan);
        add_reason_once(
            state.inspection.compile.validation.reasons,
            BuildReason::source_changed);
        state.force_compile_after_materialization = true;
    }

    return state;
}

[[nodiscard]] IncrementalCompileResult result_from_inspection(
    const IncrementalCompileInspection& inspection) {
    IncrementalCompileResult result;
    result.validation = inspection.validation;
    result.plan = inspection.plan;
    result.warnings = inspection.warnings;
    return result;
}

} // namespace

std::expected<IncrementalPchInspection, IncrementalPchError>
MsvcIncrementalPchCoordinator::inspect(const IncrementalPchRequest& request) const {
    auto inspected = inspect_pch(request, compile_coordinator_);
    if (!inspected) return std::unexpected(inspected.error());
    return std::move(inspected->inspection);
}

std::expected<IncrementalPchResult, IncrementalPchError>
MsvcIncrementalPchCoordinator::run(const IncrementalPchRequest& request) const {
    auto inspected = inspect_pch(request, compile_coordinator_);
    if (!inspected) return std::unexpected(inspected.error());

    // A warm PCH hit is now truly read-only at this layer: do not recreate
    // directories or reopen the MQB-owned synthetic source for writing.
    if (inspected->inspection.compile.plan.empty()) {
        return IncrementalPchResult{
            .compile = result_from_inspection(inspected->inspection.compile),
        };
    }

    for (const fs::path* artifact : {
             &request.artifacts.object,
             &request.artifacts.dependencies,
             &request.artifacts.precompiled_header,
             &request.artifacts.compile_cache}) {
        auto parent = ensure_parent(*artifact);
        if (!parent) return std::unexpected(parent.error());
    }
    auto creator = write_creator_if_needed(request.artifacts.source);
    if (!creator) return std::unexpected(creator.error());

    IncrementalCompileRequest compile_request = inspected->inspection.compile_request;
    if (inspected->force_compile_after_materialization) {
        compile_request.force_rebuild = true;
    }

    auto compiled = compile_coordinator_.run(compile_request);
    if (!compiled) {
        return std::unexpected(compile_failure(
            "precompiled-header creator compilation failed",
            compiled.error()));
    }

    if (inspected->force_compile_after_materialization) {
        // The force flag is an internal race/timestamp safety mechanism. Preserve
        // the semantic inspection decision for diagnostics and future plan parity.
        auto execution_warnings = std::move(compiled->warnings);
        compiled->validation = inspected->inspection.compile.validation;
        compiled->plan = inspected->inspection.compile.plan;
        compiled->warnings = inspected->inspection.compile.warnings;
        compiled->warnings.insert(
            compiled->warnings.end(),
            std::make_move_iterator(execution_warnings.begin()),
            std::make_move_iterator(execution_warnings.end()));
    }

    if (!regular_file(request.artifacts.precompiled_header)) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::compile_failed,
            "PCH creator completed without producing the owned .pch artifact"));
    }
    return IncrementalPchResult{.compile = std::move(*compiled)};
}

} // namespace mqb::orchestration
