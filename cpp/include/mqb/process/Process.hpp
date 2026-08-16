#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mqb::process {

struct EnvironmentVariable {
    std::string name;
    std::string value;
    // Removal is distinct from assigning an empty string on Windows: some
    // MSVC tools observe presence itself (for example LINK_REPRO).
    bool remove{false};
};

struct ProcessSpec {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::vector<EnvironmentVariable> environment;
    bool capture_stdout{false};
    bool capture_stderr{false};
};

struct ProcessResult {
    int exit_code{};
    std::string stdout_text;
    std::string stderr_text;
    std::chrono::nanoseconds launch_duration{};
};

enum class ProcessErrorCode {
    executable_not_found,
    working_directory_not_found,
    invalid_environment,
    pipe_creation_failed,
    process_creation_failed,
    wait_failed,
    read_failed,
};

struct ProcessError {
    ProcessErrorCode code{ProcessErrorCode::process_creation_failed};
    std::string message;
    std::uint32_t native_code{};
};

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;

    [[nodiscard]] virtual std::expected<ProcessResult, ProcessError>
    run(const ProcessSpec& spec) = 0;
};

[[nodiscard]] std::string format_command_for_display(
    const std::filesystem::path& executable,
    std::span<const std::string> arguments);

} // namespace mqb::process
