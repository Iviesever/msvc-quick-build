#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mqb {

enum class WriteStage { discovery, toolchain, scan, compile, pch, modules, link, archive };
// A namespace is the immediate directory in which temporary replacement names
// may be created, NOT recursive authority over its descendants/reparse targets.
enum class WriteExtent { file, directory_namespace };
struct KnownWrite {
    WriteStage stage{};
    WriteExtent extent{WriteExtent::file};
    std::filesystem::path path;
    std::string reason;
};
struct UnresolvedWrite {
    WriteStage stage{};
    std::string reason;
};

// Additive evidence, not another build plan or a declaration of completeness.
// No API turns an empty unresolved list into permission to execute/reserve. The
// caller still must enumerate ALL producers, before any producer starts. No IO,
// cwd lookup, environment expansion or lexical '..' removal happens here.
struct WriteInventory {
    std::vector<KnownWrite> known;
    std::vector<UnresolvedWrite> unresolved;

    void add(WriteStage stage, WriteExtent extent, std::filesystem::path path,
             const std::filesystem::path& explicit_base, std::string reason) {
        if (path.empty()) {
            unresolved.push_back({stage, "empty destination: " + reason});
            return;
        }
        if (!path.is_absolute()) {
            if (!explicit_base.is_absolute() || path.has_root_name() || path.has_root_directory()) {
                unresolved.push_back({stage, "destination needs an explicit absolute base: " + reason});
                return;
            }
            path = explicit_base / path;
        }
        known.push_back({stage, extent, std::move(path), std::move(reason)});
    }

    // The cache authority supplies its already-resolved filename. We deliberately
    // do not reproduce discovery/toolchain hashing/default-path policy here.
    void add_cache(WriteStage stage, const std::filesystem::path& resolved_file) {
        add(stage, WriteExtent::file, resolved_file, {}, "cache record");
        if (resolved_file.is_absolute())
            add(stage, WriteExtent::directory_namespace, resolved_file.parent_path(), {},
                "cache replacement namespace (temporary names are not guessed)");
    }
};
} // namespace mqb
