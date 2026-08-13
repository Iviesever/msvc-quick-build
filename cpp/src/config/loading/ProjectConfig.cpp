#include "mqb/config/ProjectConfig.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "mqb/json/Json.hpp"
#include "../schema/ProjectConfigSchema.hpp"

namespace mqb::config {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] Error make_error(
    const ErrorCode code,
    const fs::path& path,
    const std::size_t line,
    const std::size_t column,
    std::string message) {
    return Error{code, path, line, column, std::move(message)};
}

[[nodiscard]] std::expected<std::string, Error> read_file(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::unexpected(make_error(
            ErrorCode::io_error,
            path,
            1,
            1,
            "failed to open project config"));
    }

    std::string text{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (!stream.eof() && stream.fail()) {
        return std::unexpected(make_error(
            ErrorCode::io_error,
            path,
            1,
            1,
            "failed while reading project config"));
    }
    return text;
}

[[nodiscard]] Error parse_error(
    const fs::path& path,
    json::Error error) {
    return make_error(
        ErrorCode::parse_error,
        path,
        error.line,
        error.column,
        std::move(error.message));
}

} // namespace

std::expected<std::optional<fs::path>, Error>
ProjectConfigLoader::find_upwards(const fs::path& start_directory) {
    std::error_code ec;
    fs::path current = fs::absolute(start_directory, ec).lexically_normal();
    if (ec || !fs::is_directory(current, ec) || ec) {
        return std::unexpected(make_error(
            ErrorCode::io_error,
            start_directory,
            1,
            1,
            "project config search must start from an existing directory"));
    }

    while (true) {
        const fs::path candidate = current / "mqb.json";
        ec.clear();
        if (fs::exists(candidate, ec)) {
            if (ec || !fs::is_regular_file(candidate, ec) || ec) {
                return std::unexpected(make_error(
                    ErrorCode::io_error,
                    candidate,
                    1,
                    1,
                    "mqb.json exists but is not a regular file"));
            }
            return std::optional<fs::path>{candidate.lexically_normal()};
        }

        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }

    return std::optional<fs::path>{};
}

std::expected<ProjectConfig, Error>
ProjectConfigLoader::load(const fs::path& requested_file) {
    std::error_code ec;
    fs::path file = fs::absolute(requested_file, ec).lexically_normal();
    if (ec || !fs::is_regular_file(file, ec) || ec) {
        return std::unexpected(make_error(
            ErrorCode::io_error,
            requested_file,
            1,
            1,
            "project config must be an existing regular file"));
    }

    auto text = read_file(file);
    if (!text) {
        return std::unexpected(text.error());
    }

    auto root = json::parse(*text);
    if (!root) {
        return std::unexpected(parse_error(file, std::move(root.error())));
    }

    return detail::decode_project_config(file, *root);
}

} // namespace mqb::config
