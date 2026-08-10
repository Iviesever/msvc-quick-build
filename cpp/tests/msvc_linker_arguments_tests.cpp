#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcLinker.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(
    const std::vector<std::string>& arguments,
    const std::string_view expected) {
    return std::find(arguments.begin(), arguments.end(), expected) != arguments.end();
}

} // namespace

int main() {
    mqb::msvc::LinkInvocation invocation;
    invocation.objects = {"obj/main file.obj", "obj/math.obj"};
    invocation.output = "bin/my app.exe";
    invocation.options.configuration = mqb::BuildConfiguration::debug;
    invocation.options.architecture = mqb::Architecture::x64;
    invocation.options.subsystem = mqb::LinkSubsystem::console;
    invocation.options.library_directories = {"vendor libs"};
    invocation.options.libraries = {"user32.lib"};
    invocation.options.additional_arguments = {"/MAP", "/OUT:ignored.exe"};

    const auto result = mqb::msvc::MsvcLinker::build_arguments(invocation);
    expect(result.has_value(), "valid link invocation should produce argv");
    if (result) {
        expect(contains(*result, "/NOLOGO"), "link should suppress banner");
        expect(contains(*result, "/DEBUG"), "debug link should emit /DEBUG");
        expect(contains(*result, "/INCREMENTAL"), "debug link should be incremental");
        expect(contains(*result, "/LIBPATH:vendor libs"), "library path should stay one argv element");
        expect(contains(*result, "user32.lib"), "library should be preserved");
        expect(contains(*result, "/MACHINE:X64"), "x64 should map to /MACHINE:X64");
        expect(contains(*result, "/SUBSYSTEM:CONSOLE"), "console subsystem should map correctly");
        expect(contains(*result, "/OUT:bin/my app.exe"), "structured output should be emitted");

        const auto raw_out = std::find(result->begin(), result->end(), "/OUT:ignored.exe");
        const auto planned_out = std::find(result->begin(), result->end(), "/OUT:bin/my app.exe");
        expect(raw_out != result->end() && planned_out != result->end() && raw_out < planned_out,
               "planned output must override a conflicting raw /OUT");
    }

    auto release = invocation;
    release.options.configuration = mqb::BuildConfiguration::release;
    release.options.architecture = mqb::Architecture::x86;
    release.options.subsystem = mqb::LinkSubsystem::windows;
    release.options.additional_arguments.clear();
    const auto release_result = mqb::msvc::MsvcLinker::build_arguments(release);
    expect(release_result.has_value(), "release link invocation should produce argv");
    if (release_result) {
        expect(contains(*release_result, "/INCREMENTAL:NO"), "release link should disable incremental linking");
        expect(contains(*release_result, "/OPT:REF"), "release link should enable reference elimination");
        expect(contains(*release_result, "/OPT:ICF"), "release link should enable COMDAT folding");
        expect(contains(*release_result, "/MACHINE:X86"), "x86 should map correctly");
        expect(contains(*release_result, "/SUBSYSTEM:WINDOWS"), "windows subsystem should map correctly");
    }

    auto invalid = invocation;
    invalid.objects.clear();
    const auto invalid_result = mqb::msvc::MsvcLinker::build_arguments(invalid);
    expect(!invalid_result, "link with no objects should be rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_msvc_linker_arguments_tests passed\n";
    return 0;
}
