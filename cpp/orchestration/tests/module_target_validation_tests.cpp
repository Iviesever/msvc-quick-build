#include <expected>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
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

class FailIfCalledRunner final : public mqb::process::ProcessRunner {
public:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        return mqb::process::ProcessResult{.exit_code = 0};
    }

    int calls{};
};

[[nodiscard]] mqb::orchestration::IncrementalModuleTargetRequest make_request() {
    mqb::orchestration::IncrementalModuleTargetRequest request;
    request.sources = {
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = "C:/project/modules/A.ixx",
            .artifacts = mqb::SourceArtifacts{
                .object = "C:/project/.mqb/obj/A.obj",
                .dependencies = "C:/project/.mqb/deps/A.json",
                .module_dependencies = "C:/project/.mqb/scan/A.json",
                .module_interface = "C:/project/.mqb/ifc/A.ifc",
                .compile_cache = "C:/project/.mqb/cache/A.mqbcache",
            },
            .kind = mqb::TranslationUnitKind::module_interface,
        },
        mqb::orchestration::ModuleCompileSourceRequest{
            .source = "C:/project/src/main.cpp",
            .artifacts = mqb::SourceArtifacts{
                .object = "C:/project/.mqb/obj/main.obj",
                .dependencies = "C:/project/.mqb/deps/main.json",
                .module_dependencies = "C:/project/.mqb/scan/main.json",
                .module_interface = "C:/project/.mqb/ifc/main.ifc",
                .compile_cache = "C:/project/.mqb/cache/main.mqbcache",
            },
            .kind = mqb::TranslationUnitKind::source,
        },
    };
    request.target = mqb::TargetArtifacts{
        .executable = "C:/project/.mqb/bin/app.exe",
        .link_cache = "C:/project/.mqb/cache/link/app.linkcache",
    };
    request.max_parallel_scans = 2;
    request.max_parallel_compiles = 2;
    return request;
}

} // namespace

int main() {
    FailIfCalledRunner runner;
    const mqb::msvc::MsvcToolchain toolchain{
        .identity = mqb::ToolchainIdentity{
            .compiler = "C:/fake/cl.exe",
            .version = "19.51.test",
            .binary_stamp = "fake",
        },
        .linker = "C:/fake/link.exe",
        .librarian = "C:/fake/lib.exe",
        .vc_tools_root = "C:/fake",
        .source = mqb::msvc::ToolchainSource::visual_studio,
    };

    mqb::msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    mqb::msvc::MsvcCompileExecutor executor{toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{
        toolchain,
        executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};
    mqb::msvc::MsvcLinker linker{toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{toolchain, linker};
    mqb::orchestration::MsvcModuleTargetCoordinator target{
        scanner,
        module_compile,
        incremental_link};

    {
        auto request = make_request();
        const fs::path shared_object = request.sources[0].artifacts.object;
        request.sources[1].artifacts.object = shared_object;

        const auto result = target.run(request);
        expect(!result,
               "two sources sharing one object artifact should be rejected");
        if (!result) {
            expect(result.error().code
                       == mqb::orchestration::IncrementalModuleTargetErrorCode::artifact_collision,
                   "shared object path should report artifact_collision");
            expect(result.error().artifact == shared_object,
                   "artifact_collision should identify the conflicting writable path");
        }
        expect(runner.calls == 0,
               "artifact collision must fail before scan, compile, or link process execution");
    }

    {
        auto request = make_request();
        request.sources[0].artifacts.dependencies.clear();

        const auto result = target.run(request);
        expect(!result,
               "empty source-dependency metadata path should be rejected");
        if (!result) {
            expect(result.error().code
                       == mqb::orchestration::IncrementalModuleTargetErrorCode::invalid_artifact,
                   "empty writable artifact should report invalid_artifact");
            expect(result.error().source == request.sources[0].source,
                   "invalid source artifact should preserve source diagnostics");
        }
        expect(runner.calls == 0,
               "invalid artifact must fail before any external process execution");
    }

    {
        auto request = make_request();
        request.target.executable = request.sources[1].artifacts.compile_cache;

        const auto result = target.run(request);
        expect(!result,
               "target executable colliding with compile metadata should be rejected");
        if (!result) {
            expect(result.error().code
                       == mqb::orchestration::IncrementalModuleTargetErrorCode::artifact_collision,
                   "cross-role source/target collision should report artifact_collision");
        }
        expect(runner.calls == 0,
               "cross-role artifact collision must fail before process execution");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_target_validation_tests passed\n";
    return 0;
}
