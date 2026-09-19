#include "mqb/core/StorageInventory.hpp"

#include <algorithm>
#include <map>
#include <exception>
#include <string_view>
#include <utility>

#include "mqb/core/ArchiveCacheFile.hpp"
#include "mqb/core/LinkCacheFile.hpp"

namespace mqb {
namespace {
std::string ascii_lower(std::string value) {
    for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
std::string text(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace

void observe_storage_cache(const std::filesystem::path& file,
                           const StorageEntry& entry, StorageInventory& inventory) {
    const auto relative = ascii_lower(text(entry.relative_path));
    const bool link = relative.starts_with("cache/link/") && relative.ends_with(".linkcache");
    const bool archive = relative.starts_with("cache/archive/") && relative.ends_with(".archivecache");
    if (!link && !archive) return;
    // Bound each legacy decoder's input; do not inspect arbitrary large files.
    if (!entry.logical_bytes || *entry.logical_bytes > 16u * 1024u * 1024u) {
        inventory.issues.push_back({entry.relative_path, "cache reference unavailable: size exceeds 16 MiB limit", 0});
        return;
    }
    try {
        StorageReference reference;
        reference.cache = entry.relative_path;
        if (link) {
            auto cached = LinkCacheFile::load(file);
            if (!cached || !*cached) {
                inventory.issues.push_back({entry.relative_path,
                    cached ? "cache disappeared while pinned" : cached.error().message, 0});
                return;
            }
            const auto& value = **cached;
            reference.kind = "link";
            reference.output = value.output;
            reference.signature = value.signature.hex();
            reference.tool_version = value.linker.version;
            reference.paths = value.objects;
            reference.paths.insert(reference.paths.end(), value.side_outputs.begin(), value.side_outputs.end());
            reference.paths.insert(reference.paths.end(), value.libraries.begin(), value.libraries.end());
            reference.paths.insert(reference.paths.end(), value.file_inputs.begin(), value.file_inputs.end());
        } else {
            auto cached = ArchiveCacheFile::load(file);
            if (!cached || !*cached) {
                inventory.issues.push_back({entry.relative_path,
                    cached ? "cache disappeared while pinned" : cached.error().message, 0});
                return;
            }
            const auto& value = **cached;
            reference.kind = "archive";
            reference.output = value.output;
            reference.signature = value.signature.hex();
            reference.tool_version = value.librarian.version;
            reference.paths = value.objects;
        }
        reference.paths.push_back(reference.output);
        reference.paths.push_back(file);
        inventory.references.push_back(std::move(reference));
    } catch (const std::exception&) {
        // In particular, legacy path decoding may reject malformed UTF-8.
        // Keep the file and partial inventory instead of losing all rows.
        inventory.issues.push_back({entry.relative_path, "legacy cache decoding raised an exception; reference unavailable", 0});
    }
}

void associate_storage_references(StorageInventory& inventory, const StoragePathKey& key) {
    std::sort(inventory.entries.begin(), inventory.entries.end(), [](const auto& a, const auto& b) {
        return a.relative_path.generic_u8string() < b.relative_path.generic_u8string();
    });
    std::sort(inventory.references.begin(), inventory.references.end(), [](const auto& a, const auto& b) {
        return a.cache.generic_u8string() < b.cache.generic_u8string();
    });
    std::map<std::string, std::vector<std::size_t>> by_path;
    for (std::size_t i = 0; i < inventory.references.size(); ++i) {
        for (const auto& path : inventory.references[i].paths) {
            // Relative paths in untrusted legacy records have no established
            // base. Do not guess, resolve symlinks, or probe outside the scan.
            if (path.is_absolute()) by_path[key(path)].push_back(i);
        }
    }
    for (auto& entry : inventory.entries) {
        const auto first = entry.relative_path.begin();
        const auto directory = first == entry.relative_path.end() ? std::string{} : ascii_lower(text(*first));
        entry.protected_path = entry.kind != StorageEntryKind::file ||
            (directory != "bin" && directory != "obj" && directory != "ifc" &&
             directory != "deps" && directory != "scan" && directory != "cache" && directory != "pch");
        entry.references.clear();
        if (const auto found = by_path.find(key(inventory.artifact_root / entry.relative_path));
            found != by_path.end()) {
            entry.references = found->second;
            std::sort(entry.references.begin(), entry.references.end());
            entry.references.erase(std::unique(entry.references.begin(), entry.references.end()), entry.references.end());
        }
    }
}
} // namespace mqb
