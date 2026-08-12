#include <iostream>
#include <string_view>
#include <vector>

#include "mqb/core/DependencyGraph.hpp"

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
    mqb::DependencyGraph graph;

    expect(graph.size() == 0, "new dependency graph should be empty");
    expect(graph.add_node("app").has_value(), "app node should be accepted");
    expect(graph.add_node("logging").has_value(), "logging node should be accepted");
    expect(graph.add_node("ui").has_value(), "ui node should be accepted");
    expect(graph.add_node("core").has_value(), "core node should be accepted");
    expect(graph.size() == 4, "graph should contain four unique nodes");
    expect(graph.contains("core"), "contains should find an existing node");
    expect(!graph.contains("missing"), "contains should reject a missing node");

    const auto duplicate = graph.add_node("core");
    expect(!duplicate.has_value(), "duplicate nodes should be rejected");
    if (!duplicate) {
        expect(duplicate.error().code == mqb::DependencyGraphErrorCode::duplicate_node,
               "duplicate node should report duplicate_node");
        expect(duplicate.error().node == "core",
               "duplicate-node error should identify the duplicated key");
    }

    expect(graph.add_dependency("ui", "core").has_value(),
           "ui should be able to depend on core");
    expect(graph.add_dependency("app", "ui").has_value(),
           "app should be able to depend on ui");
    expect(graph.add_dependency("app", "core").has_value(),
           "app should be able to depend directly on core");
    expect(graph.add_dependency("app", "ui").has_value(),
           "duplicate edges should be idempotent");

    const auto missing_dependency = graph.add_dependency("app", "renderer");
    expect(!missing_dependency.has_value(), "missing dependencies should be rejected");
    if (!missing_dependency) {
        expect(missing_dependency.error().code == mqb::DependencyGraphErrorCode::missing_node,
               "missing dependency should report missing_node");
        expect(missing_dependency.error().node == "app",
               "missing-dependency error should retain the dependent node");
        expect(missing_dependency.error().dependency == "renderer",
               "missing-dependency error should identify the absent dependency");
    }

    const auto missing_node = graph.add_dependency("renderer", "core");
    expect(!missing_node.has_value(), "an unknown dependent node should be rejected");
    if (!missing_node) {
        expect(missing_node.error().code == mqb::DependencyGraphErrorCode::missing_node,
               "unknown dependent node should report missing_node");
        expect(missing_node.error().node == "renderer",
               "missing-node error should identify the absent dependent node");
        expect(missing_node.error().dependency.empty(),
               "missing dependent node should not invent a dependency key");
    }

    const auto levels = graph.topological_levels();
    expect(levels.has_value(), "acyclic graph should produce topological levels");
    if (levels) {
        expect(levels->size() == 3, "graph should produce three parallel build levels");
        expect((*levels)[0] == std::vector<std::string>({"core", "logging"}),
               "first level should contain independent prerequisites in deterministic order");
        expect((*levels)[1] == std::vector<std::string>({"ui"}),
               "second level should contain ui after core");
        expect((*levels)[2] == std::vector<std::string>({"app"}),
               "final level should contain app after all prerequisites");
    }

    const auto order = graph.topological_order();
    expect(order.has_value(), "acyclic graph should produce a topological order");
    if (order) {
        expect(*order == std::vector<std::string>({"core", "logging", "ui", "app"}),
               "flattened order should preserve deterministic level ordering");
    }

    mqb::DependencyGraph cycle;
    expect(cycle.add_node("a").has_value(), "cycle node a should be accepted");
    expect(cycle.add_node("b").has_value(), "cycle node b should be accepted");
    expect(cycle.add_node("c").has_value(), "cycle node c should be accepted");
    expect(cycle.add_dependency("a", "b").has_value(), "a -> b should be accepted");
    expect(cycle.add_dependency("b", "c").has_value(), "b -> c should be accepted");
    expect(cycle.add_dependency("c", "a").has_value(), "c -> a should be accepted");

    const auto cycle_result = cycle.topological_levels();
    expect(!cycle_result.has_value(), "cyclic graph should fail topological sorting");
    if (!cycle_result) {
        expect(cycle_result.error().code == mqb::DependencyGraphErrorCode::cycle,
               "cycle should report cycle error code");
        expect(cycle_result.error().involved_nodes == std::vector<std::string>({"a", "b", "c"}),
               "cycle diagnostics should list all unresolved nodes deterministically");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_dependency_graph_tests passed\n";
    return 0;
}
