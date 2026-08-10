#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/core/ProjectArtifactLayout.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    const fs::path root = fs::path{"C:/work/project"}.lexically_normal();
    auto layout = mqb::ProjectArtifactLayout::create(root);
    expect(layout.has_value(), "non-empty project root should create an artifact layout");
    if (!layout) {
        return 1;
    }

    auto src_foo = layout->for_source(root / "src" / "foo.cpp");
    auto tests_foo = layout->for_source(root / "tests" / "foo.cpp");
    auto src_cxx = layout->for_source(root / "src" / "foo.cxx");
    expect(src_foo.has_value() && tests_foo.has_value() && src_cxx.has_value(),
           "ordinary project sources should map to artifacts");
    if (src_foo && tests_foo && src_cxx) {
        expect(src_foo->object != tests_foo->object,
               "same basename in different directories must not collide");
        expect(src_foo->object != src_cxx->object,
               "same stem with different source extensions must not collide");
        expect(src_foo->object == root / ".mqb" / "obj" / "src" / "foo.cpp.obj",
               "in-project object path should preserve relative source identity");
        expect(src_foo->dependencies == root / ".mqb" / "deps" / "src" / "foo.cpp.json",
               "dependency metadata should preserve relative source identity");
        expect(src_foo->compile_cache
                   == root / ".mqb" / "cache" / "compile" / "src" / "foo.cpp.mqbcache",
               "compile cache should preserve relative source identity");
    }

    auto outside = layout->for_source(fs::path{"D:/vendor/foo.cpp"});
    expect(outside.has_value(), "external sources should still receive a stable artifact key");
    if (outside) {
        const auto generic = outside->object.generic_string();
        expect(generic.find(".mqb/obj/.external/") != std::string::npos,
               "external source artifacts should be isolated under .external hash namespace");
        expect(generic.ends_with("/foo.cpp.obj"),
               "external artifact should retain source filename for diagnostics");
    }

    auto target = layout->for_target("demo");
    expect(target.has_value(), "simple target name should map to target artifacts");
    if (target) {
        expect(target->executable == root / ".mqb" / "bin" / "demo.exe",
               "target executable should live in project-level bin directory");
        expect(target->link_cache == root / ".mqb" / "cache" / "link" / "demo.linkcache",
               "link cache should be isolated from compile cache metadata");
    }

    expect(!layout->for_target("../demo"), "target names with parent traversal must be rejected");
    expect(!mqb::ProjectArtifactLayout::create({}), "empty project root must be rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_project_artifact_layout_tests passed\n";
    return 0;
}
