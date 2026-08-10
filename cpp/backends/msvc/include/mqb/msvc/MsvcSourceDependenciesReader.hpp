#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mqb::msvc {

struct SourceDependencies {
    std::filesystem::path source;
    std::vector<std::filesystem::path> includes;

    [[nodiscard]] std::vector<std::filesystem::path> all_files() const;
};

enum class SourceDependenciesErrorCode {
    file_open_failed,
    file_read_failed,
    invalid_json,
    missing_data,
    missing_source,
    invalid_schema,
};

struct SourceDependenciesError {
    SourceDependenciesErrorCode code{SourceDependenciesErrorCode::invalid_json};
    std::filesystem::path file;
    std::size_t offset{};
    std::string message;
};

class MsvcSourceDependenciesReader {
public:
    [[nodiscard]] static std::expected<SourceDependencies, SourceDependenciesError>
    read(const std::filesystem::path& file);

    [[nodiscard]] static std::expected<SourceDependencies, SourceDependenciesError>
    parse(std::string_view json);
};

} // namespace mqb::msvc
