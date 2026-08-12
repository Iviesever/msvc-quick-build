#include "mqb/core/DependencyGraph.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace mqb {

std::expected<void, DependencyGraphError> DependencyGraph::add_node(std::string key) {
    if (index_by_key_.contains(key)) {
        return std::unexpected(DependencyGraphError{
            .code = DependencyGraphErrorCode::duplicate_node,
            .node = std::move(key),
        });
    }

    const auto index = nodes_.size();
    index_by_key_.emplace(key, index);
    nodes_.push_back(std::move(key));
    dependencies_.emplace_back();
    return {};
}

std::expected<void, DependencyGraphError> DependencyGraph::add_dependency(
    const std::string_view node,
    const std::string_view dependency) {
    const auto node_it = index_by_key_.find(std::string{node});
    if (node_it == index_by_key_.end()) {
        return std::unexpected(DependencyGraphError{
            .code = DependencyGraphErrorCode::missing_node,
            .node = std::string{node},
        });
    }

    const auto dependency_it = index_by_key_.find(std::string{dependency});
    if (dependency_it == index_by_key_.end()) {
        return std::unexpected(DependencyGraphError{
            .code = DependencyGraphErrorCode::missing_node,
            .node = std::string{node},
            .dependency = std::string{dependency},
        });
    }

    auto& dependencies = dependencies_[node_it->second];
    if (std::find(dependencies.begin(), dependencies.end(), dependency_it->second)
        == dependencies.end()) {
        dependencies.push_back(dependency_it->second);
    }

    return {};
}

bool DependencyGraph::contains(const std::string_view key) const {
    return index_by_key_.contains(std::string{key});
}

std::expected<std::vector<std::vector<std::string>>, DependencyGraphError>
DependencyGraph::topological_levels() const {
    const auto node_count = nodes_.size();
    std::vector<std::size_t> remaining_dependencies(node_count, 0);
    std::vector<std::vector<std::size_t>> dependents(node_count);

    for (std::size_t node = 0; node < node_count; ++node) {
        remaining_dependencies[node] = dependencies_[node].size();
        for (const auto dependency : dependencies_[node]) {
            dependents[dependency].push_back(node);
        }
    }

    std::vector<std::size_t> ready;
    for (std::size_t node = 0; node < node_count; ++node) {
        if (remaining_dependencies[node] == 0) {
            ready.push_back(node);
        }
    }

    const auto sort_by_key = [this](const std::size_t left, const std::size_t right) {
        return nodes_[left] < nodes_[right];
    };
    std::sort(ready.begin(), ready.end(), sort_by_key);

    std::vector<std::vector<std::string>> levels;
    std::size_t processed = 0;

    while (!ready.empty()) {
        std::vector<std::string> level;
        level.reserve(ready.size());
        for (const auto node : ready) {
            level.push_back(nodes_[node]);
        }
        levels.push_back(std::move(level));
        processed += ready.size();

        std::vector<std::size_t> next_ready;
        for (const auto dependency : ready) {
            for (const auto dependent : dependents[dependency]) {
                auto& remaining = remaining_dependencies[dependent];
                if (remaining > 0) {
                    --remaining;
                    if (remaining == 0) {
                        next_ready.push_back(dependent);
                    }
                }
            }
        }

        std::sort(next_ready.begin(), next_ready.end(), sort_by_key);
        ready = std::move(next_ready);
    }

    if (processed != node_count) {
        std::vector<std::string> involved_nodes;
        for (std::size_t node = 0; node < node_count; ++node) {
            if (remaining_dependencies[node] != 0) {
                involved_nodes.push_back(nodes_[node]);
            }
        }
        std::sort(involved_nodes.begin(), involved_nodes.end());

        return std::unexpected(DependencyGraphError{
            .code = DependencyGraphErrorCode::cycle,
            .involved_nodes = std::move(involved_nodes),
        });
    }

    return levels;
}

std::expected<std::vector<std::string>, DependencyGraphError>
DependencyGraph::topological_order() const {
    auto levels = topological_levels();
    if (!levels) {
        return std::unexpected(levels.error());
    }

    std::vector<std::string> order;
    order.reserve(nodes_.size());
    for (const auto& level : *levels) {
        order.insert(order.end(), level.begin(), level.end());
    }
    return order;
}

} // namespace mqb
