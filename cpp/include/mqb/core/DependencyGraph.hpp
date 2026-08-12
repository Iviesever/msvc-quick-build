#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mqb {

enum class DependencyGraphErrorCode {
    duplicate_node,
    missing_node,
    cycle,
};

struct DependencyGraphError {
    DependencyGraphErrorCode code{};
    std::string node;
    std::string dependency;
    std::vector<std::string> involved_nodes;
};

// Stores edges as "node depends on dependency". Topological results therefore
// place dependencies before the nodes that consume them.
class DependencyGraph {
public:
    [[nodiscard]] std::expected<void, DependencyGraphError> add_node(std::string key);

    [[nodiscard]] std::expected<void, DependencyGraphError> add_dependency(
        std::string_view node,
        std::string_view dependency);

    [[nodiscard]] bool contains(std::string_view key) const;

    [[nodiscard]] std::size_t size() const noexcept {
        return nodes_.size();
    }

    [[nodiscard]] std::expected<std::vector<std::vector<std::string>>, DependencyGraphError>
    topological_levels() const;

    [[nodiscard]] std::expected<std::vector<std::string>, DependencyGraphError>
    topological_order() const;

private:
    std::unordered_map<std::string, std::size_t> index_by_key_;
    std::vector<std::string> nodes_;
    std::vector<std::vector<std::size_t>> dependencies_;
};

} // namespace mqb
