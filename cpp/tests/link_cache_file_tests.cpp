#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
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
        .root = fs::temp_directory_path() / ("mqb_link_cache_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const std::vector<fs::path> objects{"obj/main.cpp.obj", "obj/math.cpp.obj"};
    const fs::path output{"bin/app.exe"};
    const mqb::LinkerIdentity linker{
        .linker = "C:/msvc/link.exe",
        .version = "14.51",
        .binary_stamp = "stamp-a",
    };
    mqb::LinkOptions options;
    options.libraries = {"user32.lib"};
    const auto signature = mqb::BuildSignature::for_link(objects, output, linker, options);
    const mqb::LinkCacheEntry entry{
        .linker = linker,
        .signature = signature,
        .objects = objects,
        .output = output,
    };

    const fs::path file = tree.root / "cache" / "app.linkcache";
    const auto missing = mqb::LinkCacheFile::load(file);
    expect(missing.has_value() && !missing->has_value(),
           "missing link cache should be a normal cache miss");

    const auto saved = mqb::LinkCacheFile::save(file, entry);
    expect(saved.has_value(), "valid link cache should save");
    const auto loaded = mqb::LinkCacheFile::load(file);
    expect(loaded.has_value() && loaded->has_value(), "saved link cache should load");
    if (loaded && *loaded) {
        expect((*loaded)->linker.linker == linker.linker, "linker path should round-trip");
        expect((*loaded)->linker.version == linker.version, "linker version should round-trip");
        expect((*loaded)->linker.binary_stamp == linker.binary_stamp, "linker stamp should round-trip");
        expect((*loaded)->signature == signature, "link signature should round-trip");
        expect((*loaded)->objects == objects, "link inputs should round-trip");
        expect((*loaded)->output == output, "link output should round-trip");
    }

    {
        std::fstream stream{file, std::ios::binary | std::ios::in | std::ios::out};
        char bad = 'X';
        stream.write(&bad, 1);
    }
    const auto bad_magic = mqb::LinkCacheFile::load(file);
    expect(!bad_magic.has_value(), "corrupt link-cache magic should be rejected");
    if (!bad_magic) {
        expect(bad_magic.error().code == mqb::LinkCacheFileErrorCode::invalid_magic,
               "corrupt magic should report invalid_magic");
    }

    const auto restored = mqb::LinkCacheFile::save(file, entry);
    expect(restored.has_value(), "link cache should be replaceable after corruption");
    std::error_code error_code;
    const auto size = fs::file_size(file, error_code);
    expect(!error_code && size > 8, "saved link cache should be non-trivial");
    if (!error_code && size > 8) {
        fs::resize_file(file, size - 5, error_code);
        expect(!error_code, "test should be able to truncate cache file");
    }
    const auto truncated = mqb::LinkCacheFile::load(file);
    expect(!truncated.has_value(), "truncated link cache should be rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_link_cache_file_tests passed\n";
    return 0;
}
