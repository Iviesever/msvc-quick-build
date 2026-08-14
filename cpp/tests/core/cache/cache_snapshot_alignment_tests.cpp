#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "mqb/core/ArchiveCache.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] fs::file_time_type time_at(const int seconds) {
    return fs::file_time_type{} + std::chrono::seconds{seconds};
}

} // namespace

int main() {
    const mqb::ToolchainIdentity compiler{
        .compiler = "C:/fake/cl.exe",
        .version = "19.50.test",
        .binary_stamp = "cl-stamp",
    };
    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = mqb::BuildConfiguration::debug;
    compiler_options.architecture = mqb::Architecture::x64;
    compiler_options.standard = mqb::CppStandard::cpp23;

    mqb::TranslationUnit unit;
    unit.source = "src/main.cpp";
    unit.outputs = {
        mqb::Artifact{.path = "obj/main.obj", .kind = mqb::ArtifactKind::object},
    };
    const std::vector<fs::path> dependencies{
        "include/a.hpp", "include/b.hpp", "include/c.hpp",
    };
    const mqb::CompileCacheEntry compile_entry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = compiler,
        .signature = mqb::BuildSignature::for_compile(unit, compiler, compiler_options),
        .outputs = unit.outputs,
        .dependencies = dependencies,
    };
    const mqb::FileSnapshot source_snapshot{
        .path = unit.source, .exists = true, .modified = time_at(10),
    };
    const std::vector<mqb::FileSnapshot> output_snapshots{
        {.path = "obj/main.obj", .exists = true, .modified = time_at(30)},
    };
    std::vector<mqb::FileSnapshot> dependency_snapshots{
        {.path = dependencies[0], .exists = true, .modified = time_at(20)},
        {.path = dependencies[1], .exists = true, .modified = time_at(21)},
        {.path = dependencies[2], .exists = true, .modified = time_at(22)},
    };

    const auto compile_aligned = mqb::CompileCacheValidator::validate(
        unit,
        compiler,
        compiler_options,
        compile_entry,
        source_snapshot,
        output_snapshots,
        dependency_snapshots);
    expect(compile_aligned.reusable(),
           "aligned compile snapshots should remain reusable");

    std::reverse(dependency_snapshots.begin(), dependency_snapshots.end());
    const auto compile_unordered = mqb::CompileCacheValidator::validate(
        unit,
        compiler,
        compiler_options,
        compile_entry,
        source_snapshot,
        output_snapshots,
        dependency_snapshots);
    expect(compile_unordered.reusable(),
           "unordered compile dependency snapshots should use path-search fallback");

    const std::vector<fs::path> objects{
        "obj/main.obj", "obj/math.obj", "obj/io.obj",
    };
    const std::vector<fs::path> libraries{
        "lib/a.lib", "lib/b.lib",
    };
    const fs::path linked_output{"bin/app.exe"};
    const mqb::LinkerIdentity linker{
        .linker = "C:/fake/link.exe",
        .version = "14.50.test",
        .binary_stamp = "link-stamp",
    };
    mqb::LinkOptions link_options;
    link_options.configuration = mqb::BuildConfiguration::debug;
    link_options.architecture = mqb::Architecture::x64;

    const mqb::LinkCacheEntry link_entry{
        .linker = linker,
        .signature = mqb::BuildSignature::for_link(
            objects, libraries, linked_output, linker, link_options),
        .objects = objects,
        .output = linked_output,
        .libraries = libraries,
    };
    const mqb::FileSnapshot linked_output_snapshot{
        .path = linked_output, .exists = true, .modified = time_at(50),
    };
    std::vector<mqb::FileSnapshot> object_snapshots{
        {.path = objects[0], .exists = true, .modified = time_at(40)},
        {.path = objects[1], .exists = true, .modified = time_at(41)},
        {.path = objects[2], .exists = true, .modified = time_at(42)},
    };
    std::vector<mqb::FileSnapshot> library_snapshots{
        {.path = libraries[0], .exists = true, .modified = time_at(43)},
        {.path = libraries[1], .exists = true, .modified = time_at(44)},
    };

    const auto link_aligned = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        linked_output,
        linker,
        link_options,
        link_entry,
        linked_output_snapshot,
        object_snapshots,
        library_snapshots);
    expect(link_aligned.reusable(),
           "aligned link snapshots should remain reusable");

    std::reverse(object_snapshots.begin(), object_snapshots.end());
    std::reverse(library_snapshots.begin(), library_snapshots.end());
    const auto link_unordered = mqb::LinkCacheValidator::validate(
        objects,
        libraries,
        linked_output,
        linker,
        link_options,
        link_entry,
        linked_output_snapshot,
        object_snapshots,
        library_snapshots);
    expect(link_unordered.reusable(),
           "unordered link snapshots should use path-search fallback");

    const fs::path archive_output{"bin/core.lib"};
    const mqb::LibrarianIdentity librarian{
        .librarian = "C:/fake/lib.exe",
        .version = "14.50.test",
        .binary_stamp = "lib-stamp",
    };
    const mqb::ArchiveCacheEntry archive_entry{
        .librarian = librarian,
        .signature = mqb::BuildSignature::for_archive(
            objects, archive_output, librarian, false),
        .objects = objects,
        .output = archive_output,
    };
    const mqb::FileSnapshot archive_output_snapshot{
        .path = archive_output, .exists = true, .modified = time_at(50),
    };

    std::reverse(object_snapshots.begin(), object_snapshots.end());
    const auto archive_aligned = mqb::ArchiveCacheValidator::validate(
        objects,
        archive_output,
        librarian,
        archive_entry,
        archive_output_snapshot,
        object_snapshots);
    expect(archive_aligned.reusable(),
           "aligned archive snapshots should remain reusable");

    std::reverse(object_snapshots.begin(), object_snapshots.end());
    const auto archive_unordered = mqb::ArchiveCacheValidator::validate(
        objects,
        archive_output,
        librarian,
        archive_entry,
        archive_output_snapshot,
        object_snapshots);
    expect(archive_unordered.reusable(),
           "unordered archive snapshots should use path-search fallback");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_cache_snapshot_alignment_tests passed\n";
    return 0;
}
