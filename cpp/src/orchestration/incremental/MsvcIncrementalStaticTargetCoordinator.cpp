#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

#include "TargetCompileWave.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using PathIdentitySet = std::unordered_set<std::string>;
using CompileAttempt = detail::TargetCompileAttempt;

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
    return run_impl(request, nullptr);
}

std::expected<RecordedStaticTargetResult, IncrementalStaticTargetError>
MsvcIncrementalStaticTargetCoordinator::run_recorded(
    const IncrementalStaticTargetRequest& request,
    std::optional<ArtifactGenerationLabel> caller_label) const {
    std::optional<ArchiveArtifactRecord> archive_record;
    auto result = run_impl(request, &archive_record);
    if (!result) return std::unexpected(std::move(result.error()));
    if (!archive_record) return std::unexpected(failure(
        IncrementalStaticTargetErrorCode::archive_failed, "successful static target archive record unavailable"));
    std::vector<SourceArtifactAssociation> sources;
    sources.reserve(request.sources.size());
    for (std::size_t i = 0; i < request.sources.size(); ++i) {
        const auto& source = request.sources[i];
        const auto& compiled = result->compiles[i].result;
        sources.push_back({
            .source = result->compiles[i].source,
            .object = source.artifacts.object,
            .dependencies = source.artifacts.dependencies,
            .compile_cache = source.artifacts.compile_cache,
            .completion = compiled.compiled ? ArtifactCompletion::executed : ArtifactCompletion::reused,
            .has_warnings = !compiled.warnings.empty(),
        });
    }
    StaticTargetArtifactRecord record{
        .caller_label = std::move(caller_label),
        .compiler_options = request.compiler_options,
        .sources = std::move(sources),
        .additional_object_inputs = request.additional_objects,
        .archive = std::move(*archive_record),
    };
    return RecordedStaticTargetResult{std::move(*result), std::move(record)};
}

std::expected<IncrementalStaticTargetResult, IncrementalStaticTargetError>
MsvcIncrementalStaticTargetCoordinator::run_impl(
    const IncrementalStaticTargetRequest& request, std::optional<ArchiveArtifactRecord>* record) const {
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

    auto scheduled = detail::TargetCompileWave::run(
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

    // Keep the barrier after execution, exactly as for executable/DLL targets.
    if (shared_evidence != nullptr
        && !shared_evidence->revalidate_shared()) {
        scheduled = detail::TargetCompileWave::run(
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
    const IncrementalArchiveRequest archive_request{
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
    };
    auto archived = [&]() -> std::expected<IncrementalArchiveResult, IncrementalArchiveError> {
        if (!record) return archive_coordinator_.run(archive_request);
        auto completed = archive_coordinator_.run_recorded(archive_request);
        if (!completed) return std::unexpected(std::move(completed.error()));
        record->emplace(std::move(completed->record));
        return std::move(completed->result);
    }();
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
