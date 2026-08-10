#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

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

} // namespace

std::expected<IncrementalTargetResult, IncrementalTargetError>
MsvcIncrementalTargetCoordinator::run(const IncrementalTargetRequest& request) const {
    if (request.sources.empty()) {
        return std::unexpected(failure(
            IncrementalTargetErrorCode::no_sources,
            "target build requires at least one source file"));
    }

    std::vector<std::string> seen_sources;
    std::vector<std::string> seen_objects;
    seen_sources.reserve(request.sources.size());
    seen_objects.reserve(request.sources.size());

    for (const auto& source : request.sources) {
        const std::string source_key = windows_path_key(source.source);
        if (std::find(seen_sources.begin(), seen_sources.end(), source_key) != seen_sources.end()) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_source,
                "target build contains the same source more than once",
                source.source));
        }
        seen_sources.push_back(source_key);

        const std::string object_key = windows_path_key(source.artifacts.object);
        if (std::find(seen_objects.begin(), seen_objects.end(), object_key) != seen_objects.end()) {
            return std::unexpected(failure(
                IncrementalTargetErrorCode::duplicate_object,
                "two translation units map to the same object artifact",
                source.source));
        }
        seen_objects.push_back(object_key);
    }

    IncrementalTargetResult result;
    result.compiles.reserve(request.sources.size());
    std::vector<fs::path> objects;
    objects.reserve(request.sources.size());

    for (const auto& source : request.sources) {
        TranslationUnit unit;
        unit.source = source.source;
        unit.kind = TranslationUnitKind::source;
        unit.outputs.push_back(Artifact{
            .path = source.artifacts.object,
            .kind = ArtifactKind::object,
        });

        IncrementalCompileRequest compile_request{
            .unit = std::move(unit),
            .options = request.compiler_options,
            .cache_file = source.artifacts.compile_cache,
            .source_dependencies_file = source.artifacts.dependencies,
            .working_directory = source.source.parent_path(),
        };

        auto compiled = compile_coordinator_.run(compile_request);
        if (!compiled) {
            IncrementalTargetError error = failure(
                IncrementalTargetErrorCode::compile_failed,
                "translation unit compilation failed",
                source.source);
            error.compile_error = compiled.error();
            return std::unexpected(std::move(error));
        }

        result.any_compiled = result.any_compiled || compiled->compiled;
        result.compiles.push_back(TargetCompileResult{
            .source = source.source,
            .result = std::move(*compiled),
        });
        objects.push_back(source.artifacts.object);
    }

    IncrementalLinkRequest link_request{
        .objects = std::move(objects),
        .output = request.target.executable,
        .options = request.link_options,
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory,
        .force_relink = result.any_compiled,
    };
    auto linked = link_coordinator_.run(link_request);
    if (!linked) {
        IncrementalTargetError error = failure(
            IncrementalTargetErrorCode::link_failed,
            "target link failed");
        error.link_error = linked.error();
        return std::unexpected(std::move(error));
    }
    result.link = std::move(*linked);
    return result;
}

} // namespace mqb::orchestration
