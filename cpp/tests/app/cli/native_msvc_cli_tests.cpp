#include <iostream>
#include <string_view>
#include <vector>

#include "Cli.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using namespace std::string_view_literals;

    {
        const std::vector arguments{
            "main.cpp"sv,
            "/W4"sv,
            "/O2"sv,
            "/fp:fast"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "slash-prefixed native compiler switches should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 3,
                   "native compiler switches should enter compiler raw routing");
            if (parsed->compiler_arguments.size() == 3) {
                expect(parsed->compiler_arguments[0] == "/W4"
                           && parsed->compiler_arguments[1] == "/O2"
                           && parsed->compiler_arguments[2] == "/fp:fast",
                       "native compiler switch order and spelling should be preserved");
            }
        }
    }

    {
        const std::vector arguments{
            "-W4"sv,
            "-std:c++20"sv,
            "main.cpp"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "single-dash MSVC compiler syntax should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 2
                       && parsed->compiler_arguments[0] == "-W4"
                       && parsed->compiler_arguments[1] == "-std:c++20",
                   "single-dash native compiler switches should preserve spelling and position order");
            expect(parsed->build.sources.size() == 1
                       && parsed->build.sources.front() == "main.cpp",
                   "compiler switches before the source must not become positional sources");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv,
            "/I"sv,
            "include dir"sv,
            "/D"sv,
            "FEATURE=1"sv,
            "/W4"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "spaced native /I and /D forms should parse");
        if (parsed) {
            expect(parsed->include_directories.size() == 1
                       && parsed->include_directories.front() == "include dir",
                   "spaced /I should use the structured include-directory path");
            expect(parsed->defines.size() == 1
                       && parsed->defines.front() == "FEATURE=1",
                   "spaced /D should use the structured define path");
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "/W4",
                   "spaced /I and /D values must not leak into compiler raw argv");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv,
            "/O2"sv,
            "/std:c++20"sv,
            "/link"sv,
            "/STACK:8388608"sv,
            "/DEBUG:FULL"sv,
            "/subsystem:console"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "native /link tail should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 2
                       && parsed->compiler_arguments[0] == "/O2"
                       && parsed->compiler_arguments[1] == "/std:c++20",
                   "compiler switches before /link should remain compiler arguments");
            expect(parsed->linker_arguments.size() == 3
                       && parsed->linker_arguments[0] == "/STACK:8388608"
                       && parsed->linker_arguments[1] == "/DEBUG:FULL"
                       && parsed->linker_arguments[2] == "/subsystem:console",
                   "all build argv after /link should enter linker routing verbatim");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv,
            "-O2"sv,
            "-link"sv,
            "-stack:4194304"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "single-dash -link boundary should parse");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "-O2",
                   "-link must not be mistaken for the MQB -l library shorthand");
            expect(parsed->linker_arguments.size() == 1
                       && parsed->linker_arguments.front() == "-stack:4194304",
                   "single-dash linker switches should be preserved after -link");
        }
    }

    {
        const std::vector arguments{
            "main.cpp"sv,
            "--run"sv,
            "/O2"sv,
            "/link"sv,
            "/DEBUG:FULL"sv,
            "--"sv,
            "child argument"sv,
            "/W4"sv,
        };
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "outer -- run boundary should remain valid after a native linker tail");
        if (parsed) {
            expect(parsed->build.run_after_build,
                   "--run should remain an MQB option before /link");
            expect(parsed->linker_arguments.size() == 1
                       && parsed->linker_arguments.front() == "/DEBUG:FULL",
                   "linker tail should stop at the outer -- delimiter");
            expect(parsed->build.run_arguments.size() == 2
                       && parsed->build.run_arguments[0] == "child argument"
                       && parsed->build.run_arguments[1] == "/W4",
                   "program argv after -- must remain opaque even when it looks like MSVC syntax");
        }
    }

    {
        const std::vector arguments{"/link"sv, "/DEBUG:FULL"sv, "main.cpp"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "/link before every source should fail with a structural CLI diagnostic");
    }

    {
        const std::vector arguments{"main.cpp"sv, "/link"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "/link without linker options should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "/link"sv, "/DEBUG:FULL"sv, "/link"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "duplicate /link boundaries should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "/link"sv, "/DEBUG:FULL"sv, "--verbose"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "MQB long options after /link should be rejected instead of becoming linker argv");
    }

    {
        const std::vector arguments{"main.cpp"sv, "/LINK"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(parsed.has_value(), "compiler option spelling should remain case-sensitive at the CLI boundary");
        if (parsed) {
            expect(parsed->compiler_arguments.size() == 1
                       && parsed->compiler_arguments.front() == "/LINK",
                   "uppercase /LINK should flow to the parameter engine instead of acting as /link");
        }
    }

    {
        const std::vector arguments{"main.cpp"sv, "--wat"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "unknown MQB --long options must remain parser errors");
    }

    {
        const std::vector arguments{"main.cpp"sv, "/I"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "bare /I without a directory should be rejected");
    }

    {
        const std::vector arguments{"main.cpp"sv, "/D"sv};
        auto parsed = mqb::cli::parse_arguments(arguments);
        expect(!parsed, "bare /D without a definition should be rejected");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_native_msvc_cli_tests passed\n";
    return 0;
}
