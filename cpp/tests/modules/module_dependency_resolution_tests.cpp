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
    std::vector<mqb::modules::RequiredModule> required = {},
    const bool toolchain_owned = false) {
    mqb::modules::P1689Rule rule;
    rule.provided_modules = std::move(provided);
    rule.required_modules = std::move(required);
    return mqb::modules::ScannedModuleUnit{
        .source = std::move(source),
        .rule = std::move(rule),
        .toolchain_owned = toolchain_owned,
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

[[nodiscard]] bool has_external_edge(
    const mqb::modules::ModuleDependencyPlan& plan,
    const fs::path& consumer,
    const fs::path& interface_file,
    const std::string_view logical_name) {
    return std::any_of(
        plan.resolved_external_dependencies.begin(),
        plan.resolved_external_dependencies.end(),
        [&](const mqb::modules::ResolvedExternalModuleDependency& dependency) {
            return dependency.consumer_source == consumer
                && dependency.interface_file == interface_file
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
                   "unresolved standard modules must not be fabricated as resolved edges");
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

    {
        const std::vector units{
            unit("consumer.cpp", {}, {require_named("vendor.math")}),
        };
        const std::vector external{
            mqb::ExternalModuleProvider{
                .logical_name = "vendor.math",
                .interface_file = "prebuilt/vendor.math.ifc",
            },
        };
        const auto plan = ModuleDependencyGraphBuilder::build(units, external);
        expect(plan.has_value(), "explicit external provider should resolve a named requirement");
        if (plan) {
            expect(plan->unresolved_requirements.empty(),
                   "configured external provider should remove the matching unresolved requirement");
            expect(plan->resolved_external_dependencies.size() == 1,
                   "external provider should create one typed external dependency");
            expect(has_external_edge(
                       *plan, "consumer.cpp", "prebuilt/vendor.math.ifc", "vendor.math"),
                   "external dependency should retain consumer, logical name, and exact IFC identity");
            expect(plan->compile_levels.size() == 1 && plan->compile_levels.front().size() == 1,
                   "read-only external provider must not become an MQB compile-level node");
        }
    }

    {
        const std::vector units{
            unit("local.ixx", {provide("vendor.math")}),
            unit("consumer.cpp", {}, {require_named("vendor.math")}),
        };
        const std::vector external{
            mqb::ExternalModuleProvider{
                .logical_name = "vendor.math",
                .interface_file = "prebuilt/vendor.math.ifc",
            },
        };
        const auto plan = ModuleDependencyGraphBuilder::build(units, external);
        expect(!plan.has_value(),
               "project-local and configured external providers for one logical name must be ambiguous");
        if (!plan) {
            expect(plan.error().code == mqb::modules::ModuleGraphErrorCode::ambiguous_named_provider,
                   "local/external provider conflict should preserve named-provider ambiguity diagnostics");
        }
    }

    {
        const std::vector units{
            unit("consumer.cpp", {}, {require_named("std")}),
        };
        const std::vector external{
            mqb::ExternalModuleProvider{
                .logical_name = "std",
                .interface_file = "prebuilt/std.ifc",
            },
        };
        const auto plan = ModuleDependencyGraphBuilder::build(units, external);
        expect(!plan.has_value(),
               "standard-library modules must not be smuggled through generic external provider policy");
        if (!plan) {
            expect(plan.error().code == mqb::modules::ModuleGraphErrorCode::toolchain_owned_provider,
                   "generic external configuration must keep std provider ownership toolchain-only");
        }
    }

    {
        const std::vector units{
            unit("fake-std.ixx", {provide("std")}),
            unit("consumer.cpp", {}, {require_named("std")}),
        };
        const auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(!plan.has_value(),
               "project sources must not impersonate the standard-library module provider");
        if (!plan) {
            expect(plan.error().code == mqb::modules::ModuleGraphErrorCode::toolchain_owned_provider,
                   "project-local std provider should report the toolchain ownership boundary");
        }
    }

    {
        const std::vector units{
            unit("vc/modules/std.ixx", {provide("std")}, {}, true),
            unit("consumer.cpp", {}, {require_named("std")}),
        };
        const auto plan = ModuleDependencyGraphBuilder::build(units);
        expect(plan.has_value(),
               "explicit toolchain-owned std source should participate in the authoritative provider graph");
        if (plan) {
            expect(plan->unresolved_requirements.empty(),
                   "toolchain-owned std provider should resolve import std");
            expect(has_edge(*plan, "consumer.cpp", "vc/modules/std.ixx", "std"),
                   "consumer should retain the exact selected toolchain std provider source");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_dependency_resolution_tests passed\n";
    return 0;
}
