#pragma once

#include "mqb/process/Process.hpp"

namespace mqb::platform::windows {

class WindowsProcessRunner final : public process::ProcessRunner {
public:
    [[nodiscard]] std::expected<process::ProcessResult, process::ProcessError>
    run(const process::ProcessSpec& spec) override;
};

} // namespace mqb::platform::windows
