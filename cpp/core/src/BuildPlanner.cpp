#include "mqb/core/BuildPlanner.hpp"

#include <cstddef>
#include <expected>
#include <span>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildAction.hpp"

namespace mqb {

std::expected<BuildPlan, BuildPlannerError> BuildPlanner::plan_compile(
    const std::span<const CompilePlanItem> items) {
    BuildPlan plan;
    for (const auto& item : items) {
        if (item.cache_validation.reusable()) continue;
        std::size_t object_output_count = 0;
        std::size_t module_interface_output_count = 0;
        for (const auto& output : item.unit.outputs) {
            if (output.kind == ArtifactKind::object) ++object_output_count;
            else if (output.kind == ArtifactKind::module_interface) ++module_interface_output_count;
        }
        if (item.unit.header_unit) {
            if (object_output_count != 0 || module_interface_output_count != 1
                || item.unit.outputs.size() != 1 || item.unit.outputs.front().path.empty()) {
                return std::unexpected(BuildPlannerError{
                    .code = BuildPlannerErrorCode::invalid_header_unit_outputs,
                    .source = item.unit.source,
                    .object_output_count = object_output_count,
                    .module_interface_output_count = module_interface_output_count,
                });
            }
        } else {
            const Artifact* object_output = nullptr;
            for (const auto& output : item.unit.outputs) {
                if (output.kind == ArtifactKind::object) { object_output = &output; break; }
            }
            if (object_output_count == 0 || object_output == nullptr || object_output->path.empty()) {
                return std::unexpected(BuildPlannerError{
                    .code = BuildPlannerErrorCode::missing_object_output,
                    .source = item.unit.source,
                    .object_output_count = object_output_count,
                    .module_interface_output_count = module_interface_output_count,
                });
            }
            if (object_output_count > 1) {
                return std::unexpected(BuildPlannerError{
                    .code = BuildPlannerErrorCode::multiple_object_outputs,
                    .source = item.unit.source,
                    .object_output_count = object_output_count,
                    .module_interface_output_count = module_interface_output_count,
                });
            }
        }
        plan.actions.emplace_back(CompileAction{
            .source = item.unit.source,
            .outputs = item.unit.outputs,
            .reasons = item.cache_validation.reasons,
        });
    }
    return plan;
}

std::expected<BuildPlan, BuildPlannerError> BuildPlanner::plan_link(
    const LinkPlanItem& item) {
    BuildPlan plan;
    if (item.cache_validation.reusable()) return plan;
    if (item.objects.empty()) {
        return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_link_input});
    }
    for (const auto& object : item.objects) {
        if (object.empty()) return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_link_input});
    }
    for (const auto& library : item.libraries) {
        if (library.empty()) return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_link_input});
    }
    if (item.output.empty()) {
        return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_link_output});
    }
    plan.actions.emplace_back(LinkAction{
        .objects = item.objects,
        .output = item.output,
        .libraries = item.libraries,
        .reasons = item.cache_validation.reasons,
    });
    return plan;
}

std::expected<BuildPlan, BuildPlannerError> BuildPlanner::plan_archive(
    const ArchivePlanItem& item) {
    BuildPlan plan;
    if (item.cache_validation.reusable()) return plan;
    if (item.objects.empty()) {
        return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_archive_input});
    }
    for (const auto& object : item.objects) {
        if (object.empty()) return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_archive_input});
    }
    if (item.output.empty()) {
        return std::unexpected(BuildPlannerError{.code = BuildPlannerErrorCode::missing_archive_output});
    }
    plan.actions.emplace_back(ArchiveAction{
        .objects = item.objects,
        .output = item.output,
        .reasons = item.cache_validation.reasons,
    });
    return plan;
}

} // namespace mqb
