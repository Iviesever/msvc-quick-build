#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mqb::discovery {

enum class WarningCode {
    file_read_failed,
};

struct Warning {
    WarningCode code{WarningCode::file_read_failed};
    std::filesystem::path path;
    std::string message;
};

enum class ErrorCode {
    invalid_project_root,
    invalid_entry,
    invalid_correction,
    enumeration_failed,
};

struct Error {
    ErrorCode code{ErrorCode::invalid_project_root};
    std::filesystem::path path;
    std::string message;
};

struct Request {
    std::filesystem::path project_root;
    std::filesystem::path entry;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> excluded_directories;
    std::vector<std::filesystem::path> extra_sources;
    std::vector<std::filesystem::path> excluded_sources;
};

struct Result {
    std::vector<std::filesystem::path> sources;
    std::size_t indexed_files{};
    std::vector<Warning> warnings;
};

class SourceDiscovery {
public:
    [[nodiscard]] static std::expected<Result, Error>
    discover(const Request& request);
};

} // namespace mqb::discovery
