#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/discovery/SourceDiscovery.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_text(const fs::path& path, const std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}

[[nodiscard]] bool contains_source(
    const std::vector<fs::path>& sources,
    const fs::path& source) {
    const fs::path normalized = source.lexically_normal();
    for (const auto& candidate : sources) {
        if (candidate.lexically_normal() == normalized) {
            return true;
        }
    }
    return false;
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_source_discovery_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path main_source = tree.root / "main.cpp";
    const fs::path app_header = tree.root / "src" / "app.hpp";
    const fs::path app_source = tree.root / "src" / "app.cpp";
    const fs::path stringy_source = tree.root / "src" / "stringy.cpp";
    const fs::path raw_include_noise = tree.root / "src" / "raw_include_noise.cpp";
    const fs::path raw_main_source = tree.root / "src" / "raw_main_source.cpp";
    const fs::path macro_main_source = tree.root / "src" / "macro_main_source.cpp";
    const fs::path helper_a_header = tree.root / "a" / "helper.hpp";
    const fs::path helper_a_source = tree.root / "a" / "helper.cpp";
    const fs::path helper_b_header = tree.root / "b" / "helper.hpp";
    const fs::path helper_b_source = tree.root / "b" / "helper.cpp";
    const fs::path include_dir = tree.root / "include";
    const fs::path widget_header = include_dir / "widget.hpp";
    const fs::path widget_source = include_dir / "widget.cpp";
    const fs::path other_main = tree.root / "tests" / "other_main.cpp";
    const fs::path test_only_header = tree.root / "tests" / "test_only.hpp";
    const fs::path test_only_source = tree.root / "tests" / "test_only.cpp";
    const fs::path unrelated_header = tree.root / "unrelated.hpp";
    const fs::path unrelated_source = tree.root / "unrelated.cpp";
    const fs::path comment_only = tree.root / "comment_only.cpp";
    const fs::path generated = tree.root / "build" / "generated.cpp";

    write_text(
        main_source,
        "#include \"src/app.hpp\"\n"
        "#include \"a/helper.hpp\"\n"
        "#include \"b/helper.hpp\"\n"
        "#include \"widget.hpp\"\n"
        "int main() { return app() + helper_a() + helper_b() + widget(); }\n");
    write_text(app_header, "#pragma once\nint app();\n");
    write_text(app_source, "#include \"app.hpp\"\nint app() { return 1; }\n");
    write_text(
        stringy_source,
        "#include \"app.hpp\"\n"
        "const char* text_that_is_not_a_main = \"main(\";\n"
        "int stringy() { return text_that_is_not_a_main[0]; }\n");
    write_text(
        raw_include_noise,
        "const char* discovery_noise = R\"mqb(\n"
        "#include \"app.hpp\"\n"
        ")mqb\";\n"
        "int raw_include_noise() { return 0; }\n");
    write_text(
        raw_main_source,
        "#include \"app.hpp\"\n"
        "const char* raw_main_text = R\"mqb(\n"
        "int main() { return 99; }\n"
        ")mqb\";\n"
        "int raw_main_source() { return app(); }\n");
    write_text(
        macro_main_source,
        "#include \"app.hpp\"\n"
        "#define MQB_FAKE_MAIN main(\n"
        "int macro_main_source() { return app(); }\n");
    write_text(helper_a_header, "#pragma once\nint helper_a();\n");
    write_text(
        helper_a_source,
        "// Deliberately no include: same-basename header ownership should connect this TU.\n"
        "int helper_a() { return 10; }\n");
    write_text(helper_b_header, "#pragma once\nint helper_b();\n");
    write_text(helper_b_source, "#include \"helper.hpp\"\nint helper_b() { return 20; }\n");
    write_text(widget_header, "#pragma once\nint widget();\n");
    write_text(
        widget_source,
        "// Same-basename ownership connects this source through include/widget.hpp.\n"
        "int widget() { return 30; }\n");
    write_text(test_only_header, "#pragma once\nint test_only();\n");
    write_text(test_only_source, "#include \"test_only.hpp\"\nint test_only() { return 77; }\n");
    write_text(
        other_main,
        "#include \"../src/app.hpp\"\n"
        "#include \"test_only.hpp\"\n"
        "int main() { return app() + test_only(); }\n");
    write_text(unrelated_header, "#pragma once\nint unrelated();\n");
    write_text(unrelated_source, "#include \"unrelated.hpp\"\nint unrelated() { return 9; }\n");
    write_text(
        comment_only,
        "// #include \"src/app.hpp\"\n"
        "/* #include \"a/helper.hpp\" */\n"
        "int comment_only() { return 0; }\n");
    write_text(
        generated,
        "#include \"../src/app.hpp\"\n"
        "int generated() { return app(); }\n");

    const mqb::discovery::Request request{
        .project_root = tree.root,
        .entry = main_source,
        .include_directories = {include_dir},
    };
    const auto discovered = mqb::discovery::SourceDiscovery::discover(request);
    expect(discovered.has_value(), "valid discovery fixture should succeed");
    if (discovered) {
        expect(discovered->warnings.empty(), "readable fixture should not emit warnings");
        expect(!discovered->sources.empty() && discovered->sources.front() == main_source,
               "entry translation unit must remain first");
        expect(contains_source(discovered->sources, app_source),
               "shared header should connect app.cpp");
        expect(contains_source(discovered->sources, stringy_source),
               "string literal containing main( must not classify a TU as another entry point");
        expect(!contains_source(discovered->sources, raw_include_noise),
               "quoted include text inside a raw string literal must not create a discovery edge");
        expect(contains_source(discovered->sources, raw_main_source),
               "main-like text inside a raw string literal must not create a second-main barrier");
        expect(contains_source(discovered->sources, macro_main_source),
               "main-like text inside a preprocessing directive must not create a second-main barrier");
        expect(contains_source(discovered->sources, helper_a_source),
               "same-basename header ownership should connect helper A");
        expect(contains_source(discovered->sources, helper_b_source),
               "quoted include should connect helper B");
        expect(contains_source(discovered->sources, widget_source),
               "configured include directory plus same-basename ownership should connect widget.cpp");
        expect(!contains_source(discovered->sources, other_main),
               "reachable non-entry source defining main() must be excluded");
        expect(!contains_source(discovered->sources, test_only_source),
               "second main must be a traversal barrier for its test-only subgraph");
        expect(!contains_source(discovered->sources, unrelated_source),
               "disconnected source must not be discovered");
        expect(!contains_source(discovered->sources, comment_only),
               "include-like text in comments must not create graph edges");
        expect(!contains_source(discovered->sources, generated),
               "default build directories must not be indexed");
        expect(discovered->sources.size() == 8,
               "fixture should discover exactly entry + seven connected non-main TUs");

        const auto repeated = mqb::discovery::SourceDiscovery::discover(request);
        expect(repeated.has_value() && repeated->sources == discovered->sources,
               "discovery output ordering should be deterministic");
    }

    {
        TempTree traversal_tree{
            .root = fs::temp_directory_path()
                / ("mqb_source_discovery_deep_" + std::to_string(unique)),
        };
        const fs::path deep_entry = traversal_tree.root / "a" / "b" / "c" / "main.cpp";
        const fs::path root_header = traversal_tree.root / "tail.hpp";
        const fs::path root_source = traversal_tree.root / "tail.cpp";
        write_text(
            deep_entry,
            "#include \"../../../tail.hpp\"\n"
            "int main() { return tail(); }\n");
        write_text(root_header, "#pragma once\nint tail();\n");
        write_text(root_source, "#include \"tail.hpp\"\nint tail() { return 0; }\n");

        const auto traversal = mqb::discovery::SourceDiscovery::discover({
            .project_root = traversal_tree.root,
            .entry = deep_entry,
            .include_directories = {},
        });
        expect(traversal.has_value(),
               "discovery should resume at parent directories after finishing a deep subtree");
        if (traversal) {
            expect(traversal->indexed_files == 3,
                   "deep-subtree traversal should still index root-level header and source files");
            expect(contains_source(traversal->sources, root_source),
                   "root-level source reached after a deep subtree should participate in discovery");
        }
    }

    {
        TempTree unicode_tree{
            .root = fs::temp_directory_path()
                / ("mqb_source_discovery_unicode_" + std::to_string(unique)),
        };
        const fs::path unicode_entry = unicode_tree.root / "main.cpp";
        const fs::path unicode_source =
            unicode_tree.root / L"\u68c0\u6d4bsmart_handle.cpp";
        write_text(unicode_entry, "int main() { return 0; }\n");
        write_text(unicode_source, "int unicode_probe() { return 0; }\n");

        const auto unicode_discovery = mqb::discovery::SourceDiscovery::discover({
            .project_root = unicode_tree.root,
            .entry = unicode_entry,
            .include_directories = {},
        });
        expect(unicode_discovery.has_value(),
               "discovery should index Unicode filenames without narrow path conversion");
        if (unicode_discovery) {
            expect(unicode_discovery->indexed_files == 2,
                   "Unicode-named translation unit should be indexed");
            expect(unicode_discovery->sources.size() == 1
                       && unicode_discovery->sources.front() == unicode_entry,
                   "disconnected Unicode-named source should not change selected closure");
        }
    }

    const auto invalid_root = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root / "missing-root",
        .entry = main_source,
        .include_directories = {},
    });
    expect(!invalid_root, "missing project root should be rejected");
    if (!invalid_root) {
        expect(invalid_root.error().code == mqb::discovery::ErrorCode::invalid_project_root,
               "missing root should report invalid_project_root");
    }

    const fs::path outside = tree.root.parent_path() / ("outside_" + std::to_string(unique) + ".cpp");
    write_text(outside, "int main() { return 0; }\n");
    const auto invalid_entry = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = outside,
        .include_directories = {},
    });
    expect(!invalid_entry, "entry outside project root should be rejected");
    if (!invalid_entry) {
        expect(invalid_entry.error().code == mqb::discovery::ErrorCode::invalid_entry,
               "outside entry should report invalid_entry");
    }
    std::error_code ignored;
    fs::remove(outside, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_source_discovery_tests passed\n";
    return 0;
}
