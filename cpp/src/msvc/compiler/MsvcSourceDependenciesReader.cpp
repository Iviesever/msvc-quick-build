#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mqb/json/Json.hpp"

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view utf8_bom{"\xef\xbb\xbf"};

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] std::size_t source_offset(
    const std::string_view original,
    const std::size_t target_line,
    const std::size_t target_column) noexcept {
    const std::size_t prefix = original.starts_with(utf8_bom) ? utf8_bom.size() : 0;
    const std::string_view text = original.substr(prefix);

    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (line == target_line && column == target_column) {
            return prefix + index;
        }
        if (text[index] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return original.size();
}

[[nodiscard]] SourceDependenciesError json_error(
    const std::string_view original,
    const json::Error& error) {
    return SourceDependenciesError{
        .code = SourceDependenciesErrorCode::invalid_json,
        .offset = source_offset(original, error.line, error.column),
        .message = error.message,
    };
}

[[nodiscard]] SourceDependenciesError schema_error(
    const SourceDependenciesErrorCode code,
    const std::string_view original,
    const json::Value& value,
    std::string message) {
    return SourceDependenciesError{
        .code = code,
        .offset = source_offset(original, value.line, value.column),
        .message = std::move(message),
    };
}

[[nodiscard]] const json::Value* find_member(
    const json::Value& object,
    const std::string_view name) noexcept {
    const auto iterator = object.object.find(name);
    return iterator == object.object.end() ? nullptr : &iterator->second;
}

[[nodiscard]] std::expected<SourceDependencies, SourceDependenciesError> project_schema(
    const std::string_view original,
    const json::Value& root) {
    if (root.kind != json::Kind::object) {
        return std::unexpected(schema_error(
            SourceDependenciesErrorCode::invalid_json,
            original,
            root,
            "sourceDependencies root must be a JSON object"));
    }

    const json::Value* data = find_member(root, "Data");
    if (data == nullptr) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::missing_data,
            .offset = original.size(),
            .message = "sourceDependencies JSON does not contain Data",
        });
    }
    if (data->kind != json::Kind::object) {
        return std::unexpected(schema_error(
            SourceDependenciesErrorCode::invalid_schema,
            original,
            *data,
            "Data must be a JSON object"));
    }

    const json::Value* source = find_member(*data, "Source");
    if (source == nullptr) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::missing_source,
            .offset = original.size(),
            .message = "sourceDependencies Data.Source is missing or empty",
        });
    }
    if (source->kind != json::Kind::string) {
        return std::unexpected(schema_error(
            SourceDependenciesErrorCode::invalid_schema,
            original,
            *source,
            "Data.Source must be a JSON string"));
    }
    if (source->scalar.empty()) {
        return std::unexpected(schema_error(
            SourceDependenciesErrorCode::missing_source,
            original,
            *source,
            "sourceDependencies Data.Source is missing or empty"));
    }

    SourceDependencies dependencies;
    dependencies.source = path_from_utf8(source->scalar).lexically_normal();

    const json::Value* includes = find_member(*data, "Includes");
    if (includes == nullptr) {
        return dependencies;
    }
    if (includes->kind != json::Kind::array) {
        return std::unexpected(schema_error(
            SourceDependenciesErrorCode::invalid_schema,
            original,
            *includes,
            "Data.Includes must be a JSON array"));
    }

    dependencies.includes.reserve(includes->array.size());
    for (const auto& include : includes->array) {
        if (include.kind != json::Kind::string) {
            return std::unexpected(schema_error(
                SourceDependenciesErrorCode::invalid_schema,
                original,
                include,
                "Data.Includes entries must be JSON strings"));
        }
        dependencies.includes.push_back(path_from_utf8(include.scalar).lexically_normal());
    }
    return dependencies;
}

} // namespace

std::vector<std::filesystem::path> SourceDependencies::all_files() const {
    std::vector<std::filesystem::path> result;
    result.reserve(includes.size() + 1);
    result.push_back(source);
    for (const auto& include : includes) {
        if (std::find(result.begin(), result.end(), include) == result.end()) {
            result.push_back(include);
        }
    }
    return result;
}

std::expected<SourceDependencies, SourceDependenciesError>
MsvcSourceDependenciesReader::parse(const std::string_view text) {
    auto document = json::parse(text);
    if (!document) {
        return std::unexpected(json_error(text, document.error()));
    }
    return project_schema(text, *document);
}

std::expected<SourceDependencies, SourceDependenciesError>
MsvcSourceDependenciesReader::read(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::file_open_failed,
            .file = file,
            .message = "failed to open sourceDependencies JSON",
        });
    }

    std::string bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (stream.bad()) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::file_read_failed,
            .file = file,
            .message = "failed while reading sourceDependencies JSON",
        });
    }

    auto parsed = parse(bytes);
    if (!parsed) {
        auto error = parsed.error();
        error.file = file;
        return std::unexpected(std::move(error));
    }
    return parsed;
}

} // namespace mqb::msvc
