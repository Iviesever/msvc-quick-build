#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

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

mqb::CompileCacheEntry make_entry(const std::string_view version = "19.50.10000") {
    mqb::TranslationUnit unit;
    unit.source = "src/main file.cpp";
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {
        mqb::Artifact{"cache/main file.obj", mqb::ArtifactKind::object},
    };

    mqb::ToolchainIdentity toolchain{
        .compiler = "toolchain/cl.exe",
        .version = std::string{version},
        .binary_stamp = "size=123;mtime=456",
    };

    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;
    options.defines = {"FEATURE=1"};
    options.include_directories = {"include"};

    return mqb::CompileCacheEntry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain,
        .signature = mqb::BuildSignature::for_compile(unit, toolchain, options),
        .object = unit.outputs.front(),
        .dependencies = {
            fs::path{"src/main file.cpp"},
            fs::path{"include/a.hpp"},
            fs::path{"include/nested/b.hpp"},
        },
    };
}

void write_bytes(const fs::path& file, const std::initializer_list<std::uint8_t> bytes) {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    for (const auto byte : bytes) {
        stream.put(static_cast<char>(byte));
    }
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
    const auto save_result = mqb::CompileCacheFile::save(file, original);
    expect(save_result.has_value(), "cache entry should save to a newly created parent directory");
    expect(fs::is_regular_file(file), "save should install the final cache file");

    const auto loaded = mqb::CompileCacheFile::load(file);
    expect(loaded.has_value() && loaded->has_value(), "saved cache entry should load successfully");
    if (loaded && *loaded) {
        const auto& entry = **loaded;
        expect(entry.source == original.source, "source path should round-trip");
        expect(entry.kind == original.kind, "translation-unit kind should round-trip");
        expect(entry.toolchain.compiler == original.toolchain.compiler,
               "compiler path should round-trip");
        expect(entry.toolchain.version == original.toolchain.version,
               "toolchain version should round-trip");
        expect(entry.toolchain.binary_stamp == original.toolchain.binary_stamp,
               "toolchain binary stamp should round-trip");
        expect(entry.signature == original.signature, "signature digest should round-trip exactly");
        expect(entry.object.path == original.object.path, "object path should round-trip");
        expect(entry.object.kind == original.object.kind, "artifact kind should round-trip");
        expect(entry.dependencies == original.dependencies, "dependency ordering should round-trip");
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
    write_bytes(bad_magic, {0, 1, 2, 3, 4, 5, 6, 7, 1, 0, 0, 0});
    const auto bad_magic_result = mqb::CompileCacheFile::load(bad_magic);
    expect(!bad_magic_result.has_value(), "invalid cache magic should fail loading");
    if (!bad_magic_result) {
        expect(bad_magic_result.error().code == mqb::CompileCacheFileErrorCode::invalid_magic,
               "invalid magic should have a precise error code");
    }

    const fs::path unsupported = fixture.path() / "unsupported.mqbcache";
    write_bytes(unsupported, {'M', 'Q', 'B', 'C', 'A', 'C', 'H', 'E', 2, 0, 0, 0});
    const auto unsupported_result = mqb::CompileCacheFile::load(unsupported);
    expect(!unsupported_result.has_value(), "unsupported cache version should fail loading");
    if (!unsupported_result) {
        expect(unsupported_result.error().code == mqb::CompileCacheFileErrorCode::unsupported_version,
               "unsupported version should have a precise error code");
    }

    const fs::path truncated = fixture.path() / "truncated.mqbcache";
    expect(mqb::CompileCacheFile::save(truncated, original).has_value(),
           "truncation fixture should start as a valid cache file");
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
