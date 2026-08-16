#include <chrono>
#include <filesystem>
#include <fstream>
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
    if (!layout) return 1;

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
        expect(src_foo->module_dependencies == root / ".mqb" / "scan" / "src" / "foo.cpp.json",
               "module scan metadata should preserve relative source identity");
        expect(src_foo->module_interface == root / ".mqb" / "ifc" / "src" / "foo.cpp.ifc",
               "compiled module interface path should preserve relative source identity");
        expect(src_foo->compile_cache == root / ".mqb" / "cache" / "compile" / "src" / "foo.cpp.mqbcache",
               "compile cache should preserve relative source identity");
        expect(src_foo->module_dependencies != tests_foo->module_dependencies
                   && src_foo->module_interface != tests_foo->module_interface,
               "same-basename module artifacts in different directories must not collide");
    }

    auto partition = layout->for_source(root / "modules" / "math-part.ixx");
    expect(partition.has_value(), "module interface source should map to module artifacts");
    if (partition) {
        expect(partition->module_interface == root / ".mqb" / "ifc" / "modules" / "math-part.ixx.ifc",
               "IFC filename should come from source identity rather than logical module spelling");
        expect(partition->module_interface.filename().generic_string().find(':') == std::string::npos,
               "IFC filename must not require logical-name characters such as partition ':'");
    }

    auto outside = layout->for_source(fs::path{"D:/vendor/foo.cpp"});
    expect(outside.has_value(), "external sources should still receive a stable artifact key");
    if (outside) {
        const auto generic = outside->object.generic_string();
        expect(generic.find(".mqb/obj/.external/") != std::string::npos,
               "external source artifacts should be isolated under .external hash namespace");
        expect(generic.ends_with("/foo.cpp.obj"),
               "external artifact should retain source filename for diagnostics");
        expect(outside->module_interface.generic_string().find(".mqb/ifc/.external/") != std::string::npos,
               "external IFCs should share the stable external-source namespace");
    }

    auto executable = layout->for_target("demo", mqb::TargetKind::executable);
    auto dll = layout->for_target("demo", mqb::TargetKind::dynamic_library);
    auto static_library = layout->for_target("demo", mqb::TargetKind::static_library);
    expect(executable.has_value() && dll.has_value() && static_library.has_value(),
           "all typed target kinds should map to deterministic artifacts");
    if (executable && dll && static_library) {
        expect(executable->executable == root / ".mqb" / "bin" / "demo.exe",
               "executable target should retain historical output path");
        expect(dll->executable == root / ".mqb" / "bin" / "demo.dll",
               "DLL target should map to .dll output");
        expect(static_library->executable == root / ".mqb" / "bin" / "demo.lib",
               "static target should map to .lib output");
        expect(executable->link_cache == root / ".mqb" / "cache" / "link" / "demo.linkcache"
                   && dll->link_cache == executable->link_cache,
               "exe/DLL should preserve the established link-cache path");
        expect(static_library->link_cache
                   == root / ".mqb" / "cache" / "archive" / "demo.archivecache",
               "static target should use its own archive-cache namespace");
    }

#ifdef _WIN32
    // Existing physical paths are normalized before deriving source identity.
    // The project root deliberately contains non-ASCII components: ASCII case
    // aliases must collapse without feeding the UTF-8 bytes through tolower.
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path unicode_parent = fs::temp_directory_path() / fs::path{u8"MQB_项目_テスト"};
    const fs::path physical_root = unicode_parent
        / ("MqB_Artifact_Alias_" + std::to_string(tick));
    const fs::path physical_header = physical_root / fs::path{u8"Include/模块Util.hpp"};
    fs::create_directories(physical_header.parent_path());
    {
        std::ofstream file{physical_header, std::ios::binary | std::ios::trunc};
        file << "#pragma once\n";
    }

    auto physical_layout = mqb::ProjectArtifactLayout::create(physical_root);
    expect(physical_layout.has_value(), "Unicode physical project root should create a layout");
    if (physical_layout) {
        std::string alias_component = physical_root.filename().string();
        for (auto& ch : alias_component) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        const fs::path root_alias = physical_root.parent_path() / alias_component;
        const fs::path source_alias = root_alias / fs::path{u8"include/模块util.hpp"};
        auto real_artifacts = physical_layout->for_source(physical_header);
        auto alias_artifacts = physical_layout->for_source(source_alias);
        expect(real_artifacts.has_value() && alias_artifacts.has_value(),
               "case aliases inside a Unicode Windows project should map to artifacts");
        if (real_artifacts && alias_artifacts) {
            expect(real_artifacts->object == alias_artifacts->object
                       && real_artifacts->dependencies == alias_artifacts->dependencies
                       && real_artifacts->module_dependencies == alias_artifacts->module_dependencies
                       && real_artifacts->module_interface == alias_artifacts->module_interface
                       && real_artifacts->compile_cache == alias_artifacts->compile_cache,
                   "one Unicode Windows source must have one stable artifact identity across ASCII case aliases");
        }
    }
    std::error_code cleanup_error;
    fs::remove_all(unicode_parent, cleanup_error);
#endif

    expect(!layout->for_target("../demo"), "target names with parent traversal must be rejected");
    expect(!mqb::ProjectArtifactLayout::create({}), "empty project root must be rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_project_artifact_layout_tests passed\n";
    return 0;
}
