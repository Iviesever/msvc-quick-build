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
        if (candidate.lexically_normal() == normalized) return true;
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
        .root = fs::temp_directory_path() / ("mqb_module_discovery_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const fs::path main_cpp = tree.root / "main.cpp";
    const fs::path stats_cppm = tree.root / "modules" / "stats.cppm";
    const fs::path math_ixx = tree.root / "modules" / "math.ixx";
    const fs::path math_impl = tree.root / "modules" / "math_impl.cpp";
    const fs::path detail_mpp = tree.root / "modules" / "detail.mpp";
    const fs::path unused_mpp = tree.root / "modules" / "unused.mpp";

    write_text(
        main_cpp,
        "// import ignored.comment;\n"
        "const char* fake = \"import ignored.string;\";\n"
        "import app.stats;\n"
        "int main() { return 0; }\n");
    write_text(
        stats_cppm,
        "export module app.stats;\n"
        "export import math;\n"
        "export int stats();\n");
    write_text(
        math_ixx,
        "export module math;\n"
        "export import :detail;\n"
        "export int value();\n");
    write_text(
        math_impl,
        "module math;\n"
        "int value() { return 42; }\n");
    write_text(
        detail_mpp,
        "export module math:detail;\n"
        "export int detail() { return 1; }\n");
    write_text(
        unused_mpp,
        "export module unused;\n"
        "export int unused_value() { return 9; }\n");

    const auto discovered = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = main_cpp,
    });
    expect(discovered.has_value(), "named-module source discovery should succeed");
    if (discovered) {
        expect(!discovered->sources.empty() && discovered->sources.front() == main_cpp,
               "ordinary entry should remain first after module-aware discovery");
        expect(contains_source(discovered->sources, stats_cppm),
               "named import should select a .cppm interface provider");
        expect(contains_source(discovered->sources, math_ixx),
               "export import should transitively select a .ixx provider");
        expect(contains_source(discovered->sources, detail_mpp),
               "relative partition import should select its normalized .mpp provider");
        expect(contains_source(discovered->sources, math_impl),
               "same logical module implementation unit should follow its interface provider");
        expect(!contains_source(discovered->sources, unused_mpp),
               "unreferenced module interfaces must not be pulled into the target");
        expect(discovered->sources.size() == 5,
               "module chain should select exactly entry plus four required module TUs");

        const auto repeated = mqb::discovery::SourceDiscovery::discover({
            .project_root = tree.root,
            .entry = main_cpp,
        });
        expect(repeated.has_value() && repeated->sources == discovered->sources,
               "module-aware discovery ordering must remain deterministic");
    }

    const fs::path duplicate_main = tree.root / "duplicate_main.cpp";
    const fs::path duplicate_a = tree.root / "duplicates" / "first.ixx";
    const fs::path duplicate_b = tree.root / "duplicates" / "second.cppm";
    write_text(duplicate_main, "import duplicate;\nint main() { return 0; }\n");
    write_text(duplicate_a, "export module duplicate;\nexport int first();\n");
    write_text(duplicate_b, "export module duplicate;\nexport int second();\n");

    const auto duplicate = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = duplicate_main,
    });
    expect(duplicate.has_value(),
           "discovery must retain duplicate provider candidates for P1689 validation");
    if (duplicate) {
        expect(contains_source(duplicate->sources, duplicate_a)
                   && contains_source(duplicate->sources, duplicate_b),
               "all matching local interface candidates should be selected rather than guessed");
    }

    const auto excluded_provider = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = duplicate_main,
        .excluded_sources = {duplicate_a},
    });
    expect(excluded_provider.has_value(),
           "module interface should be usable as an exact excluded-source correction");
    if (excluded_provider) {
        expect(!contains_source(excluded_provider->sources, duplicate_a)
                   && contains_source(excluded_provider->sources, duplicate_b),
               "excluded module provider should be a traversal barrier while other candidates remain");
    }

    const fs::path plain_main = tree.root / "plain_main.cpp";
    write_text(plain_main, "int main() { return 0; }\n");
    const auto extra_module = mqb::discovery::SourceDiscovery::discover({
        .project_root = tree.root,
        .entry = plain_main,
        .extra_sources = {unused_mpp},
    });
    expect(extra_module.has_value(),
           "module interface should be usable as an exact extra-source correction");
    if (extra_module) {
        expect(contains_source(extra_module->sources, unused_mpp),
               "exact extra module interface should be retained even when disconnected");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_module_source_discovery_tests passed\n";
    return 0;
}
