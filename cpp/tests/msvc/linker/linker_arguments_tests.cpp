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
    invocation.libraries = {"C:/sdk libs/user32.lib"};
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
        expect(!contains(*result, "/INCREMENTAL:NO"),
               "ordinary debug link should not disable incremental linking");
        expect(!contains(*result, "/LTCG"), "default link recipe should preserve non-LTCG behavior");
        expect(contains(*result, "/LIBPATH:vendor libs"), "library path should stay one argv element");
        expect(contains(*result, "C:/sdk libs/user32.lib"),
               "linker should consume exact resolved library path");
        expect(!contains(*result, "user32.lib"),
               "unresolved library token must not be emitted alongside resolved file");
        expect(contains(*result, "/MACHINE:X64"), "x64 should map to /MACHINE:X64");
        expect(contains(*result, "/SUBSYSTEM:CONSOLE"), "console subsystem should map correctly");
        expect(contains(*result, "/OUT:bin/my app.exe"), "structured output should be emitted");

        const auto raw_out = std::find(result->begin(), result->end(), "/OUT:ignored.exe");
        const auto planned_out = std::find(result->begin(), result->end(), "/OUT:bin/my app.exe");
        expect(raw_out != result->end() && planned_out != result->end() && raw_out < planned_out,
               "planned output must override a conflicting raw /OUT");
    }

    auto changed_library = invocation;
    changed_library.force_full_link = true;
    changed_library.options.additional_arguments.push_back("/INCREMENTAL");
    const auto changed_library_result = mqb::msvc::MsvcLinker::build_arguments(changed_library);
    expect(changed_library_result.has_value(),
           "library-change full-link invocation should produce argv");
    if (changed_library_result) {
        expect(contains(*changed_library_result, "/DEBUG"),
               "full library relink should retain Debug information");
        expect(contains(*changed_library_result, "/INCREMENTAL:NO"),
               "changed library inputs must disable MSVC incremental linking");
        const auto raw_incremental = std::find(
            changed_library_result->begin(), changed_library_result->end(), "/INCREMENTAL");
        const auto forced_full = std::find(
            changed_library_result->begin(), changed_library_result->end(), "/INCREMENTAL:NO");
        expect(raw_incremental != changed_library_result->end()
                   && forced_full != changed_library_result->end()
                   && raw_incremental < forced_full,
               "MQB full-link safety policy must override a raw /INCREMENTAL argument");
    }

    auto ltcg = invocation;
    ltcg.options.link_time_code_generation = true;
    ltcg.options.additional_arguments = {"/LTCG:OFF"};
    const auto ltcg_result = mqb::msvc::MsvcLinker::build_arguments(ltcg);
    expect(ltcg_result.has_value(), "typed LTCG link invocation should produce argv");
    if (ltcg_result) {
        expect(contains(*ltcg_result, "/LTCG"), "typed LTCG should emit /LTCG");
        expect(contains(*ltcg_result, "/INCREMENTAL:NO"),
               "debug LTCG should disable incremental linking");
        expect(!contains(*ltcg_result, "/INCREMENTAL"),
               "debug LTCG must not retain the incompatible /INCREMENTAL recipe");
        const auto raw_ltcg = std::find(ltcg_result->begin(), ltcg_result->end(), "/LTCG:OFF");
        const auto typed_ltcg = std::find(ltcg_result->begin(), ltcg_result->end(), "/LTCG");
        expect(raw_ltcg != ltcg_result->end() && typed_ltcg != ltcg_result->end()
                   && raw_ltcg < typed_ltcg,
               "typed /LTCG must follow raw linker arguments and remain authoritative");
    }

    auto dll = invocation;
    dll.output = "bin/plugin.dll";
    dll.options.target_kind = mqb::TargetKind::dynamic_library;
    dll.options.additional_arguments = {"/OUT:ignored.dll", "/IMPLIB:ignored.lib"};
    const auto dll_result = mqb::msvc::MsvcLinker::build_arguments(dll);
    expect(dll_result.has_value(), "typed DLL link invocation should produce argv");
    if (dll_result) {
        expect(contains(*dll_result, "/DLL"), "DLL target should emit /DLL");
        expect(contains(*dll_result, "/IMPLIB:bin/plugin.lib"),
               "DLL target should own deterministic import-library path");
        expect(contains(*dll_result, "/OUT:bin/plugin.dll"),
               "DLL target should own structured DLL output path");
        const auto raw_implib = std::find(dll_result->begin(), dll_result->end(), "/IMPLIB:ignored.lib");
        const auto planned_implib = std::find(dll_result->begin(), dll_result->end(), "/IMPLIB:bin/plugin.lib");
        expect(raw_implib != dll_result->end() && planned_implib != dll_result->end()
                   && raw_implib < planned_implib,
               "typed DLL import-library path must override conflicting raw /IMPLIB");
        expect(mqb::msvc::MsvcLinker::export_file_path(dll.output) == std::filesystem::path{"bin/plugin.exp"},
               "DLL export side-output path should be deterministic");
    }

    auto static_library = invocation;
    static_library.options.target_kind = mqb::TargetKind::static_library;
    const auto static_result = mqb::msvc::MsvcLinker::build_arguments(static_library);
    expect(!static_result, "static target must fail closed in linker backend");

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

    auto unresolved = invocation;
    unresolved.libraries.clear();
    const auto unresolved_result = mqb::msvc::MsvcLinker::build_arguments(unresolved);
    expect(!unresolved_result,
           "linker should reject requested libraries without exact resolved inputs");

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
