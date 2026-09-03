#pragma once

#include "BuildIntrospectionSetup.hpp"
#include "PlanOutput.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace mqb::app {

[[nodiscard]] int run_module_plan(
    plan::Format format,
    const BuildIntrospectionContext& context,
    mqb::platform::windows::WindowsProcessRunner& runner);

} // namespace mqb::app
