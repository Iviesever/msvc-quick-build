#include "mqb/msvc/MsvcCompileExecutor.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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

[[nodiscard]] const Artifact* find_single_artifact(
    const TranslationUnit& unit,
    const ArtifactKind kind,
    std::size_t& count) noexcept {
    const Artifact* found = nullptr;
    count = 0;
    for (const auto& output : unit.outputs) {
        if (output.kind != kind) {
            continue;
        }
        ++count;
        if (found == nullptr) {
            found = &output;
        }
    }
    return found;
}

[[nodiscard]] bool same_existing_file(
    const fs::path& left,
    const fs::path& right) {
    std::error_code error_code;
    const bool equivalent = fs::equivalent(left, right, error_code);
    return equivalent && !error_code;
}

[[nodiscard]] bool same_dependency_path(
    const fs::path& left,
    const fs::path& right) {
    if (same_existing_file(left, right)) {
        return true;
    }
    return left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] bool regular_file(const fs::path& path) {
    std::error_code error_code;
    return fs::is_regular_file(path, error_code) && !error_code;
}

void add_interface_dependency(
    std::vector<fs::path>& dependencies,
    const fs::path& interface_file) {
    const auto duplicate = std::find_if(
        dependencies.begin(),
        dependencies.end(),
        [&interface_file](const fs::path& dependency) {
            return same_dependency_path(dependency, interface_file);
        });
    if (duplicate == dependencies.end()) {
        dependencies.push_back(interface_file.lexically_normal());
    }
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
    const Artifact* object = find_single_artifact(
        request.unit,
        ArtifactKind::object,
        object_count);
    if (object == nullptr || object_count != 1 || object->path.empty()) {
        return std::unexpected(invalid_request(
            "translation unit must expose exactly one non-empty object artifact"));
    }

    std::size_t interface_count = 0;
    const Artifact* module_interface = find_single_artifact(
        request.unit,
        ArtifactKind::module_interface,
        interface_count);
    if (request.unit.kind == TranslationUnitKind::module_interface) {
        if (module_interface == nullptr
            || interface_count != 1
            || module_interface->path.empty()) {
            return std::unexpected(invalid_request(
                "module interface translation unit must expose exactly one non-empty IFC artifact"));
        }
    } else if (interface_count != 0) {
        return std::unexpected(invalid_request(
            "ordinary source translation unit must not expose an IFC output artifact"));
    }

    for (const auto& output : request.unit.outputs) {
        if (output.kind != ArtifactKind::object
            && output.kind != ArtifactKind::module_interface) {
            return std::unexpected(invalid_request(
                "compile translation unit contains a non-compile output artifact"));
        }
    }

    MsvcCompiler compiler{toolchain_, runner_};
    CompileInvocation invocation;
    invocation.source = request.unit.source;
    invocation.object = object->path;
    invocation.source_dependencies = request.source_dependencies_file;
    invocation.kind = request.unit.kind;
    if (module_interface != nullptr) {
        invocation.module_interface_output = module_interface->path;
    }
    invocation.module_references = request.unit.module_references;
    invocation.header_unit_references = request.unit.header_unit_references;
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

    if (!regular_file(object->path)) {
        return std::unexpected(CompileExecutorError{
            .code = CompileExecutorErrorCode::output_missing,
            .message = "MSVC reported success but the planned object artifact is missing",
        });
    }
    if (module_interface != nullptr && !regular_file(module_interface->path)) {
        return std::unexpected(CompileExecutorError{
            .code = CompileExecutorErrorCode::output_missing,
            .message = "MSVC reported success but the planned IFC artifact is missing",
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

    std::vector<fs::path> cache_dependencies = std::move(dependencies->includes);
    for (const auto& reference : request.unit.module_references) {
        add_interface_dependency(cache_dependencies, reference.interface_file);
    }
    for (const auto& reference : request.unit.header_unit_references) {
        add_interface_dependency(cache_dependencies, reference.interface_file);
    }

    CompileCacheEntry cache_entry{
        .source = request.unit.source,
        .kind = request.unit.kind,
        .toolchain = toolchain_.identity,
        .signature = BuildSignature::for_compile(
            request.unit,
            toolchain_.identity,
            request.options),
        .outputs = request.unit.outputs,
        .dependencies = std::move(cache_dependencies),
    };

    return CompileExecutionResult{
        .process = std::move(*compiled),
        .cache_entry = std::move(cache_entry),
    };
}

} // namespace mqb::msvc
