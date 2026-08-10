#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
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

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path()
            / ("mqb-link-orchestration-test-" + std::to_string(tick));
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

void write_text(const fs::path& path, const std::string_view text) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

class LinkerLikeRunner final : public mqb::process::ProcessRunner {
public:
    fs::path output;
    int calls{};
    bool fail_link{false};
    mqb::process::ProcessSpec last_spec;

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;
        if (fail_link) {
            return mqb::process::ProcessResult{
                .exit_code = 1120,
                .stderr_text = "simulated link.exe diagnostic",
            };
        }
        write_text(output, "fake executable produced by linker runner");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "simulated link.exe success",
        };
    }
};

[[nodiscard]] bool has_reason(
    const mqb::LinkCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

[[nodiscard]] bool has_warning(
    const mqb::orchestration::IncrementalLinkResult& result,
    const mqb::orchestration::IncrementalLinkWarningCode code) {
    return std::any_of(
        result.warnings.begin(),
        result.warnings.end(),
        [code](const mqb::orchestration::IncrementalLinkWarning& warning) {
            return warning.code == code;
        });
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path fake_linker = fixture.path() / "toolchain" / "link.exe";
    const fs::path fake_compiler = fixture.path() / "toolchain" / "cl.exe";
    const fs::path object = fixture.path() / "obj" / "main.cpp.obj";
    const fs::path output = fixture.path() / "bin" / "main.exe";
    const fs::path cache_file = fixture.path() / "cache" / "main.linkcache";

    write_text(fake_linker, "fake linker identity bytes");
    write_text(fake_compiler, "fake compiler identity bytes");
    write_text(object, "fake object input");

    mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = fake_compiler,
            .version = "19.51.test",
            .binary_stamp = "compiler-stamp",
        },
        .linker = fake_linker,
        .librarian = fixture.path() / "toolchain" / "lib.exe",
        .vc_tools_root = fixture.path() / "toolchain",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    LinkerLikeRunner runner;
    runner.output = output;
    mqb::msvc::MsvcLinker linker{toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator coordinator{toolchain, linker};

    mqb::LinkOptions debug_options;
    debug_options.configuration = mqb::BuildConfiguration::debug;
    debug_options.architecture = mqb::Architecture::x64;
    debug_options.subsystem = mqb::LinkSubsystem::console;

    const mqb::orchestration::IncrementalLinkRequest request{
        .objects = {object},
        .output = output,
        .options = debug_options,
        .cache_file = cache_file,
        .working_directory = fixture.path(),
        .force_relink = false,
    };

    const auto cold = coordinator.run(request);
    expect(cold.has_value(), "cold incremental link should succeed");
    if (cold) {
        expect(cold->linked, "cold link should execute one link action");
        expect(cold->plan.actions.size() == 1,
               "cold link should retain one planned LinkAction");
        expect(has_reason(cold->validation, mqb::BuildReason::missing_cache_entry),
               "cold link should explain missing link cache");
        expect(has_reason(cold->validation, mqb::BuildReason::missing_output),
               "cold link should explain missing executable");
        expect(cold->warnings.empty(),
               "ordinary missing link cache should not be a warning");
    }
    expect(runner.calls == 1, "cold link should invoke link.exe exactly once");
    expect(fs::is_regular_file(output), "cold link should create executable output");
    expect(fs::is_regular_file(cache_file), "cold link should persist link cache metadata");

    const auto warm = coordinator.run(request);
    expect(warm.has_value(), "warm link validation should succeed");
    if (warm) {
        expect(!warm->linked, "unchanged warm link should skip link.exe");
        expect(warm->plan.empty(), "unchanged warm link should produce an empty plan");
        expect(warm->validation.reusable(), "unchanged warm link cache should be reusable");
    }
    expect(runner.calls == 1, "warm link cache hit should not invoke link.exe again");

    auto forced_request = request;
    forced_request.force_relink = true;
    const auto forced = coordinator.run(forced_request);
    expect(forced.has_value(), "forced relink should succeed");
    if (forced) {
        expect(forced->linked, "fresh compile signal should force a relink");
        expect(has_reason(forced->validation, mqb::BuildReason::explicit_rebuild),
               "forced relink should remain explainable as explicit rebuild");
    }
    expect(runner.calls == 2,
           "force_relink should invoke link.exe even when timestamps otherwise match");

    auto release_request = request;
    release_request.options.configuration = mqb::BuildConfiguration::release;
    const auto release = coordinator.run(release_request);
    expect(release.has_value(), "link options transition should succeed");
    if (release) {
        expect(release->linked, "link options transition should execute link.exe");
        expect(has_reason(release->validation, mqb::BuildReason::linker_options_changed),
               "Debug to Release should report linker options changed");
    }
    expect(runner.calls == 3, "link options transition should invoke link.exe once");

    write_text(cache_file, "not an MQB link cache");
    const auto corrupt = coordinator.run(request);
    expect(corrupt.has_value(),
           "corrupt link cache should degrade to relink instead of failing the build");
    if (corrupt) {
        expect(corrupt->linked, "corrupt link cache should conservatively relink");
        expect(has_reason(corrupt->validation, mqb::BuildReason::missing_cache_entry),
               "corrupt link cache should be treated as unavailable metadata");
        expect(has_warning(
                   *corrupt,
                   mqb::orchestration::IncrementalLinkWarningCode::cache_load_failed),
               "corrupt link cache should remain visible as a warning");
    }
    expect(runner.calls == 4, "corrupt metadata should invoke link.exe exactly once more");

    std::error_code ignored;
    fs::remove(cache_file, ignored);
    fs::remove(output, ignored);
    runner.fail_link = true;
    const auto failed = coordinator.run(request);
    expect(!failed.has_value(), "linker failure should fail the incremental link operation");
    if (!failed) {
        expect(failed.error().code == mqb::orchestration::IncrementalLinkErrorCode::link_failed,
               "link.exe failure should be reported as link_failed");
        expect(failed.error().linker_error.has_value(),
               "link.exe diagnostics should remain attached to orchestration error");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_orchestration_incremental_link_tests passed\n";
    return 0;
}
