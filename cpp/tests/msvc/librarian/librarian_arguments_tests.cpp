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
} // namespace

int main() {
    mqb::msvc::ArchiveInvocation invocation;
    invocation.objects = {"obj/math one.obj", "obj/math-two.obj"};
    invocation.output = "bin/math.lib";
    const auto arguments = mqb::msvc::MsvcLibrarian::build_arguments(invocation);
    expect(arguments.has_value(), "valid archive invocation should build argv");
    if (arguments) {
        expect(contains(*arguments, "/NOLOGO"), "librarian should suppress banner");
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
