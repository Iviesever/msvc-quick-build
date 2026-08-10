#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mqb::modules {

enum class LookupMethod {
    by_name,
    include_angle,
    include_quote,
};

struct ProvidedModule {
    std::string logical_name;
    std::optional<std::filesystem::path> source_path;
    std::optional<std::filesystem::path> compiled_module_path;
    bool unique_on_source_path{false};
    bool is_interface{true};
};

struct RequiredModule {
    std::string logical_name;
    std::optional<std::filesystem::path> source_path;
    std::optional<std::filesystem::path> compiled_module_path;
    bool unique_on_source_path{false};
    LookupMethod lookup_method{LookupMethod::by_name};
};

struct P1689Rule {
    std::optional<std::filesystem::path> work_directory;
    std::optional<std::filesystem::path> primary_output;
    std::vector<std::filesystem::path> outputs;
    std::vector<ProvidedModule> provided_modules;
    std::vector<RequiredModule> required_modules;
};

struct P1689Document {
    int version{1};
    int revision{0};
    std::vector<P1689Rule> rules;
};

enum class P1689ErrorCode {
    parse_error,
    schema_error,
    unsupported_version,
    unsupported_revision,
};

struct P1689Error {
    P1689ErrorCode code{P1689ErrorCode::parse_error};
    std::size_t line{1};
    std::size_t column{1};
    std::string message;
};

class P1689Parser {
public:
    [[nodiscard]] static std::expected<P1689Document, P1689Error>
    parse(std::string_view text);
};

} // namespace mqb::modules
