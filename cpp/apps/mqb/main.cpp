#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Cli.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::expected<fs::path, std::string>
absolute_path(const fs::path& path, const std::string_view description) {
    std::error_code error_code;
    fs::path absolute = fs::absolute(path, error_code);
    if (error_code) {
        return std::unexpected(
            "failed to resolve " + std::string{description} + ": " + error_code.message());
    }
    return absolute.lexically_normal();
}

[[nodiscard]] bool supported_source(const fs::path& source) {
    std::string extension = source.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx";
}

[[nodiscard]] bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative == ".") {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] fs::path display_source(
    const fs::path& project_root,
    const fs::path& source) {
    const fs::path relative = source.lexically_relative(project_root);
    return safe_relative(relative) ? relative : source;
}

void add_portable_root_if_missing(
    std::vector<fs::path>& roots,
    const fs::path& candidate) {
    if (candidate.empty()) {
        return;
    }
    const fs::path normalized = candidate.lexically_normal();
    if (std::find(roots.begin(), roots.end(), normalized) == roots.end()) {
        roots.push_back(normalized);
    }
}

void print_process_output(const mqb::process::ProcessResult& process) {
    if (!process.stdout_text.empty()) {
        std::cout << process.stdout_text;
        if (process.stdout_text.back() != '\n') {
            std::cout << '\n';
        }
    }
    if (!process.stderr_text.empty()) {
        std::cerr << process.stderr_text;
        if (process.stderr_text.back() != '\n') {
            std::cerr << '\n';
        }
    }
}

void print_reasons(const std::vector<mqb::BuildReason>& reasons) {
    if (reasons.empty()) {
        return;
    }
    std::cout << " [";
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << mqb::to_string(reasons[index]);
    }
    std::cout << ']';
}

void print_compile_failure(const mqb::orchestration::IncrementalCompileError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (!error.compile_error) {
        return;
    }
    const auto& compile_error = *error.compile_error;
    std::cerr << "  " << compile_error.message << '\n';
    if (compile_error.compiler_error) {
        const auto& compiler_error = *compile_error.compiler_error;
        std::cerr << "  " << compiler_error.message << '\n';
        if (compiler_error.process_result) {
            print_process_output(*compiler_error.process_result);
        }
    }
    if (compile_error.dependency_error) {
        std::cerr << "  " << compile_error.dependency_error->message << '\n';
    }
}

void print_link_failure(const mqb::orchestration::IncrementalLinkError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (!error.linker_error) {
        return;
    }
    const auto& linker_error = *error.linker_error;
    std::cerr << "  " << linker_error.message << '\n';
    if (linker_error.process_result) {
        print_process_output(*linker_error.process_result);
    }
    if (linker_error.process_error) {
        std::cerr << "  " << linker_error.process_error->message << '\n';
    }
}

void print_target_failure(const mqb::orchestration::IncrementalTargetError& error) {
    std::cerr << "error: " << error.message;
    if (!error.source.empty()) {
        std::cerr << ": " << path_text(error.source);
    }
    std::cerr << '\n';
    if (error.compile_error) {
        print_compile_failure(*error.compile_error);
    }
    if (error.link_error) {
        print_link_failure(*error.link_error);
    }
}

void print_compile_warnings(
    const mqb::orchestration::IncrementalCompileResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) {
            std::cerr << ": " << path_text(warning.path);
        }
        std::cerr << '\n';
    }
}

void print_link_warnings(
    const mqb::orchestration::IncrementalLinkResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) {
            std::cerr << ": " << path_text(warning.path);
        }
        std::cerr << '\n';
    }
}

} // namespace

int main(const int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    auto parsed = mqb::cli::parse_arguments(std::span<const std::string_view>{arguments});
    if (!parsed) {
        std::cerr << "error: " << parsed.error().message << "\n\n" << mqb::cli::usage();
        return 2;
    }
    auto options = std::move(*parsed);
    if (options.show_help) {
        std::cout << mqb::cli::usage();
        return 0;
    }

    std::error_code error_code;
    fs::path project_root = fs::current_path(error_code);
    if (error_code) {
        std::cerr << "error: failed to resolve current project directory: "
                  << error_code.message() << '\n';
        return 2;
    }
    project_root = project_root.lexically_normal();

    std::vector<fs::path> sources;
    sources.reserve(options.build.sources.size());
    for (const auto& requested_source : options.build.sources) {
        auto source = absolute_path(requested_source, "source file");
        if (!source) {
            std::cerr << "error: " << source.error() << '\n';
            return 2;
        }
        if (!fs::is_regular_file(*source, error_code) || error_code) {
            std::cerr << "error: source file does not exist: " << path_text(*source) << '\n';
            return 2;
        }
        if (!supported_source(*source)) {
            std::cerr << "error: only .cpp, .cc, and .cxx sources are supported in this milestone: "
                      << path_text(*source) << '\n';
            return 2;
        }
        for (const auto& previous : sources) {
            error_code.clear();
            if (fs::equivalent(previous, *source, error_code) && !error_code) {
                std::cerr << "error: source file was provided more than once: "
                          << path_text(*source) << '\n';
                return 2;
            }
        }
        sources.push_back(std::move(*source));
    }
    options.build.sources = sources;

    for (auto& include_directory : options.include_directories) {
        auto resolved = absolute_path(include_directory, "include directory");
        if (!resolved) {
            std::cerr << "error: " << resolved.error() << '\n';
            return 2;
        }
        include_directory = std::move(*resolved);
    }
    for (auto& portable_root : options.portable_roots) {
        auto resolved = absolute_path(portable_root, "portable toolchain root");
        if (!resolved) {
            std::cerr << "error: " << resolved.error() << '\n';
            return 2;
        }
        portable_root = std::move(*resolved);
    }

    auto layout = mqb::ProjectArtifactLayout::create(project_root);
    if (!layout) {
        std::cerr << "error: " << layout.error().message << '\n';
        return 2;
    }

    std::vector<mqb::orchestration::TargetSourceRequest> target_sources;
    target_sources.reserve(sources.size());
    for (const auto& source : sources) {
        auto artifacts = layout->for_source(source);
        if (!artifacts) {
            std::cerr << "error: " << artifacts.error().message << ": "
                      << path_text(source) << '\n';
            return 2;
        }
        target_sources.push_back(mqb::orchestration::TargetSourceRequest{
            .source = source,
            .artifacts = std::move(*artifacts),
        });
    }

    const std::string target_name = sources.front().stem().string();
    auto target_artifacts = layout->for_target(target_name);
    if (!target_artifacts) {
        std::cerr << "error: " << target_artifacts.error().message << '\n';
        return 2;
    }

    add_portable_root_if_missing(options.portable_roots, project_root / "portable_msvc");
    for (const auto& source : sources) {
        add_portable_root_if_missing(options.portable_roots, source.parent_path() / "portable_msvc");
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions discovery;
    discovery.target_architecture = options.build.architecture;
    discovery.host_architecture = mqb::Architecture::x64;
    discovery.preference = options.toolchain_preference;
    discovery.portable_roots = options.portable_roots;

    auto toolchain = locator.discover(discovery);
    if (!toolchain) {
        std::cerr << "error: " << toolchain.error().message;
        if (!toolchain.error().path.empty()) {
            std::cerr << ": " << path_text(toolchain.error().path);
        }
        std::cerr << '\n';
        return 3;
    }

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = options.build.configuration;
    compiler_options.architecture = options.build.architecture;
    compiler_options.standard = options.build.standard;
    compiler_options.defines = std::move(options.defines);
    compiler_options.include_directories = std::move(options.include_directories);

    mqb::LinkOptions link_options;
    link_options.configuration = options.build.configuration;
    link_options.architecture = options.build.architecture;
    link_options.subsystem = mqb::LinkSubsystem::console;

    if (options.verbose) {
        std::cout << "[target] " << target_name << "\n"
                  << "  project: " << path_text(project_root) << '\n'
                  << "  cl:      " << path_text(toolchain->identity.compiler) << '\n'
                  << "  link:    " << path_text(toolchain->linker) << '\n';
        for (const auto& source : target_sources) {
            std::cout << "  source:  " << path_text(display_source(project_root, source.source)) << '\n'
                      << "    obj:   " << path_text(source.artifacts.object) << '\n'
                      << "    deps:  " << path_text(source.artifacts.dependencies) << '\n'
                      << "    cache: " << path_text(source.artifacts.compile_cache) << '\n';
        }
        std::cout << "  exe:     " << path_text(target_artifacts->executable) << '\n'
                  << "  cache:   " << path_text(target_artifacts->link_cache) << '\n';
    }

    mqb::msvc::MsvcCompileExecutor compile_executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        *toolchain,
        compile_executor};
    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{*toolchain, linker};
    mqb::orchestration::MsvcIncrementalTargetCoordinator target_coordinator{
        compile_coordinator,
        link_coordinator};

    const mqb::orchestration::IncrementalTargetRequest request{
        .sources = std::move(target_sources),
        .target = std::move(*target_artifacts),
        .compiler_options = std::move(compiler_options),
        .link_options = std::move(link_options),
        .working_directory = project_root,
    };

    auto result = target_coordinator.run(request);
    if (!result) {
        print_target_failure(result.error());
        return result.error().code == mqb::orchestration::IncrementalTargetErrorCode::link_failed ? 5 : 4;
    }

    for (const auto& compile : result->compiles) {
        print_compile_warnings(compile.result);
        const fs::path label = display_source(project_root, compile.source);
        if (compile.result.compiled) {
            std::cout << "[compile] " << path_text(label);
            print_reasons(compile.result.validation.reasons);
            std::cout << '\n';
            if (compile.result.process) {
                print_process_output(*compile.result.process);
            }
        } else {
            std::cout << "[up-to-date] " << path_text(label) << '\n';
        }
    }

    print_link_warnings(result->link);
    if (result->link.linked) {
        std::cout << "[link] " << path_text(request.target.executable.filename());
        print_reasons(result->link.validation.reasons);
        std::cout << '\n';
        if (result->link.process) {
            print_process_output(*result->link.process);
        }
    } else {
        std::cout << "[up-to-date] " << path_text(request.target.executable.filename()) << '\n';
    }

    std::cout << "executable: " << path_text(request.target.executable) << '\n';
    return 0;
}
