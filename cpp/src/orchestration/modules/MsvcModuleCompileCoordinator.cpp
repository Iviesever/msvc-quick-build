#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mqb/orchestration/BoundedWorkScheduler.hpp"

#include "ModuleCompilePlan.hpp"
#include "ModuleCompileRequestFactory.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] ModuleCompileError failure(
    const ModuleCompileErrorCode code,
    std::string message,
    fs::path source = {}) {
    return ModuleCompileError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

} // namespace

std::expected<ModuleCompileWaveResult, ModuleCompileError>
MsvcModuleCompileCoordinator::run(const ModuleCompileWaveRequest& request) const {
    auto planned = detail::build_module_compile_plan(request);
    if (!planned) {
        return std::unexpected(std::move(planned.error()));
    }
    const detail::ModuleCompilePlan& plan = *planned;

    using CompileAttempt = std::expected<IncrementalCompileResult, IncrementalCompileError>;
    std::vector<std::optional<CompileAttempt>> attempts(plan.node_count());
    std::vector<bool> compiled_this_run(plan.node_count(), false);

    for (const auto& level : plan.level_indices) {
        const auto scheduled = BoundedWorkScheduler::run(
            level.size(), request.max_parallel_compiles,
            [&](const std::size_t level_index) {
                const std::size_t node_index = level[level_index];
                bool force_rebuild = false;
                for (const std::size_t provider_index : plan.provider_indices[node_index]) {
                    force_rebuild = force_rebuild || compiled_this_run[provider_index];
                }

                IncrementalCompileRequest compile_request = node_index < plan.source_count
                    ? detail::make_module_compile_request(
                        request.sources[node_index],
                        request.compiler_options,
                        plan.module_references[node_index],
                        plan.header_references[node_index],
                        force_rebuild,
                        request.working_directory)
                    : detail::make_header_unit_compile_request(
                        request.header_units[node_index - plan.source_count],
                        request.compiler_options,
                        force_rebuild,
                        request.working_directory);
                attempts[node_index].emplace(compile_coordinator_.run(compile_request));
                return attempts[node_index]->has_value();
            });
        if (!scheduled) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "module compile scheduler failed: " + scheduled.error().message));
        }

        for (const std::size_t node_index : level) {
            if (!attempts[node_index]) continue;
            if (!attempts[node_index]->has_value()) {
                const fs::path source = node_index < plan.source_count
                    ? request.sources[node_index].source
                    : request.header_units[node_index - plan.source_count].source;
                ModuleCompileError error = failure(
                    ModuleCompileErrorCode::compile_failed,
                    node_index < plan.source_count
                        ? "module translation unit compilation failed"
                        : "header-unit producer compilation failed",
                    source);
                error.compile_error = attempts[node_index]->error();
                return std::unexpected(std::move(error));
            }
        }

        for (const std::size_t node_index : level) {
            if (!attempts[node_index]) {
                const fs::path source = node_index < plan.source_count
                    ? request.sources[node_index].source
                    : request.header_units[node_index - plan.source_count].source;
                return std::unexpected(failure(
                    ModuleCompileErrorCode::scheduling_failed,
                    "module scheduler stopped without a recorded compile failure",
                    source));
            }
            compiled_this_run[node_index] = attempts[node_index]->value().compiled;
        }
    }

    ModuleCompileWaveResult result;
    result.compiles.reserve(plan.source_count);
    for (std::size_t index = 0; index < plan.source_count; ++index) {
        if (!attempts[index] || !attempts[index]->has_value()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "module compile result is missing after all dependency levels completed",
                request.sources[index].source));
        }
        auto compiled = std::move(attempts[index]->value());
        result.any_compiled = result.any_compiled || compiled.compiled;
        result.compiles.push_back(ModuleCompileResult{
            .source = request.sources[index].source,
            .result = std::move(compiled),
        });
    }

    result.header_unit_compiles.reserve(plan.header_count);
    for (std::size_t index = 0; index < plan.header_count; ++index) {
        const std::size_t node_index = plan.source_count + index;
        if (!attempts[node_index] || !attempts[node_index]->has_value()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "header-unit compile result is missing after all dependency levels completed",
                request.header_units[index].source));
        }
        auto compiled = std::move(attempts[node_index]->value());
        result.any_compiled = result.any_compiled || compiled.compiled;
        result.header_unit_compiles.push_back(HeaderUnitCompileResult{
            .source = request.header_units[index].source,
            .header_name = request.header_units[index].header_name,
            .lookup_method = request.header_units[index].lookup_method,
            .result = std::move(compiled),
        });
    }
    return result;
}

} // namespace mqb::orchestration
