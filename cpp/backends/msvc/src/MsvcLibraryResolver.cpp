#include "mqb/msvc/MsvcLibraryResolver.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] LibraryResolutionError failure(
    const LibraryResolutionErrorCode code,
    std::string library,
    fs::path path,
    std::string message) {
    return LibraryResolutionError{
        .code = code,
        .library = std::move(library),
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] bool ascii_iequals(
    const std::string_view left,
    const std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view environment_value(
    const MsvcToolchain& toolchain,
    const std::string_view name) {
    for (const auto& variable : toolchain.environment) {
        if (ascii_iequals(variable.name, name)) {
            return variable.value;
        }
    }
    return {};
}

[[nodiscard]] std::vector<fs::path> split_library_path(const std::string_view value) {
    std::vector<fs::path> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t separator = value.find(';', begin);
        const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
        std::string_view token = value.substr(begin, end - begin);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
            token.remove_prefix(1);
        }
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
            token.remove_suffix(1);
        }
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
            token.remove_prefix(1);
            token.remove_suffix(1);
        }
        if (!token.empty()) {
            result.push_back(path_from_utf8(token));
        }
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1;
    }
    return result;
}

[[nodiscard]] std::expected<fs::path, LibraryResolutionError>
resolve_working_directory(const fs::path& requested) {
    std::error_code error_code;
    fs::path result;
    if (requested.empty()) {
        result = fs::current_path(error_code);
    } else {
        result = fs::absolute(requested, error_code);
    }
    if (error_code) {
        return std::unexpected(failure(
            LibraryResolutionErrorCode::working_directory_failed,
            {},
            requested,
            "failed to resolve library working directory: " + error_code.message()));
    }
    return result.lexically_normal();
}

[[nodiscard]] fs::path make_absolute(
    const fs::path& working_directory,
    const fs::path& path) {
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (working_directory / path).lexically_normal();
}

void add_search_directory(
    std::vector<fs::path>& directories,
    const fs::path& working_directory,
    const fs::path& directory) {
    if (directory.empty()) {
        return;
    }
    const fs::path normalized = make_absolute(working_directory, directory);
    if (std::find(directories.begin(), directories.end(), normalized) == directories.end()) {
        directories.push_back(normalized);
    }
}

[[nodiscard]] bool regular_file(const fs::path& path) {
    std::error_code error_code;
    return fs::is_regular_file(path, error_code) && !error_code;
}

[[nodiscard]] fs::path normalized_library_name(const std::string_view requested) {
    fs::path library = path_from_utf8(requested);
    if (library.extension().empty()) {
        library += ".lib";
    }
    return library;
}

} // namespace

std::expected<ResolvedLibraries, LibraryResolutionError>
MsvcLibraryResolver::resolve(
    const MsvcToolchain& toolchain,
    const LinkOptions& options,
    const fs::path& requested_working_directory) {
    auto working_directory = resolve_working_directory(requested_working_directory);
    if (!working_directory) {
        return std::unexpected(working_directory.error());
    }

    std::vector<fs::path> search_directories;
    search_directories.reserve(options.library_directories.size() + 8);
    for (const auto& directory : options.library_directories) {
        add_search_directory(search_directories, *working_directory, directory);
    }
    add_search_directory(search_directories, *working_directory, *working_directory);
    for (const auto& directory : split_library_path(environment_value(toolchain, "LIB"))) {
        add_search_directory(search_directories, *working_directory, directory);
    }

    ResolvedLibraries result;
    result.files.reserve(options.libraries.size());
    for (const auto& requested : options.libraries) {
        if (requested.empty()) {
            return std::unexpected(failure(
                LibraryResolutionErrorCode::invalid_request,
                requested,
                {},
                "library name must not be empty"));
        }

        const fs::path library = normalized_library_name(requested);
        if (library.has_root_path() || library.has_parent_path()) {
            const fs::path candidate = make_absolute(*working_directory, library);
            if (!regular_file(candidate)) {
                return std::unexpected(failure(
                    LibraryResolutionErrorCode::library_not_found,
                    requested,
                    candidate,
                    "requested library file does not exist"));
            }
            result.files.push_back(candidate);
            continue;
        }

        bool found = false;
        for (const auto& directory : search_directories) {
            const fs::path candidate = (directory / library).lexically_normal();
            if (!regular_file(candidate)) {
                continue;
            }
            result.files.push_back(candidate);
            found = true;
            break;
        }
        if (!found) {
            return std::unexpected(failure(
                LibraryResolutionErrorCode::library_not_found,
                requested,
                library,
                "could not resolve requested library in -L paths, working directory, or MSVC LIB environment"));
        }
    }

    return result;
}

} // namespace mqb::msvc
