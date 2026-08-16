#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"
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
            / ("mqb-archive-orchestration-test-" + std::to_string(tick));
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

class LibrarianLikeRunner final : public mqb::process::ProcessRunner {
public:
    int calls{};
    mqb::process::ProcessSpec last_spec;

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;
        const auto output_argument = std::find_if(
            spec.arguments.begin(),
            spec.arguments.end(),
            [](const std::string& argument) {
                return argument.starts_with("/OUT:");
            });
        if (output_argument == spec.arguments.end()) {
            return mqb::process::ProcessResult{
                .exit_code = 1,
                .stderr_text = "simulated lib.exe invocation had no /OUT argument",
            };
        }

        write_text(fs::path{output_argument->substr(5)}, "fake archive produced by librarian runner");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "simulated lib.exe success",
        };
    }
};

[[nodiscard]] bool has_reason(
    const mqb::ArchiveCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

[[nodiscard]] bool has_warning(
    const mqb::orchestration::IncrementalArchiveResult& result,
    const mqb::orchestration::IncrementalArchiveWarningCode code) {
    return std::any_of(
        result.warnings.begin(),
        result.warnings.end(),
        [code](const mqb::orchestration::IncrementalArchiveWarning& warning) {
            return warning.code == code;
        });
}

[[nodiscard]] bool has_argument(
    const mqb::process::ProcessSpec& spec,
    const std::string_view expected) {
    return std::find(spec.arguments.begin(), spec.arguments.end(), expected)
        != spec.arguments.end();
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path fake_compiler = fixture.path() / "toolchain" / "cl.exe";
    const fs::path fake_linker = fixture.path() / "toolchain" / "link.exe";
    const fs::path fake_librarian = fixture.path() / "toolchain" / "lib.exe";
    const fs::path object = fixture.path() / "obj" / "main.cpp.obj";
    const fs::path output = fixture.path() / "lib" / "main.lib";
    const fs::path cache_file = fixture.path() / "cache" / "main.archivecache";

    write_text(fake_compiler, "fake compiler identity bytes");
    write_text(fake_linker, "fake linker identity bytes");
    write_text(fake_librarian, "fake librarian identity bytes");
    write_text(object, "fake object input");

    const mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = fake_compiler,
            .version = "19.51.test",
            .binary_stamp = "compiler-stamp",
        },
        .linker = fake_linker,
        .librarian = fake_librarian,
        .vc_tools_root = fixture.path() / "toolchain",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    LibrarianLikeRunner runner;
    mqb::msvc::MsvcLibrarian librarian{toolchain, runner};
    mqb::orchestration::MsvcIncrementalArchiveCoordinator coordinator{toolchain, librarian};

    const mqb::orchestration::IncrementalArchiveRequest request{
        .objects = {object},
        .output = output,
        .cache_file = cache_file,
        .working_directory = fixture.path(),
        .architecture = mqb::Architecture::x64,
        .link_time_code_generation = false,
        .force_archive = false,
    };

    const auto cold = coordinator.run(request);
    expect(cold.has_value(), "cold incremental archive should succeed");
    if (cold) {
        expect(cold->archived, "cold archive should execute one librarian action");
        expect(cold->plan.actions.size() == 1,
               "cold archive should retain one planned ArchiveAction");
        expect(has_reason(cold->validation, mqb::BuildReason::missing_cache_entry),
               "cold archive should explain missing archive cache");
        expect(has_reason(cold->validation, mqb::BuildReason::missing_output),
               "cold archive should explain missing static library");
    }
    expect(runner.calls == 1, "cold archive should invoke lib.exe exactly once");
    expect(fs::is_regular_file(output), "cold archive should create static library output");
    expect(fs::is_regular_file(cache_file), "cold archive should persist archive cache metadata");

    const auto warm = coordinator.run(request);
    expect(warm.has_value(), "warm archive validation should succeed");
    if (warm) {
        expect(!warm->archived, "unchanged warm archive should skip lib.exe");
        expect(warm->plan.empty(), "unchanged warm archive should produce an empty plan");
        expect(warm->validation.reusable(), "unchanged warm archive cache should be reusable");
    }
    expect(runner.calls == 1, "warm archive cache hit should not invoke lib.exe again");

    auto native_request = request;
    native_request.output = fixture.path() / "lib" / "native.lib";
    native_request.cache_file = fixture.path() / "cache" / "native.archivecache";
    native_request.additional_arguments = {"/EXPORT:mqb_export", "/WX"};

    const auto native_cold = coordinator.run(native_request);
    expect(native_cold.has_value() && native_cold->archived,
           "cold native librarian policy should execute successfully");
    expect(runner.calls == 2,
           "cold native librarian policy should invoke lib.exe exactly once");
    expect(has_argument(runner.last_spec, "/EXPORT:mqb_export"),
           "Class C librarian /EXPORT should reach the real archive invocation");
    expect(has_argument(runner.last_spec, "/WX"),
           "Class C librarian /WX should reach the real archive invocation");
    expect(has_argument(runner.last_spec, "/MACHINE:X64"),
           "typed archive architecture should reach lib.exe as /MACHINE:X64");

    const auto native_warm = coordinator.run(native_request);
    expect(native_warm.has_value() && !native_warm->archived,
           "unchanged native librarian policy should reuse the archive cache");
    expect(runner.calls == 2,
           "warm native librarian policy should not invoke lib.exe again");

    auto changed_native_request = native_request;
    changed_native_request.additional_arguments = {"/EXPORT:mqb_export", "/WX:NO"};
    const auto native_changed = coordinator.run(changed_native_request);
    expect(native_changed.has_value() && native_changed->archived,
           "changing only librarian argv must invalidate archive freshness");
    if (native_changed) {
        expect(has_reason(native_changed->validation, mqb::BuildReason::archive_recipe_changed),
               "librarian argv mutation should report archive_recipe_changed");
    }
    expect(runner.calls == 3,
           "librarian argv mutation should cause exactly one additional lib.exe invocation");

    auto owned_output_request = native_request;
    owned_output_request.output = fixture.path() / "lib" / "owned-output.lib";
    owned_output_request.cache_file = fixture.path() / "cache" / "owned-output.archivecache";
    owned_output_request.additional_arguments = {"/OUT:hijack.lib"};
    const auto owned_output = coordinator.run(owned_output_request);
    expect(!owned_output
               && owned_output.error().code
                   == mqb::orchestration::IncrementalArchiveErrorCode::librarian_parameter_invalid,
           "Class A /OUT should fail at archive parameter routing before lib.exe");
    expect(runner.calls == 3,
           "rejected Class A librarian policy must not launch lib.exe");

    std::error_code snapshot_error;
    const auto output_time = fs::last_write_time(output, snapshot_error);
    expect(!snapshot_error, "directory regression should read archive output timestamp");
    snapshot_error.clear();
    fs::remove(object, snapshot_error);
    expect(!snapshot_error, "directory regression should remove the regular object file");
    snapshot_error.clear();
    fs::create_directory(object, snapshot_error);
    expect(!snapshot_error, "directory regression should create a directory at the object path");
    snapshot_error.clear();
    fs::last_write_time(object, output_time - std::chrono::hours{1}, snapshot_error);
    expect(!snapshot_error,
           "directory regression should make the directory older than the archive output");

    const auto directory_input = coordinator.run(request);
    expect(directory_input.has_value(),
           "directory object recheck should remain an incremental validation operation");
    if (directory_input) {
        expect(directory_input->archived,
               "a directory at an object path must not be accepted as a reusable archive input");
        expect(has_reason(
                   directory_input->validation,
                   mqb::BuildReason::archive_inputs_changed),
               "directory object should invalidate archive input freshness");
        expect(!has_warning(
                   *directory_input,
                   mqb::orchestration::IncrementalArchiveWarningCode::file_snapshot_failed),
               "non-regular object paths should be ordinary missing snapshots, not I/O warnings");
    }
    expect(runner.calls == 4,
           "directory object should force exactly one conservative rearchive");
    expect(fs::is_regular_file(output),
           "directory object regression should still leave a valid archive output");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_orchestration_incremental_archive_tests passed\n";
    return 0;
}
