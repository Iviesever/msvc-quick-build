#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;
constexpr std::string_view creator_source_text =
    "// MQB synthetic precompiled-header creator. Header injection is owned by /FI.\n";

[[nodiscard]] IncrementalPchError failure(
    const IncrementalPchErrorCode code,
    std::string message) {
    return IncrementalPchError{
        .code = code,
        .message = std::move(message),
    };
}

[[nodiscard]] bool regular_file(const fs::path& path) {
    std::error_code error_code;
    return fs::is_regular_file(path, error_code) && !error_code;
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

    if (regular_file(source)) {
        std::ifstream input{source, std::ios::binary};
        if (input) {
            const std::string existing{
                std::istreambuf_iterator<char>{input},
                std::istreambuf_iterator<char>{}};
            if (existing == creator_source_text) {
                return {};
            }
        }
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

} // namespace

std::expected<IncrementalPchResult, IncrementalPchError>
MsvcIncrementalPchCoordinator::run(const IncrementalPchRequest& request) const {
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

    IncrementalCompileRequest compile_request{
        .unit = std::move(unit),
        .options = std::move(creator_options),
        .cache_file = request.artifacts.compile_cache,
        .source_dependencies_file = request.artifacts.dependencies,
        .working_directory = request.working_directory,
    };

    auto compiled = compile_coordinator_.run(compile_request);
    if (!compiled) {
        IncrementalPchError error = failure(
            IncrementalPchErrorCode::compile_failed,
            "precompiled-header creator compilation failed");
        error.compile_error = compiled.error();
        return std::unexpected(std::move(error));
    }
    if (!regular_file(request.artifacts.precompiled_header)) {
        return std::unexpected(failure(
            IncrementalPchErrorCode::compile_failed,
            "PCH creator completed without producing the owned .pch artifact"));
    }
    return IncrementalPchResult{.compile = std::move(*compiled)};
}

} // namespace mqb::orchestration
