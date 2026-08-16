#include <barrier>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(
    const std::string& text,
    const std::string_view fragment) {
    return text.find(fragment) != std::string::npos;
}

} // namespace

int main(const int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mqb_windows_process_runner_tests <helper.exe>\n";
        return 2;
    }

    const std::filesystem::path helper = std::filesystem::absolute(argv[1]);
    mqb::platform::windows::WindowsProcessRunner runner;

    mqb::process::ProcessSpec spec;
    spec.executable = helper;
    spec.arguments = {
        "alpha",
        "two words",
        "quote\"inside",
        "",
        "ends-with-backslash\\",
    };
    spec.working_directory = helper.parent_path();
    spec.environment = {
        mqb::process::EnvironmentVariable{"MQB_TEST_ENV", "hello world"},
    };

    const auto result = runner.run(spec);
    expect(result.has_value(), "valid process specification should launch successfully");
    if (result) {
        expect(result->exit_code == 23,
               "non-zero child exit code should be returned as data, not a launch error");
        expect(contains(result->stdout_text, "ARGC=5"),
               "child should receive every structured argv element");
        expect(contains(result->stdout_text, "ARG=[alpha]"),
               "simple argument should reach the child unchanged");
        expect(contains(result->stdout_text, "ARG=[two words]"),
               "argument containing spaces should reach the child unchanged");
        expect(contains(result->stdout_text, "ARG=[quote\"inside]"),
               "argument containing a quote should reach the child unchanged");
        expect(contains(result->stdout_text, "ARG=[]"),
               "empty argument should survive process creation");
        expect(contains(result->stdout_text, "ARG=[ends-with-backslash\\]"),
               "trailing backslash should survive process creation");
        expect(contains(result->stdout_text, "ENV=[hello world]"),
               "environment override should reach the child");
        expect(contains(result->stderr_text, "STDERR-MARKER"),
               "stderr should be captured separately from stdout");
    }

    expect(_putenv_s("MQB_TEST_ENV", "parent-only") == 0,
           "test should be able to seed an inherited parent environment variable");
    mqb::process::ProcessSpec removed_environment = spec;
    removed_environment.arguments = {"removed"};
    removed_environment.environment = {
        mqb::process::EnvironmentVariable{"MQB_TEST_ENV", {}, true},
    };
    const auto removed_result = runner.run(removed_environment);
    expect(removed_result.has_value(),
           "process should launch when an inherited environment variable is removed");
    if (removed_result) {
        expect(contains(removed_result->stdout_text, "ENV=[<missing>]"),
               "remove=true must make an inherited variable absent, not merely empty");
    }
    expect(_putenv_s("MQB_TEST_ENV", "") == 0,
           "test should clean up the seeded parent environment variable");

    mqb::process::ProcessSpec isolated_environment = spec;
    isolated_environment.arguments = {"isolated"};
    isolated_environment.inherit_environment = false;
    isolated_environment.environment = {
        mqb::process::EnvironmentVariable{"MQB_TEST_ENV", "isolated-value"},
    };
    const auto isolated_result = runner.run(isolated_environment);
    expect(isolated_result.has_value(),
           "process should launch with an explicit non-inherited environment block");
    if (isolated_result) {
        expect(contains(isolated_result->stdout_text, "ENV=[isolated-value]"),
               "explicit environment block should retain requested variables");
    }

    mqb::process::ProcessSpec flood;
    flood.executable = helper;
    flood.arguments = {"--flood"};
    const auto flood_result = runner.run(flood);
    expect(flood_result.has_value(),
           "runner should drain stdout and stderr concurrently without pipe deadlock");
    if (flood_result) {
        constexpr std::size_t payload_size = 256 * 1024;
        expect(flood_result->exit_code == 17,
               "flood helper exit code should be preserved");
        expect(flood_result->stdout_text.size() == payload_size,
               "large stdout payload should be captured completely");
        expect(flood_result->stderr_text.size() == payload_size,
               "large stderr payload should be captured completely");
    }

    {
        constexpr int launch_count = 12;
        std::barrier start_gate{launch_count};
        std::vector<std::future<std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>>>
            launches;
        launches.reserve(launch_count);

        for (int index = 0; index < launch_count; ++index) {
            launches.push_back(std::async(
                std::launch::async,
                [&runner, &start_gate, helper, index] {
                    mqb::process::ProcessSpec concurrent;
                    concurrent.executable = helper;
                    concurrent.arguments = {"concurrent-" + std::to_string(index)};
                    concurrent.environment = {
                        mqb::process::EnvironmentVariable{
                            "MQB_TEST_ENV",
                            "env-" + std::to_string(index)},
                    };
                    start_gate.arrive_and_wait();
                    return runner.run(concurrent);
                }));
        }

        for (int index = 0; index < launch_count; ++index) {
            auto concurrent = launches[static_cast<std::size_t>(index)].get();
            expect(concurrent.has_value(),
                   "same WindowsProcessRunner instance should support concurrent launches");
            if (!concurrent) {
                continue;
            }
            expect(concurrent->exit_code == 23,
                   "concurrent child exit code should remain isolated");
            expect(contains(
                       concurrent->stdout_text,
                       "ARG=[concurrent-" + std::to_string(index) + "]"),
                   "concurrent stdout capture should belong to the correct child");
            expect(contains(
                       concurrent->stdout_text,
                       "ENV=[env-" + std::to_string(index) + "]"),
                   "concurrent environment blocks should remain isolated");
            expect(contains(concurrent->stderr_text, "STDERR-MARKER"),
                   "concurrent stderr capture should complete independently");
        }
    }

    mqb::process::ProcessSpec empty_executable;
    const auto empty_result = runner.run(empty_executable);
    expect(!empty_result.has_value(), "empty executable should be rejected before launch");
    if (!empty_result) {
        expect(empty_result.error().code == mqb::process::ProcessErrorCode::invalid_specification,
               "empty executable should report invalid_specification");
    }

    mqb::process::ProcessSpec invalid_environment;
    invalid_environment.executable = helper;
    invalid_environment.environment = {
        mqb::process::EnvironmentVariable{"BAD=NAME", "value"},
    };
    const auto invalid_environment_result = runner.run(invalid_environment);
    expect(!invalid_environment_result.has_value(),
           "invalid environment variable name should be rejected");
    if (!invalid_environment_result) {
        expect(invalid_environment_result.error().code
                   == mqb::process::ProcessErrorCode::invalid_specification,
               "invalid environment name should report invalid_specification");
    }

    mqb::process::ProcessSpec missing_executable;
    missing_executable.executable = helper.parent_path() / "definitely-missing-mqb-helper.exe";
    const auto missing_result = runner.run(missing_executable);
    expect(!missing_result.has_value(), "missing executable should fail at launch");
    if (!missing_result) {
        expect(missing_result.error().code == mqb::process::ProcessErrorCode::launch_failed,
               "missing executable should report launch_failed");
        expect(missing_result.error().native_code != 0,
               "launch failure should preserve the Windows error code");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_windows_process_runner_tests passed\n";
    return 0;
}