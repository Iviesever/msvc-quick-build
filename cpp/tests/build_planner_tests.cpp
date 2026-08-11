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
void expect(bool condition, std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
mqb::TranslationUnit make_unit(const std::filesystem::path& source, const std::filesystem::path& object) {
    mqb::TranslationUnit unit;
    unit.source = source;
    unit.outputs = {mqb::Artifact{object, mqb::ArtifactKind::object}};
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
            .reasons = {mqb::BuildReason::source_changed, mqb::BuildReason::dependency_changed},
        },
    };
    const std::vector<mqb::CompilePlanItem> items{cached, stale};
    const auto plan_result = mqb::BuildPlanner::plan_compile(items);
    expect(plan_result.has_value(), "valid compile inputs should produce a build plan");
    if (plan_result) {
        expect(plan_result->size() == 1, "reusable translation units should be omitted from the build plan");
        const auto& action = std::get<mqb::CompileAction>(plan_result->actions.front());
        expect(action.source == std::filesystem::path{"src/stale.cpp"}, "compile action should preserve source identity");
        expect(action.outputs.size() == 1
                   && action.outputs.front().path == std::filesystem::path{"build/stale.obj"}
                   && action.outputs.front().kind == mqb::ArtifactKind::object,
               "compile action should preserve planned object artifact mapping");
        expect(action.reasons == stale.cache_validation.reasons, "planner should preserve typed invalidation reasons");
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
    expect(module_plan.has_value(), "module TU with exactly one object output should be plannable");
    if (module_plan) {
        const auto& action = std::get<mqb::CompileAction>(module_plan->actions.front());
        expect(action.outputs.size() == 2
                   && action.outputs[0].path == std::filesystem::path{"build/math.ifc"}
                   && action.outputs[0].kind == mqb::ArtifactKind::module_interface
                   && action.outputs[1].path == std::filesystem::path{"build/math.obj"}
                   && action.outputs[1].kind == mqb::ArtifactKind::object,
               "planner should preserve complete module output set and ordering");
    }

    mqb::CompilePlanItem header_unit;
    header_unit.unit.source = "include/util.hpp";
    header_unit.unit.header_unit = mqb::HeaderUnitIdentity{
        .header_name = "util.hpp",
        .lookup_method = mqb::HeaderUnitLookupMethod::quote,
    };
    header_unit.unit.outputs = {mqb::Artifact{"build/util.ifc", mqb::ArtifactKind::module_interface}};
    header_unit.cache_validation.reasons = {mqb::BuildReason::missing_output};
    const std::vector<mqb::CompilePlanItem> header_items{header_unit};
    const auto header_plan = mqb::BuildPlanner::plan_compile(header_items);
    expect(header_plan.has_value() && header_plan->size() == 1,
           "explicit header-unit producer should be plannable with one IFC and no object");
    if (header_plan) {
        const auto& action = std::get<mqb::CompileAction>(header_plan->actions.front());
        expect(action.outputs.size() == 1
                   && action.outputs.front().kind == mqb::ArtifactKind::module_interface
                   && action.outputs.front().path == std::filesystem::path{"build/util.ifc"},
               "header-unit compile action should preserve IFC-only output set");
    }

    mqb::CompilePlanItem invalid_header_unit = header_unit;
    invalid_header_unit.unit.outputs.push_back(mqb::Artifact{"build/util.obj", mqb::ArtifactKind::object});
    const std::vector<mqb::CompilePlanItem> invalid_header_items{invalid_header_unit};
    const auto invalid_header_result = mqb::BuildPlanner::plan_compile(invalid_header_items);
    expect(!invalid_header_result, "header-unit producer with object output should fail planning");
    if (!invalid_header_result) {
        expect(invalid_header_result.error().code == mqb::BuildPlannerErrorCode::invalid_header_unit_outputs,
               "invalid header-unit output shape should have a dedicated planner error");
    }

    mqb::CompilePlanItem missing_object;
    missing_object.unit.source = "src/no_object.cpp";
    missing_object.unit.outputs = {mqb::Artifact{"build/no_object.ifc", mqb::ArtifactKind::module_interface}};
    missing_object.cache_validation.reasons = {mqb::BuildReason::missing_output};
    const std::vector<mqb::CompilePlanItem> missing_items{missing_object};
    const auto missing_result = mqb::BuildPlanner::plan_compile(missing_items);
    expect(!missing_result, "ordinary stale unit without object output should still fail planning");
    if (!missing_result) {
        expect(missing_result.error().code == mqb::BuildPlannerErrorCode::missing_object_output,
               "missing object should report precise planner error");
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
    expect(!duplicate_result, "multiple object outputs should fail planning");
    if (!duplicate_result) {
        expect(duplicate_result.error().code == mqb::BuildPlannerErrorCode::multiple_object_outputs,
               "multiple objects should report precise planner error");
        expect(duplicate_result.error().object_output_count == 2,
               "planner error should report object output count");
    }

    if (failures != 0) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "mqb_build_planner_tests passed\n";
    return 0;
}
