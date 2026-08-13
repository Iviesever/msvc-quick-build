#include <filesystem>
#include <iostream>
#include <string_view>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

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
    mqb::ToolchainIdentity toolchain{
        .compiler = "toolchain/cl.exe",
        .version = "19.50",
        .binary_stamp = "stamp",
    };

    mqb::TranslationUnit c;
    c.source = "src/helper.c";
    c.kind = mqb::TranslationUnitKind::source;
    c.outputs = {
        mqb::Artifact{.path = "build/helper.obj", .kind = mqb::ArtifactKind::object},
    };

    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp14;

    const auto c_cpp14 = mqb::BuildSignature::for_compile(c, toolchain, options);
    options.standard = mqb::CppStandard::latest;
    const auto c_latest = mqb::BuildSignature::for_compile(c, toolchain, options);
    expect(c_cpp14 == c_latest,
           "changing only the target C++ standard must not invalidate a .c compile recipe");

    options.additional_arguments = {"/std:c17"};
    const auto c_explicit_c17 = mqb::BuildSignature::for_compile(c, toolchain, options);
    expect(c_explicit_c17 != c_latest,
           "an explicit raw C language switch must remain part of C compile identity");

    options.additional_arguments.clear();
    options.runtime_library = mqb::RuntimeLibrary::mt;
    const auto c_mt = mqb::BuildSignature::for_compile(c, toolchain, options);
    expect(c_mt != c_latest,
           "an explicit typed CRT runtime must invalidate C compile identity");
    options.runtime_library.reset();
    expect(mqb::BuildSignature::for_compile(c, toolchain, options) == c_latest,
           "leaving runtime unset must preserve the historical default signature");

    auto cpp = c;
    cpp.source = "src/helper.cpp";
    options.standard = mqb::CppStandard::cpp14;
    const auto cpp14 = mqb::BuildSignature::for_compile(cpp, toolchain, options);
    options.standard = mqb::CppStandard::latest;
    const auto cpp_latest = mqb::BuildSignature::for_compile(cpp, toolchain, options);
    expect(cpp14 != cpp_latest,
           "C++ standard must remain part of C++ compile recipe identity");
    options.runtime_library = mqb::RuntimeLibrary::mtd;
    expect(mqb::BuildSignature::for_compile(cpp, toolchain, options) != cpp_latest,
           "typed runtime must also invalidate C++ compile identity");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_c_compile_signature_tests passed\n";
    return 0;
}
