#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {
namespace include_freshness_detail {

namespace fs = std::filesystem;

[[nodiscard]] inline bool same_path(const fs::path& left, const fs::path& right) {
    return left == right || left.lexically_normal() == right.lexically_normal();
}

[[nodiscard]] inline bool ascii_iequals(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto fold = [](const char value) noexcept {
            return value >= 'A' && value <= 'Z'
                ? static_cast<char>(value - 'A' + 'a')
                : value;
        };
        if (fold(left[index]) != fold(right[index])) return false;
    }
    return true;
}

[[nodiscard]] inline fs::path utf8_path(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] inline fs::path effective_working_directory(
    const std::optional<fs::path>& working_directory) {
    std::error_code error_code;
    if (working_directory && !working_directory->empty()) {
        if (working_directory->is_absolute()) {
            return working_directory->lexically_normal();
        }
        auto absolute = fs::absolute(*working_directory, error_code);
        if (!error_code) return absolute.lexically_normal();
        return working_directory->lexically_normal();
    }
    auto current = fs::current_path(error_code);
    return error_code ? fs::path{} : current.lexically_normal();
}

[[nodiscard]] inline fs::path absolute_search_path(
    const fs::path& path,
    const fs::path& working_directory) {
    if (path.empty()) return {};
    if (path.is_absolute() || working_directory.empty()) {
        return path.lexically_normal();
    }
    return (working_directory / path).lexically_normal();
}

inline void append_unique(std::vector<fs::path>& paths, fs::path path) {
    if (path.empty()) return;
    path = path.lexically_normal();
    if (std::none_of(paths.begin(), paths.end(), [&](const fs::path& existing) {
            return same_path(existing, path);
        })) {
        paths.push_back(std::move(path));
    }
}

[[nodiscard]] inline fs::path deepest_existing_directory(fs::path candidate) {
    candidate = candidate.lexically_normal();
    while (!candidate.empty()) {
        std::error_code error_code;
        const auto status = fs::status(candidate, error_code);
        if (!error_code && fs::is_directory(status)) {
            return candidate;
        }

        const fs::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) break;
        candidate = parent;
    }
    return {};
}

[[nodiscard]] inline std::optional<fs::path> relative_if_within(
    const fs::path& child,
    const fs::path& root) {
    if (child.empty() || root.empty()) return std::nullopt;
    const fs::path relative = child.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty()) {
        return same_path(child, root) ? std::optional<fs::path>{fs::path{}} : std::nullopt;
    }
    if (relative.is_absolute()) return std::nullopt;
    for (const auto& component : relative) {
        if (component == "..") return std::nullopt;
    }
    return relative;
}

inline void append_raw_include_roots(
    std::vector<fs::path>& roots,
    const std::vector<std::string>& arguments,
    const fs::path& working_directory) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        std::optional<std::string_view> value;

        if (argument == "/I" || argument == "-I"
            || argument == "/external:I" || argument == "-external:I") {
            if (index + 1 < arguments.size()) {
                value = arguments[++index];
            }
        } else if (argument.starts_with("/external:I") && argument.size() > 11u) {
            value = argument.substr(11u);
        } else if (argument.starts_with("-external:I") && argument.size() > 11u) {
            value = argument.substr(11u);
        } else if (argument.starts_with("/I") && argument.size() > 2u) {
            value = argument.substr(2u);
        } else if (argument.starts_with("-I") && argument.size() > 2u) {
            value = argument.substr(2u);
        }

        if (value && !value->empty()) {
            append_unique(
                roots,
                absolute_search_path(utf8_path(*value), working_directory));
        }
    }
}

[[nodiscard]] inline bool ignores_environment_include_roots(
    const std::vector<std::string>& arguments) noexcept {
    return std::any_of(arguments.begin(), arguments.end(), [](const std::string& argument) {
        return ascii_iequals(argument, "/X") || ascii_iequals(argument, "-X");
    });
}

inline void append_environment_include_roots(
    std::vector<fs::path>& roots,
    const std::span<const process::EnvironmentVariable> environment,
    const fs::path& working_directory) {
    for (const auto& variable : environment) {
        if (!ascii_iequals(variable.name, "INCLUDE")) continue;

        std::size_t begin = 0;
        while (begin <= variable.value.size()) {
            const std::size_t end = variable.value.find(';', begin);
            const std::string_view part{
                variable.value.data() + begin,
                (end == std::string::npos ? variable.value.size() : end) - begin};
            if (!part.empty()) {
                append_unique(
                    roots,
                    absolute_search_path(utf8_path(part), working_directory));
            }
            if (end == std::string::npos) break;
            begin = end + 1u;
        }
        return;
    }
}

} // namespace include_freshness_detail

// Return directory paths whose namespace determines whether a previously
// resolved include remains the first MSVC search hit. These paths are sealed
// into the existing compile/module-scan dependency evidence. Warm validation
// only stats them; it never launches cl.exe or /scanDependencies.
//
// Ordinary source/header directories are conservative quote-include roots.
// The synthetic PCH creator is the exception: its source lives beside MQB-owned
// outputs and contains no textual includes, so watching that artifact directory
// would make its own first build alter its freshness evidence. Its forced PCH
// header and every transitive include are still represented by resolved-header
// parent directories below. Ordered /I and /external:I roots are watched
// directly, and for a dependency resolved below a later root we also watch the
// corresponding parent directory (or its deepest existing ancestor) in every
// higher-priority root. This catches a new same-name header appearing without
// changing argv or the previously resolved file.
[[nodiscard]] inline std::vector<std::filesystem::path>
include_search_freshness_directories(
    const std::span<const std::filesystem::path> resolved_includes,
    const std::filesystem::path& source,
    const CompilerOptions& options,
    const std::span<const process::EnvironmentVariable> environment,
    const std::optional<std::filesystem::path>& working_directory) {
    namespace fs = std::filesystem;
    using namespace include_freshness_detail;

    const fs::path working = effective_working_directory(working_directory);
    const fs::path absolute_source = absolute_search_path(source, working);
    std::vector<fs::path> ordered_roots;
    const bool synthetic_pch_creator = options.precompiled_header
        && options.precompiled_header->role == PrecompiledHeaderRole::create;
    if (!synthetic_pch_creator) {
        append_unique(ordered_roots, absolute_source.parent_path());
    }
    for (const auto& include_directory : options.include_directories) {
        append_unique(
            ordered_roots,
            absolute_search_path(include_directory, working));
    }
    append_raw_include_roots(ordered_roots, options.additional_arguments, working);
    if (!ignores_environment_include_roots(options.additional_arguments)) {
        append_environment_include_roots(ordered_roots, environment, working);
    }

    std::vector<fs::path> watched;
    for (const auto& root : ordered_roots) {
        append_unique(watched, deepest_existing_directory(root));
    }

    std::vector<fs::path> normalized_dependencies;
    normalized_dependencies.reserve(resolved_includes.size());
    for (const auto& dependency : resolved_includes) {
        const fs::path normalized = absolute_search_path(dependency, working);
        normalized_dependencies.push_back(normalized);
        append_unique(watched, deepest_existing_directory(normalized.parent_path()));
    }

    for (const auto& dependency : normalized_dependencies) {
        for (std::size_t winning_index = 0; winning_index < ordered_roots.size(); ++winning_index) {
            const auto relative = relative_if_within(dependency, ordered_roots[winning_index]);
            if (!relative) continue;

            const fs::path relative_parent = relative->parent_path();
            for (std::size_t candidate_index = 0;
                 candidate_index <= winning_index;
                 ++candidate_index) {
                append_unique(
                    watched,
                    deepest_existing_directory(
                        ordered_roots[candidate_index] / relative_parent));
            }
            break;
        }
    }

    return watched;
}

} // namespace mqb::msvc
