#include "Cli.hpp"

#include <charconv>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace mqb::cli {
namespace {

[[nodiscard]] Error error(std::string message) {
    return Error{.message = std::move(message)};
}

[[nodiscard]] std::expected<std::string_view, Error>
require_value(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    const std::string_view option) {
    if (index + 1 >= arguments.size()) {
        return std::unexpected(error("missing value for " + std::string{option}));
    }
    ++index;
    if (arguments[index].empty()) {
        return std::unexpected(error("empty value for " + std::string{option}));
    }
    return arguments[index];
}

[[nodiscard]] std::expected<CppStandard, Error>
parse_standard(const std::string_view value) {
    if (value == "20" || value == "c++20") return CppStandard::cpp20;
    if (value == "23" || value == "c++23") return CppStandard::cpp23;
    if (value == "latest" || value == "c++latest") return CppStandard::latest;
    return std::unexpected(error(
        "unsupported C++ standard '" + std::string{value}
        + "' (expected 20, 23, or latest)"));
}

[[nodiscard]] std::expected<std::size_t, Error>
parse_jobs(const std::string_view value) {
    std::size_t jobs = 0;
    const char* const begin = value.data();
    const char* const end = value.data() + value.size();
    const auto [parsed_end, parse_error] = std::from_chars(begin, end, jobs);
    if (parse_error != std::errc{} || parsed_end != end || jobs == 0) {
        return std::unexpected(error(
            "invalid compile job count '" + std::string{value}
            + "' (expected a positive integer)"));
    }
    return jobs;
}

[[nodiscard]] std::expected<msvc::ToolchainPreference, Error>
parse_toolchain_preference(const std::string_view value) {
    if (value == "auto") return msvc::ToolchainPreference::automatic;
    if (value == "vs") return msvc::ToolchainPreference::visual_studio;
    if (value == "portable") return msvc::ToolchainPreference::portable;
    return std::unexpected(error(
        "unsupported toolchain preference '" + std::string{value}
        + "' (expected auto, vs, or portable)"));
}

[[nodiscard]] std::expected<std::string_view, Error>
attached_or_next(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    const std::string_view argument,
    const std::string_view option) {
    if (argument.size() > option.size()) return argument.substr(option.size());
    return require_value(arguments, index, option);
}

[[nodiscard]] std::expected<std::string_view, Error>
long_equals_value(
    const std::string_view argument,
    const std::string_view prefix) {
    const std::string_view value = argument.substr(prefix.size());
    if (value.empty()) {
        return std::unexpected(error(
            "empty value for " + std::string{prefix.substr(0, prefix.size() - 1)}));
    }
    return value;
}

} // namespace

std::expected<Options, Error>
parse_arguments(const std::span<const std::string_view> arguments) {
    Options options;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];

        if (argument == "--") {
            for (++index; index < arguments.size(); ++index) {
                options.build.run_arguments.emplace_back(arguments[index]);
            }
            break;
        }
        if (argument == "-h" || argument == "--help") {
            options.show_help = true;
            continue;
        }
        if (argument == "--verbose" || argument == "-v") {
            options.verbose = true;
            continue;
        }
        if (argument == "--discover") {
            options.discover_sources = true;
            options.discovery_override = true;
            continue;
        }
        if (argument == "--no-discover") {
            options.discover_sources = false;
            options.discovery_override = false;
            continue;
        }
        if (argument == "--run") {
            options.build.run_after_build = true;
            continue;
        }
        if (argument == "-j" || argument == "--jobs") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto jobs = parse_jobs(*value);
            if (!jobs) return std::unexpected(jobs.error());
            options.jobs = *jobs;
            continue;
        }
        if (argument.starts_with("--jobs=")) {
            auto value = long_equals_value(argument, "--jobs=");
            if (!value) return std::unexpected(value.error());
            auto jobs = parse_jobs(*value);
            if (!jobs) return std::unexpected(jobs.error());
            options.jobs = *jobs;
            continue;
        }
        if (argument.starts_with("-j") && argument.size() > 2) {
            auto jobs = parse_jobs(argument.substr(2));
            if (!jobs) return std::unexpected(jobs.error());
            options.jobs = *jobs;
            continue;
        }
        if (argument == "-o" || argument == "--output") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.build.output_name = std::string{*value};
            continue;
        }
        if (argument.starts_with("--output=")) {
            auto value = long_equals_value(argument, "--output=");
            if (!value) return std::unexpected(value.error());
            options.build.output_name = std::string{*value};
            continue;
        }
        if (argument == "--debug") {
            options.build.configuration = BuildConfiguration::debug;
            options.configuration_override = BuildConfiguration::debug;
            continue;
        }
        if (argument == "--release") {
            options.build.configuration = BuildConfiguration::release;
            options.configuration_override = BuildConfiguration::release;
            continue;
        }
        if (argument == "--x86") {
            options.build.architecture = Architecture::x86;
            options.architecture_override = Architecture::x86;
            continue;
        }
        if (argument == "--x64") {
            options.build.architecture = Architecture::x64;
            options.architecture_override = Architecture::x64;
            continue;
        }
        if (argument == "--std") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto standard = parse_standard(*value);
            if (!standard) return std::unexpected(standard.error());
            options.build.standard = *standard;
            options.standard_override = *standard;
            continue;
        }
        if (argument == "--env") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto preference = parse_toolchain_preference(*value);
            if (!preference) return std::unexpected(preference.error());
            options.toolchain_preference = *preference;
            continue;
        }
        if (argument == "--portable-root") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.portable_roots.emplace_back(std::string{*value});
            continue;
        }
        if (argument == "--lib-path") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.library_directories.emplace_back(std::string{*value});
            continue;
        }
        if (argument.starts_with("--lib-path=")) {
            auto value = long_equals_value(argument, "--lib-path=");
            if (!value) return std::unexpected(value.error());
            options.library_directories.emplace_back(std::string{*value});
            continue;
        }
        if (argument == "--lib") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.libraries.emplace_back(*value);
            continue;
        }
        if (argument.starts_with("--lib=")) {
            auto value = long_equals_value(argument, "--lib=");
            if (!value) return std::unexpected(value.error());
            options.libraries.emplace_back(*value);
            continue;
        }
        if (argument == "-I" || argument.starts_with("-I")) {
            auto value = attached_or_next(arguments, index, argument, "-I");
            if (!value) return std::unexpected(value.error());
            if (value->empty()) return std::unexpected(error("empty include directory"));
            options.include_directories.emplace_back(std::string{*value});
            continue;
        }
        if (argument == "-D" || argument.starts_with("-D")) {
            auto value = attached_or_next(arguments, index, argument, "-D");
            if (!value) return std::unexpected(value.error());
            if (value->empty()) return std::unexpected(error("empty preprocessor define"));
            options.defines.emplace_back(*value);
            continue;
        }
        if (argument == "-L" || argument.starts_with("-L")) {
            auto value = attached_or_next(arguments, index, argument, "-L");
            if (!value) return std::unexpected(value.error());
            if (value->empty()) return std::unexpected(error("empty library directory"));
            options.library_directories.emplace_back(std::string{*value});
            continue;
        }
        if (argument == "-l" || argument.starts_with("-l")) {
            auto value = attached_or_next(arguments, index, argument, "-l");
            if (!value) return std::unexpected(value.error());
            if (value->empty()) return std::unexpected(error("empty library name"));
            options.libraries.emplace_back(*value);
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            return std::unexpected(error("unknown option '" + std::string{argument} + "'"));
        }

        options.build.sources.emplace_back(std::string{argument});
    }

    if (!options.show_help && options.build.sources.empty()) {
        return std::unexpected(error("missing source file"));
    }
    if (!options.show_help
        && !options.build.run_after_build
        && !options.build.run_arguments.empty()) {
        return std::unexpected(error("arguments after -- require --run"));
    }
    return options;
}

std::string_view usage() noexcept {
    return R"(MQB - MSVC Quick Build (C++ V2)

Usage:
  mqb <entry.cpp> [options] [-- program-args...]
  mqb <source.cpp> <more-sources...> [options] [-- program-args...]

Project configuration:
  MQB searches upward from the invocation directory for the nearest mqb.json.
  Scalar precedence is: explicit CLI > mqb.json > built-in defaults.
  Config paths are relative to mqb.json; CLI paths are relative to the invocation directory.

Source selection:
  One positional source       Smart-discover connected ordinary C++ TUs (default)
  Multiple positional sources Build exactly that explicit ordered source set
  --discover                  Explicitly enable smart discovery for one source
  --no-discover               Disable smart discovery for a single source

Options:
  --debug                  Explicitly select Debug compile/link preset
  --release                Explicitly select Release compile/link preset
  --std <20|23|latest>     Explicitly select C++ language standard
  --x86 | --x64            Explicitly select target architecture
  -j, --jobs <N>           Maximum concurrent TU compiles (default: hardware concurrency)
  -o, --output <name>      Set target executable name under .mqb/bin/
  --run                    Run the executable after a successful build
  -I <dir>, -I<dir>        Add an include directory
  -D <value>, -D<value>    Add a preprocessor definition
  -L <dir>, -L<dir>        Add a library search directory
  --lib-path <dir>         Add a library search directory
  -l <name>, -l<name>      Link a library ('.lib' is optional)
  --lib <name>             Link a library (name or explicit path)
  --env <auto|vs|portable> Toolchain selection (default: auto)
  --portable-root <dir>    Add a portable_msvc root candidate
  -v, --verbose            Show config, discovery, toolchain, and artifact details
  -h, --help               Show this help
  --                       Pass all remaining argv elements to the program

Compile job count is execution policy only; changing -j does not invalidate build caches.
Discovery is source selection only. Incremental header freshness continues to use
MSVC /sourceDependencies metadata.

Generated state:
  .mqb/obj/    collision-free object files
  .mqb/deps/   compiler dependency metadata
  .mqb/cache/  compile and link cache metadata
  .mqb/bin/    linked executable
)";
}

} // namespace mqb::cli
