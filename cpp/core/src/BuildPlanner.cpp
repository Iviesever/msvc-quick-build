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
        if (item.cache_validation.reusable()) {
            continue;
        }

        const Artifact* object_output = nullptr;
        std::size_t object_output_count = 0;
        for (const auto& output : item.unit.outputs) {
            if (output.kind == ArtifactKind::object) {
                ++object_output_count;
                if (object_output == nullptr) {
                    object_output = &output;
                }
            }
        }

        if (object_output_count == 0 || object_output == nullptr || object_output->path.empty()) {
            return std::unexpected(BuildPlannerError{
                .code = BuildPlannerErrorCode::missing_object_output,
                .source = item.unit.source,
                .object_output_count = object_output_count,
            });
        }

        if (object_output_count > 1) {
            return std::unexpected(BuildPlannerError{
                .code = BuildPlannerErrorCode::multiple_object_outputs,
                .source = item.unit.source,
                .object_output_count = object_output_count,
            });
        }

        plan.actions.emplace_back(CompileAction{
            .source = item.unit.source,
            .object = object_output->path,
            .reasons = item.cache_validation.reasons,
        });
    }

    return plan;
}

} // namespace mqb
