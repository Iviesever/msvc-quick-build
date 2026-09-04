#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"

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
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"
#include "mqb/msvc/MsvcFuzzerPolicy.hpp"
#include "mqb/msvc/MsvcOpenMpPolicy.hpp"
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

[[nodiscard]] IncrementalTargetError failure(
    const IncrementalTargetErrorCode code,
    std::string message,
    fs::path source = {}) {
    return IncrementalTargetError{
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
    const IncrementalTargetRequest& request,
    MsvcIncrementalCompileCoordinator& compile_coordinator,
    const bool force_rebuild,
    detail::FilesystemEvidenceTable* evidence_table,
    std::vector<std::optional<CompileAttempt>>& attempts) {
    attempts.clear();
    attempts.resize(request.sources.size());

    return BoundedWorkScheduler::run(
        request.sources.size(),
        request.max_parallel_compiles,
        [&](const std::size_t index) {
            detail::ScopedFilesystemEvidenceActivation evidence_activation{
                evidence_table};
            auto compile_request = compile_request_for(
                request.sources[index],
                request.compiler_options,
                force_rebuild);
            attempts[index].emplace(compile_coordinator.run(compile_request));
            return attempts[index]->has_value();
        });
}

[[nodiscard]] std::optional<IncrementalTargetError> first_compile_error(
    const IncrementalTargetRequest& request,
    const std::vector<std::optional<CompileAttempt>>& attempts) {
    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (!attempts[index] || attempts[index]->has_value()) {
            continue;
        }

        IncrementalTargetError error = failure(
            IncrementalTargetErrorCode::compile_failed,
            "translation unit compilation failed",
            request.sources[index].source);
        error.compile_error = attempts[index]->error();
        return error;
    }
    return std::nullopt;
}

} // namespace

std::expected<IncrementalTargetResult, IncrementalTargetError>
MsvcIncrementalTargetCoordinator::run(const IncrementalTargetRequest& request) const {
    mqb::performance::ScopedWall validation_evidence{
        mqb::performance::WallKind::target_validation};
    if (request.sources.empty()) {
        return std::unexpected(failure(
            IncrementalTargetErrorCode::no_sources,
            "target build requires at least one source file"));
    }
    if (!request.max_parallel_compiles.valid()) {
        return std::unexpected(failure(
            IncrementalTargetErrorCode::invalid_parallelism,
            "target compile parallelism must be automatic or a positive fixed worker count"));
    }

    TargetTimings timings;
    const auto queue_started = Clock::now();

    PathIdentitySet seen_sources;
    PathIdentitySet seen_objects;
    PathIdentitySet seen_dependencies;
    PathIdentitySet seen_compile_caches;
    seen_sources.reserve(request.sources.size());
    seen_objects.reserve(request.sources.size() + request.additional_objects.size());
    seen_dependencies.reserve(request.sources.size());
    seen_compile_caches.reserve(request.sources.size());

    for (const auto& source : request.sources) {
        if (!insert_unique(seen_sources, source.source)) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_source,
                "target build contains the same source more than once",
                source.source));
        }
        if (!insert_unique(seen_objects, source.artifacts.object)) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_object,
                "two translation units map to the same object artifact",
                source.source));
        }
        if (!insert_unique(seen_dependencies, source.artifacts.dependencies)) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_dependencies,
                "two translation units map to the same dependency metadata artifact",
                source.source));
        }
        if (!insert_unique(seen_compile_caches, source.artifacts.compile_cache)) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_compile_cache,
                "two translation units map to the same compile cache artifact",
                source.source));
        }
    }
    for (const auto& object : request.additional_objects) {
        if (object.empty() || !insert_unique(seen_objects, object)) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_object,
                object.empty()
                    ? "MQB-owned additional object path must not be empty"
                    : "MQB-owned additional object collides with another target object",
                object));
        }
    }

    std::vector<std::optional<CompileAttempt>> attempts(request.sources.size());
    timings.compile_queue = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - queue_started);
    validation_evidence.finish();

    const auto compile_started = Clock::now();

    // A target-scoped table is activated independently on every scheduler
    // callback thread. Only physical filesystem observations are shared; each
    // compile keeps its own cache load, validation, warnings, and result.
    detail::FilesystemEvidenceTable filesystem_evidence;
    detail::FilesystemEvidenceTable* shared_evidence =
        request.sources.size() > 1 && !request.force_downstream_rebuild
        ? &filesystem_evidence
        : nullptr;

    auto scheduled = schedule_compile_wave(
        request,
        compile_coordinator_,
        request.force_downstream_rebuild,
        shared_evidence,
        attempts);
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalTargetErrorCode::scheduling_failed,
            "target compile scheduler failed: " + scheduled.error().message));
    }
    if (auto error = first_compile_error(request, attempts)) {
        return std::unexpected(std::move(*error));
    }

    if (shared_evidence != nullptr
        && !shared_evidence->revalidate_shared()) {
        // A shared path changed (or could not be revalidated) across the
        // inspection window. Do not trust any hit derived from the old
        // observation: rebuild the complete target without evidence reuse.
        scheduled = schedule_compile_wave(
            request,
            compile_coordinator_,
            true,
            nullptr,
            attempts);
        if (!scheduled) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::scheduling_failed,
                "target conservative rebuild scheduler failed: "
                    + scheduled.error().message));
        }
        if (auto error = first_compile_error(request, attempts)) {
            return std::unexpected(std::move(*error));
        }
    }

    timings.compile = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - compile_started);

    IncrementalTargetResult result;
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
                IncrementalTargetErrorCode::scheduling_failed,
                "target compile scheduler stopped without a recorded compile failure",
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

    LinkOptions effective_link_options = request.link_options;
    msvc::MsvcAddressSanitizerPolicy::apply_link_policy(
        request.compiler_options,
        effective_link_options);
    msvc::MsvcFuzzerPolicy::apply_link_policy(
        request.compiler_options,
        effective_link_options);
    msvc::MsvcOpenMpPolicy::apply_link_policy(
        request.compiler_options,
        effective_link_options);

    IncrementalLinkRequest link_request{
        .objects = std::move(objects),
        .output = request.target.executable,
        .options = std::move(effective_link_options),
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory,
        .force_relink = request.force_downstream_rebuild || result.any_compiled,
    };
    const auto link_started = Clock::now();
    auto linked = link_coordinator_.run(link_request);
    timings.link = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - link_started);
    if (!linked) {
        IncrementalTargetError error = failure(
            IncrementalTargetErrorCode::link_failed,
            "target link failed");
        error.link_error = linked.error();
        return std::unexpected(std::move(error));
    }
    result.link = std::move(*linked);
    result.timings = timings;
    return result;
}

} // namespace mqb::orchestration
