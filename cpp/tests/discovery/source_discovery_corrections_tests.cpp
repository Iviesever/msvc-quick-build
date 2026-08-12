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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
}
bool contains(const std::vector<fs::path>& sources, const fs::path& source) {
    for (const auto& candidate : sources) {
        if (candidate.lexically_normal() == source.lexically_normal()) return true;
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
        .root = fs::temp_directory_path() / ("mqb_discovery_corrections_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path main_cpp = tree.root / "main.cpp";
    const fs::path prod_hpp = tree.root / "prod.hpp";
    const fs::path prod_cpp = tree.root / "prod.cpp";
    const fs::path legacy_hpp = tree.root / "legacy.hpp";
    const fs::path legacy_cpp = tree.root / "legacy.cpp";
    const fs::path private_hpp = tree.root / "private.hpp";
    const fs::path private_cpp = tree.root / "private.cpp";
    const fs::path extra_cpp = tree.root / "manual.cpp";
    const fs::path tests_dir = tree.root / "tests";
    const fs::path test_cpp = tests_dir / "test_helper.cpp";

    write_text(main_cpp,
        "#include \"prod.hpp\"\n"
        "#include \"legacy.hpp\"\n"
        "int main() { return prod() + legacy(); }\n");
    write_text(prod_hpp, "#pragma once\nint prod();\n");
    write_text(prod_cpp, "#include \"prod.hpp\"\nint prod() { return 1; }\n");
    write_text(legacy_hpp, "#pragma once\nint legacy();\n");
    write_text(legacy_cpp,
        "#include \"legacy.hpp\"\n"
        "#include \"private.hpp\"\n"
        "int legacy() { return private_value(); }\n");
    write_text(private_hpp, "#pragma once\nint private_value();\n");
    write_text(private_cpp, "#include \"private.hpp\"\nint private_value() { return 9; }\n");
    write_text(extra_cpp, "int manually_added() { return 7; }\n");
    write_text(test_cpp,
        "#include \"../prod.hpp\"\n"
        "int test_helper() { return prod(); }\n");

    const mqb::discovery::Request request{
        .project_root = tree.root,
        .entry = main_cpp,
        .include_directories = {},
        .excluded_directories = {tests_dir},
        .extra_sources = {extra_cpp},
        .excluded_sources = {legacy_cpp},
    };
    auto discovered = mqb::discovery::SourceDiscovery::discover(request);
    expect(discovered.has_value(), "configured discovery should succeed");
    if (discovered) {
        expect(discovered->sources.size() == 3,
               "configured discovery should select entry, production TU, and exact extra TU");
        expect(!discovered->sources.empty() && discovered->sources.front() == main_cpp,
               "entry should remain first after configured corrections");
        expect(contains(discovered->sources, prod_cpp),
               "production source connected through header should remain selected");
        expect(contains(discovered->sources, extra_cpp),
               "disconnected exact extra source should be selected");
        expect(!contains(discovered->sources, legacy_cpp),
               "exact excluded source should not be selected");
        expect(!contains(discovered->sources, private_cpp),
               "excluded source should be a traversal barrier to its private subgraph");
        expect(!contains(discovered->sources, test_cpp),
               "configured excluded directory should be pruned before graph construction");
    }

    auto entry_excluded = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = main_cpp,
        .include_directories = {},
        .excluded_directories = {},
        .extra_sources = {},
        .excluded_sources = {main_cpp},
    });
    expect(!entry_excluded
               && entry_excluded.error().code == mqb::discovery::ErrorCode::invalid_correction,
           "entry source must not be excludable");

    const fs::path second_main = tree.root / "manual_main.cpp";
    write_text(second_main, "int main() { return 1; }\n");
    auto extra_main = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = main_cpp,
        .include_directories = {},
        .excluded_directories = {},
        .extra_sources = {second_main},
        .excluded_sources = {},
    });
    expect(!extra_main
               && extra_main.error().code == mqb::discovery::ErrorCode::invalid_correction,
           "explicit extra source defining another main must be rejected");

    auto conflicting = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = main_cpp,
        .include_directories = {},
        .excluded_directories = {},
        .extra_sources = {extra_cpp},
        .excluded_sources = {extra_cpp},
    });
    expect(!conflicting
               && conflicting.error().code == mqb::discovery::ErrorCode::invalid_correction,
           "same exact source cannot be both extra and excluded");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_source_discovery_corrections_tests passed\n";
    return 0;
}
