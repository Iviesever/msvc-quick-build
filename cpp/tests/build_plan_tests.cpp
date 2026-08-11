#include <filesystem>
#include <iostream>
#include <string_view>
#include <variant>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/BuildTypes.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    mqb::BuildPlan plan;
    expect(plan.empty(), "a new build plan should be empty");

    mqb::CompileAction compile;
    compile.source = std::filesystem::path{"src/main.cpp"};
    compile.outputs = {
        mqb::Artifact{std::filesystem::path{"build/main.obj"}, mqb::ArtifactKind::object},
    };
    compile.reasons = {mqb::BuildReason::source_changed};
    plan.actions.emplace_back(compile);

    expect(!plan.empty(), "plan should no longer be empty after adding an action");
    expect(plan.size() == 1, "plan should contain one action");
    expect(std::holds_alternative<mqb::CompileAction>(plan.actions.front()),
           "first action should retain its compile-action type");

    const auto& stored_compile = std::get<mqb::CompileAction>(plan.actions.front());
    expect(stored_compile.source == std::filesystem::path{"src/main.cpp"},
           "compile action should retain its source path");
    expect(stored_compile.outputs.size() == 1
               && stored_compile.outputs.front().path == std::filesystem::path{"build/main.obj"}
               && stored_compile.outputs.front().kind == mqb::ArtifactKind::object,
           "compile action should retain its complete planned output set");
    expect(stored_compile.reasons.size() == 1,
           "compile action should retain rebuild reasons");
    expect(stored_compile.reasons.front() == mqb::BuildReason::source_changed,
           "compile action should retain the exact rebuild reason");
    expect(mqb::to_string(stored_compile.reasons.front()) == "source changed",
           "rebuild reason should have a stable diagnostic string");

    mqb::LinkAction link;
    link.objects = {std::filesystem::path{"build/main.obj"}};
    link.output = std::filesystem::path{"build/app.exe"};
    link.reasons = {mqb::BuildReason::missing_output};
    plan.actions.emplace_back(link);

    mqb::RunAction run;
    run.executable = std::filesystem::path{"build/app.exe"};
    run.arguments = {"--example"};
    plan.actions.emplace_back(run);

    expect(plan.size() == 3, "plan should preserve compile, link, and run actions");
    expect(std::holds_alternative<mqb::LinkAction>(plan.actions[1]),
           "second action should be a link action");
    expect(std::holds_alternative<mqb::RunAction>(plan.actions[2]),
           "third action should be a run action");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_build_plan_tests passed\n";
    return 0;
}
