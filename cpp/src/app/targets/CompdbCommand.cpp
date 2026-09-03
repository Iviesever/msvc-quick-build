#include "CompdbCommand.hpp"

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "BuildIntrospectionSetup.hpp"
#include "Cli.hpp"
#include "Diagnostics.hpp"
#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

struct ParsedCompdbOptions {
    mqb::cli::Options build;
    std::optional<fs::path> output;
};

struct CompilationDatabaseEntry {
    fs::path directory;
    fs::path file;
    fs::path output;
    std::vector<std::string> arguments;
};

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] fs::path absolute_from(
    const fs::path& base,
    const fs::path& path) {
    return path.is_absolute()
        ? path.lexically_normal()
        : (base / path).lexically_normal();
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream escaped;
    escaped << std::hex << std::uppercase;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20u) {
                escaped << "\\u00"
                        << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(ch);
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

[[nodiscard]] std::expected<ParsedCompdbOptions, std::string>
parse_compdb_arguments(const std::span<const std::string_view> arguments) {
    std::vector<std::string_view> forwarded;
    forwarded.reserve(arguments.size() + 1u);
    forwarded.emplace_back("build");

    std::optional<fs::path> output;
    bool native_tail = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (!native_tail && (argument == "/link" || argument == "-link"
                || argument == "/lib" || argument == "/LIB")) {
            native_tail = true;
            forwarded.push_back(argument);
            continue;
        }
        if (!native_tail && (argument == "-o" || argument == "--output")) {
            if (output) {
                return std::unexpected("mqb compdb --output may be specified only once");
            }
            if (index + 1 >= arguments.size() || arguments[index + 1].empty()) {
                return std::unexpected("mqb compdb --output requires a path");
            }
            output = path_from_utf8(arguments[++index]);
            continue;
        }
        if (!native_tail && argument.starts_with("--output=")) {
            if (output) {
                return std::unexpected("mqb compdb --output may be specified only once");
            }
            const std::string_view value = argument.substr(std::string_view{"--output="}.size());
            if (value.empty()) {
                return std::unexpected("mqb compdb --output requires a path");
            }
            output = path_from_utf8(value);
            continue;
        }
        forwarded.push_back(argument);
    }

    auto parsed = mqb::cli::parse_arguments(forwarded);
    if (!parsed) return std::unexpected(parsed.error().message);
    return ParsedCompdbOptions{
        .build = std::move(*parsed),
        .output = std::move(output),
    };
}

[[nodiscard]] std::string render_compilation_database(
    const std::vector<CompilationDatabaseEntry>& entries) {
    std::ostringstream output;
    output << "[\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        output << "  {\n"
               << "    \"directory\": \"" << json_escape(path_to_utf8(entry.directory)) << "\",\n"
               << "    \"file\": \"" << json_escape(path_to_utf8(entry.file)) << "\",\n"
               << "    \"arguments\": [";
        for (std::size_t argument_index = 0;
             argument_index < entry.arguments.size();
             ++argument_index) {
            if (argument_index != 0) output << ", ";
            output << "\"" << json_escape(entry.arguments[argument_index]) << "\"";
        }
        output << "],\n"
               << "    \"output\": \"" << json_escape(path_to_utf8(entry.output)) << "\"\n"
               << "  }";
        if (index + 1 != entries.size()) output << ',';
        output << '\n';
    }
    output << "]\n";
    return output.str();
}

[[nodiscard]] std::expected<void, std::string>
write_database_file(
    const fs::path& output,
    const std::string_view content) {
    std::error_code error_code;
    if (!output.parent_path().empty()) {
        fs::create_directories(output.parent_path(), error_code);
        if (error_code) {
            return std::unexpected("failed to create compilation database output directory");
        }
    }

    fs::path temporary = output;
    temporary += ".tmp-";
    temporary += std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) {
            return std::unexpected("failed to create temporary compilation database file");
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected("failed to write compilation database file");
        }
    }

    fs::rename(temporary, output, error_code);
    if (error_code) {
        error_code.clear();
        fs::remove(output, error_code);
        if (error_code) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected("failed to replace existing compilation database");
        }
        fs::rename(temporary, output, error_code);
    }
    if (error_code) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return std::unexpected("failed to publish compilation database");
    }
    return {};
}

int print_setup_error(const BuildIntrospectionError& error) {
    if (error.config_error) {
        diagnostics::print_config_error(*error.config_error);
    } else {
        diagnostics::print_error(error.message);
    }
    return error.exit_code;
}

} // namespace

int run_compdb_command(const std::span<const std::string_view> arguments) {
    auto parsed = parse_compdb_arguments(arguments);
    if (!parsed) {
        diagnostics::print_error(parsed.error());
        return 2;
    }
    if (parsed->build.show_help) {
        std::cout << compdb_usage();
        return 0;
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    auto context = prepare_build_introspection(
        std::move(parsed->build),
        runner);
    if (!context) return print_setup_error(context.error());

    if (context->module_target) {
        diagnostics::print_error(
            "mqb compdb currently supports ordinary C/C++ targets only; "
            "Modules/Header Units require P1689 graph materialization and are intentionally fail-closed");
        return 2;
    }

    const mqb::CompilerOptions compiler_options = context->consumer_compiler_options();
    mqb::msvc::MsvcCompileExecutor executor{context->toolchain, runner};

    std::vector<CompilationDatabaseEntry> entries;
    entries.reserve(context->target_sources.size());
    for (const auto& source : context->target_sources) {
        const auto kind = mqb::classify_translation_unit_path(source.source);
        if (!kind || *kind != mqb::TranslationUnitKind::source) {
            diagnostics::print_error(
                "mqb compdb encountered a non-ordinary translation unit after ordinary-target validation");
            return 2;
        }

        mqb::TranslationUnit unit;
        unit.source = source.source;
        unit.kind = *kind;
        unit.outputs = {
            mqb::Artifact{source.artifacts.object, mqb::ArtifactKind::object},
        };
        // Match the ordinary target coordinator exactly: each TU is compiled
        // from its source directory, not from the project root.
        const mqb::msvc::CompileExecutionRequest request{
            .unit = std::move(unit),
            .options = compiler_options,
            .source_dependencies_file = source.artifacts.dependencies,
            .working_directory = source.source.parent_path(),
        };
        auto recipe = executor.build_recipe(request);
        if (!recipe) {
            diagnostics::print_error(
                "failed to model compile recipe: " + recipe.error().message);
            return 2;
        }

        CompilationDatabaseEntry entry;
        entry.directory = absolute_from(
            context->invocation.directory,
            recipe->process.working_directory.value_or(source.source.parent_path()));
        entry.file = absolute_from(context->invocation.directory, source.source);
        entry.output = absolute_from(context->project.project_root, source.artifacts.object);
        entry.arguments.reserve(recipe->process.arguments.size() + 1u);
        entry.arguments.push_back(path_to_utf8(recipe->process.executable));
        entry.arguments.insert(
            entry.arguments.end(),
            recipe->process.arguments.begin(),
            recipe->process.arguments.end());
        entries.push_back(std::move(entry));
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const CompilationDatabaseEntry& left, const CompilationDatabaseEntry& right) {
            return mqb::platform::windows::path_identity_key(left.file)
                < mqb::platform::windows::path_identity_key(right.file);
        });

    const fs::path output = parsed->output
        ? absolute_from(context->invocation.directory, *parsed->output)
        : (context->project.project_root / "compile_commands.json").lexically_normal();
    const std::string content = render_compilation_database(entries);
    auto written = write_database_file(output, content);
    if (!written) {
        diagnostics::print_error(written.error());
        return 2;
    }

    std::cout << "[compdb] " << entries.size() << " translation units\n"
              << "output: " << path_to_utf8(output) << '\n';
    return 0;
}

std::string_view compdb_usage() noexcept {
    return R"(Usage:
  mqb compdb [entry.cpp] [options] [MSVC-compiler-options]
  mqb compdb [source...] [options] [MSVC-compiler-options]

Generate a deterministic compile_commands.json from MQB's exact MSVC compile recipes.
The command performs project/config resolution, source discovery, artifact layout and toolchain
discovery, but does not compile, link, archive, or run the target.

Options are the same compile-side options accepted by `mqb build`.
For this command only:
  -o, --output <path>      Compilation database path (default: <project>/compile_commands.json)

The first release supports ordinary C/C++ targets, including MQB-owned PCH consumer recipes.
Targets requiring the Modules/Header Units P1689 pipeline fail closed until graph-aware compdb
materialization is added in a follow-up iteration.
)";
}

} // namespace mqb::app
