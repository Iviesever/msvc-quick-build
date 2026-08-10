#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/modules/ModuleDependencyGraph.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] mqb::modules::ProvidedModule provide(
    std::string name,
    const bool is_interface = true) {
    return mqb::modules::ProvidedModule{
        .logical_name = std::move(name),
        .is_interface = is_interface,
    };
}

[[nodiscard]] mqb::modules::RequiredModule require_named(std::string name) {
    return mqb::modules::RequiredModule{
        .logical_name = std::move(name),
    };
}

[[nodiscard]] mqb::modules::ScannedModuleUnit unit(
    fs::path source,
    std::vector<mqb::modules::ProvidedModule> provided = {},
    std::vector<mqb::modules::RequiredModule> required = {}) {
    mqb::modules::P1689Rule rule;
    rule.provided_modules = std::move(provided);
    rule.required_modules = std::move(required);
    return mqb::modules::ScannedModuleUnit{
        .source = std::move(source),
        .rule = std::move(rule),
    };
}

[[nodiscard]] bool has_edge(
    const mqb::modules::ModuleDependencyPlan& plan,
    const fs::path& consumer,
    const fs::path& provider,
    const std::string_view logical_name) {
    return std::any_of(
        plan.resolved_dependencies.begin(),
        plan.resolved_dependencies.end(),
        [&](const mqb::modules::ResolvedModuleDependency& dependency) {
            return dependency.consumer_source == consumer
                && dependency.provider_source == provider
                && dependency.logical_name == logical_name;
        });
}

} // namespace

int main() {
    using mqb::modules::ModuleDependencyGraphBuilder;

    {
        const std::vector units{
            unit("math.ixx", {provide("math")}),
            unit("stats.ixx", {provide("stats")}, {require_named("math")}),
            unit("app.cpp", {}, {require_named("stats"), require_named("std")}),
        };

        const auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(), "named module chain should resolve providers");
        if (plan) {
            expect(plan->resolved_dependencies.size() == 2,
                   "only internal named-module requirements should become resolved edges");
            expect(has_edge(*plan, "stats.ixx", "math.ixx", "math"),
                   "stats should retain the exact provider source selected for logical module math");
            expect(has_edge(*plan, "app.cpp", "stats.ixx", "stats"),
                   "app should retain the exact provider source selected for logical module stats");
            expect(!has_edge(*plan, "app.cpp", {}, "std"),
                   "unresolved external modules must not be fabricated as resolved edges");
        }
    }

    {
        const std::vector units{
            unit("M.ixx", {provide("M", true)}),
            unit("M_impl.cpp", {provide("M", false)}, {require_named("M")}),
            unit("consumer.cpp", {}, {require_named("M")}),
        };

        const auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(), "interface provider should disambiguate same-name module units");
        if (plan) {
            expect(has_edge(*plan, "M_impl.cpp", "M.ixx", "M"),
                   "implementation unit should reference the selected interface provider");
            expect(has_edge(*plan, "consumer.cpp", "M.ixx", "M"),
                   "ordinary consumer should reference the selected interface provider");
            expect(!has_edge(*plan, "consumer.cpp", "M_impl.cpp", "M"),
                   "non-interface same-name unit must not leak into provider resolution");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_dependency_resolution_tests passed\n";
    return 0;
}
