#include "StorageReport.hpp"

#include <iomanip>
#include <limits>
#include <map>
#include <ostream>
#include <sstream>
#include <string_view>
#include <stdexcept>

namespace mqb::app::diagnostics {
namespace {
std::string text(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
void require_utf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80) continue;
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 7; minimum = 0x10000; }
        else throw std::runtime_error("storage report contains invalid UTF-8");
        if (count > value.size() - i) throw std::runtime_error("storage report contains truncated UTF-8");
        for (unsigned n = 0; n < count; ++n) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) throw std::runtime_error("storage report contains invalid UTF-8 continuation");
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            throw std::runtime_error("storage report contains invalid Unicode scalar");
    }
}
void quoted(std::ostream& out, std::string_view value) {
    require_utf8(value);
    constexpr char digits[] = "0123456789abcdef";
    out << '"';
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') out << '\\' << static_cast<char>(ch);
        else if (ch < 0x20) out << "\\u00" << digits[ch >> 4] << digits[ch & 15];
        else out << static_cast<char>(ch);
    }
    out << '"';
}
void number(std::ostream& out, std::optional<std::uint64_t> value) {
    if (value) out << *value;
    else out << "null";
}
std::string_view kind(StorageEntryKind value) {
    switch (value) {
    case StorageEntryKind::file: return "file";
    case StorageEntryKind::directory: return "directory";
    case StorageEntryKind::reparse_point: return "reparse_point";
    default: return "unavailable";
    }
}
std::string_view evidence(const StorageEntry& entry) {
    if (entry.protected_path) return "protected";
    if (entry.references.size() > 1) return "shared_cache_reference";
    if (!entry.references.empty()) return "cache_reference";
    return "unknown";
}
struct Total {
    std::uint64_t files{};
    std::optional<std::uint64_t> logical{0};
    std::optional<std::uint64_t> allocated{0};
};
void add(std::optional<std::uint64_t>& total, std::optional<std::uint64_t> value) {
    if (!total || !value || *value > (std::numeric_limits<std::uint64_t>::max)() - *total) total.reset();
    else *total += *value;
}
void add(Total& total, const StorageEntry& entry) {
    ++total.files;
    add(total.logical, entry.logical_bytes);
    add(total.allocated, entry.allocated_bytes);
}
void total_json(std::ostream& out, const Total& total) {
    out << "{\"files\":" << total.files << ",\"logical_bytes\":";
    number(out, total.logical);
    out << ",\"path_allocated_bytes\":";
    number(out, total.allocated);
    out << '}';
}
using Groups = std::map<std::string, Total>;
void groups_json(std::ostream& out, const Groups& groups) {
    out << '{';
    bool comma = false;
    for (const auto& [name, total] : groups) {
        if (comma) out << ',';
        comma = true;
        quoted(out, name); out << ':'; total_json(out, total);
    }
    out << '}';
}
} // namespace

bool write_storage_report(std::ostream& out, const StorageInventory& inventory, bool json) {
    Total total;
    Groups directories, types, categories;
    std::vector<Total> targets(inventory.references.size());
    std::map<std::string, std::pair<std::optional<std::uint64_t>, std::optional<std::uint64_t>>> physical;
    std::optional<std::uint64_t> unique_allocated{0};
    for (const auto& entry : inventory.entries) {
        if (entry.kind != StorageEntryKind::file) continue;
        add(total, entry);
        auto extension = text(entry.relative_path.extension());
        for (auto& ch : extension) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        add(types[extension.empty() ? "(none)" : extension], entry);
        add(directories[text(entry.relative_path.parent_path())], entry);
        add(categories[std::string{evidence(entry)}], entry);
        for (const auto id : entry.references) if (id < targets.size()) add(targets[id], entry);
        if (entry.physical_id.empty()) unique_allocated.reset();
        else {
            const auto sizes = std::pair{entry.logical_bytes, entry.allocated_bytes};
            const auto [found, inserted] = physical.emplace(entry.physical_id, sizes);
            if (inserted) add(unique_allocated, entry.allocated_bytes);
            else if (found->second != sizes) unique_allocated.reset();
        }
    }
    // Even a known-row aggregate is not the complete physical tree after a
    // refusal, skipped reparse subtree, decoder failure or enumeration error.
    if (!inventory.issues.empty()) unique_allocated.reset();
    if (!json) {
        out << "MQB storage inventory (read-only; not a deletion plan)\nroot: ";
        quoted(out, text(inventory.artifact_root));
        out << "\nroot exists: " << (inventory.root_exists ? "yes" : "no")
            << "\ncoverage: " << (inventory.issues.empty() ? "walk completed" : "partial / issues recorded")
            << "; NOT an atomic snapshot\nobserved files: " << total.files << "\nlogical bytes: ";
        number(out, total.logical);
        if (total.logical) {
            std::ostringstream units;
            units << std::fixed << std::setprecision(6)
                  << static_cast<long double>(*total.logical) / 1000000000.0L << " GB; "
                  << static_cast<long double>(*total.logical) / 1073741824.0L << " GiB";
            out << " (" << units.str() << ')';
        }
        out << "\npath allocated bytes (hard links may repeat): "; number(out, total.allocated);
        out << "\nunique file allocation bytes: "; number(out, unique_allocated);
        out << "\nreclaimable bytes: unavailable; live/obsolete/configuration: unknown\n";
        auto print_groups = [&](std::string_view title, const Groups& groups) {
            out << '\n' << title << " (direct files; bytes)\n";
            for (const auto& [name, value] : groups) {
                quoted(out, name);
                out << " files=" << value.files << " logical=";
                number(out, value.logical); out << " allocated="; number(out, value.allocated); out << '\n';
            }
        };
        print_groups("Directories", directories);
        print_groups("Extensions (not ownership)", types);
        print_groups("Evidence categories (not liveness)", categories);
        out << "\nHistorical target references (overlap; do not sum):\n";
        for (std::size_t i = 0; i < inventory.references.size(); ++i) {
            quoted(out, text(inventory.references[i].cache));
            out << " logical="; number(out, targets[i].logical);
            out << " signature=" << inventory.references[i].signature << " configuration=unknown\n";
        }
        out << "\nRaw entries (relative path; unrounded bytes):\n";
        for (const auto& entry : inventory.entries) {
            quoted(out, text(entry.relative_path));
            out << ' ' << kind(entry.kind) << ' ' << evidence(entry) << " logical=";
            number(out, entry.logical_bytes); out << " allocated="; number(out, entry.allocated_bytes); out << '\n';
        }
        for (const auto& problem : inventory.issues) {
            out << "issue: "; quoted(out, text(problem.path)); out << " code=" << problem.native_code << ' ';
            quoted(out, problem.message); out << '\n';
        }
    } else {
        out << "{\"schema_version\":1,\"mode\":\"read_only_observation\",\"artifact_root\":";
        quoted(out, text(inventory.artifact_root));
        out << ",\"root_exists\":" << (inventory.root_exists ? "true" : "false")
            << ",\"complete\":" << (inventory.issues.empty() ? "true" : "false")
            << ",\"atomic_snapshot\":false,\"deletion_authorized\":false,\"reclaimable_bytes\":null"
            << ",\"live_bytes\":null,\"obsolete_bytes\":null,\"units\":{\"GB\":1000000000,\"GiB\":1073741824}"
            << ",\"coverage\":\"observed unnamed file streams; excludes directory metadata, alternate streams and skipped subtrees\""
            << ",\"totals\":";
        total_json(out, total);
        out << ",\"unique_file_allocated_bytes\":"; number(out, unique_allocated);
        out << ",\"by_directory\":"; groups_json(out, directories);
        out << ",\"by_extension\":"; groups_json(out, types);
        out << ",\"by_evidence\":"; groups_json(out, categories);
        out << ",\"by_configuration\":{\"unknown\":"; total_json(out, total); out << '}';
        out << ",\"target_references\":[";
        for (std::size_t i = 0; i < inventory.references.size(); ++i) {
            if (i) out << ',';
            const auto& ref = inventory.references[i];
            out << "{\"id\":" << i << ",\"cache\":"; quoted(out, text(ref.cache));
            out << ",\"output\":"; quoted(out, text(ref.output));
            out << ",\"kind\":"; quoted(out, ref.kind);
            out << ",\"signature\":"; quoted(out, ref.signature);
            out << ",\"tool_version\":"; quoted(out, ref.tool_version);
            out << ",\"configuration\":null,\"lifecycle\":\"unknown\",\"overlapping_observed_totals\":";
            total_json(out, targets[i]); out << '}';
        }
        out << "],\"entries\":[";
        bool comma = false;
        for (const auto& entry : inventory.entries) {
            if (comma) out << ',';
            comma = true;
            out << "{\"path\":"; quoted(out, text(entry.relative_path));
            out << ",\"kind\":"; quoted(out, kind(entry.kind));
            out << ",\"evidence\":"; quoted(out, evidence(entry));
            out << ",\"lifecycle\":"; quoted(out, entry.protected_path ? "protected" : "unknown");
            out << ",\"logical_bytes\":"; number(out, entry.logical_bytes);
            out << ",\"allocated_bytes\":"; number(out, entry.allocated_bytes);
            out << ",\"physical_id\":";
            if (entry.physical_id.empty()) out << "null"; else quoted(out, entry.physical_id);
            out << ",\"hard_links\":";
            if (entry.hard_links) out << *entry.hard_links; else out << "null";
            out << ",\"reference_ids\":[";
            for (std::size_t i = 0; i < entry.references.size(); ++i) {
                if (i) out << ',';
                out << entry.references[i];
            }
            out << "]}";
        }
        out << "],\"issues\":[";
        for (std::size_t i = 0; i < inventory.issues.size(); ++i) {
            if (i) out << ',';
            const auto& problem = inventory.issues[i];
            out << "{\"path\":"; quoted(out, text(problem.path));
            out << ",\"message\":"; quoted(out, problem.message);
            out << ",\"native_code\":" << problem.native_code << '}';
        }
        out << "]}\n";
    }
    out.flush();
    return static_cast<bool>(out);
}
} // namespace mqb::app::diagnostics
