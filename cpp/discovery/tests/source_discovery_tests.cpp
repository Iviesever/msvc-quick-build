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
    const fs::path helper_a_header = tree.root / "a" / "helper.hpp";
    const fs::path helper_a_source = tree.root / "a" / "helper.cpp";
    const fs::path helper_b_header = tree.root / "b" / "helper.hpp";
    const fs::path helper_b_source = tree.root / "b" / "helper.cpp";
    const fs::path include_dir = tree.root / "include";
    const fs::path widget_header = include_dir / "widget.hpp";
    const fs::path widget_source = include_dir / "widget.cpp";
    const fs::path other_main = tree.root / "tests" / "other_main.cpp";
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
    write_text(
        other_main,
        "#include \"../src/app.hpp\"\n"
        "int main() { return app(); }\n");
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
        expect(contains_source(discovered->sources, helper_a_source),
               "same-basename header ownership should connect helper A");
        expect(contains_source(discovered->sources, helper_b_source),
               "quoted include should connect helper B");
        expect(contains_source(discovered->sources, widget_source),
               "configured include directory plus same-basename ownership should connect widget.cpp");
        expect(!contains_source(discovered->sources, other_main),
               "reachable non-entry source defining main() must be excluded");
        expect(!contains_source(discovered->sources, unrelated_source),
               "disconnected source must not be discovered");
        expect(!contains_source(discovered->sources, comment_only),
               "include-like text in comments must not create graph edges");
        expect(!contains_source(discovered->sources, generated),
               "default build directories must not be indexed");
        expect(discovered->sources.size() == 6,
               "fixture should discover exactly entry + five connected non-main TUs");

        const auto repeated = mqb::discovery::SourceDiscovery::discover(request);
        expect(repeated.has_value() && repeated->sources == discovered->sources,
               "discovery output ordering should be deterministic");
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
