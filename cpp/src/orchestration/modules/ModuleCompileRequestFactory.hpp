#pragma once

#include <filesystem>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"

namespace mqb::orchestration::detail {

[[nodiscard]] IncrementalCompileRequest make_module_compile_request(
    const ModuleCompileSourceRequest& source,
    const CompilerOptions& options,
    const std::vector<ModuleReference>& module_references,
    const std::vector<HeaderUnitReference>& header_unit_references,
    bool force_rebuild,
    const std::filesystem::path& working_directory);

[[nodiscard]] IncrementalCompileRequest make_header_unit_compile_request(
    const ModuleCompileHeaderUnitRequest& header,
    const CompilerOptions& options,
    bool force_rebuild,
    const std::filesystem::path& working_directory);

} // namespace mqb::orchestration::detail
