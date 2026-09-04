#include "mqb/msvc/MsvcLibraryResolver.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

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

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
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

[[nodiscard]] bool contains_windows_path(
    const std::vector<fs::path>& paths,
    const fs::path& expected) {
    const std::string expected_key =
        mqb::platform::windows::path_identity_key(expected);
    return std::any_of(paths.begin(), paths.end(), [&](const fs::path& path) {
        return mqb::platform::windows::path_identity_key(path) == expected_key;
    });
}

void append_unique_path(std::vector<fs::path>& paths, const fs::path& path) {
    if (!contains_windows_path(paths, path)) {
        paths.push_back(path.lexically_normal());
    }
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
    if (!contains_windows_path(directories, normalized)) {
        directories.push_back(normalized);
    }
}

[[nodiscard]] std::vector<fs::path> search_directories_for(
    const MsvcToolchain& toolchain,
    const std::span<const fs::path> library_directories,
    const fs::path& working_directory) {
    std::vector<fs::path> search_directories;
    search_directories.reserve(library_directories.size() + 8);
    for (const auto& directory : library_directories) {
        add_search_directory(search_directories, working_directory, directory);
    }
    add_search_directory(search_directories, working_directory, working_directory);
    for (const auto& directory : split_library_path(environment_value(toolchain, "LIB"))) {
        add_search_directory(search_directories, working_directory, directory);
    }
    return search_directories;
}

[[nodiscard]] bool search_roots_changed_since(
    const std::vector<fs::path>& search_directories,
    const fs::path& observation_seal_file) {
    if (observation_seal_file.empty()) {
        return true;
    }

    std::error_code error_code;
    mqb::performance::ScopedFilesystemProbe seal_evidence{
        observation_seal_file,
        mqb::performance::FilesystemKind::link};
    const auto sealed = fs::last_write_time(observation_seal_file, error_code);
    if (error_code) {
        return true;
    }

    for (const auto& directory : search_directories) {
        error_code.clear();
        mqb::performance::ScopedFilesystemProbe directory_evidence{
            directory,
            mqb::performance::FilesystemKind::link};
        const auto modified = fs::last_write_time(directory, error_code);
        // Missing/inaccessible search roots need the conservative path. A root
        // can become available later and introduce a higher-priority library.
        if (error_code || modified > sealed) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool regular_file(const fs::path& path) {
    mqb::performance::ScopedFilesystemProbe evidence{
        path,
        mqb::performance::FilesystemKind::link};
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

[[nodiscard]] bool library_path(const fs::path& path) {
    return ascii_iequals(path_to_utf8(path.extension()), ".lib");
}

[[nodiscard]] std::expected<ResolvedLibraries, LibraryResolutionError>
resolve_requests(
    const MsvcToolchain& toolchain,
    const std::span<const std::string> libraries,
    const std::span<const fs::path> library_directories,
    const fs::path& requested_working_directory,
    const bool require_all) {
    auto working_directory = resolve_working_directory(requested_working_directory);
    if (!working_directory) {
        return std::unexpected(working_directory.error());
    }

    const auto search_directories = search_directories_for(
        toolchain,
        library_directories,
        *working_directory);

    ResolvedLibraries result;
    result.files.reserve(libraries.size());
    for (const auto& requested : libraries) {
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
                if (!require_all) {
                    continue;
                }
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
        if (!found && require_all) {
            return std::unexpected(failure(
                LibraryResolutionErrorCode::library_not_found,
                requested,
                library,
                "could not resolve requested library in -L paths, working directory, or MSVC LIB environment"));
        }
    }

    return result;
}

} // namespace

std::expected<ResolvedLibraries, LibraryResolutionError>
MsvcLibraryResolver::resolve(
    const MsvcToolchain& toolchain,
    const LinkOptions& options,
    const fs::path& requested_working_directory) {
    mqb::performance::ScopedWork evidence{
        mqb::performance::WorkKind::link_resolution};
    return resolve_requests(
        toolchain,
        options.libraries,
        options.library_directories,
        requested_working_directory,
        true);
}

std::expected<ResolvedLibraries, LibraryResolutionError>
MsvcLibraryResolver::resolve_available(
    const MsvcToolchain& toolchain,
    const std::span<const std::string> libraries,
    const std::span<const fs::path> library_directories,
    const fs::path& requested_working_directory) {
    mqb::performance::ScopedWork evidence{
        mqb::performance::WorkKind::link_resolution};
    return resolve_requests(
        toolchain,
        libraries,
        library_directories,
        requested_working_directory,
        false);
}

std::expected<ResolvedLibraries, LibraryResolutionError>
MsvcLibraryResolver::refresh_observed(
    const MsvcToolchain& toolchain,
    const std::span<const fs::path> observed_inputs,
    const std::span<const fs::path> library_directories,
    const fs::path& requested_working_directory,
    const fs::path& observation_seal_file) {
    mqb::performance::ScopedWork evidence{
        mqb::performance::WorkKind::link_resolution};
    auto working_directory = resolve_working_directory(requested_working_directory);
    if (!working_directory) {
        return std::unexpected(working_directory.error());
    }
    const auto search_directories = search_directories_for(
        toolchain,
        library_directories,
        *working_directory);
    const bool reroute = search_roots_changed_since(
        search_directories,
        observation_seal_file);

    ResolvedLibraries result;
    result.files.reserve(observed_inputs.size());
    for (const auto& observed_input : observed_inputs) {
        if (!library_path(observed_input)) {
            continue;
        }

        fs::path current = make_absolute(*working_directory, observed_input);
        if (reroute && contains_windows_path(search_directories, current.parent_path())) {
            const std::vector<std::string> basename{path_to_utf8(current.filename())};
            auto rerouted = resolve_requests(
                toolchain,
                basename,
                library_directories,
                *working_directory,
                false);
            if (!rerouted) {
                return std::unexpected(rerouted.error());
            }
            if (!rerouted->files.empty()) {
                current = rerouted->files.front();
            }
        }
        append_unique_path(result.files, current);
    }
    return result;
}

} // namespace mqb::msvc
