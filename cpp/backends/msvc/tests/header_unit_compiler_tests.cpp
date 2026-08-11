#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains_pair(
    const std::vector<std::string>& arguments,
    const std::string_view first,
    const std::string_view second) {
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == first && arguments[index + 1] == second) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool contains_prefix(
    const std::vector<std::string>& arguments,
    const std::string_view prefix) {
    return std::any_of(
        arguments.begin(),
        arguments.end(),
        [prefix](const std::string& argument) {
            return argument.starts_with(prefix);
        });
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using mqb::HeaderUnitLookupMethod;
    using mqb::msvc::CompileInvocation;
    using mqb::msvc::HeaderUnitCompileInvocation;
    using mqb::msvc::HeaderUnitReference;
    using mqb::msvc::MsvcCompiler;

    CompileInvocation consumer;
    consumer.source = fs::path{"src/consumer.cpp"};
    consumer.object = fs::path{"out/consumer.obj"};
    consumer.header_unit_references = {
        HeaderUnitReference{
            .header_name = "util.hpp",
            .lookup_method = HeaderUnitLookupMethod::quote,
            .interface_file = fs::path{"ifc/util.ifc"},
        },
        HeaderUnitReference{
            .header_name = "vector",
            .lookup_method = HeaderUnitLookupMethod::angle,
            .interface_file = fs::path{"ifc/vector.ifc"},
        },
    };

    auto consumer_arguments = MsvcCompiler::build_arguments(consumer);
    expect(consumer_arguments.has_value(),
           "typed quote/angle header-unit references should build compiler arguments");
    if (consumer_arguments) {
        expect(contains_pair(*consumer_arguments, "/headerUnit:quote", "util.hpp=ifc/util.ifc"),
               "quote header unit should emit /headerUnit:quote header=ifc");
        expect(contains_pair(*consumer_arguments, "/headerUnit:angle", "vector=ifc/vector.ifc"),
               "angle header unit should emit /headerUnit:angle header=ifc");
    }

    auto duplicate = consumer;
    duplicate.header_unit_references.push_back(consumer.header_unit_references.front());
    auto duplicate_arguments = MsvcCompiler::build_arguments(duplicate);
    expect(!duplicate_arguments,
           "duplicate header-unit import identity should be rejected before compiler execution");

    auto missing_ifc = consumer;
    missing_ifc.header_unit_references.front().interface_file.clear();
    expect(!MsvcCompiler::build_arguments(missing_ifc),
           "header-unit reference with an empty IFC path should be rejected");

    HeaderUnitCompileInvocation quote_header;
    quote_header.header_name = "util.hpp";
    quote_header.lookup_method = HeaderUnitLookupMethod::quote;
    quote_header.interface_output = fs::path{"ifc/util.ifc"};
    quote_header.options.include_directories = {fs::path{"include"}};

    auto quote_arguments = MsvcCompiler::build_header_unit_arguments(quote_header);
    expect(quote_arguments.has_value(),
           "quote header-unit producer should build compiler arguments");
    if (quote_arguments) {
        expect(std::find(quote_arguments->begin(), quote_arguments->end(), "/exportHeader")
                   != quote_arguments->end(),
               "header-unit producer should emit /exportHeader");
        expect(contains_pair(*quote_arguments, "/headerName:quote", "util.hpp"),
               "quote producer should preserve quote lookup identity");
        expect(contains_pair(*quote_arguments, "/ifcOutput", "ifc/util.ifc"),
               "header-unit producer should route the IFC to the planned path");
        expect(!contains_prefix(*quote_arguments, "/Fo"),
               "IFC-only header-unit producer should not invent an object output");
    }

    auto angle_header = quote_header;
    angle_header.header_name = "vector";
    angle_header.lookup_method = HeaderUnitLookupMethod::angle;
    angle_header.interface_output = fs::path{"ifc/vector.ifc"};
    angle_header.object = fs::path{"obj/vector.obj"};
    auto angle_arguments = MsvcCompiler::build_header_unit_arguments(angle_header);
    expect(angle_arguments.has_value(),
           "angle header-unit producer with an explicit object should build compiler arguments");
    if (angle_arguments) {
        expect(contains_pair(*angle_arguments, "/headerName:angle", "vector"),
               "angle producer should preserve angle lookup identity");
        expect(contains_prefix(*angle_arguments, "/Foobj/vector.obj"),
               "explicit header-unit object output should emit /Fo");
    }

    auto invalid_header = quote_header;
    invalid_header.header_name.clear();
    expect(!MsvcCompiler::build_header_unit_arguments(invalid_header),
           "empty header-unit name should be rejected before compiler execution");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_header_unit_compiler_tests passed\n";
    return 0;
}
