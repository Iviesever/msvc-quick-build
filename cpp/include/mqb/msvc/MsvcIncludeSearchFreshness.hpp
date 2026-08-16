#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
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
    if (left == right || left.lexically_normal() == right.lexically_normal()) {
        return true;
    }
    std::error_code error_code;
    return fs::equivalent(left, right, error_code) && !error_code;
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

[[nodiscard]] inline std::optional<fs::path> lexical_relative_if_within(
    const fs::path& child,
    const fs::path& root) {
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

// The compiler reports resolved dependency paths using filesystem spelling,
// while a user-supplied /I root may differ only by Windows casing or by an
// equivalent path spelling. Fall back to an ancestor walk using equivalent()
// so we still recover the include-relative suffix in those cases.
[[nodiscard]] inline std::optional<fs::path> relative_if_within(
    const fs::path& child,
    const fs::path& root) {
    if (child.empty() || root.empty()) return std::nullopt;
    if (auto relative = lexical_relative_if_within(child, root)) {
        return relative;
    }

    fs::path cursor = child.lexically_normal();
    fs::path suffix;
    while (!cursor.empty()) {
        if (same_path(cursor, root)) {
            return suffix;
        }
        const fs::path filename = cursor.filename();
        if (!filename.empty()) {
            suffix = suffix.empty() ? filename : filename / suffix;
        }
        const fs::path parent = cursor.parent_path();
        if (parent.empty() || parent == cursor) break;
        cursor = parent;
    }
    return std::nullopt;
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

inline void collect_relative_parent_suffix(
    std::vector<fs::path>& suffixes,
    const fs::path& dependency,
    const fs::path& search_root) {
    const auto relative = relative_if_within(dependency, search_root);
    if (!relative) return;
    const fs::path parent = relative->parent_path();
    if (!parent.empty()) append_unique(suffixes, parent);
}

// A local directory participates in MSVC's special quoted-include search only
// when the corresponding source/header can issue a quoted (or macro-expanded)
// include. Pure angle-include files do not search their own directory. This
// cheap cold-path source inspection prevents unrelated artifacts created beside
// a TU (for example LINK /MAP output) from poisoning the compile warm path.
// Unknown/macro include operands conservatively opt into local search.
[[nodiscard]] inline bool may_use_local_include_search(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) return true;

    std::string line;
    while (std::getline(stream, line)) {
        std::size_t position = 0;
        while (position < line.size()
            && (line[position] == ' ' || line[position] == '\t')) {
            ++position;
        }
        if (position >= line.size() || line[position] != '#') continue;
        ++position;
        while (position < line.size()
            && (line[position] == ' ' || line[position] == '\t')) {
            ++position;
        }
        constexpr std::string_view include_token = "include";
        if (position + include_token.size() > line.size()
            || std::string_view{line}.substr(position, include_token.size()) != include_token) {
            continue;
        }
        position += include_token.size();
        if (position < line.size()
            && line[position] != ' '
            && line[position] != '\t'
            && line[position] != '"'
            && line[position] != '<') {
            continue;
        }
        while (position < line.size()
            && (line[position] == ' ' || line[position] == '\t')) {
            ++position;
        }
        if (position >= line.size()) return true;
        if (line[position] == '<') continue;
        return true;
    }
    return false;
}

} // namespace include_freshness_detail

// Return the ordered compiler-global include roots. Typed -I roots are emitted
// before native passthrough arguments, matching CompilerArgumentBuilder; the
// vcvars INCLUDE list follows them unless /X disables standard include paths.
// The exact ordered list is persisted in the compile cache so environment-root
// replacement/removal is identity, not merely directory timestamp evidence.
[[nodiscard]] inline std::vector<std::filesystem::path> include_search_roots(
    const CompilerOptions& options,
    const std::span<const process::EnvironmentVariable> environment,
    const std::optional<std::filesystem::path>& working_directory) {
    using namespace include_freshness_detail;

    const std::filesystem::path working = effective_working_directory(working_directory);
    std::vector<std::filesystem::path> roots;
    for (const auto& include_directory : options.include_directories) {
        append_unique(
            roots,
            absolute_search_path(include_directory, working));
    }
    append_raw_include_roots(roots, options.additional_arguments, working);
    if (!ignores_environment_include_roots(options.additional_arguments)) {
        append_environment_include_roots(roots, environment, working);
    }
    return roots;
}

// Return directory paths whose namespace determines whether a previously
// resolved include remains the first MSVC search hit. These paths are sealed
// into the existing compile/module-scan dependency evidence. Warm validation
// only stats them; it never launches cl.exe or /scanDependencies.
//
// Quoted includes may search the current header's directory and directories of
// still-open parent headers before the global /I + INCLUDE roots. /sourceDependencies
// reports the resolved files but not those include edges/spellings, so every
// resolved header that can actually issue a quoted/macro include contributes a
// conservative possible local root. Pure angle-only files do not: their local
// directory is irrelevant to resolution and may contain unrelated build output.
// For every include-relative parent suffix we can recover from the resolved
// dependency graph, the corresponding directory (or deepest existing ancestor)
// is watched under every possible local/global root. This catches a new shadow
// file even when its candidate subdirectory already existed and only that nested
// directory's mtime changes.
//
// The synthetic PCH creator is the exception to adding its source directory as
// a local root: that source lives beside MQB-owned outputs and has no textual
// includes. Its forced PCH header and all transitive includes still contribute
// real local/global freshness evidence.
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

    std::vector<fs::path> normalized_dependencies;
    normalized_dependencies.reserve(resolved_includes.size());
    for (const auto& dependency : resolved_includes) {
        normalized_dependencies.push_back(absolute_search_path(dependency, working));
    }

    // No resolved textual header means there is no previous include resolution
    // for directory namespace churn to reroute. Exact global-root identity is
    // still persisted separately by include_search_roots().
    if (normalized_dependencies.empty()) return {};

    const std::vector<fs::path> global_roots = include_search_roots(
        options,
        environment,
        working_directory);

    std::vector<fs::path> local_roots;
    const bool synthetic_pch_creator = options.precompiled_header
        && options.precompiled_header->role == PrecompiledHeaderRole::create;
    if (!synthetic_pch_creator && may_use_local_include_search(absolute_source)) {
        append_unique(local_roots, absolute_source.parent_path());
    }
    for (const auto& dependency : normalized_dependencies) {
        if (may_use_local_include_search(dependency)) {
            append_unique(local_roots, dependency.parent_path());
        }
    }

    std::vector<fs::path> watched;
    for (const auto& root : local_roots) {
        append_unique(watched, deepest_existing_directory(root));
    }
    for (const auto& root : global_roots) {
        append_unique(watched, deepest_existing_directory(root));
    }

    std::vector<fs::path> relative_parent_suffixes;
    for (const auto& dependency : normalized_dependencies) {
        for (const auto& root : local_roots) {
            collect_relative_parent_suffix(relative_parent_suffixes, dependency, root);
        }
        for (const auto& root : global_roots) {
            collect_relative_parent_suffix(relative_parent_suffixes, dependency, root);
        }
    }

    for (const auto& suffix : relative_parent_suffixes) {
        for (const auto& root : local_roots) {
            append_unique(watched, deepest_existing_directory(root / suffix));
        }
        for (const auto& root : global_roots) {
            append_unique(watched, deepest_existing_directory(root / suffix));
        }
    }

    return watched;
}

} // namespace mqb::msvc
