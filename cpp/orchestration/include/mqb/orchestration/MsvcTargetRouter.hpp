#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration {

enum class MsvcTargetPipeline {
    ordinary,
    named_modules,
};

struct RoutedTargetSourceRequest {
    std::filesystem::path source;
    SourceArtifacts artifacts;
    TranslationUnitKind kind{TranslationUnitKind::source};
};

struct RoutedTargetRequest {
    std::vector<RoutedTargetSourceRequest> sources;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    LinkOptions link_options;
    std::filesystem::path working_directory;
    std::size_t max_parallel_jobs{1};
    // Execution-routing state only. This is used when discovery observed named
    // module syntax but found no local interface provider; it must not affect
    // compile/link signatures or cache identity.
    bool force_named_modules{false};
};

enum class RoutedTargetErrorCode {
    ordinary_target_failed,
    module_target_failed,
};

struct RoutedTargetError {
    RoutedTargetErrorCode code{RoutedTargetErrorCode::ordinary_target_failed};
    std::string message;
    std::optional<IncrementalTargetError> ordinary_error;
    std::optional<IncrementalModuleTargetError> module_error;
};

struct RoutedTargetResult {
    MsvcTargetPipeline pipeline{MsvcTargetPipeline::ordinary};
    std::vector<TargetCompileResult> compiles;
    IncrementalLinkResult link;
    bool any_compiled{false};
};

class MsvcTargetRouter {
public:
    MsvcTargetRouter(
        MsvcIncrementalTargetCoordinator& ordinary_target,
        MsvcModuleTargetCoordinator& module_target)
        : ordinary_target_(ordinary_target), module_target_(module_target) {}

    [[nodiscard]] static MsvcTargetPipeline select_pipeline(
        std::span<const RoutedTargetSourceRequest> sources,
        bool force_named_modules = false) noexcept;

    [[nodiscard]] std::expected<RoutedTargetResult, RoutedTargetError>
    run(const RoutedTargetRequest& request) const;

private:
    MsvcIncrementalTargetCoordinator& ordinary_target_;
    MsvcModuleTargetCoordinator& module_target_;
};

} // namespace mqb::orchestration
