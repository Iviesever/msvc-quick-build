#include "Invocation.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

[[nodiscard]] std::expected<fs::path, std::string>
validate_source(fs::path source, const std::string_view description) {
    std::error_code error_code;
    if (!fs::is_regular_file(source, error_code) || error_code) {
        return std::unexpected(
            std::string{description} + " does not exist: " + diagnostics::path_text(source));
    }
    if (!mqb::is_translation_unit_path(source)) {
        return std::unexpected(
            std::string{description}
            + " must use .c, .cpp, .cc, .cxx, .ixx, .cppm, or .mpp: "
            + diagnostics::path_text(source));
    }
    return source.lexically_normal();
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

[[nodiscard]] std::expected<std::vector<fs::path>, std::string>
conventional_entry_candidates(const fs::path& project_root) {
    static constexpr std::array<std::string_view, 4> names{
        "main.c",
        "main.cpp",
        "main.cc",
        "main.cxx",
    };

    std::vector<fs::path> result;
    for (const auto& directory : std::array<fs::path, 2>{project_root, project_root / "src"}) {
        for (const auto name : names) {
            const fs::path candidate = (directory / name).lexically_normal();
            std::error_code error_code;
            const bool regular = fs::is_regular_file(candidate, error_code);
            if (error_code) {
                return std::unexpected(
                    "failed to inspect default entry candidate "
                    + diagnostics::path_text(candidate) + ": " + error_code.message());
            }
            if (regular) {
                result.push_back(candidate);
            }
        }
    }
    return result;
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
        auto validated = validate_source(std::move(*source), "source file");
        if (!validated) {
            return std::unexpected(validated.error());
        }
        for (const auto& previous : requested_sources) {
            error_code.clear();
            if (fs::equivalent(previous, *validated, error_code) && !error_code) {
                return std::unexpected(
                    "source file was provided more than once: "
                    + diagnostics::path_text(*validated));
            }
        }
        requested_sources.push_back(std::move(*validated));
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

std::expected<fs::path, std::string>
resolve_default_entry(
    const fs::path& project_root,
    const std::optional<fs::path>& configured_entry) {
    if (configured_entry) {
        auto resolved = absolute_path_from(project_root, *configured_entry, "build.entry");
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        auto validated = validate_source(std::move(*resolved), "configured build.entry");
        if (!validated) {
            return std::unexpected(validated.error());
        }
        return validated;
    }

    auto candidates = conventional_entry_candidates(project_root);
    if (!candidates) {
        return std::unexpected(candidates.error());
    }
    if (candidates->size() == 1) {
        return candidates->front();
    }
    if (candidates->empty()) {
        return std::unexpected(
            "no default entry found; pass a source file or set build.entry in mqb.json "
            "(conventional fallback checks main.{c,cpp,cc,cxx} in the project root and src/)" );
    }

    std::string message =
        "multiple conventional default entries found; pass a source file or set build.entry in mqb.json:";
    for (const auto& candidate : *candidates) {
        message += " ";
        message += diagnostics::path_text(candidate);
    }
    return std::unexpected(std::move(message));
}

} // namespace mqb::app
