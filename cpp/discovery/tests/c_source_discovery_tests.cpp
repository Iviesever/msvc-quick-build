#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

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

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
    std::error_code error;
    return fs::equivalent(left, right, error) && !error;
}
} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{.root = fs::temp_directory_path() / ("mqb_c_discovery_" + std::to_string(unique))};

    const fs::path header = tree.root / "shared.h";
    const fs::path main_c = tree.root / "main.c";
    const fs::path helper_c = tree.root / "helper.c";

    write_text(header, "#pragma once\nint helper(void);\n");
    write_text(main_c, R"c(#include "shared.h"
typedef int module;
module local_module_identifier;
typedef int import;
import local_import_identifier;
int main(void) { return helper() == 42 ? 0 : 1; }
)c");
    write_text(helper_c, R"c(#include "shared.h"
typedef int export;
export local_export_identifier;
int helper(void) { return 42; }
)c");

    mqb::discovery::Request request{
        .project_root = tree.root,
        .entry = main_c,
    };
    auto result = mqb::discovery::SourceDiscovery::discover(request);
    expect(result.has_value(), "C entry should participate in ordinary smart discovery");
    if (result) {
        expect(result->sources.size() == 2,
               "shared local header should connect main.c and helper.c");
        if (result->sources.size() == 2) {
            expect(same_path(result->sources[0], main_c),
                   "C entry should remain first in deterministic discovery order");
            expect(same_path(result->sources[1], helper_c),
                   "connected C helper should be selected as an ordinary TU");
        }
        expect(!result->requires_module_pipeline,
               "legal C identifiers named module/import/export must not route into the C++ module pipeline");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_c_source_discovery_tests passed\n";
    return 0;
}
