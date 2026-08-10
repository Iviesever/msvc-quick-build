#include <filesystem>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

mqb::TranslationUnit make_unit(
    const std::filesystem::path& source,
    const std::filesystem::path& object) {
    mqb::TranslationUnit unit;
    unit.source = source;
    unit.outputs = {
        mqb::Artifact{object, mqb::ArtifactKind::object},
    };
    return unit;
}

} // namespace

int main() {
    const mqb::CompilePlanItem cached{
        .unit = make_unit("src/cached.cpp", "build/cached.obj"),
        .cache_validation = mqb::CompileCacheValidation{},
    };

    const mqb::CompilePlanItem stale{
        .unit = make_unit("src/stale.cpp", "build/stale.obj"),
        .cache_validation = mqb::CompileCacheValidation{
            .reasons = {
                mqb::BuildReason::source_changed,
                mqb::BuildReason::dependency_changed,
            },
        },
    };

    const std::vector<mqb::CompilePlanItem> items{cached, stale};
    const auto plan_result = mqb::BuildPlanner::plan_compile(items);
    expect(plan_result.has_value(), "valid compile inputs should produce a build plan");
    if (plan_result) {
        expect(plan_result->size() == 1,
               "reusable translation units should be omitted from the build plan");
        expect(std::holds_alternative<mqb::CompileAction>(plan_result->actions.front()),
               "stale translation unit should become a compile action");

        const auto& action = std::get<mqb::CompileAction>(plan_result->actions.front());
        expect(action.source == std::filesystem::path{"src/stale.cpp"},
               "compile action should preserve source identity");
        expect(action.object == std::filesystem::path{"build/stale.obj"},
               "compile action should preserve object artifact mapping");
        expect(action.reasons == stale.cache_validation.reasons,
               "planner should preserve typed cache invalidation reasons");
    }

    mqb::CompilePlanItem module_item;
    module_item.unit.source = "src/math.ixx";
    module_item.unit.kind = mqb::TranslationUnitKind::module_interface;
    module_item.unit.outputs = {
        mqb::Artifact{"build/math.ifc", mqb::ArtifactKind::module_interface},
        mqb::Artifact{"build/math.obj", mqb::ArtifactKind::object},
    };
    module_item.cache_validation.reasons = {mqb::BuildReason::missing_output};

    const std::vector<mqb::CompilePlanItem> module_items{module_item};
    const auto module_plan = mqb::BuildPlanner::plan_compile(module_items);
    expect(module_plan.has_value(),
           "module translation unit with exactly one object output should be plannable");
    if (module_plan) {
        const auto& action = std::get<mqb::CompileAction>(module_plan->actions.front());
        expect(action.object == std::filesystem::path{"build/math.obj"},
               "planner should select the object artifact and ignore additional module artifacts");
    }

    mqb::CompilePlanItem missing_object;
    missing_object.unit.source = "src/no_object.cpp";
    missing_object.unit.outputs = {
        mqb::Artifact{"build/no_object.ifc", mqb::ArtifactKind::module_interface},
    };
    missing_object.cache_validation.reasons = {mqb::BuildReason::missing_output};

    const std::vector<mqb::CompilePlanItem> missing_items{missing_object};
    const auto missing_result = mqb::BuildPlanner::plan_compile(missing_items);
    expect(!missing_result.has_value(), "stale unit without object output should fail planning");
    if (!missing_result) {
        expect(missing_result.error().code == mqb::BuildPlannerErrorCode::missing_object_output,
               "missing object should report the precise planner error");
        expect(missing_result.error().source == std::filesystem::path{"src/no_object.cpp"},
               "planner error should identify the invalid source");
    }

    mqb::CompilePlanItem duplicate_objects;
    duplicate_objects.unit.source = "src/duplicate.cpp";
    duplicate_objects.unit.outputs = {
        mqb::Artifact{"build/a.obj", mqb::ArtifactKind::object},
        mqb::Artifact{"build/b.obj", mqb::ArtifactKind::object},
    };
    duplicate_objects.cache_validation.reasons = {mqb::BuildReason::explicit_rebuild};

    const std::vector<mqb::CompilePlanItem> duplicate_items{duplicate_objects};
    const auto duplicate_result = mqb::BuildPlanner::plan_compile(duplicate_items);
    expect(!duplicate_result.has_value(), "multiple object outputs should fail planning");
    if (!duplicate_result) {
        expect(duplicate_result.error().code == mqb::BuildPlannerErrorCode::multiple_object_outputs,
               "multiple objects should report the precise planner error");
        expect(duplicate_result.error().object_output_count == 2,
               "planner error should report how many object outputs were found");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_build_planner_tests passed\n";
    return 0;
}
