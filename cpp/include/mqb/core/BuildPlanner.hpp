#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include "mqb/core/ArchiveCache.hpp"
#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb {

enum class BuildPlannerErrorCode {
    missing_object_output,
    multiple_object_outputs,
    invalid_header_unit_outputs,
    missing_link_input,
    missing_link_output,
    missing_archive_input,
    missing_archive_output,
};

struct BuildPlannerError {
    BuildPlannerErrorCode code{};
    std::filesystem::path source;
    std::size_t object_output_count{};
    std::size_t module_interface_output_count{};
};

struct CompilePlanItem {
    TranslationUnit unit;
    CompileCacheValidation cache_validation;
};

struct LinkPlanItem {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::vector<std::filesystem::path> libraries;
    LinkCacheValidation cache_validation;
};

struct ArchivePlanItem {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    ArchiveCacheValidation cache_validation;
};

class BuildPlanner {
public:
    [[nodiscard]] static std::expected<BuildPlan, BuildPlannerError> plan_compile(
        std::span<const CompilePlanItem> items);

    [[nodiscard]] static std::expected<BuildPlan, BuildPlannerError> plan_link(
        const LinkPlanItem& item);

    [[nodiscard]] static std::expected<BuildPlan, BuildPlannerError> plan_archive(
        const ArchivePlanItem& item);
};

} // namespace mqb
