#include <filesystem>
#include <iostream>
#include <string_view>

#include "mqb/core/TranslationUnitClassifier.hpp"

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
    using mqb::TranslationUnitKind;
    using mqb::classify_translation_unit_path;
    using mqb::is_c_translation_unit_path;
    using mqb::is_cpp_translation_unit_path;
    using mqb::is_translation_unit_path;

    expect(classify_translation_unit_path("main.cpp") == TranslationUnitKind::source,
           ".cpp should classify as an ordinary source TU");
    expect(classify_translation_unit_path("helper.CC") == TranslationUnitKind::source,
           "ordinary C++ source extensions should be case-insensitive");
    expect(classify_translation_unit_path("feature.cxx") == TranslationUnitKind::source,
           ".cxx should classify as an ordinary source TU");
    expect(classify_translation_unit_path("source.c") == TranslationUnitKind::source,
           ".c should classify as an ordinary source TU");
    expect(classify_translation_unit_path("SOURCE.C") == TranslationUnitKind::source,
           "C source extension should be case-insensitive");

    expect(classify_translation_unit_path("math.ixx") == TranslationUnitKind::module_interface,
           ".ixx should classify as a module interface TU");
    expect(classify_translation_unit_path("math.CPPM") == TranslationUnitKind::module_interface,
           ".cppm module extensions should be case-insensitive");
    expect(classify_translation_unit_path("math.mpp") == TranslationUnitKind::module_interface,
           ".mpp should classify as a module interface TU");

    expect(!classify_translation_unit_path("header.hpp").has_value(),
           "headers must not classify as translation units");
    expect(is_translation_unit_path("module.ixx"),
           "is_translation_unit_path should accept module interfaces");
    expect(is_translation_unit_path("helper.c"),
           "is_translation_unit_path should accept C sources");
    expect(!is_translation_unit_path("notes.txt"),
           "is_translation_unit_path should reject unrelated files");

    expect(is_c_translation_unit_path("helper.c"),
           "C language helper should identify .c sources");
    expect(is_c_translation_unit_path("HELPER.C"),
           "C language helper should be case-insensitive");
    expect(!is_c_translation_unit_path("helper.cpp"),
           "C language helper must reject C++ sources");

    expect(is_cpp_translation_unit_path("helper.cpp"),
           "C++ language helper should identify ordinary C++ sources");
    expect(is_cpp_translation_unit_path("module.ixx"),
           "C++ language helper should identify module interfaces");
    expect(!is_cpp_translation_unit_path("helper.c"),
           "C++ language helper must reject C sources");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_translation_unit_classifier_tests passed\n";
    return 0;
}
