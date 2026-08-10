#pragma once

#include <cstddef>
#include <vector>

#include "mqb/core/BuildAction.hpp"

namespace mqb {

struct BuildPlan {
    std::vector<BuildAction> actions;

    [[nodiscard]] bool empty() const noexcept {
        return actions.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return actions.size();
    }
};

} // namespace mqb
