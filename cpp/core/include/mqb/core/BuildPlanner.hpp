#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb {

enum class BuildPlannerErrorCode {
    missing_object_output,
    multiple_object_outputs,
};

struct BuildPlannerError {
    BuildPlannerErrorCode code{};
    std::filesystem::path source;
    std::size_t object_output_count{};
};

struct CompilePlanItem {
    TranslationUnit unit;
    CompileCacheValidation cache_validation;
};

class BuildPlanner {
public:
    [[nodiscard]] static std::expected<BuildPlan, BuildPlannerError> plan_compile(
        std::span<const CompilePlanItem> items);
};

} // namespace mqb
