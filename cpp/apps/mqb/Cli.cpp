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

#ifndef MQB_VERSION
#define MQB_VERSION "0.0.0-dev"
#endif

namespace mqb::cli {
namespace {

[[nodiscard]] Error error(std::string message) { return Error{.message = std::move(message)}; }

[[nodiscard]] std::expected<std::string_view, Error> require_value(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    const std::string_view option) {
    if (index + 1 >= arguments.size()) return std::unexpected(error("missing value for " + std::string{option}));
    ++index;
    if (arguments[index].empty()) return std::unexpected(error("empty value for " + std::string{option}));
    return arguments[index];
}

[[nodiscard]] std::expected<CppStandard, Error> parse_standard(const std::string_view value) {
    if (value == "14" || value == "c++14") return CppStandard::cpp14;
    if (value == "17" || value == "c++17") return CppStandard::cpp17;
    if (value == "20" || value == "c++20") return CppStandard::cpp20;
    if (value == "23" || value == "c++23") return CppStandard::cpp23;
    if (value == "latest" || value == "c++latest") return CppStandard::latest;
    return std::unexpected(error("unsupported C++ standard '" + std::string{value}
        + "' (expected 14, 17, 20, 23, or latest)"));
}

[[nodiscard]] std::expected<BuildConfiguration, Error> parse_configuration(const std::string_view value) {
    if (value == "debug") return BuildConfiguration::debug;
    if (value == "release") return BuildConfiguration::release;
    return std::unexpected(error("unsupported build configuration '" + std::string{value}
        + "' (expected debug or release)"));
}

[[nodiscard]] std::expected<RuntimeLibrary, Error> parse_runtime(const std::string_view value) {
    if (value == "MD" || value == "md") return RuntimeLibrary::md;
    if (value == "MDd" || value == "mdd") return RuntimeLibrary::mdd;
    if (value == "MT" || value == "mt") return RuntimeLibrary::mt;
    if (value == "MTd" || value == "mtd") return RuntimeLibrary::mtd;
    return std::unexpected(error("unsupported runtime library '" + std::string{value}
        + "' (expected MD, MDd, MT, or MTd)"));
}

[[nodiscard]] std::expected<LinkSubsystem, Error> parse_subsystem(const std::string_view value) {
    if (value == "console") return LinkSubsystem::console;
    if (value == "windows") return LinkSubsystem::windows;
    return std::unexpected(error("unsupported subsystem '" + std::string{value}
        + "' (expected console or windows)"));
}

[[nodiscard]] std::expected<std::size_t, Error> parse_jobs(const std::string_view value) {
    std::size_t jobs = 0;
    const auto [parsed_end, parse_error] = std::from_chars(value.data(), value.data() + value.size(), jobs);
    if (parse_error != std::errc{} || parsed_end != value.data() + value.size() || jobs == 0) {
        return std::unexpected(error("invalid compile job count '" + std::string{value}
            + "' (expected a positive integer)"));
    }
    return jobs;
}

[[nodiscard]] std::expected<msvc::ToolchainPreference, Error> parse_toolchain_preference(const std::string_view value) {
    if (value == "auto") return msvc::ToolchainPreference::automatic;
    if (value == "vs") return msvc::ToolchainPreference::visual_studio;
    if (value == "portable" || value == "port" || value == "p") return msvc::ToolchainPreference::portable;
    return std::unexpected(error("unsupported toolchain preference '" + std::string{value}
        + "' (expected auto, vs, or portable)"));
}

[[nodiscard]] std::expected<std::string_view, Error> attached_or_next(
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    const std::string_view argument,
    const std::string_view option) {
    if (argument.size() > option.size()) return argument.substr(option.size());
    return require_value(arguments, index, option);
}

[[nodiscard]] std::expected<std::string_view, Error> long_equals_value(
    const std::string_view argument,
    const std::string_view prefix) {
    const std::string_view value = argument.substr(prefix.size());
    if (value.empty()) return std::unexpected(error("empty value for " + std::string{prefix.substr(0, prefix.size() - 1)}));
    return value;
}

} // namespace

std::expected<Options, Error> parse_arguments(const std::span<const std::string_view> arguments) {
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--") {
            for (++index; index < arguments.size(); ++index) options.build.run_arguments.emplace_back(arguments[index]);
            break;
        }
        if (argument == "-h" || argument == "--help" || argument == "-help" || argument == "-?") { options.show_help = true; continue; }
        if (argument == "--verbose" || argument == "-v") { options.verbose = true; continue; }
        if (argument == "--discover") { options.discover_sources = true; options.discovery_override = true; continue; }
        if (argument == "--no-discover") { options.discover_sources = false; options.discovery_override = false; continue; }
        if (argument == "--run" || argument == "-run") { options.build.run_after_build = true; continue; }
        if (argument == "-j" || argument == "--jobs") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            auto parsed = parse_jobs(*value); if (!parsed) return std::unexpected(parsed.error()); options.jobs = *parsed; continue;
        }
        if (argument.starts_with("--jobs=")) {
            auto value = long_equals_value(argument, "--jobs="); if (!value) return std::unexpected(value.error());
            auto parsed = parse_jobs(*value); if (!parsed) return std::unexpected(parsed.error()); options.jobs = *parsed; continue;
        }
        if (argument.starts_with("-j") && argument.size() > 2) {
            auto parsed = parse_jobs(argument.substr(2)); if (!parsed) return std::unexpected(parsed.error()); options.jobs = *parsed; continue;
        }
        if (argument == "-o" || argument == "--output" || argument == "-output") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            options.build.output_name = std::string{*value}; continue;
        }
        if (argument.starts_with("--output=")) {
            auto value = long_equals_value(argument, "--output="); if (!value) return std::unexpected(value.error());
            options.build.output_name = std::string{*value}; continue;
        }
        if (argument == "--debug") { options.build.configuration = BuildConfiguration::debug; options.configuration_override = BuildConfiguration::debug; continue; }
        if (argument == "--release") { options.build.configuration = BuildConfiguration::release; options.configuration_override = BuildConfiguration::release; continue; }
        if (argument == "--config" || argument == "-config") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            auto parsed = parse_configuration(*value); if (!parsed) return std::unexpected(parsed.error());
            options.build.configuration = *parsed; options.configuration_override = *parsed; continue;
        }
        if (argument == "--x86" || argument == "-x86") { options.build.architecture = Architecture::x86; options.architecture_override = Architecture::x86; continue; }
        if (argument == "--x64" || argument == "-x64") { options.build.architecture = Architecture::x64; options.architecture_override = Architecture::x64; continue; }
        if (argument == "--std" || argument == "-std") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            auto parsed = parse_standard(*value); if (!parsed) return std::unexpected(parsed.error());
            options.build.standard = *parsed; options.standard_override = *parsed; continue;
        }
        if (argument == "--runtime" || argument == "-runtime") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            auto parsed = parse_runtime(*value); if (!parsed) return std::unexpected(parsed.error());
            options.runtime_override = *parsed; continue;
        }
        if (argument.starts_with("--runtime=")) {
            auto value = long_equals_value(argument, "--runtime="); if (!value) return std::unexpected(value.error());
            auto parsed = parse_runtime(*value); if (!parsed) return std::unexpected(parsed.error());
            options.runtime_override = *parsed; continue;
        }
        if (argument == "--subsystem" || argument == "-subsystem") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            auto parsed = parse_subsystem(*value); if (!parsed) return std::unexpected(parsed.error());
            options.subsystem_override = *parsed; continue;
        }
        if (argument.starts_with("--subsystem=")) {
            auto value = long_equals_value(argument, "--subsystem="); if (!value) return std::unexpected(value.error());
            auto parsed = parse_subsystem(*value); if (!parsed) return std::unexpected(parsed.error());
            options.subsystem_override = *parsed; continue;
        }
        if (argument == "--compiler-arg" || argument == "-flags") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.compiler_arguments.emplace_back(*value); continue;
        }
        if (argument.starts_with("--compiler-arg=")) {
            auto value = long_equals_value(argument, "--compiler-arg="); if (!value) return std::unexpected(value.error()); options.compiler_arguments.emplace_back(*value); continue;
        }
        if (argument == "--linker-arg" || argument == "-link_flags") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.linker_arguments.emplace_back(*value); continue;
        }
        if (argument.starts_with("--linker-arg=")) {
            auto value = long_equals_value(argument, "--linker-arg="); if (!value) return std::unexpected(value.error()); options.linker_arguments.emplace_back(*value); continue;
        }
        if (argument == "--env" || argument == "-env") {
            auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error());
            auto parsed = parse_toolchain_preference(*value); if (!parsed) return std::unexpected(parsed.error()); options.toolchain_preference = *parsed; continue;
        }
        if (argument == "--portable-root") { auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.portable_roots.emplace_back(std::string{*value}); continue; }
        if (argument == "--lib-path" || argument == "-libpath") { auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.library_directories.emplace_back(std::string{*value}); continue; }
        if (argument.starts_with("--lib-path=")) { auto value = long_equals_value(argument, "--lib-path="); if (!value) return std::unexpected(value.error()); options.library_directories.emplace_back(std::string{*value}); continue; }
        if (argument == "--lib" || argument == "-libs") { auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.libraries.emplace_back(*value); continue; }
        if (argument.starts_with("--lib=")) { auto value = long_equals_value(argument, "--lib="); if (!value) return std::unexpected(value.error()); options.libraries.emplace_back(*value); continue; }
        if (argument == "-include") { auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.include_directories.emplace_back(std::string{*value}); continue; }
        if (argument == "-defines") { auto value = require_value(arguments, index, argument); if (!value) return std::unexpected(value.error()); options.defines.emplace_back(*value); continue; }
        if (argument == "-I" || argument.starts_with("-I")) { auto value = attached_or_next(arguments, index, argument, "-I"); if (!value) return std::unexpected(value.error()); if (value->empty()) return std::unexpected(error("empty include directory")); options.include_directories.emplace_back(std::string{*value}); continue; }
        if (argument == "-D" || argument.starts_with("-D")) { auto value = attached_or_next(arguments, index, argument, "-D"); if (!value) return std::unexpected(value.error()); if (value->empty()) return std::unexpected(error("empty preprocessor define")); options.defines.emplace_back(*value); continue; }
        if (argument == "-L" || argument.starts_with("-L")) { auto value = attached_or_next(arguments, index, argument, "-L"); if (!value) return std::unexpected(value.error()); if (value->empty()) return std::unexpected(error("empty library directory")); options.library_directories.emplace_back(std::string{*value}); continue; }
        if (argument == "-l" || argument.starts_with("-l")) { auto value = attached_or_next(arguments, index, argument, "-l"); if (!value) return std::unexpected(value.error()); if (value->empty()) return std::unexpected(error("empty library name")); options.libraries.emplace_back(*value); continue; }
        if (!argument.empty() && argument.front() == '-') return std::unexpected(error("unknown option '" + std::string{argument} + "'"));
        options.build.sources.emplace_back(std::string{argument});
    }
    if (!options.show_help && options.build.sources.empty()) return std::unexpected(error("missing source file"));
    if (!options.show_help && !options.build.run_after_build && !options.build.run_arguments.empty()) return std::unexpected(error("arguments after -- require --run"));
    return options;
}

std::string_view usage() noexcept {
    return "MQB " MQB_VERSION " - MSVC Quick Build (C++ refactor)\n\n"
R"(Usage:
  mqb <entry.cpp> [options] [-- program-args...]
  mqb <source.cpp> <more-sources...|module.ixx...> [options] [-- program-args...]

Options:
  --debug | --release      Select compile/link preset
  --config <debug|release> Legacy -config alias accepted
  --std <14|17|20|23|latest>
                           Select C++ standard (legacy -std accepted)
  --runtime <MD|MDd|MT|MTd>
                           Select MSVC CRT runtime (legacy -runtime accepted)
  --subsystem <console|windows>
                           Select executable subsystem (legacy -subsystem accepted)
  --x86 | --x64            Select target architecture
  -j, --jobs <N>           Maximum concurrent TU scans/compiles
  -o, --output <name>      Set target executable name under .mqb/bin/
  --run                    Run executable after a successful build
  -I <dir>, -D <value>     Include directory / preprocessor definition
  -L <dir>, -l <name>      Library directory / library
  --compiler-arg <arg>     Append one raw cl.exe argument
  --linker-arg <arg>       Append one raw link.exe argument
  --env <auto|vs|portable> Toolchain selection
  --portable-root <dir>    Add a portable_msvc root candidate
  --discover | --no-discover
                           Override single-entry smart discovery
  -v, --verbose            Show config, toolchain, and artifact details
  -h, --help               Show this help
  --                       Pass all remaining argv elements to the program

Runtime and subsystem are typed policy. Explicit runtime changes compile recipe identity;
subsystem changes link recipe identity. DLL/static output kinds remain tracked separately by
the stable-v5 parity campaign.

Generated state:
  .mqb/obj/ .mqb/deps/ .mqb/scan/ .mqb/ifc/ .mqb/cache/ .mqb/bin/
)";
}

} // namespace mqb::cli