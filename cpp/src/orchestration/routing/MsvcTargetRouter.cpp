#include "mqb/orchestration/MsvcTargetRouter.hpp"

#include <algorithm>
#include <expected>
#include <span>
#include <utility>
#include <vector>

namespace mqb::orchestration {

MsvcTargetPipeline MsvcTargetRouter::select_pipeline(
    const std::span<const RoutedTargetSourceRequest> sources,
    const bool force_named_modules) noexcept {
    if (force_named_modules) {
        return MsvcTargetPipeline::named_modules;
    }
    const bool has_module_interface = std::any_of(
        sources.begin(),
        sources.end(),
        [](const RoutedTargetSourceRequest& source) {
            return source.kind == TranslationUnitKind::module_interface;
        });
    return has_module_interface
        ? MsvcTargetPipeline::named_modules
        : MsvcTargetPipeline::ordinary;
}

std::expected<RoutedTargetResult, RoutedTargetError>
MsvcTargetRouter::run(const RoutedTargetRequest& request) const {
    const MsvcTargetPipeline pipeline = select_pipeline(
        request.sources,
        request.force_named_modules);

    if (pipeline == MsvcTargetPipeline::ordinary) {
        std::vector<TargetSourceRequest> sources;
        sources.reserve(request.sources.size());
        for (const auto& source : request.sources) {
            sources.push_back(TargetSourceRequest{
                .source = source.source,
                .artifacts = source.artifacts,
            });
        }

        IncrementalTargetRequest ordinary_request{
            .sources = std::move(sources),
            .target = request.target,
            .compiler_options = request.compiler_options,
            .link_options = request.link_options,
            .working_directory = request.working_directory,
            .max_parallel_compiles = request.max_parallel_jobs,
        };
        auto result = ordinary_target_.run(ordinary_request);
        if (!result) {
            return std::unexpected(RoutedTargetError{
                .code = RoutedTargetErrorCode::ordinary_target_failed,
                .message = "ordinary MSVC target build failed",
                .ordinary_error = result.error(),
            });
        }

        RoutedTargetResult routed{
            .pipeline = MsvcTargetPipeline::ordinary,
            .compiles = std::move(result->compiles),
            .link = std::move(result->link),
            .timings = result->timings,
            .any_compiled = result->any_compiled,
        };
        for (const auto& compile : routed.compiles) {
            if (compile.result.compiled) {
                ++routed.compile_misses;
            } else {
                ++routed.compile_hits;
            }
        }
        return routed;
    }

    std::vector<ModuleCompileSourceRequest> sources;
    sources.reserve(request.sources.size());
    for (const auto& source : request.sources) {
        sources.push_back(ModuleCompileSourceRequest{
            .source = source.source,
            .artifacts = source.artifacts,
            .kind = source.kind,
        });
    }

    IncrementalModuleTargetRequest module_request{
        .sources = std::move(sources),
        .target = request.target,
        .artifact_layout = request.artifact_layout,
        .compiler_options = request.compiler_options,
        .link_options = request.link_options,
        .working_directory = request.working_directory,
        .max_parallel_scans = request.max_parallel_jobs,
        .max_parallel_compiles = request.max_parallel_jobs,
    };
    auto result = module_target_.run(module_request);
    if (!result) {
        return std::unexpected(RoutedTargetError{
            .code = RoutedTargetErrorCode::module_target_failed,
            .message = "named-module MSVC target build failed",
            .module_error = result.error(),
        });
    }

    RoutedTargetResult routed;
    routed.pipeline = MsvcTargetPipeline::named_modules;
    routed.any_compiled = result->compiles.any_compiled;
    routed.link = std::move(result->link);
    routed.timings = result->timings;
    for (const auto& compile : result->compiles.compiles) {
        if (compile.result.compiled) {
            ++routed.compile_misses;
        } else {
            ++routed.compile_hits;
        }
    }

    // Module-target execution may append toolchain-owned std/std.compat sources
    // after the caller's source prefix. Keep public compile ordering scoped to
    // the original target while still letting provider rebuilds affect
    // any_compiled, cache counters, and downstream link freshness.
    const std::size_t public_source_count = std::min(
        request.sources.size(),
        result->compiles.compiles.size());
    routed.compiles.reserve(public_source_count);
    for (std::size_t index = 0; index < public_source_count; ++index) {
        auto& compile = result->compiles.compiles[index];
        routed.compiles.push_back(TargetCompileResult{
            .source = std::move(compile.source),
            .result = std::move(compile.result),
        });
    }
    return routed;
}

} // namespace mqb::orchestration
