#pragma once

#include <set>
#include <sstream>

#include "mqb/platform/windows/WindowsWriteInventory.hpp"

// Uses the same native harness helpers; it never reserves or withdraws a domain.
namespace write_inventory_tests {
namespace fs = std::filesystem;
using namespace mqb::platform::windows;
using write_domain_tests::require;
using write_domain_tests::write;
using write_domain_tests::read;
using write_domain_tests::utf8;
std::string quote(const std::string& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string identity(const std::optional<WriteDirectoryIdentity>& id) {
    if (!id) return "null";
    constexpr char hex[] = "0123456789abcdef";
    std::string file;
    for (auto b : id->file) { file += hex[b >> 4]; file += hex[b & 15]; }
    return "{\"volume\":" + std::to_string(id->volume) + ",\"file_id\":" + quote(file) + '}';
}
void record(const fs::path& destination, const WindowsWriteInventory& inventory) {
    std::string out = "{\"authorizes_writes\":false,\"directories\":[";
    for (std::size_t i = 0; i != inventory.directory_count(); ++i) {
        if (i) out += ',';
        out += identity(inventory.directory_identity(i));
    }
    out += "],\"entries\":[";
    for (std::size_t i = 0; i != inventory.entries().size(); ++i) {
        if (i) out += ',';
        const auto& e = inventory.entries()[i];
        out += "{\"index\":" + std::to_string(i) + ",\"path\":" + quote(utf8(e.declaration.path))
            + ",\"reason\":" + quote(e.declaration.reason) + ",\"directory_index\":"
            + (e.directory_index ? std::to_string(*e.directory_index) : "null")
            + ",\"file_absent\":" + (e.file_absent ? "true" : "false") + ",\"existing_file\":" + identity(e.existing_file)
            + ",\"error\":";
        if (e.error) out += "{\"message\":" + quote(e.error->message) + ",\"win32\":" + std::to_string(e.error->win32_error)
            + ",\"ntstatus\":" + (e.error->ntstatus ? std::to_string(*e.error->ntstatus) : "null") + '}';
        else out += "null";
        out += '}';
    }
    out += "],\"unresolved\":[";
    for (std::size_t i = 0; i != inventory.unresolved().size(); ++i) {
        if (i) out += ',';
        const auto& gap = inventory.unresolved()[i];
        out += "{\"stage\":" + std::to_string(static_cast<int>(gap.stage)) + ",\"reason\":" + quote(gap.reason) + '}';
    }
    write(destination, out + "]}\n");
}
int run() {
    fs::path root;
    bool preserve = false;
    unsigned checks = 0;
    std::vector<std::pair<std::string, bool>> outcomes;
    const auto save = [&](bool passed) {
        std::string out = "{\"passed\":" + std::string(passed ? "true" : "false")
            + ",\"authorizes_writes\":false,\"complete_write_set\":false,\"checks\":[";
        for (std::size_t i = 0; i < outcomes.size(); ++i) {
            if (i) out += ',';
            out += "{\"name\":" + quote(outcomes[i].first) + ",\"passed\":" + (outcomes[i].second ? "true" : "false") + '}';
        }
        write(root / "checks.json", out + "]}\n");
    };
    const auto check = [&](bool value, const char* label) {
        ++checks; outcomes.emplace_back(label, value);
        std::cout << "WRITE_INVENTORY_CHECK " << label << ' ' << (value ? "PASS" : "FAIL") << '\n';
        require(value, label);
    };
    try {
        LARGE_INTEGER q{}; require(::QueryPerformanceCounter(&q), "fixture clock");
        wchar_t destination[32768]{};
        const auto n = ::GetEnvironmentVariableW(L"MQB_WRITE_INVENTORY_EVIDENCE_ROOT", destination, 32768);
        require(n < 32768, "evidence path too long"); preserve = n != 0;
        root = preserve ? fs::path{destination} : fs::temp_directory_path() /
            (L"mqb-write-inventory-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(q.QuadPart));
        require(!fs::exists(root), "refuse reused inventory evidence");
        const auto actual = root / L"Project-\u65e5\u672c" / ".mqb", external = root / "external";
        fs::create_directories(actual / "nested"); fs::create_directory(external);
        write(actual / "kept.obj", "unchanged object\n"); write(actual / "multi.obj", "hard-linked\n");
        write(actual / "file-parent", "not a directory\n");
        fs::create_hard_link(actual / "multi.obj", external / "alias.obj");
        const auto junction = [&](const fs::path& name, const fs::path& target) {
            wchar_t system[32768]{}; require(::GetSystemDirectoryW(system, 32768), "system directory");
            mqb::process::ProcessSpec command; command.executable = fs::path{system} / "cmd.exe";
            command.arguments = {"/d", "/c", "mklink", "/J", utf8(name), utf8(target)};
            WindowsProcessRunner runner;
            const auto r = runner.run(command); require(r && r->exit_code == 0, "native junction setup failed");
        };
        junction(root / "alias", actual); junction(actual / "junction-leaf", external);
        const auto signature = [&] {
            std::set<std::string> records;
            for (const auto& dir : std::vector<fs::path>{root, actual.parent_path(), actual, actual / "nested", external})
                for (const auto& e : fs::directory_iterator(dir)) {
                    auto item = utf8(e.path()) + ' ' + std::to_string(fs::last_write_time(e.path()).time_since_epoch().count());
                    if (fs::is_regular_file(e.path())) item += ' ' + read(e.path());
                    records.insert(std::move(item));
                }
            std::string value; for (const auto& item : records) value += item + '\n'; return value;
        };
        mqb::WriteInventory declared;
        const auto add = [&](fs::path path, mqb::WriteExtent extent = mqb::WriteExtent::file) {
            // Deliberately bypass add() for malformed-path controls; mapping must
            // preserve/reject those records rather than trusting its producer.
            declared.known.push_back({mqb::WriteStage::compile, extent, std::move(path), "fixed output declaration"});
        };
        add(actual / "kept.obj"); add(root / "alias/kept.obj"); add(actual / "./kept.obj");
        add(actual, mqb::WriteExtent::directory_namespace); add(actual / "pending.obj");
        add(actual / "missing-parent/x.obj"); add(actual / "missing-directory", mqb::WriteExtent::directory_namespace);
        add(actual / "nested/x.obj"); add(external / "x.obj"); add(actual / "multi.obj");
        add(actual / "junction-leaf"); add(actual / "nested"); add(actual / "file-parent/x.obj");
        add(actual / "kept.obj:stream"); add(actual / "NUL.obj"); add(actual / "trail.");
        add(actual / L"COM\u00b2.log"); add(actual / "../escape.obj"); add({}); add(L"\\\\.\\C:\\bad.obj");
        add("relative.obj"); add(actual / "last.obj");
        auto upper = actual.wstring(); for (auto& c : upper) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
        add(fs::path{upper} / "kept.obj"); add(actual / "COM10.obj"); add(actual / "KEPT.OBJ");
        declared.unresolved = {{mqb::WriteStage::discovery, "early cache unresolved"},
            {mqb::WriteStage::compile, "native tool side effects unresolved"}};
        const auto before = signature();
        {
            auto mapped = WindowsWriteInventory::inspect(declared);
            const auto after = signature();
            const auto e = mapped.entries();
            check(e.size() == declared.known.size(), "all-original-indices-preserved");
            check(mapped.unresolved().size() == 2 && mapped.unresolved()[0].reason == declared.unresolved[0].reason
                && mapped.unresolved()[1].stage == declared.unresolved[1].stage, "coverage-gaps-not-closed-by-mapping");
            check(mapped.directory_count() == 3, "only-physical-parents-deduplicated");
            check(e[0].existing_file && !e[0].file_absent && !e[0].error, "existing-file-identity-observed");
            check(e[0].directory_index == e[1].directory_index && e[0].existing_file == e[1].existing_file,
                "junction-alias-same-file-and-parent");
            check(e[0].directory_index == e[2].directory_index && e[0].existing_file == e[2].existing_file,
                "dot-parent-not-new-domain");
            check(e[0].directory_index == e[22].directory_index && e[22].existing_file == e[0].existing_file,
                "case-parent-not-new-domain");
            check(e[3].directory_index == e[0].directory_index && !e[3].error && !e[3].file_absent,
                "cache-namespace-is-immediate-directory");
            check(e[4].file_absent && !e[4].error && e[4].directory_index == e[0].directory_index,
                "missing-leaf-kept-distinct");
            check(e[5].error && !e[5].directory_index && !e[5].file_absent, "missing-parent-not-nearest-ancestor");
            check(e[6].error && !e[6].directory_index, "missing-namespace-not-created");
            check(e[7].directory_index != e[0].directory_index && e[7].file_absent, "nested-parent-not-collapsed");
            check(e[8].directory_index != e[0].directory_index && e[8].directory_index != e[7].directory_index,
                "external-parent-retained");
            check(e[9].error && !e[9].existing_file && !e[9].file_absent, "multiple-hard-links-remain-ambiguous");
            check(e[10].error && !e[10].file_absent, "final-reparse-not-followed");
            check(e[11].error && !e[11].file_absent, "directory-is-not-output-file");
            check(e[12].error && !e[12].directory_index, "file-parent-rejected");
            for (std::size_t i = 13; i <= 20; ++i)
                check(e[i].error && !e[i].directory_index && !e[i].file_absent, "unsafe-path-refused-before-pin");
            check(e[21].file_absent && !e[21].error, "bad-records-do-not-hide-later-good-output");
            check(e[23].file_absent && !e[23].error, "device-prefix-is-not-overrejected");
            check(!e[24].file_absent && (e[24].error || e[24].existing_file == e[0].existing_file),
                "case-alias-is-never-falsely-absent");
            check(before == after, "inspection-preserves-tree-bytes-and-mtimes");
            check(!fs::exists(actual / "missing-parent") && !fs::exists(actual / "missing-directory")
                && !fs::exists(actual / WindowsWriteDomain::marker_name) && !fs::exists(external / WindowsWriteDomain::marker_name),
                "inspection-creates-no-parents-or-markers");
            record(root / "observations.json", mapped);
            write(root / "before-tree.txt", before); write(root / "after-tree.txt", after);
            const auto original = mapped.directory_identity(*e[0].directory_index);
            fs::remove(root / "alias"); junction(root / "alias", external);
            mqb::WriteInventory redirect; redirect.known.push_back(declared.known[1]);
            auto changed = WindowsWriteInventory::inspect(redirect);
            check(changed.entries()[0].directory_index && changed.directory_identity(*changed.entries()[0].directory_index) != original,
                "retargeted-alias-next-observation-is-different");
            check(mapped.directory_identity(*e[0].directory_index) == original, "prior-parent-pin-not-retargeted");
            auto moved = std::move(mapped);
            check(moved.entries().size() == declared.known.size() && moved.directory_identity(0) == original,
                "moving-inventory-retains-read-only-pins");
            record(root / "retargeted.json", changed);
        }
        fs::remove(root / "alias"); fs::remove(actual / "junction-leaf");
        save(true);
        if (!preserve) fs::remove_all(root);
        std::cout << "windows_write_inventory_cases " << checks << " checks passed; authorizes_writes=false\n";
        return 0;
    } catch (const std::exception& e) {
        if (!root.empty() && fs::exists(root)) try { save(false); write(root / "failure.txt", e.what()); } catch (...) {}
        std::cerr << "WRITE_INVENTORY_NATIVE_FAILURE " << e.what() << '\n'; return 1;
    }
}
} // namespace write_inventory_tests
