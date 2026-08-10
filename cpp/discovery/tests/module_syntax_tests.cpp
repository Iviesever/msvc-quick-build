#include <iostream>
#include <string_view>
#include <vector>

#include "mqb/discovery/ModuleSyntax.hpp"

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
    using mqb::discovery::ModuleSyntaxParser;

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "export module math.core;\n"
            "import util;\n"
            "export import stats.detail;\n");
        expect(syntax.declared_module == "math.core",
               "export module should expose its logical module name");
        expect(syntax.imported_modules == std::vector<std::string>({"util", "stats.detail"}),
               "named import and export import should be preserved in source order");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "module graphics:render;\n"
            "import :types;\n"
            "import graphics.io:codec;\n");
        expect(syntax.declared_module == "graphics:render",
               "module partitions should remain part of the declaration identity");
        expect(syntax.imported_modules
                   == std::vector<std::string>({"graphics:types", "graphics.io:codec"}),
               "relative partition imports should normalize against the primary module name");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "module;\n"
            "export module actual;\n"
            "module : private;\n");
        expect(syntax.declared_module == "actual",
               "global/private module fragments must not replace the named module declaration");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "import <vector>;\n"
            "import \"local.hpp\";\n"
            "import project.api;\n");
        expect(syntax.imported_modules == std::vector<std::string>({"project.api"}),
               "header-unit imports must be excluded from named-module discovery edges");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "// import fake.comment;\n"
            "/* export module fake.block; */\n"
            "const char* a = \"import fake.string;\";\n"
            "const char* b = R\"tag(export module fake.raw;)tag\";\n"
            "char c = ';';\n"
            "export module real;\n"
            "import dependency;\n");
        expect(syntax.declared_module == "real",
               "comment and literal contents must not create fake module declarations");
        expect(syntax.imported_modules == std::vector<std::string>({"dependency"}),
               "comment and literal contents must not create fake imports");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "#define FAKE import macro.module;\n"
            "#define LONG export module macro\\\n"
            "  .continued;\n"
            "export module real;\n"
            "import actual;\n");
        expect(syntax.declared_module == "real",
               "preprocessor directives must not create discovery module declarations");
        expect(syntax.imported_modules == std::vector<std::string>({"actual"}),
               "preprocessor directive text must not create discovery imports");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "int prior_code = 0; /* crosses a physical line\n"
            "and ends before a directive */ #define FAKE import macro.after_comment;\n"
            "export module real;\n"
            "import actual;\n");
        expect(syntax.declared_module == "real",
               "a multiline block comment must reset preprocessing-line state");
        expect(syntax.imported_modules == std::vector<std::string>({"actual"}),
               "directive text after a multiline block comment must remain ignored");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "export module app;\n"
            "import dep;\n"
            "import dep;\n"
            "export import dep;\n");
        expect(syntax.imported_modules == std::vector<std::string>({"dep"}),
               "duplicate named imports should collapse deterministically");
    }

    {
        const auto syntax = ModuleSyntaxParser::parse(
            "import :orphan;\n"
            "int import_value = 0;\n"
            "void import_fn();\n");
        expect(!syntax.declared_module.has_value(),
               "ordinary source without a module declaration should remain undeclared");
        expect(syntax.imported_modules.empty(),
               "relative partition imports without module context should not invent providers");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_syntax_tests passed\n";
    return 0;
}
