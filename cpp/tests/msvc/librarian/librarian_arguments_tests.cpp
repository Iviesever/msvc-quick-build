#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcLibrarian.hpp"

namespace {
int failures = 0;
void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
[[nodiscard]] bool contains(const std::vector<std::string>& values, std::string_view expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}
[[nodiscard]] std::size_t count(const std::vector<std::string>& values, std::string_view expected) {
    return static_cast<std::size_t>(std::count(values.begin(), values.end(), expected));
}
} // namespace

int main() {
    mqb::msvc::ArchiveInvocation invocation;
    invocation.objects = {"obj/math one.obj", "obj/math-two.obj"};
    invocation.output = "bin/math.lib";
    const auto arguments = mqb::msvc::MsvcLibrarian::build_arguments(invocation);
    expect(arguments.has_value(), "valid archive invocation should build argv");
    if (arguments) {
        expect(contains(*arguments, "/NOLOGO"), "librarian should suppress banner");
        expect(contains(*arguments, "/MACHINE:X64"),
               "typed target architecture should emit authoritative librarian /MACHINE");
        expect(!contains(*arguments, "/LTCG"), "default archive recipe should preserve non-LTCG behavior");
        expect(contains(*arguments, "/OUT:bin/math.lib"), "librarian should own archive output");
        expect(contains(*arguments, "obj/math one.obj"), "object path with spaces should stay one argv element");
        expect(contains(*arguments, "obj/math-two.obj"), "all object inputs should be emitted");
    }

    auto ltcg = invocation;
    ltcg.link_time_code_generation = true;
    const auto ltcg_arguments = mqb::msvc::MsvcLibrarian::build_arguments(ltcg);
    expect(ltcg_arguments.has_value(), "LTCG archive invocation should build argv");
    if (ltcg_arguments) {
        expect(contains(*ltcg_arguments, "/LTCG"),
               "typed static LTCG should emit lib.exe /LTCG");
    }

    auto native = invocation;
    native.additional_arguments = {
        "/EXPORT:mqb_export",
        "/WX",
        "/MACHINE:X64",
        "/LTCG",
    };
    const auto native_arguments = mqb::msvc::MsvcLibrarian::build_arguments(native);
    expect(native_arguments.has_value(), "supported native librarian arguments should build argv");
    if (native_arguments) {
        expect(contains(*native_arguments, "/EXPORT:mqb_export"),
               "Class C /EXPORT should reach lib.exe unchanged");
        expect(contains(*native_arguments, "/WX"),
               "Class C /WX should reach lib.exe unchanged");
        expect(count(*native_arguments, "/MACHINE:X64") == 1,
               "semantic native /MACHINE should canonicalize to one authoritative argv element");
        expect(count(*native_arguments, "/LTCG") == 1,
               "semantic native /LTCG should canonicalize to one authoritative argv element");
    }

    auto x86 = invocation;
    x86.architecture = mqb::Architecture::x86;
    x86.additional_arguments = {"/MACHINE:X86"};
    const auto x86_arguments = mqb::msvc::MsvcLibrarian::build_arguments(x86);
    expect(x86_arguments.has_value() && contains(*x86_arguments, "/MACHINE:X86"),
           "matching native x86 /MACHINE should be accepted");

    auto conflicting_machine = invocation;
    conflicting_machine.additional_arguments = {"/MACHINE:X86"};
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(conflicting_machine),
           "native /MACHINE must not override conflicting MQB typed architecture");

    auto owned_output = invocation;
    owned_output.additional_arguments = {"/OUT:hijack.lib"};
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(owned_output),
           "Class A librarian /OUT must fail closed before lib.exe");

    auto repro = invocation;
    repro.additional_arguments = {"/LINKREPRO:repro"};
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(repro),
           "unsupported librarian repro artifacts must fail closed");

    auto empty = invocation;
    empty.objects.clear();
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(empty),
           "archive invocation without objects should fail closed");

    auto missing_output = invocation;
    missing_output.output.clear();
    expect(!mqb::msvc::MsvcLibrarian::build_arguments(missing_output),
           "archive invocation without output should fail closed");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_msvc_librarian_arguments_tests passed\n";
    return 0;
}
