#include <iostream>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcDefaultLibraryPolicy.hpp"

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
    using mqb::msvc::MsvcDefaultLibraryPolicy;

    const std::vector<std::string> selective_args{
        "/DEFAULTLIB:user32.lib",
        "/NODEFAULTLIB:clang_rt.fuzzer_MDd-x86_64.lib",
    };
    const auto selective = MsvcDefaultLibraryPolicy::route(selective_args);
    expect(selective.has_value(), "selective /NODEFAULTLIB policy should parse");
    if (selective) {
        expect(!selective->suppress_all_default_libraries,
               "selective suppression must not disable every default library");
        expect(MsvcDefaultLibraryPolicy::allows_implicit_library(
                   *selective, "user32.lib"),
               "unrelated implicit default library should remain enabled");
        expect(!MsvcDefaultLibraryPolicy::allows_implicit_library(
                   *selective, "clang_rt.fuzzer_MDd-x86_64.lib"),
               "named LibFuzzer default library should be suppressed");
        expect(!MsvcDefaultLibraryPolicy::allows_implicit_library(
                   *selective, "CLANG_RT.FUZZER_MDD-X86_64"),
               "suppression matching should be case-insensitive and normalize .lib");
        expect(selective->effective_libraries.size() == 1
                   && selective->effective_libraries.front() == "user32.lib",
               "unrelated explicit /DEFAULTLIB should remain freshness evidence");
    }

    const std::vector<std::string> global_args{
        "/DEFAULTLIB:user32.lib",
        "/NODEFAULTLIB",
    };
    const auto global = MsvcDefaultLibraryPolicy::route(global_args);
    expect(global.has_value(), "global /NODEFAULTLIB policy should parse");
    if (global) {
        expect(global->suppress_all_default_libraries,
               "bare /NODEFAULTLIB should suppress all default-library directives");
        expect(global->effective_libraries.empty(),
               "global suppression should remove explicit /DEFAULTLIB freshness evidence");
        expect(!MsvcDefaultLibraryPolicy::allows_implicit_library(
                   *global, "clang_rt.fuzzer_MDd-x86_64.lib"),
               "global suppression should disable compiler-injected LibFuzzer runtime");
    }

    const std::vector<std::string> ordinary_args{
        "/NODEFAULTLIB:legacy.lib",
    };
    const auto ordinary = MsvcDefaultLibraryPolicy::route(ordinary_args);
    expect(ordinary.has_value(), "ordinary selective policy should parse");
    if (ordinary) {
        expect(MsvcDefaultLibraryPolicy::allows_implicit_library(
                   *ordinary, "clang_rt.fuzzer_MT-x86_64.lib"),
               "unrelated suppression must not hide LibFuzzer runtime");
    }

    if (failures != 0) {
        std::cerr << failures << " default-library policy test(s) failed\n";
        return 1;
    }
    std::cout << "default-library policy tests passed\n";
    return 0;
}