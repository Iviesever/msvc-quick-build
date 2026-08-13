#pragma once

#include <cstddef>
#include <expected>
#include <vector>

#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"

namespace mqb::orchestration::detail {

struct ModuleCompilePlan {
    std::size_t source_count{};
    std::size_t header_count{};
    std::vector<std::vector<std::size_t>> level_indices;
    std::vector<std::vector<ModuleReference>> module_references;
    std::vector<std::vector<HeaderUnitReference>> header_references;
    std::vector<std::vector<std::size_t>> provider_indices;

    [[nodiscard]] std::size_t node_count() const noexcept {
        return source_count + header_count;
    }
};

[[nodiscard]] std::expected<ModuleCompilePlan, ModuleCompileError>
build_module_compile_plan(const ModuleCompileWaveRequest& request);

} // namespace mqb::orchestration::detail
