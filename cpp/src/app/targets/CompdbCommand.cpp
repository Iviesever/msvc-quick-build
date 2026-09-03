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
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"
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

[[nodiscard]] std::optional<fs::path>
primary_compile_output(const mqb::TranslationUnit& unit) {
    const mqb::ArtifactKind preferred = unit.header_unit
        ? mqb::ArtifactKind::module_interface
        : mqb::ArtifactKind::object;
    const auto found = std::find_if(
        unit.outputs.begin(),
        unit.outputs.end(),
        [&](const mqb::Artifact& output) {
            return output.kind == preferred;
        });
    if (found != unit.outputs.end()) return found->path;
    if (!unit.outputs.empty()) return unit.outputs.front().path;
    return std::nullopt;
}

[[nodiscard]] mqb::msvc::CompileExecutionRequest execution_request_for(
    const mqb::orchestration::IncrementalCompileRequest& request) {
    return mqb::msvc::CompileExecutionRequest{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };
}

[[nodiscard]] std::expected<CompilationDatabaseEntry, std::string>
make_database_entry(
    const BuildIntrospectionContext& context,
    mqb::msvc::MsvcCompileExecutor& executor,
    const mqb::msvc::CompileExecutionRequest& request) {
    auto recipe = executor.build_recipe(request);
    if (!recipe) {
        return std::unexpected(
            "failed to model compile recipe: " + recipe.error().message);
    }
    const auto primary_output = primary_compile_output(request.unit);
    if (!primary_output) {
        return std::unexpected(
            "compile recipe has no primary output: "
            + diagnostics::path_text(request.unit.source));
    }

    CompilationDatabaseEntry entry;
    entry.directory = absolute_from(
        context.invocation.directory,
        recipe->process.working_directory.value_or(
            request.unit.source.parent_path()));
    entry.file = absolute_from(context.invocation.directory, request.unit.source);
    entry.output = absolute_from(context.project.project_root, *primary_output);
    entry.arguments.reserve(recipe->process.arguments.size() + 1u);
    entry.arguments.push_back(path_to_utf8(recipe->process.executable));
    entry.arguments.insert(
        entry.arguments.end(),
        recipe->process.arguments.begin(),
        recipe->process.arguments.end());
    return entry;
}

void sort_entries(std::vector<CompilationDatabaseEntry>& entries) {
    std::sort(
        entries.begin(),
        entries.end(),
        [](const CompilationDatabaseEntry& left, const CompilationDatabaseEntry& right) {
            const std::string left_file =
                mqb::platform::windows::path_identity_key(left.file);
            const std::string right_file =
                mqb::platform::windows::path_identity_key(right.file);
            if (left_file != right_file) return left_file < right_file;
            return mqb::platform::windows::path_identity_key(left.output)
                < mqb::platform::windows::path_identity_key(right.output);
        });
}

[[nodiscard]] mqb::LinkOptions make_module_link_options(
    const BuildIntrospectionContext& context) {
    mqb::LinkOptions options;
    options.configuration = context.options.build.configuration;
    options.architecture = context.options.build.architecture;
    options.target_kind = context.options.build.target_kind;
    options.subsystem = context.options.subsystem_override.value_or(
        mqb::LinkSubsystem::console);
    options.link_time_code_generation =
        context.project.effective.link_time_code_generation;
    options.library_directories = context.options.library_directories;
    options.libraries = context.options.libraries;
    options.additional_arguments = context.options.linker_arguments;
    return options;
}

[[nodiscard]] int append_module_entries(
    const BuildIntrospectionContext& context,
    mqb::platform::windows::WindowsProcessRunner& runner,
    std::vector<CompilationDatabaseEntry>& entries) {
    if (context.options.build.target_kind == mqb::TargetKind::static_library) {
        diagnostics::print_error(
            "static-library targets do not yet support the Modules/Header Unit pipeline");
        return 2;
    }

    std::vector<mqb::orchestration::ModuleCompileSourceRequest> sources;
    sources.reserve(context.target_sources.size());
    for (const auto& source : context.target_sources) {
        const auto kind = mqb::classify_translation_unit_path(source.source);
        if (!kind) {
            diagnostics::print_error(
                "unsupported translation unit in module compilation database: "
                + diagnostics::path_text(source.source));
            return 2;
        }
        sources.push_back(mqb::orchestration::ModuleCompileSourceRequest{
            .source = source.source,
            .artifacts = source.artifacts,
            .kind = *kind,
        });
    }

    mqb::msvc::MsvcCompileExecutor executor{context.toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{
        context.toolchain,
        executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{
        incremental_compile};
    // MsvcModuleTargetCoordinator owns both compile-side and full target
    // inspection. The link coordinator is constructed for that stable facade,
    // but inspect_compilation() deliberately does not resolve or inspect link
    // policy, so compdb cannot fail because of unrelated linker inputs.
    mqb::msvc::MsvcLinker linker{context.toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{
        context.toolchain,
        linker};
    mqb::msvc::MsvcModuleDependencyScanner scanner{context.toolchain, runner};
    mqb::orchestration::MsvcModuleTargetCoordinator target{
        scanner,
        module_compile,
        incremental_link};

    const mqb::orchestration::ParallelismPolicy parallelism =
        context.options.jobs.value_or(
            mqb::orchestration::ParallelismPolicy::automatic());
    const mqb::orchestration::IncrementalModuleTargetRequest request{
        .sources = std::move(sources),
        .target = context.target_artifacts,
        .artifact_layout = context.layout,
        .compiler_options = context.compiler_options,
        .link_options = make_module_link_options(context),
        .working_directory = context.project.project_root,
        .max_parallel_scans = parallelism,
        .max_parallel_compiles = parallelism,
    };

    auto inspected = target.inspect_compilation(request);
    if (!inspected) {
        diagnostics::print_module_target_failure(inspected.error());
        return 4;
    }
    if (!inspected->graph_ready()) {
        diagnostics::print_error(
            "mqb compdb requires reusable P1689 topology for Modules/Header Units; "
            "run a successful mqb build first, or use mqb plan to inspect the pending scans");
        for (const auto& scan : inspected->scans) {
            if (!scan.result.scan_required()) continue;
            std::string message = "pending module scan: ";
            message += diagnostics::path_text(scan.source);
            if (!scan.result.reasons.empty()) {
                message += " [";
                for (std::size_t index = 0;
                     index < scan.result.reasons.size();
                     ++index) {
                    if (index != 0) message += ", ";
                    message += mqb::orchestration::to_string(
                        scan.result.reasons[index]);
                }
                message += ']';
            }
            diagnostics::print_warning(message);
        }
        return 2;
    }
    if (!inspected->compiles) {
        diagnostics::print_error(
            "module compilation inspection completed without compile-wave results");
        return 4;
    }

    entries.reserve(
        inspected->compiles->compiles.size()
        + inspected->compiles->header_unit_compiles.size());
    for (const auto& compile : inspected->compiles->compiles) {
        auto entry = make_database_entry(
            context,
            executor,
            execution_request_for(compile.request));
        if (!entry) {
            diagnostics::print_error(entry.error());
            return 2;
        }
        entries.push_back(std::move(*entry));
    }
    for (const auto& header : inspected->compiles->header_unit_compiles) {
        auto entry = make_database_entry(
            context,
            executor,
            execution_request_for(header.request));
        if (!entry) {
            diagnostics::print_error(entry.error());
            return 2;
        }
        entries.push_back(std::move(*entry));
    }
    return 0;
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

    std::vector<CompilationDatabaseEntry> entries;
    if (context->module_target) {
        const int module_result = append_module_entries(*context, runner, entries);
        if (module_result != 0) return module_result;
    } else {
        const mqb::CompilerOptions compiler_options =
            context->consumer_compiler_options();
        mqb::msvc::MsvcCompileExecutor executor{context->toolchain, runner};

        entries.reserve(context->target_sources.size());
        for (const auto& source : context->target_sources) {
            const auto kind = mqb::classify_translation_unit_path(source.source);
            if (!kind || *kind != mqb::TranslationUnitKind::source) {
                diagnostics::print_error(
                    "mqb compdb encountered a non-ordinary translation unit "
                    "after ordinary-target validation");
                return 2;
            }

            mqb::TranslationUnit unit;
            unit.source = source.source;
            unit.kind = *kind;
            unit.outputs = {
                mqb::Artifact{
                    source.artifacts.object,
                    mqb::ArtifactKind::object},
            };
            // Match the ordinary target coordinator exactly: each TU is
            // compiled from its source directory, not from the project root.
            const mqb::msvc::CompileExecutionRequest request{
                .unit = std::move(unit),
                .options = compiler_options,
                .source_dependencies_file = source.artifacts.dependencies,
                .working_directory = source.source.parent_path(),
            };
            auto entry = make_database_entry(*context, executor, request);
            if (!entry) {
                diagnostics::print_error(entry.error());
                return 2;
            }
            entries.push_back(std::move(*entry));
        }
    }

    sort_entries(entries);

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
discovery, but does not compile, scan, link, archive, or run the target.

Options are the same compile-side options accepted by `mqb build`.
For this command only:
  -o, --output <path>      Compilation database path (default: <project>/compile_commands.json)

Ordinary C/C++ targets include MQB-owned PCH consumer recipes. Modules and Header Units are
exported when their complete P1689 topology is reusable from a prior successful build, including
project/module consumers, Header Unit producers, and selected toolchain-owned std/std.compat
providers. Cold or stale module topology fails closed without publishing partial JSON; use
`mqb plan` to inspect pending scans, then run `mqb build` once to materialize trustworthy P1689.
)";
}

} // namespace mqb::app
