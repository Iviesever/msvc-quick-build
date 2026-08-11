#include "StaticCliTarget.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

namespace mqb::cli {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void write_forwarded_text(std::ostream& stream, const std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            stream.put('\n');
            ++index;
        } else {
            stream.put(ch);
        }
    }
}

void print_process_output(const process::ProcessResult& result) {
    if (!result.stdout_text.empty()) {
        write_forwarded_text(std::cout, result.stdout_text);
        if (result.stdout_text.back() != '\n') std::cout << '\n';
    }
    if (!result.stderr_text.empty()) {
        write_forwarded_text(std::cerr, result.stderr_text);
        if (result.stderr_text.back() != '\n') std::cerr << '\n';
    }
}

void print_reasons(const std::vector<BuildReason>& reasons) {
    if (reasons.empty()) return;
    std::cout << " [";
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0) std::cout << ", ";
        std::cout << to_string(reasons[index]);
    }
    std::cout << ']';
}

} // namespace

int run_static_target(
    StaticCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner) {
    if (request.verbose) {
        std::cout << "[target] " << request.target_name << '\n'
                  << "  project: " << path_text(request.project_root) << '\n'
                  << "  type:    static\n"
                  << "  cl:      " << path_text(toolchain.identity.compiler) << '\n'
                  << "  lib:     " << path_text(toolchain.librarian) << '\n'
                  << "  output:  " << path_text(request.target.executable) << '\n'
                  << "  cache:   " << path_text(request.target.link_cache) << '\n';
    }

    msvc::MsvcCompileExecutor compile_executor{toolchain, runner};
    orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{toolchain, compile_executor};
    msvc::MsvcLibrarian librarian{toolchain, runner};
    orchestration::MsvcIncrementalArchiveCoordinator archive_coordinator{toolchain, librarian};
    orchestration::MsvcIncrementalStaticTargetCoordinator target_coordinator{
        compile_coordinator, archive_coordinator};

    auto result = target_coordinator.run(orchestration::IncrementalStaticTargetRequest{
        .sources = std::move(request.sources),
        .target = request.target,
        .compiler_options = std::move(request.compiler_options),
        .working_directory = request.project_root,
        .max_parallel_compiles = request.max_parallel_jobs,
    });
    if (!result) {
        std::cerr << "error: " << result.error().message;
        if (!result.error().source.empty()) std::cerr << ": " << path_text(result.error().source);
        std::cerr << '\n';
        if (result.error().compile_error) {
            std::cerr << "  " << result.error().compile_error->message << '\n';
        }
        if (result.error().archive_error) {
            std::cerr << "  " << result.error().archive_error->message << '\n';
            if (result.error().archive_error->librarian_error) {
                const auto& library_error = *result.error().archive_error->librarian_error;
                std::cerr << "  " << library_error.message << '\n';
                if (library_error.process_result) print_process_output(*library_error.process_result);
            }
        }
        return result.error().code == orchestration::IncrementalStaticTargetErrorCode::archive_failed ? 5 : 4;
    }

    for (const auto& compile : result->compiles) {
        if (compile.result.compiled) {
            std::cout << "[compile] " << path_text(compile.source.filename());
            print_reasons(compile.result.validation.reasons);
            std::cout << '\n';
            if (compile.result.process) print_process_output(*compile.result.process);
        } else {
            std::cout << "[up-to-date] " << path_text(compile.source.filename()) << '\n';
        }
    }

    for (const auto& warning : result->archive.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) std::cerr << ": " << path_text(warning.path);
        std::cerr << '\n';
    }
    if (result->archive.archived) {
        std::cout << "[archive] " << path_text(request.target.executable.filename());
        print_reasons(result->archive.validation.reasons);
        std::cout << '\n';
        if (result->archive.process) print_process_output(*result->archive.process);
    } else {
        std::cout << "[up-to-date] " << path_text(request.target.executable.filename()) << '\n';
    }
    std::cout << "output: " << path_text(request.target.executable) << '\n';
    return 0;
}

} // namespace mqb::cli
