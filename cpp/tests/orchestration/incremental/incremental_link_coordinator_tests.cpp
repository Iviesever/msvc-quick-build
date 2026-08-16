#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
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

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
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

    const fs::path first_order_file = fixture.path() / "order" / "first.txt";
    const fs::path effective_order_file = fixture.path() / "order" / "effective.txt";
    write_text(first_order_file, "first_symbol\n");
    write_text(effective_order_file, "effective_symbol\n");

    const std::vector<std::string> layered_order_arguments{
        "/ORDER:@first.txt",
        "/ORDER:@order/effective.txt",
    };
    const auto order_routing = mqb::msvc::MsvcParameterEngine::linker_file_inputs(
        layered_order_arguments,
        fixture.path());
    expect(order_routing.has_value(), "valid repeated /ORDER options should route");
    if (order_routing) {
        expect(order_routing->requires_full_link,
               "/ORDER routing should expose its non-incremental execution requirement");
        expect(order_routing->inputs.size() == 1
                   && order_routing->inputs.front().kind
                       == mqb::msvc::LinkerFileInputKind::function_order,
               "repeated /ORDER should expose one effective function-order file input");
        if (order_routing->inputs.size() == 1) {
            expect(order_routing->inputs.front().path == effective_order_file.lexically_normal(),
                   "the last /ORDER option should own effective file freshness");
        }
        expect(order_routing->passthrough.size() == 2,
               "last-wins observation must preserve both raw /ORDER argv positions");
    }

    const std::vector<std::string> malformed_order_argument{"/ORDER:order.txt"};
    const auto malformed_order = mqb::msvc::MsvcParameterEngine::linker_file_inputs(
        malformed_order_argument,
        fixture.path());
    expect(!malformed_order
               && malformed_order.error().code == mqb::msvc::ParameterErrorCode::invalid_value,
           "/ORDER without @filename syntax should fail before LINK.exe");
    const std::vector<std::string> empty_order_argument{"/ORDER:@"};
    const auto empty_order = mqb::msvc::MsvcParameterEngine::linker_file_inputs(
        empty_order_argument,
        fixture.path());
    expect(!empty_order
               && empty_order.error().code == mqb::msvc::ParameterErrorCode::invalid_value,
           "empty /ORDER:@ should fail before LINK.exe");

    const fs::path order_output = fixture.path() / "bin" / "ordered.exe";
    const fs::path order_cache = fixture.path() / "cache" / "ordered.linkcache";
    LinkerLikeRunner order_runner;
    order_runner.output = order_output;
    mqb::msvc::MsvcLinker order_linker{toolchain, order_runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator order_coordinator{
        toolchain, order_linker};

    mqb::LinkOptions order_options = debug_options;
    order_options.additional_arguments = {
        "/INCREMENTAL",
        "/ORDER:@" + path_text(effective_order_file),
    };
    const mqb::orchestration::IncrementalLinkRequest order_request{
        .objects = {object},
        .output = order_output,
        .options = order_options,
        .cache_file = order_cache,
        .working_directory = fixture.path(),
        .force_relink = false,
    };

    const auto order_cold = order_coordinator.run(order_request);
    expect(order_cold.has_value() && order_cold->linked,
           "cold /ORDER link should execute successfully");
    expect(order_runner.calls == 1, "cold /ORDER link should invoke link.exe once");
    if (order_runner.calls == 1) {
        const auto raw_incremental = std::find(
            order_runner.last_spec.arguments.begin(),
            order_runner.last_spec.arguments.end(),
            "/INCREMENTAL");
        const auto forced_full = std::find(
            order_runner.last_spec.arguments.begin(),
            order_runner.last_spec.arguments.end(),
            "/INCREMENTAL:NO");
        expect(raw_incremental != order_runner.last_spec.arguments.end()
                   && forced_full != order_runner.last_spec.arguments.end()
                   && raw_incremental < forced_full,
               "active /ORDER must append authoritative /INCREMENTAL:NO after raw /INCREMENTAL");
    }

    const auto order_warm = order_coordinator.run(order_request);
    expect(order_warm.has_value() && !order_warm->linked,
           "unchanged /ORDER link should still use the warm no-op fast path");
    expect(order_runner.calls == 1,
           "warm /ORDER cache hit should skip link.exe entirely");

    auto order_forced_request = order_request;
    order_forced_request.force_relink = true;
    const auto order_forced = order_coordinator.run(order_forced_request);
    expect(order_forced.has_value() && order_forced->linked,
           "an actual relink with unchanged /ORDER input should succeed");
    expect(order_runner.calls == 2,
           "explicit /ORDER relink should invoke link.exe exactly once more");
    if (order_runner.calls == 2) {
        expect(std::find(
                   order_runner.last_spec.arguments.begin(),
                   order_runner.last_spec.arguments.end(),
                   "/INCREMENTAL:NO") != order_runner.last_spec.arguments.end(),
               "unchanged /ORDER input must still force non-incremental LINK execution");
    }

    std::error_code order_time_error;
    const auto order_output_time = fs::last_write_time(order_output, order_time_error);
    expect(!order_time_error, "order-file fixture should read linked output timestamp");
    write_text(effective_order_file, "effective_symbol_changed\n");
    order_time_error.clear();
    fs::last_write_time(
        effective_order_file,
        order_output_time + std::chrono::seconds{2},
        order_time_error);
    expect(!order_time_error,
           "order-file fixture should make the effective order file newer than output");

    const auto order_changed = order_coordinator.run(order_request);
    expect(order_changed.has_value() && order_changed->linked,
           "changed effective /ORDER file should invalidate link freshness");
    if (order_changed) {
        expect(order_changed->validation.file_inputs_changed,
               "changed /ORDER file should be classified as generic linker file-input change");
    }
    expect(order_runner.calls == 3,
           "changed /ORDER file should cause exactly one additional link execution");

    const fs::path base_lib_directory = fixture.path() / "base-lib";
    const fs::path base_file = base_lib_directory / "bases.txt";
    const fs::path base_output = fixture.path() / "bin" / "based.exe";
    const fs::path base_cache = fixture.path() / "cache" / "based.linkcache";
    write_text(base_file, "app 0x160000000\n");

    auto base_toolchain = toolchain;
    base_toolchain.environment.push_back({
        .name = "LIB",
        .value = path_text(base_lib_directory),
    });
    LinkerLikeRunner base_runner;
    base_runner.output = base_output;
    mqb::msvc::MsvcLinker base_linker{base_toolchain, base_runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator base_coordinator{
        base_toolchain, base_linker};

    mqb::LinkOptions base_options = debug_options;
    base_options.additional_arguments = {"/BASE:@bases.txt,app"};
    const mqb::orchestration::IncrementalLinkRequest base_request{
        .objects = {object},
        .output = base_output,
        .options = base_options,
        .cache_file = base_cache,
        .working_directory = fixture.path(),
        .force_relink = false,
    };

    const auto base_cold = base_coordinator.run(base_request);
    expect(base_cold.has_value() && base_cold->linked,
           "cold /BASE response-file link should execute successfully");
    expect(base_runner.calls == 1,
           "cold /BASE response-file link should invoke link.exe once");
    if (base_runner.calls == 1) {
        expect(std::find(
                   base_runner.last_spec.arguments.begin(),
                   base_runner.last_spec.arguments.end(),
                   "/BASE:@bases.txt,app") != base_runner.last_spec.arguments.end(),
               "/BASE freshness observation must preserve the user's raw LINK argv");
    }

    const auto base_warm = base_coordinator.run(base_request);
    expect(base_warm.has_value() && !base_warm->linked,
           "unchanged /BASE response file should remain a warm no-op");
    expect(base_runner.calls == 1,
           "warm /BASE response-file link should skip link.exe");

    std::error_code base_time_error;
    const auto base_output_time = fs::last_write_time(base_output, base_time_error);
    expect(!base_time_error,
           "BASE response-file fixture should read linked output timestamp");
    write_text(base_file, "app 0x170000000\n");
    base_time_error.clear();
    fs::last_write_time(
        base_file,
        base_output_time + std::chrono::seconds{2},
        base_time_error);
    expect(!base_time_error,
           "BASE response-file fixture should make the response file newer than output");

    const auto base_changed = base_coordinator.run(base_request);
    expect(base_changed.has_value() && base_changed->linked,
           "changed /BASE response file should invalidate link freshness");
    if (base_changed) {
        expect(base_changed->validation.file_inputs_changed,
               "changed /BASE response file should be a tracked linker file-input change");
    }
    expect(base_runner.calls == 2,
           "changed /BASE response file should cause exactly one additional link execution");

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

    const fs::path directory_object = fixture.path() / "obj" / "directory-input.obj";
    const fs::path directory_output = fixture.path() / "bin" / "directory-input.exe";
    const fs::path directory_cache = fixture.path() / "cache" / "directory-input.linkcache";
    write_text(directory_object, "regular object before directory regression");

    LinkerLikeRunner directory_runner;
    directory_runner.output = directory_output;
    mqb::msvc::MsvcLinker directory_linker{toolchain, directory_runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator directory_coordinator{
        toolchain, directory_linker};
    const mqb::orchestration::IncrementalLinkRequest directory_request{
        .objects = {directory_object},
        .output = directory_output,
        .options = debug_options,
        .cache_file = directory_cache,
        .working_directory = fixture.path(),
        .force_relink = false,
    };

    const auto directory_cold = directory_coordinator.run(directory_request);
    expect(directory_cold.has_value() && directory_cold->linked,
           "directory regression fixture should create a warm link cache");
    const auto directory_warm = directory_coordinator.run(directory_request);
    expect(directory_warm.has_value() && !directory_warm->linked,
           "directory regression fixture should begin as a reusable warm link");
    expect(directory_runner.calls == 1,
           "directory regression warm-up should invoke link.exe only for the cold build");

    std::error_code directory_error;
    const auto directory_output_time = fs::last_write_time(directory_output, directory_error);
    expect(!directory_error,
           "directory regression should read the existing output timestamp");
    directory_error.clear();
    fs::remove(directory_object, directory_error);
    expect(!directory_error,
           "directory regression should replace the object file");
    directory_error.clear();
    fs::create_directory(directory_object, directory_error);
    expect(!directory_error,
           "directory regression should create a directory at the object path");
    directory_error.clear();
    fs::last_write_time(
        directory_object,
        directory_output_time - std::chrono::hours{1},
        directory_error);
    expect(!directory_error,
           "directory regression should make the directory older than the linked output");

    const auto directory_recheck = directory_coordinator.run(directory_request);
    expect(directory_recheck.has_value(),
           "directory object recheck should remain an incremental validation operation");
    if (directory_recheck) {
        expect(directory_recheck->linked,
               "a directory at an object path must not be accepted as a reusable file input");
        expect(has_reason(
                   directory_recheck->validation,
                   mqb::BuildReason::link_inputs_changed),
               "directory object should invalidate link input freshness");
        expect(!has_warning(
                   *directory_recheck,
                   mqb::orchestration::IncrementalLinkWarningCode::file_snapshot_failed),
               "non-regular object paths should be ordinary missing snapshots, not I/O warnings");
    }
    expect(directory_runner.calls == 2,
           "directory object should force exactly one conservative relink");

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
