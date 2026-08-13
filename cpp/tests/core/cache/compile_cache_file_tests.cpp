#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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
        path_ = fs::temp_directory_path() / ("mqb-cache-file-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

mqb::ToolchainIdentity make_toolchain(const std::string_view version) {
    return mqb::ToolchainIdentity{
        .compiler = "toolchain/cl.exe",
        .version = std::string{version},
        .binary_stamp = "size=123;mtime=456",
    };
}

mqb::CompilerOptions make_options() {
    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;
    options.defines = {"FEATURE=1"};
    options.include_directories = {"include"};
    return options;
}

mqb::CompileCacheEntry make_entry(const std::string_view version = "19.50.10000") {
    mqb::TranslationUnit unit;
    unit.source = "src/main file.cpp";
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {
        mqb::Artifact{"cache/main file.obj", mqb::ArtifactKind::object},
    };

    const auto toolchain = make_toolchain(version);
    const auto options = make_options();
    return mqb::CompileCacheEntry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain,
        .signature = mqb::BuildSignature::for_compile(unit, toolchain, options),
        .outputs = unit.outputs,
        .dependencies = {
            fs::path{"src/main file.cpp"},
            fs::path{"include/a.hpp"},
            fs::path{"include/nested/b.hpp"},
        },
    };
}

mqb::CompileCacheEntry make_module_entry(const bool ifc_only) {
    mqb::TranslationUnit unit;
    unit.source = ifc_only ? fs::path{"include/value.hpp"} : fs::path{"src/math.ixx"};
    unit.kind = mqb::TranslationUnitKind::module_interface;
    if (!ifc_only) {
        unit.outputs.push_back(mqb::Artifact{
            .path = "cache/math.obj",
            .kind = mqb::ArtifactKind::object,
        });
    }
    unit.outputs.push_back(mqb::Artifact{
        .path = ifc_only ? fs::path{"ifc/value.ifc"} : fs::path{"ifc/math.ifc"},
        .kind = mqb::ArtifactKind::module_interface,
    });

    const auto toolchain = make_toolchain("19.51.modules");
    const auto options = make_options();
    return mqb::CompileCacheEntry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain,
        .signature = mqb::BuildSignature::for_compile(unit, toolchain, options),
        .outputs = unit.outputs,
        .dependencies = {fs::path{"include/shared.hpp"}},
    };
}

void write_bytes(const fs::path& file, const std::initializer_list<std::uint8_t> bytes) {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    for (const auto byte : bytes) {
        stream.put(static_cast<char>(byte));
    }
}

void expect_round_trip(
    const fs::path& file,
    const mqb::CompileCacheEntry& original,
    const std::string_view description) {
    const auto saved = mqb::CompileCacheFile::save(file, original);
    expect(saved.has_value(), std::string{description} + " should save");
    const auto loaded = mqb::CompileCacheFile::load(file);
    expect(loaded.has_value() && loaded->has_value(),
           std::string{description} + " should load");
    if (!loaded || !*loaded) return;

    const auto& entry = **loaded;
    expect(entry.source == original.source,
           std::string{description} + " source should round-trip");
    expect(entry.kind == original.kind,
           std::string{description} + " TU kind should round-trip");
    expect(entry.toolchain.compiler == original.toolchain.compiler,
           std::string{description} + " compiler path should round-trip");
    expect(entry.toolchain.version == original.toolchain.version,
           std::string{description} + " toolchain version should round-trip");
    expect(entry.toolchain.binary_stamp == original.toolchain.binary_stamp,
           std::string{description} + " binary stamp should round-trip");
    expect(entry.signature == original.signature,
           std::string{description} + " signature should round-trip");
    expect(entry.outputs.size() == original.outputs.size(),
           std::string{description} + " output count should round-trip");
    if (entry.outputs.size() == original.outputs.size()) {
        for (std::size_t index = 0; index < entry.outputs.size(); ++index) {
            expect(entry.outputs[index].path == original.outputs[index].path,
                   std::string{description} + " output path ordering should round-trip");
            expect(entry.outputs[index].kind == original.outputs[index].kind,
                   std::string{description} + " output kind ordering should round-trip");
        }
    }
    expect(entry.dependencies == original.dependencies,
           std::string{description} + " dependencies should round-trip");
}

} // namespace

int main() {
    TemporaryDirectory fixture;
    const fs::path file = fixture.path() / "nested" / "main.mqbcache";

    const auto missing = mqb::CompileCacheFile::load(file);
    expect(missing.has_value(), "missing cache file should be a normal cache miss");
    if (missing) {
        expect(!missing->has_value(), "missing cache file should return null optional entry");
    }

    const auto original = make_entry();
    expect_round_trip(file, original, "ordinary object-only cache entry");
    expect(fs::is_regular_file(file), "save should install the final cache file");

    const auto module_entry = make_module_entry(false);
    expect_round_trip(
        fixture.path() / "module.mqbcache",
        module_entry,
        "module object+IFC cache entry");

    const auto ifc_only_entry = make_module_entry(true);
    expect_round_trip(
        fixture.path() / "ifc-only.mqbcache",
        ifc_only_entry,
        "IFC-only cache entry");

    auto empty_outputs = original;
    empty_outputs.outputs.clear();
    const auto empty_output_save = mqb::CompileCacheFile::save(
        fixture.path() / "empty-output.mqbcache",
        empty_outputs);
    expect(!empty_output_save.has_value(),
           "cache serializer should reject entries without planned outputs");
    if (!empty_output_save) {
        expect(empty_output_save.error().code == mqb::CompileCacheFileErrorCode::file_write_failed,
               "empty planned outputs should report file_write_failed");
    }

    const auto replacement = make_entry("19.51.20000");
    expect(mqb::CompileCacheFile::save(file, replacement).has_value(),
           "saving an existing cache path should replace the previous entry");
    const auto reloaded = mqb::CompileCacheFile::load(file);
    expect(reloaded.has_value() && reloaded->has_value(), "replacement cache entry should load");
    if (reloaded && *reloaded) {
        expect((**reloaded).toolchain.version == "19.51.20000",
               "replacement should not leave stale metadata behind");
    }

    const fs::path bad_magic = fixture.path() / "bad-magic.mqbcache";
    write_bytes(bad_magic, {0, 1, 2, 3, 4, 5, 6, 7, 2, 0, 0, 0});
    const auto bad_magic_result = mqb::CompileCacheFile::load(bad_magic);
    expect(!bad_magic_result.has_value(), "invalid cache magic should fail loading");
    if (!bad_magic_result) {
        expect(bad_magic_result.error().code == mqb::CompileCacheFileErrorCode::invalid_magic,
               "invalid magic should have a precise error code");
    }

    const fs::path legacy_v1 = fixture.path() / "legacy-v1.mqbcache";
    write_bytes(legacy_v1, {'M', 'Q', 'B', 'C', 'A', 'C', 'H', 'E', 1, 0, 0, 0});
    const auto legacy_v1_result = mqb::CompileCacheFile::load(legacy_v1);
    expect(!legacy_v1_result.has_value(),
           "object-only v1 cache must not be interpreted as v2 planned-output metadata");
    if (!legacy_v1_result) {
        expect(legacy_v1_result.error().code == mqb::CompileCacheFileErrorCode::unsupported_version,
               "legacy v1 should request a conservative cache epoch rebuild");
    }

    const fs::path future_version = fixture.path() / "future.mqbcache";
    write_bytes(future_version, {'M', 'Q', 'B', 'C', 'A', 'C', 'H', 'E', 3, 0, 0, 0});
    const auto future_result = mqb::CompileCacheFile::load(future_version);
    expect(!future_result.has_value(), "future cache version should fail loading");
    if (!future_result) {
        expect(future_result.error().code == mqb::CompileCacheFileErrorCode::unsupported_version,
               "future version should have a precise error code");
    }

    const fs::path truncated = fixture.path() / "truncated.mqbcache";
    expect(mqb::CompileCacheFile::save(truncated, module_entry).has_value(),
           "truncation fixture should start as a valid multi-output cache file");
    fs::resize_file(truncated, 20);
    const auto truncated_result = mqb::CompileCacheFile::load(truncated);
    expect(!truncated_result.has_value(), "truncated cache file should never deserialize partially");

    const fs::path trailing = fixture.path() / "trailing.mqbcache";
    expect(mqb::CompileCacheFile::save(trailing, original).has_value(),
           "trailing-byte fixture should start as a valid cache file");
    {
        std::ofstream stream{trailing, std::ios::binary | std::ios::app};
        stream.put(static_cast<char>(0x7f));
    }
    const auto trailing_result = mqb::CompileCacheFile::load(trailing);
    expect(!trailing_result.has_value(), "trailing bytes should be rejected instead of ignored");
    if (!trailing_result) {
        expect(trailing_result.error().code == mqb::CompileCacheFileErrorCode::corrupt_data,
               "trailing bytes should report corrupt_data");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_compile_cache_file_tests passed\n";
    return 0;
}
