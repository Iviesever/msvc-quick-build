#pragma once

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

[[nodiscard]] IncrementalLinkRequest make_module_target_link_request(
    const IncrementalModuleTargetRequest& request,
    const ModuleCompileWaveRequest& compile_request,
    bool force_relink);

} // namespace mqb::orchestration::detail
