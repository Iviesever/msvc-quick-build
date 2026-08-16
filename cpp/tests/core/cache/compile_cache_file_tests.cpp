#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
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

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path()
            / ("mqb-cache-file-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

[[nodiscard]] mqb::ToolchainIdentity make_toolchain(
    const std::string_view version) {
    return mqb::ToolchainIdentity{
        .compiler = "toolchain/cl.exe",
        .version = std::string{version},
        .binary_stamp = "size=123;mtime=456",
    };
}

[[nodiscard]] mqb::CompilerOptions make_options() {
    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;
    options.defines = {"FEATURE=1"};
    options.include_directories = {"include"};
    return options;
}

[[nodiscard]] mqb::FileSnapshot snapshot(
    const fs::path& path,
    const std::int64_t ticks) {
    return mqb::FileSnapshot{
        .path = path,
        .exists = true,
        .modified = fs::file_time_type{
            std::chrono::duration_cast<fs::file_time_type::duration>(
                std::chrono::nanoseconds{ticks})},
    };
}

[[nodiscard]] bool same_snapshot(
    const mqb::FileSnapshot& left,
    const mqb::FileSnapshot& right) {
    return left.path == right.path
        && left.exists == right.exists
        && left.modified == right.modified;
}

[[nodiscard]] mqb::CompileCacheEntry make_entry(
    const std::string_view version = "19.50.10000") {
    mqb::TranslationUnit unit;
    unit.source = "src/main file.cpp";
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {
        mqb::Artifact{
            "cache/main file.obj",
            mqb::ArtifactKind::object,
        },
    };

    const auto toolchain = make_toolchain(version);
    const auto options = make_options();
    return mqb::CompileCacheEntry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain,
        .signature = mqb::BuildSignature::for_compile(
            unit,
            toolchain,
            options),
        .outputs = unit.outputs,
        .dependencies = {
            fs::path{"src/main file.cpp"},
            fs::path{"include/a.hpp"},
            fs::path{"include/nested/b.hpp"},
        },
        .include_search_roots = {
            fs::path{"include/first"},
            fs::path{"include/second"},
            fs::path{"toolchain/include"},
        },
    };
}

[[nodiscard]] mqb::CompileCacheEntry make_module_entry(
    const bool ifc_only) {
    mqb::TranslationUnit unit;
    unit.source = ifc_only
        ? fs::path{"include/value.hpp"}
        : fs::path{"src/math.ixx"};
    unit.kind = mqb::TranslationUnitKind::module_interface;
    if (!ifc_only) {
        unit.outputs.push_back(mqb::Artifact{
            .path = "cache/math.obj",
            .kind = mqb::ArtifactKind::object,
        });
    }
    unit.outputs.push_back(mqb::Artifact{
        .path = ifc_only
            ? fs::path{"ifc/value.ifc"}
            : fs::path{"ifc/math.ifc"},
        .kind = mqb::ArtifactKind::module_interface,
    });

    const auto toolchain = make_toolchain("19.51.modules");
    const auto options = make_options();
    mqb::CompileCacheEntry entry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain,
        .signature = mqb::BuildSignature::for_compile(
            unit,
            toolchain,
            options),
        .outputs = unit.outputs,
        .dependencies = {fs::path{"include/shared.hpp"}},
        .include_search_roots = {
            fs::path{"include"},
            fs::path{"toolchain/include"},
        },
    };
    if (!ifc_only) {
        entry.module_scan = mqb::ModuleScanEvidence{
            .signature = mqb::BuildSignature::for_module_scan(
                unit.source,
                unit.kind,
                toolchain,
                options),
            .source = snapshot(unit.source, 100),
            .output = snapshot("scan/math.json", 200),
            .dependencies = {
                snapshot("include/shared.hpp", 150),
                snapshot("include/config.hpp", 175),
            },
        };
    }
    return entry;
}

void write_bytes(
    const fs::path& file,
    const std::initializer_list<std::uint8_t> bytes) {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    for (const auto byte : bytes) {
        stream.put(static_cast<char>(byte));
    }
}

[[nodiscard]] std::vector<char> read_all(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    return std::vector<char>{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
}

void write_all(const fs::path& file, const std::vector<char>& bytes) {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void set_format_version(std::vector<char>& bytes, const std::uint32_t version) {
    if (bytes.size() < 12) return;
    bytes[8] = static_cast<char>(version & 0xffu);
    bytes[9] = static_cast<char>((version >> 8u) & 0xffu);
    bytes[10] = static_cast<char>((version >> 16u) & 0xffu);
    bytes[11] = static_cast<char>((version >> 24u) & 0xffu);
}

void expect_round_trip(
    const fs::path& file,
    const mqb::CompileCacheEntry& original,
    const std::string_view description) {
    expect(
        mqb::CompileCacheFile::save(file, original).has_value(),
        std::string{description} + " should save");

    const auto loaded = mqb::CompileCacheFile::load(file);
    expect(
        loaded && *loaded,
        std::string{description} + " should load");
    if (!loaded || !*loaded) return;

    const auto& entry = **loaded;
    expect(
        entry.source == original.source,
        std::string{description} + " source should round-trip");
    expect(
        entry.kind == original.kind,
        std::string{description} + " TU kind should round-trip");
    expect(
        entry.toolchain.compiler == original.toolchain.compiler
            && entry.toolchain.version == original.toolchain.version
            && entry.toolchain.binary_stamp == original.toolchain.binary_stamp,
        std::string{description} + " toolchain should round-trip");
    expect(
        entry.signature == original.signature,
        std::string{description} + " signature should round-trip");
    expect(
        entry.outputs.size() == original.outputs.size(),
        std::string{description} + " output count should round-trip");
    if (entry.outputs.size() == original.outputs.size()) {
        for (std::size_t index = 0; index < entry.outputs.size(); ++index) {
            expect(
                entry.outputs[index].path == original.outputs[index].path
                    && entry.outputs[index].kind == original.outputs[index].kind,
                "output should round-trip");
        }
    }
    expect(
        entry.dependencies == original.dependencies,
        std::string{description} + " dependencies should round-trip");
    expect(
        entry.include_search_roots == original.include_search_roots,
        std::string{description} + " ordered include-search roots should round-trip");
    expect(
        entry.module_scan.has_value() == original.module_scan.has_value(),
        std::string{description} + " scan-evidence presence should round-trip");

    if (entry.module_scan && original.module_scan) {
        expect(
            entry.module_scan->signature == original.module_scan->signature,
            "scan signature should round-trip");
        expect(
            same_snapshot(entry.module_scan->source, original.module_scan->source),
            "scan source snapshot should round-trip");
        expect(
            same_snapshot(entry.module_scan->output, original.module_scan->output),
            "scan output snapshot should round-trip");
        expect(
            entry.module_scan->dependencies.size()
                == original.module_scan->dependencies.size(),
            "scan dependency count should round-trip");
        if (entry.module_scan->dependencies.size()
            == original.module_scan->dependencies.size()) {
            for (std::size_t index = 0;
                 index < entry.module_scan->dependencies.size();
                 ++index) {
                expect(
                    same_snapshot(
                        entry.module_scan->dependencies[index],
                        original.module_scan->dependencies[index]),
                    "scan dependency snapshot should round-trip");
            }
        }
    }
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path file = fixture.path() / "nested/main.mqbcache";

    const auto missing = mqb::CompileCacheFile::load(file);
    expect(
        missing && !missing->has_value(),
        "missing cache file should be a normal cache miss");

    const auto original = make_entry();
    expect_round_trip(file, original, "ordinary object-only cache entry");
    expect(fs::is_regular_file(file), "save should install final cache file");

    const auto module_entry = make_module_entry(false);
    const fs::path module_file = fixture.path() / "module.mqbcache";
    expect_round_trip(
        module_file,
        module_entry,
        "module object+IFC cache entry with scan evidence");
    expect_round_trip(
        fixture.path() / "ifc-only.mqbcache",
        make_module_entry(true),
        "IFC-only cache entry");

    // Cache v4 inserts an ordered include-root vector between dependencies and
    // the v3 scan-evidence marker. Build v2/v3 fixtures from a v4 entry with an
    // empty root vector so removing the final four-byte root count recreates
    // the old payload exactly (plus/removing the v3 presence marker).
    auto legacy = original;
    legacy.include_search_roots.clear();
    legacy.module_scan.reset();

    const fs::path v3_file = fixture.path() / "legacy-v3.mqbcache";
    expect(
        mqb::CompileCacheFile::save(v3_file, legacy).has_value(),
        "v3 fixture should start as v4");
    auto v3_bytes = read_all(v3_file);
    expect(v3_bytes.size() > 16, "v3 fixture should contain root count and scan marker");
    if (v3_bytes.size() > 16) {
        // The v4 tail for zero roots/no scan is: root_count(u32=0), present(u8=0).
        v3_bytes.erase(v3_bytes.end() - 5, v3_bytes.end() - 1);
        set_format_version(v3_bytes, 3);
        write_all(v3_file, v3_bytes);
        const auto v3 = mqb::CompileCacheFile::load(v3_file);
        expect(v3 && *v3, "legacy v3 should load");
        if (v3 && *v3) {
            expect((**v3).include_search_roots.empty(), "legacy v3 should have no root identity evidence");
            expect(!(**v3).module_scan, "legacy v3 no-scan fixture should remain no-scan");
        }
    }

    const fs::path v2_file = fixture.path() / "legacy-v2.mqbcache";
    expect(
        mqb::CompileCacheFile::save(v2_file, legacy).has_value(),
        "v2 fixture should start as v4");
    auto v2_bytes = read_all(v2_file);
    expect(v2_bytes.size() > 16, "v2 fixture should contain root count and scan marker");
    if (v2_bytes.size() > 16) {
        v2_bytes.resize(v2_bytes.size() - 5);
        set_format_version(v2_bytes, 2);
        write_all(v2_file, v2_bytes);
        const auto v2 = mqb::CompileCacheFile::load(v2_file);
        expect(v2 && *v2, "legacy v2 should load");
        if (v2 && *v2) {
            expect((**v2).include_search_roots.empty(), "legacy v2 should have no root identity evidence");
            expect(!(**v2).module_scan, "legacy v2 should load without scan evidence");
        }
    }

    auto empty_outputs = original;
    empty_outputs.outputs.clear();
    expect(
        !mqb::CompileCacheFile::save(
            fixture.path() / "empty.mqbcache",
            empty_outputs),
        "empty outputs should be rejected");

    const auto replacement = make_entry("19.51.20000");
    expect(
        mqb::CompileCacheFile::save(file, replacement).has_value(),
        "replacement should save");
    const auto reloaded = mqb::CompileCacheFile::load(file);
    expect(
        reloaded && *reloaded
            && (**reloaded).toolchain.version == "19.51.20000",
        "replacement should not leave stale metadata");

    const fs::path bad_magic = fixture.path() / "bad.mqbcache";
    write_bytes(bad_magic, {0, 1, 2, 3, 4, 5, 6, 7, 4, 0, 0, 0});
    const auto bad_result = mqb::CompileCacheFile::load(bad_magic);
    expect(!bad_result, "bad magic should fail");
    if (!bad_result) {
        expect(
            bad_result.error().code
                == mqb::CompileCacheFileErrorCode::invalid_magic,
            "bad magic should report invalid_magic");
    }

    const fs::path v1 = fixture.path() / "v1.mqbcache";
    write_bytes(v1, {'M', 'Q', 'B', 'C', 'A', 'C', 'H', 'E', 1, 0, 0, 0});
    const auto v1_result = mqb::CompileCacheFile::load(v1);
    expect(!v1_result, "v1 should fail");
    if (!v1_result) {
        expect(
            v1_result.error().code
                == mqb::CompileCacheFileErrorCode::unsupported_version,
            "v1 should report unsupported_version");
    }

    const fs::path future = fixture.path() / "future.mqbcache";
    write_bytes(future, {'M', 'Q', 'B', 'C', 'A', 'C', 'H', 'E', 5, 0, 0, 0});
    const auto future_result = mqb::CompileCacheFile::load(future);
    expect(!future_result, "future version should fail");
    if (!future_result) {
        expect(
            future_result.error().code
                == mqb::CompileCacheFileErrorCode::unsupported_version,
            "future version should report unsupported_version");
    }

    const fs::path truncated = fixture.path() / "truncated.mqbcache";
    expect(
        mqb::CompileCacheFile::save(truncated, module_entry).has_value(),
        "truncation fixture should start valid");
    fs::resize_file(truncated, 20);
    expect(
        !mqb::CompileCacheFile::load(truncated),
        "truncated file should fail to deserialize");

    const fs::path trailing = fixture.path() / "trailing.mqbcache";
    expect(
        mqb::CompileCacheFile::save(trailing, original).has_value(),
        "trailing-byte fixture should start valid");
    {
        std::ofstream stream{trailing, std::ios::binary | std::ios::app};
        stream.put(static_cast<char>(0x7f));
    }
    const auto trailing_result = mqb::CompileCacheFile::load(trailing);
    expect(!trailing_result, "trailing bytes should fail");
    if (!trailing_result) {
        expect(
            trailing_result.error().code
                == mqb::CompileCacheFileErrorCode::corrupt_data,
            "trailing bytes should report corrupt_data");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_compile_cache_file_tests passed\n";
    return 0;
}
