#include "Invocation.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "Diagnostics.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::expected<fs::path, std::string>
absolute_path_from(
    const fs::path& base,
    const fs::path& path,
    const std::string_view description) {
    std::error_code error_code;
    const fs::path candidate = path.is_absolute() ? path : base / path;
    fs::path absolute = fs::absolute(candidate, error_code);
    if (error_code) {
        return std::unexpected(
            "failed to resolve " + std::string{description} + ": " + error_code.message());
    }
    return absolute.lexically_normal();
}

[[nodiscard]] std::expected<void, std::string>
resolve_directory_list(
    const fs::path& base,
    std::vector<fs::path>& paths,
    const std::string_view description) {
    for (auto& path : paths) {
        auto resolved = absolute_path_from(base, path, description);
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        path = std::move(*resolved);
    }
    return {};
}

} // namespace

std::expected<Invocation, std::string>
resolve_invocation(mqb::cli::Options& options) {
    std::error_code error_code;
    fs::path invocation_directory = fs::current_path(error_code);
    if (error_code) {
        return std::unexpected(
            "failed to resolve current working directory: " + error_code.message());
    }
    invocation_directory = invocation_directory.lexically_normal();

    std::vector<fs::path> requested_sources;
    requested_sources.reserve(options.build.sources.size());
    for (const auto& requested_source : options.build.sources) {
        auto source = absolute_path_from(invocation_directory, requested_source, "source file");
        if (!source) {
            return std::unexpected(source.error());
        }
        if (!fs::is_regular_file(*source, error_code) || error_code) {
            return std::unexpected(
                "source file does not exist: " + diagnostics::path_text(*source));
        }
        if (!mqb::is_translation_unit_path(*source)) {
            return std::unexpected(
                "only .c, .cpp, .cc, .cxx, .ixx, .cppm, and .mpp sources are supported: "
                + diagnostics::path_text(*source));
        }
        for (const auto& previous : requested_sources) {
            error_code.clear();
            if (fs::equivalent(previous, *source, error_code) && !error_code) {
                return std::unexpected(
                    "source file was provided more than once: "
                    + diagnostics::path_text(*source));
            }
        }
        requested_sources.push_back(std::move(*source));
    }

    if (auto resolved = resolve_directory_list(
            invocation_directory,
            options.include_directories,
            "include directory"); !resolved) {
        return std::unexpected(resolved.error());
    }
    if (auto resolved = resolve_directory_list(
            invocation_directory,
            options.library_directories,
            "library directory"); !resolved) {
        return std::unexpected(resolved.error());
    }
    if (auto resolved = resolve_directory_list(
            invocation_directory,
            options.portable_roots,
            "portable toolchain root"); !resolved) {
        return std::unexpected(resolved.error());
    }

    return Invocation{
        .directory = std::move(invocation_directory),
        .requested_sources = std::move(requested_sources),
    };
}

} // namespace mqb::app
