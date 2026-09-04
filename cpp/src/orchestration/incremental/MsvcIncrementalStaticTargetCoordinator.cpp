#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/BoundedWorkScheduler.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

#include "IncrementalFileSnapshot.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using PathIdentitySet = std::unordered_set<std::string>;
using CompileAttempt =
    std::expected<IncrementalCompileResult, IncrementalCompileError>;

[[nodiscard]] IncrementalStaticTargetError failure(
    const IncrementalStaticTargetErrorCode code,
    std::string message,
    fs::path source = {}) {
    return IncrementalStaticTargetError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

[[nodiscard]] bool insert_unique(
    PathIdentitySet& seen,
    const fs::path& path) {
    return seen.emplace(
        mqb::platform::windows::path_identity_key(path)).second;
}

[[nodiscard]] IncrementalCompileRequest compile_request_for(
    const TargetSourceRequest& source,
    const CompilerOptions& options,
    const bool force_rebuild) {
    TranslationUnit unit;
    unit.source = source.source;
    unit.kind = TranslationUnitKind::source;
    unit.outputs.push_back(Artifact{
        .path = source.artifacts.object,
        .kind = ArtifactKind::object,
    });
    return IncrementalCompileRequest{
        .unit = std::move(unit),
        .options = options,
        .cache_file = source.artifacts.compile_cache,
        .source_dependencies_file = source.artifacts.dependencies,
        .working_directory = source.source.parent_path(),
        .force_rebuild = force_rebuild,
    };
}

[[nodiscard]] std::expected<BoundedWorkSummary, BoundedWorkError>
schedule_compile_wave(
    const IncrementalStaticTargetRequest& request,
    MsvcIncrementalCompileCoordinator& compile_coordinator,
    const bool force_rebuild,
    detail::FilesystemEvidenceTable* evidence_table,
    std::vector<std::optional<CompileAttempt>>& attempts) {
    attempts.clear();
    attempts.resize(request.sources.size());

    const auto compile_one = [&](const std::size_t index) {
        auto compile_request = compile_request_for(
            request.sources[index],
            request.compiler_options,
            force_rebuild);
        attempts[index].emplace(compile_coordinator.run(compile_request));
        return attempts[index]->has_value();
    };

    // Preserve the exact historical callback path when evidence sharing is
    // disabled. In particular, one- and two-TU targets should not pay even
    // a thread-local activation/deactivation for a table that cannot reduce
    // physical metadata queries after mandatory revalidation.
    if (evidence_table == nullptr) {
        return BoundedWorkScheduler::run(
            request.sources.size(),
            request.max_parallel_compiles,
            compile_one);
    }

    return BoundedWorkScheduler::run(
        request.sources.size(),
        request.max_parallel_compiles,
        [&](const std::size_t index) {
            detail::ScopedFilesystemEvidenceActivation evidence_activation{
                evidence_table};
            return compile_one(index);
        });
}

[[nodiscard]] std::optional<IncrementalStaticTargetError> first_compile_error(
    const IncrementalStaticTargetRequest& request,
    const std::vector<std::optional<CompileAttempt>>& attempts) {
    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (!attempts[index] || attempts[index]->has_value()) {
            continue;
        }

        auto error = failure(
            IncrementalStaticTargetErrorCode::compile_failed,
            "static target translation unit compilation failed",
            request.sources[index].source);
        error.compile_error = attempts[index]->error();
        return error;
    }
    return std::nullopt;
}

} // namespace

std::expected<IncrementalStaticTargetResult, IncrementalStaticTargetError>
MsvcIncrementalStaticTargetCoordinator::run(
    const IncrementalStaticTargetRequest& request) const {
    mqb::performance::ScopedWall validation_evidence{
        mqb::performance::WallKind::target_validation};
    if (request.sources.empty()) {
        return std::unexpected(failure(
            IncrementalStaticTargetErrorCode::no_sources,
            "static target build requires at least one source file"));
    }
    if (!request.max_parallel_compiles.valid()) {
        return std::unexpected(failure(
            IncrementalStaticTargetErrorCode::invalid_parallelism,
            "static target compile parallelism must be automatic or a positive fixed worker count"));
    }

    TargetTimings timings;
    const auto queue_started = Clock::now();

    PathIdentitySet seen_sources;
    PathIdentitySet seen_objects;
    PathIdentitySet seen_dependencies;
    PathIdentitySet seen_caches;
    seen_sources.reserve(request.sources.size());
    seen_objects.reserve(request.sources.size() + request.additional_objects.size());
    seen_dependencies.reserve(request.sources.size());
    seen_caches.reserve(request.sources.size());

    for (const auto& source : request.sources) {
        if (!insert_unique(seen_sources, source.source)) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::duplicate_source,
                "static target contains the same source more than once",
                source.source));
        }
        if (!insert_unique(seen_objects, source.artifacts.object)) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::duplicate_object,
                "two static target translation units map to the same object",
                source.source));
        }
        if (!insert_unique(seen_dependencies, source.artifacts.dependencies)) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::duplicate_dependencies,
                "two static target translation units map to the same dependency metadata",
                source.source));
        }
        if (!insert_unique(seen_caches, source.artifacts.compile_cache)) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::duplicate_compile_cache,
                "two static target translation units map to the same compile cache",
                source.source));
        }
    }
    for (const auto& object : request.additional_objects) {
        if (object.empty() || !insert_unique(seen_objects, object)) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::duplicate_object,
                object.empty()
                    ? "MQB-owned additional object path must not be empty"
                    : "MQB-owned additional object collides with another static target object",
                object));
        }
    }

    std::vector<std::optional<CompileAttempt>> attempts(request.sources.size());
    timings.compile_queue = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - queue_started);
    validation_evidence.finish();

    const auto compile_started = Clock::now();

    std::optional<detail::FilesystemEvidenceTable> filesystem_evidence;
    // The race-safety barrier revalidates every reused dependency once.
    // With only two translation units, one initial observation plus one
    // revalidation is the same two physical probes as the uncached path.
    // Do not even construct the synchronized table on that break-even path.
    if (request.sources.size() > 2
        && !request.force_downstream_rebuild) {
        filesystem_evidence.emplace();
    }
    detail::FilesystemEvidenceTable* shared_evidence =
        filesystem_evidence ? &*filesystem_evidence : nullptr;

    auto scheduled = schedule_compile_wave(
        request,
        compile_coordinator_,
        request.force_downstream_rebuild,
        shared_evidence,
        attempts);
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalStaticTargetErrorCode::scheduling_failed,
            "static target compile scheduler failed: "
                + scheduled.error().message));
    }
    if (auto error = first_compile_error(request, attempts)) {
        return std::unexpected(std::move(*error));
    }

    if (shared_evidence != nullptr
        && !shared_evidence->revalidate_shared()) {
        scheduled = schedule_compile_wave(
            request,
            compile_coordinator_,
            true,
            nullptr,
            attempts);
        if (!scheduled) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::scheduling_failed,
                "static target conservative rebuild scheduler failed: "
                    + scheduled.error().message));
        }
        if (auto error = first_compile_error(request, attempts)) {
            return std::unexpected(std::move(*error));
        }
    }

    timings.compile = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - compile_started);

    IncrementalStaticTargetResult result;
    result.compiles.reserve(request.sources.size());
    std::vector<fs::path> objects;
    objects.reserve(request.sources.size() + request.additional_objects.size());
    objects.insert(
        objects.end(),
        request.additional_objects.begin(),
        request.additional_objects.end());
    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (!attempts[index]) {
            return std::unexpected(failure(
                IncrementalStaticTargetErrorCode::scheduling_failed,
                "static target compile scheduler stopped without a recorded failure",
                request.sources[index].source));
        }
        auto compiled = std::move(attempts[index]->value());
        result.any_compiled = result.any_compiled || compiled.compiled;
        result.compiles.push_back(TargetCompileResult{
            .source = request.sources[index].source,
            .result = std::move(compiled),
        });
        objects.push_back(request.sources[index].artifacts.object);
    }

    const auto archive_started = Clock::now();
    auto archived = archive_coordinator_.run(IncrementalArchiveRequest{
        .objects = std::move(objects),
        .output = request.target.executable,
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory,
        .architecture = request.compiler_options.architecture,
        .link_time_code_generation =
            request.compiler_options.link_time_code_generation,
        .additional_arguments = request.librarian_arguments,
        .force_archive =
            request.force_downstream_rebuild || result.any_compiled,
    });
    timings.archive = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - archive_started);
    if (!archived) {
        auto error = failure(
            IncrementalStaticTargetErrorCode::archive_failed,
            "static target archive failed");
        error.archive_error = archived.error();
        return std::unexpected(std::move(error));
    }
    result.archive = std::move(*archived);
    result.timings = timings;
    return result;
}

} // namespace mqb::orchestration
