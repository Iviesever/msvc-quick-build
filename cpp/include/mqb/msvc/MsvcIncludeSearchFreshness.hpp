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

[[nodiscard]] inline bool ascii_identifier_char(const char value) noexcept {
    return (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z')
        || (value >= '0' && value <= '9')
        || value == '_';
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

struct IncludeSearchUse {
    bool any_search{false};
    bool may_search_local{false};
    std::vector<fs::path> parent_suffixes;
};

inline void record_literal_operand(
    IncludeSearchUse& use,
    const std::string_view line,
    std::size_t position) {
    while (position < line.size()
        && (line[position] == ' ' || line[position] == '\t')) {
        ++position;
    }
    if (position >= line.size()) {
        use.any_search = true;
        use.may_search_local = true;
        return;
    }

    const char opener = line[position];
    if (opener != '<' && opener != '"') {
        // Macro-expanded include / __has_include operand. Its eventual form may
        // be quoted, so preserve local-search freshness conservatively.
        use.any_search = true;
        use.may_search_local = true;
        return;
    }

    use.any_search = true;
    if (opener == '"') use.may_search_local = true;
    const char closer = opener == '<' ? '>' : '"';
    const std::size_t end = line.find(closer, position + 1u);
    if (end == std::string_view::npos) return;

    const fs::path operand = utf8_path(line.substr(position + 1u, end - position - 1u));
    const fs::path parent = operand.parent_path();
    if (!parent.empty()) append_unique(use.parent_suffixes, parent);
}

// Cold-path inspection distinguishes files that can participate in MSVC's
// local quoted-include search from pure angle/no-include files. It also records
// literal parent suffixes for unresolved probes such as __has_include(<x/y.hpp>)
// and C++ header-unit imports such as import <x/y.hpp> so a file appearing in an
// already-existing nested directory is visible. Named-module imports do not
// participate because they do not carry a header-name token. Unknown/macro
// include operands opt into local search conservatively.
[[nodiscard]] inline IncludeSearchUse inspect_include_search_use(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return IncludeSearchUse{.any_search = true, .may_search_local = true};
    }

    IncludeSearchUse use;
    std::string line_storage;
    while (std::getline(stream, line_storage)) {
        const std::string_view line{line_storage};

        std::size_t has_include = 0;
        while ((has_include = line.find("__has_include", has_include)) != std::string_view::npos) {
            std::size_t position = has_include + std::string_view{"__has_include"}.size();
            while (position < line.size()
                && (line[position] == ' ' || line[position] == '\t')) {
                ++position;
            }
            if (position < line.size() && line[position] == '(') ++position;
            record_literal_operand(use, line, position);
            has_include = position;
        }

        // C++20 Header Unit imports use the same header-name lookup semantics as
        // textual includes. Recognize both `import <x>;` and `import "x";`
        // (including `export import ...`). A named-module import such as
        // `import math;` is deliberately ignored.
        std::size_t import_position = 0;
        constexpr std::string_view import_token = "import";
        while ((import_position = line.find(import_token, import_position)) != std::string_view::npos) {
            const bool left_boundary = import_position == 0
                || !ascii_identifier_char(line[import_position - 1u]);
            std::size_t operand_position = import_position + import_token.size();
            const bool right_boundary = operand_position >= line.size()
                || !ascii_identifier_char(line[operand_position]);
            if (left_boundary && right_boundary) {
                while (operand_position < line.size()
                    && (line[operand_position] == ' ' || line[operand_position] == '\t')) {
                    ++operand_position;
                }
                if (operand_position < line.size()
                    && (line[operand_position] == '<' || line[operand_position] == '"')) {
                    record_literal_operand(use, line, operand_position);
                }
            }
            import_position += import_token.size();
        }

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
            || line.substr(position, include_token.size()) != include_token) {
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
        record_literal_operand(use, line, position);
    }
    return use;
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

// Return directory paths whose namespace determines whether include-search
// resolution can change. These paths are sealed into the existing compile and
// module-scan freshness evidence. Warm validation only stats filesystem
// metadata; it never launches cl.exe or /scanDependencies.
//
// Quoted includes may search the current header's directory and directories of
// still-open parent headers before global /I + INCLUDE roots. /sourceDependencies
// reports resolved files but not include edges/spellings, so every source/header
// that can issue a quoted or macro include contributes a conservative possible
// local root. Pure angle/no-include files do not, preventing unrelated artifacts
// created beside a TU (for example LINK /MAP output) from poisoning the compile
// warm path.
//
// Literal #include / __has_include / Header Unit import operands additionally
// contribute parent suffixes even when the header was not resolved on the
// previous build. This covers the important "absent then appears" case,
// including a file appearing in an already-existing nested candidate directory.
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
    const std::vector<fs::path> global_roots = include_search_roots(
        options,
        environment,
        working_directory);

    std::vector<fs::path> normalized_dependencies;
    normalized_dependencies.reserve(resolved_includes.size());
    for (const auto& dependency : resolved_includes) {
        normalized_dependencies.push_back(absolute_search_path(dependency, working));
    }

    bool any_search_usage = false;
    std::vector<fs::path> local_roots;
    std::vector<fs::path> relative_parent_suffixes;

    const bool synthetic_pch_creator = options.precompiled_header
        && options.precompiled_header->role == PrecompiledHeaderRole::create;
    const IncludeSearchUse source_use = inspect_include_search_use(absolute_source);
    any_search_usage = any_search_usage || source_use.any_search;
    if (!synthetic_pch_creator && source_use.may_search_local) {
        append_unique(local_roots, absolute_source.parent_path());
    }
    for (const auto& suffix : source_use.parent_suffixes) {
        append_unique(relative_parent_suffixes, suffix);
    }

    for (const auto& dependency : normalized_dependencies) {
        const IncludeSearchUse dependency_use = inspect_include_search_use(dependency);
        any_search_usage = any_search_usage || dependency_use.any_search;
        if (dependency_use.may_search_local) {
            append_unique(local_roots, dependency.parent_path());
        }
        for (const auto& suffix : dependency_use.parent_suffixes) {
            append_unique(relative_parent_suffixes, suffix);
        }
    }

    // A successful TU with no textual/header-unit search (and no __has_include)
    // cannot be rerouted by directory namespace churn. Exact global-root identity
    // is still stored separately so ambient INCLUDE replacement remains visible.
    if (!any_search_usage && normalized_dependencies.empty()) return {};

    std::vector<fs::path> watched;
    for (const auto& root : local_roots) {
        append_unique(watched, deepest_existing_directory(root));
    }
    for (const auto& root : global_roots) {
        append_unique(watched, deepest_existing_directory(root));
    }

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
