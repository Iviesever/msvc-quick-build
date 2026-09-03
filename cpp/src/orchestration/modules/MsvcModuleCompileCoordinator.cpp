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

[[nodiscard]] fs::path source_for_node(
    const ModuleCompileWaveRequest& request,
    const detail::ModuleCompilePlan& plan,
    const std::size_t node_index) {
    return node_index < plan.source_count
        ? request.sources[node_index].source
        : request.header_units[node_index - plan.source_count].source;
}

[[nodiscard]] IncrementalCompileRequest make_node_request(
    const ModuleCompileWaveRequest& request,
    const detail::ModuleCompilePlan& plan,
    const std::size_t node_index,
    const bool force_rebuild) {
    if (node_index < plan.source_count) {
        return detail::make_module_compile_request(
            request.sources[node_index],
            request.compiler_options,
            plan.module_references[node_index],
            plan.header_references[node_index],
            force_rebuild,
            request.working_directory);
    }
    return detail::make_header_unit_compile_request(
        request.header_units[node_index - plan.source_count],
        request.compiler_options,
        force_rebuild,
        request.working_directory);
}

[[nodiscard]] bool provider_work_planned(
    const detail::ModuleCompilePlan& plan,
    const std::vector<bool>& planned_this_run,
    const std::size_t node_index) {
    bool force_rebuild = false;
    for (const std::size_t provider_index : plan.provider_indices[node_index]) {
        force_rebuild = force_rebuild || planned_this_run[provider_index];
    }
    return force_rebuild;
}

[[nodiscard]] ModuleCompileError incremental_failure(
    const ModuleCompileWaveRequest& request,
    const detail::ModuleCompilePlan& plan,
    const std::size_t node_index,
    const ModuleCompileErrorCode code,
    std::string message,
    IncrementalCompileError error) {
    ModuleCompileError result = failure(
        code,
        std::move(message),
        source_for_node(request, plan, node_index));
    result.compile_error = std::move(error);
    return result;
}

} // namespace

std::expected<ModuleCompileWaveInspection, ModuleCompileError>
MsvcModuleCompileCoordinator::inspect(const ModuleCompileWaveRequest& request) const {
    if (!request.max_parallel_compiles.valid()) {
        return std::unexpected(failure(
            ModuleCompileErrorCode::invalid_parallelism,
            "module compile parallelism must be automatic or a positive fixed worker count"));
    }

    auto planned = detail::build_module_compile_plan(request);
    if (!planned) {
        return std::unexpected(std::move(planned.error()));
    }
    const detail::ModuleCompilePlan& plan = *planned;

    using InspectionAttempt = std::expected<
        IncrementalCompileInspection,
        IncrementalCompileError>;
    std::vector<std::optional<IncrementalCompileRequest>> requests(plan.node_count());
    std::vector<std::optional<InspectionAttempt>> attempts(plan.node_count());
    std::vector<bool> planned_this_run(plan.node_count(), false);

    for (const auto& level : plan.level_indices) {
        const auto scheduled = BoundedWorkScheduler::run(
            level.size(),
            request.max_parallel_compiles,
            [&](const std::size_t level_index) {
                const std::size_t node_index = level[level_index];
                requests[node_index].emplace(make_node_request(
                    request,
                    plan,
                    node_index,
                    provider_work_planned(plan, planned_this_run, node_index)));
                attempts[node_index].emplace(
                    compile_coordinator_.inspect(*requests[node_index]));
                return attempts[node_index]->has_value();
            });
        if (!scheduled) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "module compile inspection scheduler failed: "
                    + scheduled.error().message));
        }

        for (const std::size_t node_index : level) {
            if (attempts[node_index] && !attempts[node_index]->has_value()) {
                return std::unexpected(incremental_failure(
                    request,
                    plan,
                    node_index,
                    ModuleCompileErrorCode::inspection_failed,
                    node_index < plan.source_count
                        ? "module translation unit inspection failed"
                        : "header-unit producer inspection failed",
                    attempts[node_index]->error()));
            }
        }

        for (const std::size_t node_index : level) {
            if (!requests[node_index] || !attempts[node_index]) {
                return std::unexpected(failure(
                    ModuleCompileErrorCode::scheduling_failed,
                    "module compile inspection scheduler stopped without a recorded failure",
                    source_for_node(request, plan, node_index)));
            }
            planned_this_run[node_index] =
                !attempts[node_index]->value().plan.empty();
        }
    }

    ModuleCompileWaveInspection result;
    result.compiles.reserve(plan.source_count);
    for (std::size_t index = 0; index < plan.source_count; ++index) {
        if (!requests[index] || !attempts[index] || !attempts[index]->has_value()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "module compile inspection is missing after all dependency levels completed",
                request.sources[index].source));
        }
        result.any_planned = result.any_planned || planned_this_run[index];
        result.compiles.push_back(ModuleCompileInspection{
            .source = request.sources[index].source,
            .request = std::move(*requests[index]),
            .result = std::move(attempts[index]->value()),
        });
    }

    result.header_unit_compiles.reserve(plan.header_count);
    for (std::size_t index = 0; index < plan.header_count; ++index) {
        const std::size_t node_index = plan.source_count + index;
        if (!requests[node_index]
            || !attempts[node_index]
            || !attempts[node_index]->has_value()) {
            return std::unexpected(failure(
                ModuleCompileErrorCode::scheduling_failed,
                "header-unit inspection is missing after all dependency levels completed",
                request.header_units[index].source));
        }
        result.any_planned = result.any_planned || planned_this_run[node_index];
        result.header_unit_compiles.push_back(HeaderUnitCompileInspection{
            .source = request.header_units[index].source,
            .header_name = request.header_units[index].header_name,
            .lookup_method = request.header_units[index].lookup_method,
            .request = std::move(*requests[node_index]),
            .result = std::move(attempts[node_index]->value()),
        });
    }
    return result;
}

std::expected<ModuleCompileWaveResult, ModuleCompileError>
MsvcModuleCompileCoordinator::run(const ModuleCompileWaveRequest& request) const {
    if (!request.max_parallel_compiles.valid()) {
        return std::unexpected(failure(
            ModuleCompileErrorCode::invalid_parallelism,
            "module compile parallelism must be automatic or a positive fixed worker count"));
    }

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
                IncrementalCompileRequest compile_request = make_node_request(
                    request,
                    plan,
                    node_index,
                    provider_work_planned(plan, compiled_this_run, node_index));
                attempts[node_index].emplace(
                    compile_coordinator_.run(compile_request));
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
                return std::unexpected(incremental_failure(
                    request,
                    plan,
                    node_index,
                    ModuleCompileErrorCode::compile_failed,
                    node_index < plan.source_count
                        ? "module translation unit compilation failed"
                        : "header-unit producer compilation failed",
                    attempts[node_index]->error()));
            }
        }

        for (const std::size_t node_index : level) {
            if (!attempts[node_index]) {
                return std::unexpected(failure(
                    ModuleCompileErrorCode::scheduling_failed,
                    "module scheduler stopped without a recorded compile failure",
                    source_for_node(request, plan, node_index)));
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
