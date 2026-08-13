#include "ModuleTargetLinkRequestFactory.hpp"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace mqb::orchestration::detail {

namespace fs = std::filesystem;

IncrementalLinkRequest make_module_target_link_request(
    const IncrementalModuleTargetRequest& request,
    const ModuleCompileWaveRequest& compile_request,
    const bool force_relink) {
    std::vector<fs::path> objects;
    objects.reserve(compile_request.sources.size());
    for (const auto& source : compile_request.sources) {
        objects.push_back(source.artifacts.object);
    }

    return IncrementalLinkRequest{
        .objects = std::move(objects),
        .output = request.target.executable,
        .options = request.link_options,
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory.empty()
            ? std::nullopt
            : std::optional<fs::path>{request.working_directory},
        .force_relink = force_relink,
    };
}

} // namespace mqb::orchestration::detail
