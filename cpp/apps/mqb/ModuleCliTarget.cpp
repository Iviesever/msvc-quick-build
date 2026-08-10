#include "ModuleCliTarget.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#include "mqb/orchestration/MsvcTargetRouter.hpp"

namespace mqb::cli {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative == ".") return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

[[nodiscard]] fs::path display_source(const fs::path& root, const fs::path& source) {
    const fs::path relative = source.lexically_relative(root);
    return safe_relative(relative) ? relative : source;
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

void print_process_output(const process::ProcessResult& process_result) {
    if (!process_result.stdout_text.empty()) {
        write_forwarded_text(std::cout, process_result.stdout_text);
        if (process_result.stdout_text.back() != '\n') std::cout << '\n';
    }
    if (!process_result.stderr_text.empty()) {
        write_forwarded_text(std::cerr, process_result.stderr_text);
        if (process_result.stderr_text.back() != '\n') std::cerr << '\n';
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

void print_compile_failure(const orchestration::IncrementalCompileError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (!error.compile_error) return;
    const auto& compile_error = *error.compile_error;
    std::cerr << "  " << compile_error.message << '\n';
    if (compile_error.compiler_error) {
        std::cerr << "  " << compile_error.compiler_error->message << '\n';
        if (compile_error.compiler_error->process_result) {
            print_process_output(*compile_error.compiler_error->process_result);
        }
    }
    if (compile_error.dependency_error) {
        std::cerr << "  " << compile_error.dependency_error->message << '\n';
    }
}

void print_link_failure(const orchestration::IncrementalLinkError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (error.library_resolution_error) {
        const auto& resolution = *error.library_resolution_error;
        std::cerr << "  " << resolution.message;
        if (!resolution.library.empty()) std::cerr << ": " << resolution.library;
        if (!resolution.path.empty()) std::cerr << " (" << path_text(resolution.path) << ')';
        std::cerr << '\n';
    }
    if (!error.linker_error) return;
    std::cerr << "  " << error.linker_error->message << '\n';
    if (error.linker_error->process_result) print_process_output(*error.linker_error->process_result);
    if (error.linker_error->process_error) {
        std::cerr << "  " << error.linker_error->process_error->message << '\n';
    }
}

void print_module_failure(const orchestration::IncrementalModuleTargetError& error) {
    std::cerr << "error: " << error.message;
    if (!error.source.empty()) std::cerr << ": " << path_text(error.source);
    if (!error.artifact.empty()) std::cerr << " (" << path_text(error.artifact) << ')';
    std::cerr << '\n';
    if (error.scan_error) {
        std::cerr << "  " << error.scan_error->message << '\n';
        if (error.scan_error->process_result) print_process_output(*error.scan_error->process_result);
        if (error.scan_error->process_error) std::cerr << "  " << error.scan_error->process_error->message << '\n';
        if (error.scan_error->dependency_error) std::cerr << "  " << error.scan_error->dependency_error->message << '\n';
    }
    if (error.graph_error) std::cerr << "  " << error.graph_error->message << '\n';
    if (error.compile_error) {
        std::cerr << "  " << error.compile_error->message << '\n';
        if (error.compile_error->compile_error) print_compile_failure(*error.compile_error->compile_error);
    }
    if (error.link_error) print_link_failure(*error.link_error);
}

void print_compile_warnings(const orchestration::IncrementalCompileResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) std::cerr << ": " << path_text(warning.path);
        std::cerr << '\n';
    }
}

void print_link_warnings(const orchestration::IncrementalLinkResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) std::cerr << ": " << path_text(warning.path);
        std::cerr << '\n';
    }
}

} // namespace

bool is_module_interface_source(const fs::path& source) {
    const auto kind = classify_translation_unit_path(source);
    return kind && *kind == TranslationUnitKind::module_interface;
}

int run_module_target(
    ModuleCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner) {
    std::vector<orchestration::RoutedTargetSourceRequest> routed_sources;
    routed_sources.reserve(request.sources.size());
    for (auto& source : request.sources) {
        const auto kind = classify_translation_unit_path(source.source);
        if (!kind) {
            std::cerr << "error: unsupported translation unit: " << path_text(source.source) << '\n';
            return 2;
        }
        routed_sources.push_back(orchestration::RoutedTargetSourceRequest{
            .source = source.source,
            .artifacts = std::move(source.artifacts),
            .kind = *kind,
        });
    }

    if (request.verbose) {
        std::cout << "[target] " << request.target_name << "\n"
                  << "  project: " << path_text(request.project_root) << '\n';
        if (request.config_file) std::cout << "  config:  " << path_text(*request.config_file) << '\n';
        std::cout << "  pipeline: named-modules\n"
                  << "  jobs:    " << request.max_parallel_jobs
                  << (request.jobs_explicit ? "" : " (auto)") << '\n'
                  << "  cl:      " << path_text(toolchain.identity.compiler) << '\n'
                  << "  link:    " << path_text(toolchain.linker) << '\n';
        for (const auto& source : routed_sources) {
            std::cout << "  source:  " << path_text(display_source(request.project_root, source.source)) << '\n'
                      << "    kind:  " << (source.kind == TranslationUnitKind::module_interface ? "module-interface" : "source") << '\n'
                      << "    obj:   " << path_text(source.artifacts.object) << '\n'
                      << "    deps:  " << path_text(source.artifacts.dependencies) << '\n'
                      << "    scan:  " << path_text(source.artifacts.module_dependencies) << '\n';
            if (source.kind == TranslationUnitKind::module_interface) {
                std::cout << "    ifc:   " << path_text(source.artifacts.module_interface) << '\n';
            }
            std::cout << "    cache: " << path_text(source.artifacts.compile_cache) << '\n';
        }
        for (const auto& directory : request.link_options.library_directories) {
            std::cout << "  libpath: " << path_text(directory) << '\n';
        }
        for (const auto& library : request.link_options.libraries) std::cout << "  lib:     " << library << '\n';
        std::cout << "  exe:     " << path_text(request.target.executable) << '\n'
                  << "  cache:   " << path_text(request.target.link_cache) << '\n';
    }

    msvc::MsvcCompileExecutor compile_executor{toolchain, runner};
    orchestration::MsvcIncrementalCompileCoordinator incremental_compile{toolchain, compile_executor};
    msvc::MsvcLinker linker{toolchain, runner};
    orchestration::MsvcIncrementalLinkCoordinator incremental_link{toolchain, linker};
    orchestration::MsvcIncrementalTargetCoordinator ordinary_target{incremental_compile, incremental_link};
    msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};
    orchestration::MsvcModuleTargetCoordinator module_target{scanner, module_compile, incremental_link};
    orchestration::MsvcTargetRouter router{ordinary_target, module_target};

    orchestration::RoutedTargetRequest target_request{
        .sources = std::move(routed_sources),
        .target = request.target,
        .compiler_options = std::move(request.compiler_options),
        .link_options = std::move(request.link_options),
        .working_directory = request.project_root,
        .max_parallel_jobs = request.max_parallel_jobs,
    };
    auto result = router.run(target_request);
    if (!result) {
        if (result.error().ordinary_error) {
            const auto& ordinary = *result.error().ordinary_error;
            std::cerr << "error: " << ordinary.message;
            if (!ordinary.source.empty()) std::cerr << ": " << path_text(ordinary.source);
            std::cerr << '\n';
            if (ordinary.compile_error) print_compile_failure(*ordinary.compile_error);
            if (ordinary.link_error) print_link_failure(*ordinary.link_error);
            return ordinary.code == orchestration::IncrementalTargetErrorCode::link_failed ? 5 : 4;
        }
        if (result.error().module_error) {
            print_module_failure(*result.error().module_error);
            return result.error().module_error->code == orchestration::IncrementalModuleTargetErrorCode::link_failed ? 5 : 4;
        }
        std::cerr << "error: " << result.error().message << '\n';
        return 4;
    }

    for (const auto& compile : result->compiles) {
        print_compile_warnings(compile.result);
        const fs::path label = display_source(request.project_root, compile.source);
        if (compile.result.compiled) {
            std::cout << "[compile] " << path_text(label);
            print_reasons(compile.result.validation.reasons);
            std::cout << '\n';
            if (compile.result.process) print_process_output(*compile.result.process);
        } else {
            std::cout << "[up-to-date] " << path_text(label) << '\n';
        }
    }

    print_link_warnings(result->link);
    if (result->link.linked) {
        std::cout << "[link] " << path_text(target_request.target.executable.filename());
        print_reasons(result->link.validation.reasons);
        std::cout << '\n';
        if (result->link.process) print_process_output(*result->link.process);
    } else {
        std::cout << "[up-to-date] " << path_text(target_request.target.executable.filename()) << '\n';
    }
    std::cout << "executable: " << path_text(target_request.target.executable) << '\n';

    if (!request.run_after_build) return 0;
    std::cout << "[run] " << path_text(target_request.target.executable.filename()) << '\n';
    process::ProcessSpec run_spec;
    run_spec.executable = target_request.target.executable;
    run_spec.arguments = std::move(request.run_arguments);
    run_spec.working_directory = request.project_root;
    run_spec.capture_stdout = true;
    run_spec.capture_stderr = true;
    auto run_result = runner.run(run_spec);
    if (!run_result) {
        std::cerr << "error: failed to run executable: " << run_result.error().message << '\n';
        return 6;
    }
    print_process_output(*run_result);
    return run_result->exit_code;
}

} // namespace mqb::cli
