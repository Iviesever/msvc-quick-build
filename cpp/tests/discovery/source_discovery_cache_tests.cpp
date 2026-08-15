#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

void force_newer_timestamp(const fs::path& path) {
    std::error_code error_code;
    const auto current = fs::last_write_time(path, error_code);
    if (!error_code) {
        fs::last_write_time(path, current + std::chrono::seconds{2}, error_code);
    }
}

void force_cache_format_version(const fs::path& cache_file, const std::uint32_t version) {
    std::fstream stream{cache_file, std::ios::binary | std::ios::in | std::ios::out};
    if (!stream) return;
    const std::array<char, 4> bytes{
        static_cast<char>(version & 0xffu),
        static_cast<char>((version >> 8u) & 0xffu),
        static_cast<char>((version >> 16u) & 0xffu),
        static_cast<char>((version >> 24u) & 0xffu),
    };
    stream.seekp(8, std::ios::beg);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
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
        .root = fs::temp_directory_path()
            / ("mqb_source_discovery_cache_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root / "src");

    const fs::path entry = tree.root / "main.cpp";
    const fs::path header = tree.root / "src" / "value.hpp";
    const fs::path implementation = tree.root / "src" / "value.cpp";
    write_text(
        entry,
        "#include \"src/value.hpp\"\n"
        "int main() { return value(); }\n");
    write_text(header, "#pragma once\nint value();\n");
    write_text(
        implementation,
        "#include \"value.hpp\"\n"
        "int value() { return 0; }\n");

    const mqb::discovery::Request request{
        .project_root = tree.root,
        .entry = entry,
        .include_directories = {},
    };
    const fs::path default_cache =
        tree.root / ".mqb" / "cache" / "discovery" / "source-discovery.mqbcache";

    const auto cold = mqb::discovery::SourceDiscovery::discover(request);
    expect(cold.has_value(), "cold discovery should succeed");
    if (cold) {
        expect(!cold->reused, "cold discovery must not report persistent reuse");
        expect(contains_source(cold->sources, implementation),
               "cold discovery should select the implementation source");
    }
    expect(fs::is_regular_file(default_cache),
           "successful cacheable discovery should persist project-local evidence");

    const auto warm = mqb::discovery::SourceDiscovery::discover(request);
    expect(warm.has_value(), "warm discovery should succeed");
    if (warm && cold) {
        expect(warm->reused, "unchanged discovery should reuse persistent evidence");
        expect(warm->sources == cold->sources,
               "persistent discovery reuse must preserve exact source ordering");
        expect(warm->indexed_files == cold->indexed_files,
               "persistent discovery reuse must preserve indexed-file accounting");
    }

    force_cache_format_version(default_cache, 1u);
    const auto stale_format = mqb::discovery::SourceDiscovery::discover(request);
    expect(stale_format.has_value(), "stale-format discovery cache should fall back safely");
    if (stale_format) {
        expect(!stale_format->reused,
               "previous discovery-semantics cache versions must not be reused");
        expect(contains_source(stale_format->sources, implementation),
               "format-version fallback must preserve fresh discovery correctness");
    }
    const auto resealed_format = mqb::discovery::SourceDiscovery::discover(request);
    expect(resealed_format.has_value() && resealed_format->reused,
           "stale-format fallback should reseal evidence using the current format");

    write_text(header, "#pragma once\nint value();\n// changed\n");
    force_newer_timestamp(header);
    const auto header_changed = mqb::discovery::SourceDiscovery::discover(request);
    expect(header_changed.has_value(), "header mutation discovery should succeed");
    if (header_changed) {
        expect(!header_changed->reused,
               "indexed header timestamp changes must invalidate discovery reuse");
    }
    const auto resealed_after_header = mqb::discovery::SourceDiscovery::discover(request);
    expect(resealed_after_header.has_value() && resealed_after_header->reused,
           "successful header-triggered rediscovery should reseal warm evidence");

    const fs::path new_source = tree.root / "src" / "unrelated.cpp";
    write_text(new_source, "int unrelated() { return 7; }\n");
    force_newer_timestamp(tree.root / "src");
    const auto file_added = mqb::discovery::SourceDiscovery::discover(request);
    expect(file_added.has_value(), "discovery after adding a source should succeed");
    if (file_added) {
        expect(!file_added->reused,
               "parent directory changes must invalidate reuse when indexed files are added");
        expect(file_added->indexed_files == 4,
               "new translation unit should enter the fresh source index");
        expect(!contains_source(file_added->sources, new_source),
               "unrelated added source should not change selected closure");
    }

    std::error_code error_code;
    fs::remove(new_source, error_code);
    force_newer_timestamp(tree.root / "src");
    const auto file_removed = mqb::discovery::SourceDiscovery::discover(request);
    expect(file_removed.has_value(), "discovery after removing a source should succeed");
    if (file_removed) {
        expect(!file_removed->reused,
               "parent directory changes must invalidate reuse when indexed files are removed");
        expect(file_removed->indexed_files == 3,
               "removed translation unit must leave the fresh source index");
    }

    const fs::path include_root = tree.root / "include";
    fs::create_directories(include_root);
    mqb::discovery::Request changed_request = request;
    changed_request.include_directories.push_back(include_root);
    const auto request_changed = mqb::discovery::SourceDiscovery::discover(changed_request);
    expect(request_changed.has_value(), "discovery with changed request identity should succeed");
    if (request_changed) {
        expect(!request_changed->reused,
               "include search order changes must invalidate discovery request identity");
    }

    write_text(default_cache, "not a valid MQB discovery cache");
    const auto corrupt_cache = mqb::discovery::SourceDiscovery::discover(request);
    expect(corrupt_cache.has_value(), "corrupt discovery cache must fall back to full discovery");
    if (corrupt_cache) {
        expect(!corrupt_cache->reused,
               "corrupt cache fallback must not be mislabeled as a cache hit");
        expect(contains_source(corrupt_cache->sources, implementation),
               "corrupt cache fallback must preserve discovery correctness");
    }
    const auto repaired_cache = mqb::discovery::SourceDiscovery::discover(request);
    expect(repaired_cache.has_value() && repaired_cache->reused,
           "full fallback after corrupt cache should repair evidence for the next invocation");

    mqb::discovery::Request uncached = request;
    uncached.persistent_cache = false;
    const auto disabled_first = mqb::discovery::SourceDiscovery::discover(uncached);
    const auto disabled_second = mqb::discovery::SourceDiscovery::discover(uncached);
    expect(disabled_first.has_value() && disabled_second.has_value(),
           "explicitly uncached discovery should still succeed");
    if (disabled_first && disabled_second) {
        expect(!disabled_first->reused && !disabled_second->reused,
               "persistent_cache=false must force ordinary discovery on every call");
        expect(disabled_first->sources == disabled_second->sources,
               "disabling persistent state must not change discovery semantics");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_source_discovery_cache_tests passed\n";
    return 0;
}
