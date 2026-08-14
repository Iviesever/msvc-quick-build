#include "Cli.hpp"

#include <algorithm>
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

[[nodiscard]] Error error(std::string message) {
    return Error{.message = std::move(message)};
}

[[nodiscard]] bool is_legacy_option(const std::string_view argument) noexcept {
    return argument == "-config"
        || argument == "-std"
        || argument == "-type"
        || argument == "-runtime"
        || argument == "-run"
        || argument == "-env"
        || argument == "-x86"
        || argument == "-x64"
        || argument == "-output"
        || argument == "-include"
        || argument == "-defines"
        || argument == "-libpath"
        || argument == "-libs"
        || argument == "-flags"
        || argument == "-link_flags"
        || argument == "-ltcg"
        || argument == "-subsystem"
        || argument == "-help"
        || argument == "-?";
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
    if (value == "14" || value == "c++14") return CppStandard::cpp14;
    if (value == "17" || value == "c++17") return CppStandard::cpp17;
    if (value == "20" || value == "c++20") return CppStandard::cpp20;
    if (value == "23" || value == "c++23") return CppStandard::cpp23;
    if (value == "latest" || value == "c++latest") return CppStandard::latest;
    return std::unexpected(error(
        "unsupported C++ standard '" + std::string{value}
        + "' (expected 14, 17, 20, 23, or latest)"));
}

[[nodiscard]] std::expected<BuildConfiguration, Error>
parse_configuration(const std::string_view value) {
    if (value == "debug") return BuildConfiguration::debug;
    if (value == "release") return BuildConfiguration::release;
    return std::unexpected(error(
        "unsupported build configuration '" + std::string{value}
        + "' (expected debug or release)"));
}

[[nodiscard]] std::expected<TargetKind, Error>
parse_target_kind(const std::string_view value) {
    if (value == "exe" || value == "executable") return TargetKind::executable;
    if (value == "dll" || value == "dynamic") return TargetKind::dynamic_library;
    if (value == "static" || value == "lib") return TargetKind::static_library;
    return std::unexpected(error(
        "unsupported target kind '" + std::string{value}
        + "' (expected exe, dll, or static)"));
}

[[nodiscard]] std::expected<RuntimeLibrary, Error>
parse_runtime(const std::string_view value) {
    if (value == "MD" || value == "md") return RuntimeLibrary::md;
    if (value == "MDd" || value == "mdd") return RuntimeLibrary::mdd;
    if (value == "MT" || value == "mt") return RuntimeLibrary::mt;
    if (value == "MTd" || value == "mtd") return RuntimeLibrary::mtd;
    return std::unexpected(error(
        "unsupported runtime library '" + std::string{value}
        + "' (expected MD, MDd, MT, or MTd)"));
}

[[nodiscard]] std::expected<LinkSubsystem, Error>
parse_subsystem(const std::string_view value) {
    if (value == "console") return LinkSubsystem::console;
    if (value == "windows") return LinkSubsystem::windows;
    return std::unexpected(error(
        "unsupported subsystem '" + std::string{value}
        + "' (expected console or windows)"));
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

[[nodiscard]] std::expected<ExternalModuleProvider, Error>
parse_external_module_provider(const std::string_view value) {
    const std::size_t separator = value.find('=');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == value.size()) {
        return std::unexpected(error(
            "invalid external module provider '" + std::string{value}
            + "' (expected <logical-name>=<ifc-path>)"));
    }
    return ExternalModuleProvider{
        .logical_name = std::string{value.substr(0, separator)},
        .interface_file = std::filesystem::u8path(value.substr(separator + 1)),
    };
}

[[nodiscard]] std::expected<void, Error>
add_external_module_provider(Options& options, const std::string_view value) {
    auto provider = parse_external_module_provider(value);
    if (!provider) return std::unexpected(provider.error());
    const auto duplicate = std::find_if(
        options.external_module_providers.begin(),
        options.external_module_providers.end(),
        [&provider](const ExternalModuleProvider& current) {
            return current.logical_name == provider->logical_name;
        });
    if (duplicate != options.external_module_providers.end()) {
        return std::unexpected(error(
            "duplicate --module-ifc provider for logical module '"
            + provider->logical_name + "'"));
    }
    options.external_module_providers.push_back(std::move(*provider));
    return {};
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

[[nodiscard]] std::expected<app::performance::Format, Error>
parse_timings_format(const std::string_view value) {
    if (value == "text") return app::performance::Format::text;
    if (value == "json") return app::performance::Format::json;
    return std::unexpected(error(
        "unsupported timings format '" + std::string{value}
        + "' (expected text or json)"));
}

[[nodiscard]] std::expected<void, Error>
select_profile(Options& options, const std::string_view value) {
    if (options.profile) {
        return std::unexpected(error("--profile may be specified only once"));
    }
    options.profile = std::string{value};
    return {};
}

} // namespace

std::expected<Options, Error>
parse_arguments(const std::span<const std::string_view> arguments) {
    Options options;
    std::size_t first_argument = 0;
    if (!arguments.empty()) {
        if (arguments.front() == "build") {
            options.command = Command::build;
            first_argument = 1;
        } else if (arguments.front() == "run") {
            options.command = Command::run;
            options.build.run_after_build = true;
            first_argument = 1;
        }
    }

    bool native_linker_tail = false;
    bool native_linker_tail_has_argument = false;

    for (std::size_t index = first_argument; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];

        if (argument == "--") {
            for (++index; index < arguments.size(); ++index) {
                options.build.run_arguments.emplace_back(arguments[index]);
            }
            break;
        }
        if (native_linker_tail) {
            if (argument == "/link" || argument == "-link") {
                return std::unexpected(error("duplicate native MSVC /link separator"));
            }
            if (argument.starts_with("--")) {
                return std::unexpected(error(
                    "MQB option '" + std::string{argument}
                    + "' must appear before native MSVC /link"));
            }
            options.linker_arguments.emplace_back(argument);
            native_linker_tail_has_argument = true;
            continue;
        }
        if (argument == "/link" || argument == "-link") {
            if (options.build.sources.empty() && options.command == Command::direct) {
                return std::unexpected(error(
                    "native MSVC /link must appear after at least one source file"));
            }
            native_linker_tail = true;
            continue;
        }
        if (is_legacy_option(argument)) {
            return std::unexpected(error("unknown option '" + std::string{argument} + "'"));
        }
        if (argument == "-h" || argument == "--help") {
            options.show_help = true;
            continue;
        }
        if (argument == "--verbose" || argument == "-v") {
            options.verbose = true;
            continue;
        }
        if (argument == "--timings") {
            options.timings = app::performance::Format::text;
            continue;
        }
        if (argument.starts_with("--timings=")) {
            auto value = long_equals_value(argument, "--timings=");
            if (!value) return std::unexpected(value.error());
            auto format = parse_timings_format(*value);
            if (!format) return std::unexpected(format.error());
            options.timings = *format;
            continue;
        }
        if (argument == "--profile") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto selected = select_profile(options, *value);
            if (!selected) return std::unexpected(selected.error());
            continue;
        }
        if (argument.starts_with("--profile=")) {
            auto value = long_equals_value(argument, "--profile=");
            if (!value) return std::unexpected(value.error());
            auto selected = select_profile(options, *value);
            if (!selected) return std::unexpected(selected.error());
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
            if (options.command == Command::build) {
                return std::unexpected(error(
                    "mqb build does not accept --run; use 'mqb run' instead"));
            }
            options.build.run_after_build = true;
            continue;
        }
        if (argument == "--ltcg") {
            options.ltcg_override = true;
            continue;
        }
        if (argument == "--no-ltcg") {
            options.ltcg_override = false;
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
        if (argument == "--type") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto target_kind = parse_target_kind(*value);
            if (!target_kind) return std::unexpected(target_kind.error());
            options.build.target_kind = *target_kind;
            options.target_kind_override = *target_kind;
            continue;
        }
        if (argument.starts_with("--type=")) {
            auto value = long_equals_value(argument, "--type=");
            if (!value) return std::unexpected(value.error());
            auto target_kind = parse_target_kind(*value);
            if (!target_kind) return std::unexpected(target_kind.error());
            options.build.target_kind = *target_kind;
            options.target_kind_override = *target_kind;
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
        if (argument == "--config") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto configuration = parse_configuration(*value);
            if (!configuration) return std::unexpected(configuration.error());
            options.build.configuration = *configuration;
            options.configuration_override = *configuration;
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
        if (argument == "--runtime") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto runtime = parse_runtime(*value);
            if (!runtime) return std::unexpected(runtime.error());
            options.runtime_override = *runtime;
            continue;
        }
        if (argument.starts_with("--runtime=")) {
            auto value = long_equals_value(argument, "--runtime=");
            if (!value) return std::unexpected(value.error());
            auto runtime = parse_runtime(*value);
            if (!runtime) return std::unexpected(runtime.error());
            options.runtime_override = *runtime;
            continue;
        }
        if (argument == "--subsystem") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto subsystem = parse_subsystem(*value);
            if (!subsystem) return std::unexpected(subsystem.error());
            options.subsystem_override = *subsystem;
            continue;
        }
        if (argument.starts_with("--subsystem=")) {
            auto value = long_equals_value(argument, "--subsystem=");
            if (!value) return std::unexpected(value.error());
            auto subsystem = parse_subsystem(*value);
            if (!subsystem) return std::unexpected(subsystem.error());
            options.subsystem_override = *subsystem;
            continue;
        }
        if (argument == "--module-ifc") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            auto added = add_external_module_provider(options, *value);
            if (!added) return std::unexpected(added.error());
            continue;
        }
        if (argument.starts_with("--module-ifc=")) {
            auto value = long_equals_value(argument, "--module-ifc=");
            if (!value) return std::unexpected(value.error());
            auto added = add_external_module_provider(options, *value);
            if (!added) return std::unexpected(added.error());
            continue;
        }
        if (argument == "--compiler-arg") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.compiler_arguments.emplace_back(*value);
            continue;
        }
        if (argument.starts_with("--compiler-arg=")) {
            auto value = long_equals_value(argument, "--compiler-arg=");
            if (!value) return std::unexpected(value.error());
            options.compiler_arguments.emplace_back(*value);
            continue;
        }
        if (argument == "--linker-arg") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.linker_arguments.emplace_back(*value);
            continue;
        }
        if (argument.starts_with("--linker-arg=")) {
            auto value = long_equals_value(argument, "--linker-arg=");
            if (!value) return std::unexpected(value.error());
            options.linker_arguments.emplace_back(*value);
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
        if (argument == "/I") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.include_directories.emplace_back(std::string{*value});
            continue;
        }
        if (argument == "/D") {
            auto value = require_value(arguments, index, argument);
            if (!value) return std::unexpected(value.error());
            options.defines.emplace_back(std::string{*value});
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
            options.defines.emplace_back(std::string{*value});
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
            options.libraries.emplace_back(std::string{*value});
            continue;
        }
        if (!argument.empty() && argument.front() == '@') {
            options.compiler_arguments.emplace_back(argument);
            continue;
        }
        if (!argument.empty() && argument.front() == '/') {
            options.compiler_arguments.emplace_back(argument);
            continue;
        }
        if (argument.size() > 1 && argument.front() == '-' && argument[1] != '-') {
            options.compiler_arguments.emplace_back(argument);
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            return std::unexpected(error("unknown option '" + std::string{argument} + "'"));
        }

        options.build.sources.emplace_back(std::string{argument});
    }

    if (native_linker_tail && !native_linker_tail_has_argument) {
        return std::unexpected(error("native MSVC /link requires at least one linker option"));
    }
    if (!options.show_help
        && options.build.sources.empty()
        && options.command == Command::direct) {
        return std::unexpected(error("missing source file"));
    }
    if (!options.show_help
        && !options.build.run_after_build
        && !options.build.run_arguments.empty()) {
        return std::unexpected(error("arguments after -- require 'mqb run' or --run"));
    }
    return options;
}

std::string_view usage() noexcept {
    return "MQB " MQB_VERSION " - MSVC Quick Build (C++ refactor)\n\n"
R"(Usage:
  mqb build [entry.cpp] [options] [MSVC-compiler-options] [/link linker-options...]
  mqb run [entry.cpp] [options] [MSVC-compiler-options] [/link linker-options...] [-- program-args...]
  mqb <entry.cpp> [options] [MSVC-compiler-options] [/link linker-options...] [-- program-args...]
  mqb <source.cpp> <more-sources...|module.ixx...> [options] [MSVC-compiler-options] [/link linker-options...] [-- program-args...]

Commands:
  build                     Build without running; source may be omitted when a default entry resolves
  run                       Build an executable and run it; source may be omitted when a default entry resolves

Default entry resolution for `mqb build` / `mqb run` with no positional source:
  1. build.entry from the nearest mqb.json (config-relative)
  2. exactly one conventional main source under project root or project root/src
     using main.c, main.cpp, main.cc, or main.cxx
  Zero or multiple conventional candidates fail closed; pass a source or set build.entry.

Project configuration:
  MQB searches upward from the invocation directory for the nearest mqb.json.
  Scalar precedence is: explicit CLI > selected profile > mqb.json > built-in defaults.
  List-valued settings append in the same base -> profile -> CLI order.
  Config/profile paths are relative to mqb.json; CLI paths are relative to the invocation directory.
  Explicit positional sources always take precedence over build.entry.
  Profiles are selected explicitly with --profile and may not override build.entry.
  External named-module IFCs may be declared under modules.external as logical-name -> IFC path.

Source selection:
  One positional source       Smart-discover connected C/C++ TUs and reachable project-local named-module providers (default)
  Multiple positional sources Build exactly that explicit ordered source set
  Explicit module source(s)   Route targets containing .ixx/.cppm/.mpp through P1689 named-module scanning
  --discover                  Explicitly enable smart discovery for one ordinary source
  --no-discover               Disable smart discovery for a single ordinary source

Single-entry discovery follows reachable project-local named imports to project-local
module-interface candidates. Discovery selects candidates only; MSVC P1689 scanning remains
the authority for module topology and provider validation. Project-local header-unit imports
stay outside TU discovery but route through the P1689 module pipeline, where their IFCs are
built and cached automatically. Explicit external/prebuilt named-module IFCs are resolved by
the same P1689 provider graph and remain read-only. `std` and `std.compat` are owned by the
selected MSVC toolchain: only a P1689 requirement triggers their VCToolsInstallDir/modules
provider source, which is then scanned, built, cached under project .mqb, and linked like any
other generated module provider. MSVC standard-library named modules require --std latest;
missing toolchain capability fails closed with a dedicated diagnostic.

Options:
  --profile <name>         Select one named profile from mqb.json
  --debug                  Explicitly select Debug compile/link preset
  --release                Explicitly select Release compile/link preset
  --config <debug|release> Select compile/link preset
  --std <14|17|20|23|latest>
                           Explicitly select C++ language standard; import std requires latest
  --type <exe|dll|static>  Select executable, DLL, or static-library output
  --runtime <MD|MDd|MT|MTd>
                           Explicitly select MSVC CRT runtime
  --ltcg | --no-ltcg      Enable/disable coupled /GL + downstream /LTCG
  --subsystem <console|windows>
                           Explicitly select PE subsystem for executable/DLL targets
  --x86 | --x64           Explicitly select target architecture
  -j, --jobs <N>          Maximum concurrent TU scans/compiles (default: hardware concurrency)
  -o, --output <name>     Set target name under .mqb/bin/
  --run                   Legacy source-first alias for build-then-run; prefer `mqb run`
  --timings               Show phase timings and cache hit/miss counters
  --timings=<text|json>   Select human-readable or JSON-line timing output
  --module-ifc <name=path>
                           Bind one external/prebuilt named module to a read-only IFC
  -I <dir>, -I<dir>       Add an include directory
  -D <value>, -D<value>   Add a preprocessor definition
  -L <dir>, -L<dir>       Add a library search directory
  --lib-path <dir>        Add a library search directory
  -l <name>, -l<name>     Link a library ('.lib' is optional)
  --lib <name>            Link a library
  /option, -option        Route a native MSVC compiler switch through the parameter engine
  /I <dir> | /D <value>  Native spaced include/define forms map to MQB structured inputs
  /link <link-options...> Route the remaining build argv to native linker options
  --compiler-arg <arg>    Append one raw cl.exe argument
  --linker-arg <arg>      Append one raw link.exe argument
  --env <auto|vs|portable>
                           Toolchain selection
  --portable-root <dir>   Add a portable_msvc root candidate
  -v, --verbose           Show config, discovery, toolchain, and artifact details
  -h, --help              Show this help and the embedded build version
  --                      Pass all remaining argv elements to an executable program

Native MSVC compiler switches may use '/' or a single '-' prefix. MQB '--long' options remain
in the MQB namespace. `/link` (or `-link`) is a one-way compiler-to-linker boundary for build
arguments; MQB options must appear before it. The outer `--` delimiter still ends build parsing
and starts executable argv. `mqb run ... /link ... -- child-args` and the legacy source-first
`--run` form remain supported. Native switches and @response syntax are not semantically
reimplemented by the CLI: they flow through the same ownership, normalization, conflict, and
cache-identity rules as --compiler-arg/--linker-arg. Bare `/I` and `/D` are the only native
compiler forms that consume a following argv element in the CLI; attached forms remain opaque
native tokens.

Static libraries are produced by MSVC lib.exe from the compiled object set. Linker-only policy
(libraries, library search paths, subsystem, and raw linker arguments) is rejected for static
targets. Typed LTCG remains valid for static targets and couples /GL compilation with lib.exe
/LTCG archive policy.

Raw compiler/linker arguments are one argv element per option occurrence; MQB does not split a
quoted string into multiple switches. Project config entries are applied first, selected profile
entries append/override next, and CLI entries apply last. Typed runtime/LTCG and structured
artifact routing are emitted after raw arguments so the BuildPlan remains authoritative.

MQB v5 intentionally does not accept the PowerShell-era single-dash command aliases. Use the
native options shown above; unknown legacy spellings fail instead of silently entering a
compatibility path.

Job count is execution policy only; changing -j does not invalidate build caches.
Timing output is observation-only and never participates in build/cache identity.
Discovery is source selection only. Incremental header freshness uses MSVC
/sourceDependencies metadata; /scanDependencies is module topology only.

Generated state:
  .mqb/obj/    collision-free object files (including generated std/std.compat providers)
  .mqb/deps/   compiler dependency metadata
  .mqb/scan/   module dependency scan metadata
  .mqb/ifc/    module/header-unit interface artifacts
  .mqb/cache/  compile, link, and archive cache metadata
  .mqb/bin/    executable, DLL/import-library, or static-library target artifacts
)";
}

} // namespace mqb::cli
