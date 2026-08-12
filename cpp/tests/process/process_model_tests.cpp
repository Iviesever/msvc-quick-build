#include <filesystem>
#include <iostream>
#include <string_view>

#include "mqb/process/Process.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    mqb::process::ProcessSpec spec;
    spec.executable = std::filesystem::path{"toolchain/cl.exe"};
    spec.arguments = {"/c", "/std:c++23", "src/main.cpp", "/Fo:build/main.obj"};
    spec.working_directory = std::filesystem::path{"project"};
    spec.environment = {
        mqb::process::EnvironmentVariable{"INCLUDE", "sdk/include"},
        mqb::process::EnvironmentVariable{"LIB", "sdk/lib"},
    };
    spec.inherit_environment = true;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    expect(spec.executable == std::filesystem::path{"toolchain/cl.exe"},
           "process specification should retain executable separately from argv");
    expect(spec.arguments.size() == 4,
           "process specification should preserve argument boundaries");
    expect(spec.arguments[2] == "src/main.cpp",
           "source path should remain one argv element");
    expect(spec.working_directory == std::filesystem::path{"project"},
           "working directory should remain a structured path");
    expect(spec.environment.size() == 2,
           "environment overrides should remain structured name/value pairs");

    const mqb::process::ProcessResult result{
        .exit_code = 7,
        .stdout_text = "compiler output",
        .stderr_text = "compiler error",
    };
    expect(result.exit_code == 7, "process result should carry the child exit code");
    expect(result.stdout_text == "compiler output",
           "process result should keep stdout separate from stderr");
    expect(result.stderr_text == "compiler error",
           "process result should keep stderr separate from stdout");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_process_model_tests passed\n";
    return 0;
}
