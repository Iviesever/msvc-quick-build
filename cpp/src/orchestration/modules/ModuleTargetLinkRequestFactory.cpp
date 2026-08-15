#include "ModuleTargetLinkRequestFactory.hpp"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"

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

    LinkOptions effective_link_options = request.link_options;
    msvc::MsvcAddressSanitizerPolicy::apply_link_policy(
        request.compiler_options,
        effective_link_options);

    return IncrementalLinkRequest{
        .objects = std::move(objects),
        .output = request.target.executable,
        .options = std::move(effective_link_options),
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory.empty()
            ? std::nullopt
            : std::optional<fs::path>{request.working_directory},
        .force_relink = force_relink,
    };
}

} // namespace mqb::orchestration::detail
