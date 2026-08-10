#include <filesystem>
#include <iostream>
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

} // namespace

int main() {
    using mqb::modules::LookupMethod;
    using mqb::modules::ModuleDependencyGraphBuilder;
    using mqb::modules::ModuleGraphErrorCode;
    using mqb::modules::UnresolvedRequirementKind;

    {
        const std::vector units{
            unit("math.ixx", {provide("math")}),
            unit("stats.ixx", {provide("stats")}, {require_named("math")}),
            unit("app.cpp", {}, {require_named("stats"), require_named("std")}),
        };

        auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(), "named module chain should build a dependency plan");
        if (plan) {
            expect(plan->compile_levels.size() == 3,
                   "provider chain should form three compile levels");
            if (plan->compile_levels.size() == 3) {
                expect(plan->compile_levels[0] == std::vector<fs::path>{"math.ixx"},
                       "math provider should compile first");
                expect(plan->compile_levels[1] == std::vector<fs::path>{"stats.ixx"},
                       "stats provider should compile after math");
                expect(plan->compile_levels[2] == std::vector<fs::path>{"app.cpp"},
                       "consumer should compile after stats");
            }
            expect(plan->unresolved_requirements.size() == 1
                       && plan->unresolved_requirements[0].requirement.logical_name == "std"
                       && plan->unresolved_requirements[0].kind == UnresolvedRequirementKind::named_module,
                   "external named modules should remain typed unresolved requirements");
        }
    }

    {
        const std::vector units{
            unit("A.ixx", {provide("A")}),
            unit("B.ixx", {provide("B")}, {require_named("A")}),
            unit("C.ixx", {provide("C")}, {require_named("A")}),
            unit("D.cpp", {}, {require_named("B"), require_named("C")}),
        };

        auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(), "diamond graph should build successfully");
        if (plan && plan->compile_levels.size() == 3) {
            expect(plan->compile_levels[0] == std::vector<fs::path>{"A.ixx"},
                   "diamond root should be first level");
            expect(plan->compile_levels[1]
                       == std::vector<fs::path>({"B.ixx", "C.ixx"}),
                   "independent consumers should share one deterministic compile level");
            expect(plan->compile_levels[2] == std::vector<fs::path>{"D.cpp"},
                   "diamond sink should compile last");
        }
    }

    {
        const std::vector units{
            unit("M.ixx", {provide("M", true)}),
            unit("M_impl.cpp", {provide("M", false)}, {require_named("M")}),
            unit("consumer.cpp", {}, {require_named("M")}),
        };

        auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(),
               "one interface provider should disambiguate same-name non-interface units");
        if (plan) {
            expect(plan->compile_levels.size() == 2,
                   "interface provider should precede implementation and consumer");
            if (plan->compile_levels.size() == 2) {
                expect(plan->compile_levels[0] == std::vector<fs::path>{"M.ixx"},
                       "interface unit should be selected as import provider");
                expect(plan->compile_levels[1]
                           == std::vector<fs::path>({"M_impl.cpp", "consumer.cpp"}),
                       "implementation and ordinary consumer may follow the interface together");
            }
        }
    }

    {
        const std::vector units{
            unit("part.cppm", {provide("M:part", false)}),
            unit("consumer.cpp", {}, {require_named("M:part")}),
        };
        auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value() && plan->compile_levels.size() == 2,
               "a unique non-interface partition provider should be usable for ordering");
    }

    {
        mqb::modules::RequiredModule header;
        header.logical_name = "header.hpp";
        header.source_path = fs::path{"include/header.hpp"};
        header.unique_on_source_path = true;
        header.lookup_method = LookupMethod::include_quote;

        const std::vector units{
            unit("consumer.cpp", {}, {header}),
        };
        auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(), "header-unit requirements should not corrupt named-module graphing");
        if (plan) {
            expect(plan->unresolved_requirements.size() == 1
                       && plan->unresolved_requirements[0].kind == UnresolvedRequirementKind::header_unit,
                   "header units should remain explicitly unresolved until that milestone exists");
        }
    }

    {
        const std::vector units{
            unit("first.ixx", {provide("M", true)}),
            unit("second.ixx", {provide("M", true)}),
        };
        auto duplicate = ModuleDependencyGraphBuilder::build(units);
        expect(!duplicate
                   && duplicate.error().code == ModuleGraphErrorCode::duplicate_interface_provider,
               "multiple interface providers for one logical module should fail explicitly");
    }

    {
        const std::vector units{
            unit("one.cpp", {provide("M", false)}),
            unit("two.cpp", {provide("M", false)}),
        };
        auto ambiguous = ModuleDependencyGraphBuilder::build(units);
        expect(!ambiguous
                   && ambiguous.error().code == ModuleGraphErrorCode::ambiguous_named_provider,
               "multiple non-interface providers without an interface should be ambiguous");
    }

    {
        const std::vector units{
            unit("same.cpp", {provide("A")}),
            unit("same.cpp", {provide("B")}),
        };
        auto duplicate = ModuleDependencyGraphBuilder::build(units);
        expect(!duplicate && duplicate.error().code == ModuleGraphErrorCode::duplicate_source,
               "duplicate source units should be rejected before graphing");
    }

    {
        const std::vector units{
            unit("A.ixx", {provide("A")}, {require_named("B")}),
            unit("B.ixx", {provide("B")}, {require_named("A")}),
        };
        auto cycle = ModuleDependencyGraphBuilder::build(units);
        expect(!cycle && cycle.error().code == ModuleGraphErrorCode::dependency_cycle,
               "module import cycles should surface as dependency-cycle errors");
        if (!cycle) {
            expect(cycle.error().graph_error.has_value(),
                   "cycle error should retain the underlying graph diagnostic");
        }
    }

    {
        const std::vector units{
            unit({}, {provide("A")}),
        };
        auto empty = ModuleDependencyGraphBuilder::build(units);
        expect(!empty && empty.error().code == ModuleGraphErrorCode::empty_source,
               "empty scanned source identity should fail closed");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_dependency_graph_tests passed\n";
    return 0;
}
