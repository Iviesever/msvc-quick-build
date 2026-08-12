#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class UnexpectedRunner final : public mqb::process::ProcessRunner {
public:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        return std::unexpected(mqb::process::ProcessError{
            .code = mqb::process::ProcessErrorCode::launch_failed,
            .message = "portable discovery must not launch a process",
        });
    }

    int calls{};
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mqb-portable-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    file << "fixture";
}

void make_sdk_version(const fs::path& root, const std::string_view version) {
    const std::string version_text{version};
    fs::create_directories(root / "Windows Kits" / "10" / "Include" / version_text);
    fs::create_directories(root / "Windows Kits" / "10" / "Lib" / version_text);
    fs::create_directories(root / "Windows Kits" / "10" / "bin" / version_text);
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path portable = fixture.path() / "portable_msvc";

    fs::create_directories(portable / "VC" / "Tools" / "MSVC" / "14.40.10000");
    const fs::path latest_vc = portable / "VC" / "Tools" / "MSVC" / "14.50.20000";
    const fs::path tool_bin = latest_vc / "bin" / "Hostx64" / "x64";
    touch(tool_bin / "cl.exe");
    touch(tool_bin / "link.exe");
    touch(tool_bin / "lib.exe");

    make_sdk_version(portable, "10.0.20000.0");
    make_sdk_version(portable, "10.0.30000.0");

    UnexpectedRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};

    mqb::msvc::DiscoveryOptions options;
    options.preference = mqb::msvc::ToolchainPreference::automatic;
    options.target_architecture = mqb::Architecture::x64;
    options.host_architecture = mqb::Architecture::x64;
    options.portable_roots = {portable};

    const auto result = locator.discover(options);
    expect(result.has_value(), "automatic discovery should select an existing portable toolchain first");
    expect(runner.calls == 0, "portable discovery should not invoke vswhere or cmd.exe");
    if (result) {
        expect(result->source == mqb::msvc::ToolchainSource::portable,
               "portable discovery should retain toolchain provenance");
        expect(result->identity.compiler == tool_bin / "cl.exe",
               "portable discovery should resolve the requested host/target compiler");
        expect(result->linker == tool_bin / "link.exe",
               "portable discovery should resolve link.exe beside cl.exe");
        expect(result->librarian == tool_bin / "lib.exe",
               "portable discovery should resolve lib.exe beside cl.exe");
        expect(result->identity.version == "14.50.20000",
               "portable discovery should select the lexicographically latest VC tools version");
        expect(!result->identity.binary_stamp.empty(),
               "compiler identity should include a binary stamp");
        expect(result->environment.size() == 3,
               "portable discovery should provide PATH/INCLUDE/LIB overrides");
        expect(result->environment[0].name == "PATH",
               "portable environment should expose PATH first");
        expect(result->environment[0].value.find("10.0.30000.0") != std::string::npos,
               "portable PATH should use the latest Windows Kit version");
    }

    TemporaryDirectory broken_fixture;
    const fs::path broken = broken_fixture.path() / "portable_msvc";
    fs::create_directories(broken);

    mqb::msvc::DiscoveryOptions broken_options;
    broken_options.preference = mqb::msvc::ToolchainPreference::portable;
    broken_options.portable_roots = {broken};
    const auto broken_result = locator.discover(broken_options);
    expect(!broken_result.has_value(), "invalid portable layout should fail instead of falling through to VS");
    if (!broken_result) {
        expect(broken_result.error().code == mqb::msvc::ToolchainErrorCode::invalid_portable_layout,
               "invalid portable layout should report a precise error code");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_portable_tests passed\n";
    return 0;
}
