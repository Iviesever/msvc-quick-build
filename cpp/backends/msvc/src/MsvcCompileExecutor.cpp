#include "mqb/msvc/MsvcCompileExecutor.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] CompileExecutorError invalid_request(std::string message) {
    return CompileExecutorError{
        .code = CompileExecutorErrorCode::invalid_request,
        .message = std::move(message),
    };
}

[[nodiscard]] const Artifact* find_single_object(
    const TranslationUnit& unit,
    std::size_t& object_count) noexcept {
    const Artifact* object = nullptr;
    object_count = 0;
    for (const auto& output : unit.outputs) {
        if (output.kind != ArtifactKind::object) {
            continue;
        }
        ++object_count;
        if (object == nullptr) {
            object = &output;
        }
    }
    return object;
}

[[nodiscard]] bool same_existing_file(
    const fs::path& left,
    const fs::path& right) {
    std::error_code error_code;
    const bool equivalent = fs::equivalent(left, right, error_code);
    return equivalent && !error_code;
}

} // namespace

std::expected<CompileExecutionResult, CompileExecutorError>
MsvcCompileExecutor::execute(const CompileExecutionRequest& request) const {
    if (request.unit.source.empty()) {
        return std::unexpected(invalid_request("translation unit source path is empty"));
    }
    if (request.source_dependencies_file.empty()) {
        return std::unexpected(invalid_request("sourceDependencies file path is empty"));
    }

    std::size_t object_count = 0;
    const Artifact* object = find_single_object(request.unit, object_count);
    if (object == nullptr || object_count != 1 || object->path.empty()) {
        return std::unexpected(invalid_request(
            "translation unit must expose exactly one non-empty object artifact"));
    }

    MsvcCompiler compiler{toolchain_, runner_};
    CompileInvocation invocation;
    invocation.source = request.unit.source;
    invocation.object = object->path;
    invocation.source_dependencies = request.source_dependencies_file;
    invocation.options = request.options;
    invocation.working_directory = request.working_directory;

    auto compiled = compiler.compile(invocation);
    if (!compiled) {
        return std::unexpected(CompileExecutorError{
            .code = CompileExecutorErrorCode::compiler_failed,
            .message = "MSVC compile invocation failed",
            .compiler_error = compiled.error(),
        });
    }

    std::error_code output_error;
    if (!fs::is_regular_file(object->path, output_error) || output_error) {
        return std::unexpected(CompileExecutorError{
            .code = CompileExecutorErrorCode::output_missing,
            .message = "MSVC reported success but the planned object artifact is missing",
        });
    }

    auto dependencies = MsvcSourceDependenciesReader::read(request.source_dependencies_file);
    if (!dependencies) {
        return std::unexpected(CompileExecutorError{
            .code = CompileExecutorErrorCode::dependency_metadata_failed,
            .message = "failed to read MSVC source dependency metadata",
            .dependency_error = dependencies.error(),
        });
    }

    if (!same_existing_file(dependencies->source, request.unit.source)) {
        return std::unexpected(CompileExecutorError{
            .code = CompileExecutorErrorCode::dependency_metadata_failed,
            .message = "sourceDependencies metadata belongs to a different translation unit",
        });
    }

    CompileCacheEntry cache_entry{
        .source = request.unit.source,
        .kind = request.unit.kind,
        .toolchain = toolchain_.identity,
        .signature = BuildSignature::for_compile(
            request.unit,
            toolchain_.identity,
            request.options),
        .object = *object,
        .dependencies = std::move(dependencies->includes),
    };

    return CompileExecutionResult{
        .process = std::move(*compiled),
        .cache_entry = std::move(cache_entry),
    };
}

} // namespace mqb::msvc
