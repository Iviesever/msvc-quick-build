#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mqb::process {

struct EnvironmentVariable {
    std::string name;
    std::string value;
};

struct ProcessSpec {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::optional<std::filesystem::path> working_directory;
    std::vector<EnvironmentVariable> environment;
    bool inherit_environment{true};
    bool capture_stdout{true};
    bool capture_stderr{true};
};

struct ProcessResult {
    int exit_code{};
    std::string stdout_text;
    std::string stderr_text;
    std::chrono::nanoseconds launch_duration{};
};

enum class ProcessErrorCode {
    invalid_specification,
    launch_failed,
    wait_failed,
    io_failed,
};

struct ProcessError {
    ProcessErrorCode code{ProcessErrorCode::launch_failed};
    std::uint32_t native_code{};
    std::string message;
};

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;

    // Orchestration may call run() concurrently on the same runner instance.
    // Implementations must keep per-launch state isolated.
    [[nodiscard]] virtual std::expected<ProcessResult, ProcessError>
    run(const ProcessSpec& spec) = 0;
};

} // namespace mqb::process
