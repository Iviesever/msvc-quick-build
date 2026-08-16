#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"
#include "mqb/msvc/MsvcFuzzerPolicy.hpp"
#include "mqb/msvc/MsvcOpenMpPolicy.hpp"
#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    std::string value = path.lexically_normal().generic_string();
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

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
    std::vector<std::string>& seen,
    const fs::path& path) {
    const std::string key = windows_path_key(path);
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
        return false;
    }
    seen.push_back(key);
    return true;
}

[[nodiscard]] IncrementalCompileRequest compile_request_for(
    const TargetSourceRequest& source,
    const CompilerOptions& options) {
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
    };
}

} // namespace

std::expected<IncrementalTargetResult, IncrementalTargetError>
MsvcIncrementalTargetCoordinator::run(const IncrementalTargetRequest& request) const {
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

    std::vector<std::string> seen_sources;
    std::vector<std::string> seen_objects;
    std::vector<std::string> seen_dependencies;
    std::vector<std::string> seen_compile_caches;
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

    using CompileAttempt = std::expected<IncrementalCompileResult, IncrementalCompileError>;
    std::vector<std::optional<CompileAttempt>> attempts(request.sources.size());
    timings.compile_queue = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - queue_started);

    const auto compile_started = Clock::now();
    const auto scheduled = BoundedWorkScheduler::run(
        request.sources.size(),
        request.max_parallel_compiles,
        [&](const std::size_t index) {
            const auto& source = request.sources[index];
            auto compile_request = compile_request_for(source, request.compiler_options);
            attempts[index].emplace(compile_coordinator_.run(compile_request));
            return attempts[index]->has_value();
        });
    timings.compile = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - compile_started);
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalTargetErrorCode::scheduling_failed,
            "target compile scheduler failed: " + scheduled.error().message));
    }

    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (!attempts[index]) {
            continue;
        }
        if (!attempts[index]->has_value()) {
            IncrementalTargetError error = failure(
                IncrementalTargetErrorCode::compile_failed,
                "translation unit compilation failed",
                request.sources[index].source);
            error.compile_error = attempts[index]->error();
            return std::unexpected(std::move(error));
        }
    }

    IncrementalTargetResult result;
    result.compiles.reserve(request.sources.size());
    std::vector<fs::path> objects;
    objects.reserve(request.sources.size() + request.additional_objects.size());
    objects.insert(objects.end(), request.additional_objects.begin(), request.additional_objects.end());

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
