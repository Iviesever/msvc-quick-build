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
#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
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

[[nodiscard]] fs::path with_suffix(fs::path path, const std::string_view suffix) {
    path += std::string{suffix};
    return path;
}

} // namespace

int main(const int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    auto parsed = mqb::cli::parse_arguments(
        std::span<const std::string_view>{arguments});
    if (!parsed) {
        std::cerr << "error: " << parsed.error().message << "\n\n"
                  << mqb::cli::usage();
        return 2;
    }
    auto options = std::move(*parsed);
    if (options.show_help) {
        std::cout << mqb::cli::usage();
        return 0;
    }

    auto source = absolute_path(options.build.entry, "source file");
    if (!source) {
        std::cerr << "error: " << source.error() << '\n';
        return 2;
    }

    std::error_code error_code;
    if (!fs::is_regular_file(*source, error_code) || error_code) {
        std::cerr << "error: source file does not exist: " << path_text(*source) << '\n';
        return 2;
    }
    if (!supported_source(*source)) {
        std::cerr << "error: this milestone supports .cpp, .cc, and .cxx files only\n";
        return 2;
    }
    options.build.entry = *source;

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

    const fs::path source_directory = source->parent_path();
    const fs::path artifact_root = source_directory / ".mqb";

    // Keep the source extension in internal artifact names so sibling files
    // such as foo.cpp and foo.cxx cannot alias the same cached object.
    fs::path object_name = source->filename();
    object_name += ".obj";
    const fs::path object_file = artifact_root / "obj" / object_name;
    const fs::path dependency_file = artifact_root / "deps" / with_suffix(source->filename(), ".json");
    const fs::path cache_file = artifact_root / "cache" / with_suffix(source->filename(), ".mqbcache");

    add_portable_root_if_missing(options.portable_roots, source_directory / "portable_msvc");
    const fs::path current_directory = fs::current_path(error_code);
    if (!error_code) {
        add_portable_root_if_missing(options.portable_roots, current_directory / "portable_msvc");
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

    mqb::TranslationUnit unit;
    unit.source = *source;
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs.push_back(mqb::Artifact{
        .path = object_file,
        .kind = mqb::ArtifactKind::object,
    });

    if (options.verbose) {
        std::cout << "[toolchain] MSVC " << toolchain->identity.version
                  << " (" << mqb::to_string(options.build.architecture) << ")\n"
                  << "  cl:    " << path_text(toolchain->identity.compiler) << '\n'
                  << "  obj:   " << path_text(object_file) << '\n'
                  << "  deps:  " << path_text(dependency_file) << '\n'
                  << "  cache: " << path_text(cache_file) << '\n';
    }

    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator coordinator{*toolchain, executor};
    mqb::orchestration::IncrementalCompileRequest request{
        .unit = std::move(unit),
        .options = std::move(compiler_options),
        .cache_file = cache_file,
        .source_dependencies_file = dependency_file,
        .working_directory = source_directory,
    };

    auto result = coordinator.run(request);
    if (!result) {
        print_compile_failure(result.error());
        return 4;
    }

    for (const auto& warning : result->warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) {
            std::cerr << ": " << path_text(warning.path);
        }
        std::cerr << '\n';
    }

    if (result->compiled) {
        std::cout << "[compile] " << path_text(source->filename());
        if (!result->validation.reasons.empty()) {
            std::cout << " [";
            for (std::size_t index = 0; index < result->validation.reasons.size(); ++index) {
                if (index != 0) {
                    std::cout << ", ";
                }
                std::cout << mqb::to_string(result->validation.reasons[index]);
            }
            std::cout << ']';
        }
        std::cout << '\n';
        if (result->process) {
            print_process_output(*result->process);
        }
    } else {
        std::cout << "[up-to-date] " << path_text(source->filename()) << '\n';
    }

    std::cout << "object: " << path_text(object_file) << '\n';
    return 0;
}
