#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "StorageReport.hpp"
#include "mqb/core/StorageInventory.hpp"
#include "mqb/json/Json.hpp"

#ifdef _WIN32
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
#include "mqb/platform/windows/StorageInventory.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string text(const fs::path& path) {
    const auto b = path.generic_u8string();
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}
mqb::json::Value report(const mqb::StorageInventory& inventory) {
    std::ostringstream out;
    require(mqb::app::diagnostics::write_storage_report(out, inventory, true), "render JSON");
    auto parsed = mqb::json::parse(out.str());
    require(parsed.has_value(), "report is valid JSON");
    return *parsed;
}
void portable_contracts() {
    mqb::StorageInventory inventory;
    inventory.artifact_root = fs::absolute("storage-model/.mqb");
    inventory.root_exists = true;
    auto row = [](const char* path, std::uint64_t bytes, const char* id) {
        mqb::StorageEntry e;
        e.relative_path = path;
        e.kind = mqb::StorageEntryKind::file;
        e.logical_bytes = bytes;
        e.allocated_bytes = 4096;
        e.hard_links = 1;
        e.physical_id = id;
        return e;
    };
    inventory.entries = {row("bin/a.pdb", 10, "v:1"), row("bin/b.pdb", 10, "v:1"),
        row("obj/shared.obj", 20, "v:2"), row("logs/failure.txt", 3, "v:3")};
    for (const auto* name : {"cache/link/a.linkcache", "cache/link/b.linkcache"}) {
        mqb::StorageReference ref;
        ref.cache = name;
        ref.signature = "opaque-not-configuration";
        ref.paths = {inventory.artifact_root / "obj/shared.obj", inventory.artifact_root / "obj/shared.obj",
            inventory.artifact_root / "logs/failure.txt", fs::path{"obj/shared.obj"}};
        inventory.references.push_back(ref);
    }
    mqb::associate_storage_references(inventory, text);
    require(inventory.entries.back().references.size() == 2, "shared object deduplicates each record");
    require(inventory.entries[2].protected_path, "referenced log remains protected");
    auto json = report(inventory);
    require(json.object.at("totals").object.at("logical_bytes").scalar == "43", "raw logical closure");
    require(json.object.at("totals").object.at("path_allocated_bytes").scalar == "16384", "path allocation repeats hardlinks");
    require(json.object.at("unique_file_allocated_bytes").scalar == "12288", "physical allocation deduplicates");
    require(json.object.at("reclaimable_bytes").kind == mqb::json::Kind::null_value, "no reclamation claim");
    require(!json.object.at("atomic_snapshot").boolean && !json.object.at("deletion_authorized").boolean,
        "no atomicity/deletion claim");
    require(json.object.at("target_references").array[0].object.at("configuration").kind == mqb::json::Kind::null_value,
        "signature not decoded as configuration");
    inventory.entries[0].allocated_bytes.reset();
    require(report(inventory).object.at("totals").object.at("path_allocated_bytes").kind == mqb::json::Kind::null_value,
        "unavailable allocation is not zero");
    inventory.entries[0].logical_bytes = (std::numeric_limits<std::uint64_t>::max)();
    require(report(inventory).object.at("totals").object.at("logical_bytes").kind == mqb::json::Kind::null_value,
        "overflow cannot wrap");
    inventory.issues.push_back({"a\"b\n", "bad\tdata\n", 5});
    require(!report(inventory).object.at("complete").boolean, "errors preserve valid partial JSON");
    for (const auto& invalid : {std::string{"\xff"}, std::string{"\xc0\xaf"},
             std::string{"\xed\xa0\x80"}, std::string{"\xf4\x90\x80\x80"}, std::string{"\xe4\xb8"}}) {
        inventory.references[0].tool_version = invalid;
        bool rejected = false;
        try { (void)report(inventory); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "invalid UTF-8 must not be reported as successful JSON");
    }
    inventory.references[0].tool_version = "valid \xe6\x97\xa5";
    (void)report(inventory);
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    require(!mqb::app::diagnostics::write_storage_report(failed, inventory, false), "output failure is surfaced");
}
#ifdef _WIN32
void write(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out << content;
    out.close();
    require(static_cast<bool>(out), "fixture/evidence write");
}
struct FileState {
    std::uintmax_t bytes{};
    fs::file_time_type modified;
    std::string contents;
    bool operator==(const FileState&) const = default;
};
std::map<std::string, FileState> snapshot(const fs::path& root) {
    std::map<std::string, FileState> result;
    if (!fs::exists(root)) return result;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file{entry.path(), std::ios::binary};
        std::string contents{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        require(!file.bad(), "read fixture snapshot");
        result.emplace(text(entry.path().lexically_relative(root)),
            FileState{entry.file_size(), entry.last_write_time(), std::move(contents)});
    }
    return result;
}
void windows_contracts(const fs::path& root, const fs::path& evidence) {
    fs::create_directories(root);
    fs::create_directories(evidence);
    auto record = [&](const char* label, bool succeeded, DWORD code) {
        std::ostringstream result;
        result << "succeeded=" << succeeded << "\nnative_code=" << code << '\n';
        write(evidence / (std::string{label} + ".txt"), result.str());
    };
    const fs::path artifacts = root / ".mqb";
    const auto missing = mqb::platform::windows::scan_storage(artifacts, mqb::observe_storage_cache);
    require(!missing.root_exists && missing.issues.empty() && !fs::exists(artifacts), "missing root is not created");
    write(artifacts / "bin/a.pdb", "0123456789");
    write(artifacts / "obj/shared.obj", "object");
    write(artifacts / "generated/input.cpp", "protected input");
    write(artifacts / fs::path{L"generated/\u65e5 space.txt"}, "unicode protected input");
    write(artifacts / "logs/failure.txt", "retained failure");
    require(::CreateHardLinkW((artifacts / "bin/b.pdb").c_str(), (artifacts / "bin/a.pdb").c_str(), nullptr),
        "native hardlink fixture");
    mqb::LinkCacheEntry cached{
        .linker = {fs::path{L"C:/unavailable/link.exe"}, "fixture", "stamp"},
        .signature = mqb::BuildSignature::from_digest({1, 2}),
        .objects = {artifacts / "obj/shared.obj"},
        .output = artifacts / "bin/a.exe",
        .libraries = {}, .file_inputs = {}, .side_outputs = {artifacts / "bin/a.pdb"}};
    require(mqb::LinkCacheFile::save(artifacts / "cache/link/a.linkcache", cached).has_value(), "write valid cache fixture");
    cached.output = artifacts / "bin/b.exe";
    require(mqb::LinkCacheFile::save(artifacts / "cache/link/b.linkcache", cached).has_value(), "second shared record");
    const auto before = snapshot(artifacts);
    bool attempted = false;
    auto inventory = mqb::platform::windows::scan_storage(artifacts,
        [&](const fs::path& path, const mqb::StorageEntry& entry, mqb::StorageInventory& value) {
            mqb::observe_storage_cache(path, entry, value);
            if (entry.relative_path == fs::path{L"obj/shared.obj"}) {
                attempted = true;
                const bool renamed = ::MoveFileExW(path.c_str(), (path.parent_path() / "moved.obj").c_str(), 0) != FALSE;
                const DWORD rename_code = renamed ? ERROR_SUCCESS : ::GetLastError();
                record("pinned-file-rename", renamed, rename_code);
                require(!renamed && rename_code == ERROR_SHARING_VIOLATION, "pinned file resists rename by sharing policy");
                const bool parent_renamed = ::MoveFileExW(path.parent_path().c_str(), (artifacts / "moved-obj").c_str(), 0) != FALSE;
                const DWORD parent_code = parent_renamed ? ERROR_SUCCESS : ::GetLastError();
                record("pinned-parent-rename", parent_renamed, parent_code);
                require(!parent_renamed, "pinned ancestor resists rename");
                // Include DELETE explicitly: attribute-only pins mistakenly
                // allow these accesses even when their share mask omits them.
                for (const DWORD access : {DWORD{GENERIC_WRITE}, DWORD{DELETE}}) {
                    const auto handle = ::CreateFileW(path.c_str(), access,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, 0, nullptr);
                    const DWORD code = handle == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
                    if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
                    record(access == DELETE ? "pinned-delete-access" : "pinned-write-access", handle != INVALID_HANDLE_VALUE, code);
                    require(handle == INVALID_HANDLE_VALUE && code == ERROR_SHARING_VIOLATION,
                        "pinned observation excludes write/delete access by sharing policy");
                }
            }
        });
    require(attempted && inventory.issues.empty() && inventory.references.size() == 2, "native pinned cache observation");
    mqb::associate_storage_references(inventory, mqb::platform::windows::path_identity_key);
    require(before == snapshot(artifacts), "inventory does not change file bytes/mtime/names");
    // Positive controls after the observer returns prove the fixture permits
    // the operations and the walker released both file and ancestor handles.
    const auto object = artifacts / "obj/shared.obj";
    const auto writable = ::CreateFileW(object.c_str(), GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    const DWORD open_code = writable == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
    if (writable != INVALID_HANDLE_VALUE) ::CloseHandle(writable);
    record("released-write-delete-access", writable != INVALID_HANDLE_VALUE, open_code);
    require(writable != INVALID_HANDLE_VALUE, "released observation permits write/delete access");
    const bool moved = ::MoveFileExW(object.c_str(), (artifacts / "obj/moved.obj").c_str(), 0) != FALSE;
    const DWORD move_code = moved ? ERROR_SUCCESS : ::GetLastError();
    record("released-file-rename", moved, move_code);
    require(moved, "released file can be renamed");
    require(::MoveFileExW((artifacts / "obj/moved.obj").c_str(), object.c_str(), 0), "restore file control");
    const bool parent_moved = ::MoveFileExW((artifacts / "obj").c_str(), (artifacts / "moved-obj").c_str(), 0) != FALSE;
    const DWORD parent_move_code = parent_moved ? ERROR_SUCCESS : ::GetLastError();
    record("released-parent-rename", parent_moved, parent_move_code);
    require(parent_moved, "released ancestor can be renamed");
    require(::MoveFileExW((artifacts / "moved-obj").c_str(), (artifacts / "obj").c_str(), 0), "restore ancestor control");
    require(before == snapshot(artifacts), "positive controls restore fixture bytes/mtime/names");
    const auto parsed = report(inventory);
    require(parsed.object.at("unique_file_allocated_bytes").kind == mqb::json::Kind::number, "native allocated size available");
    bool shared = false;
    for (const auto& entry : inventory.entries) if (entry.relative_path == fs::path{L"obj/shared.obj"}) shared = entry.references.size() == 2;
    require(shared, "two target cache references share one observed object");
    // Existing writer is a recorded refusal, not an invitation to kill it.
    const auto busy = ::CreateFileW((artifacts / "bin/a.pdb").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    require(busy != INVALID_HANDLE_VALUE, "open busy fixture");
    auto blocked = mqb::platform::windows::scan_storage(artifacts);
    ::CloseHandle(busy);
    require(!blocked.issues.empty(), "in-use file reported as unavailable");
    write(artifacts / "cache/link/corrupt.linkcache", "not a cache");
    auto corrupt = mqb::platform::windows::scan_storage(artifacts, mqb::observe_storage_cache);
    require(!corrupt.issues.empty(), "corrupt cache error preserved");
    require(fs::exists(artifacts / "cache/link/corrupt.linkcache"), "corrupt evidence retained");
}

void cli_lifecycle(const fs::path& executable, const fs::path& root, const fs::path& evidence) {
    fs::create_directories(root);
    fs::create_directories(evidence);
    mqb::platform::windows::WindowsProcessRunner runner;
    unsigned sequence = 0;
    auto call = [&](std::vector<std::string> args, const char* label, int expected) {
        mqb::process::ProcessSpec spec;
        spec.executable = executable;
        spec.arguments = std::move(args);
        spec.working_directory = root;
        spec.capture_stdout = spec.capture_stderr = true;
        const auto value = runner.run(spec);
        const auto prefix = evidence / (std::to_string(++sequence) + "-" + label);
        std::ostringstream command;
        for (const auto& arg : spec.arguments) command << arg << '\n';
        write(prefix.string() + ".argv.txt", command.str());
        if (!value) write(prefix.string() + ".launch-error.txt",
            std::to_string(value.error().native_code) + " " + value.error().message);
        require(value.has_value(), "launch lifecycle command (raw error retained)");
        write(prefix.string() + ".stdout.txt", value->stdout_text);
        write(prefix.string() + ".stderr.txt", value->stderr_text);
        write(prefix.string() + ".exit.txt", std::to_string(value->exit_code));
        if (value->exit_code != expected) std::cerr << value->stdout_text << value->stderr_text;
        require(value->exit_code == expected, "unexpected lifecycle exit (raw evidence retained)");
        return *value;
    };
    auto inspect = [&](const char* label) {
        const auto before = snapshot(root / ".mqb");
        auto value = call({"storage", "--format", "json"}, label, 0);
        auto json = mqb::json::parse(value.stdout_text);
        require(json.has_value(), "CLI storage JSON parses");
        require(before == snapshot(root / ".mqb"), "CLI inventory is read-only");
        const auto& totals = json->object.at("totals").object;
        std::uint64_t sum = 0, count = 0;
        for (const auto& entry : json->object.at("entries").array) {
            if (entry.object.at("kind").scalar != "file") continue;
            ++count;
            sum += std::stoull(entry.object.at("logical_bytes").scalar);
        }
        require(std::to_string(count) == totals.at("files").scalar && std::to_string(sum) == totals.at("logical_bytes").scalar,
            "lifecycle byte/file list closes exactly");
        return *json;
    };
    call({"storage", "--format", "invalid"}, "bad-format", 2);
    inspect("empty");
    require(!fs::exists(root / ".mqb"), "empty CLI scan does not create cache");
    write(root / "main.cpp", "int helper(); int main() { return helper() == 7 ? 0 : 1; }\n");
    write(root / "helper.cpp", "int helper() { return 7; }\n");
    auto build = [&](const char* source, const char* target, const char* config, const char* label) {
        return call({"build", "main.cpp", source, "--env", "vs", config, "-o", target, "--verbose", "/link", "/INCREMENTAL:NO"}, label, 0);
    };
    // Fixed budget: eight builds per native configuration, never retry to green.
    build("helper.cpp", "stable", "--debug", "stable-cold");
    inspect("after-cold");
    const auto stable_files = snapshot(root / ".mqb");
    build("helper.cpp", "stable", "--debug", "stable-repeat");
    require(stable_files == snapshot(root / ".mqb"), "stable repeat does not append/rewrite generations");
    fs::rename(root / "helper.cpp", root / "renamed.cpp");
    build("renamed.cpp", "stable", "--debug", "source-rename");
    require(fs::exists(root / ".mqb/obj/helper.cpp.obj"), "source rename leaves historical object to classify unknown");
    build("renamed.cpp", "renamed-target", "--debug", "target-rename");
    require(fs::exists(root / ".mqb/bin/stable.exe"), "target rename does not imply safe retirement");
    build("renamed.cpp", "renamed-target", "--release", "configuration-switch");
    build("renamed.cpp", "aot_debug_1111", "--debug", "aot-first");
    build("renamed.cpp", "aot_debug_2222", "--debug", "aot-second");
    build("renamed.cpp", "aot_debug_2222", "--debug", "aot-repeat");
    const auto final = inspect("after-lifecycle");
    require(final.object.at("target_references").array.size() == 4, "four historical output/cache identities observed");
    require(fs::exists(root / ".mqb/bin/aot_debug_1111.exe"), "older hash preserved without retirement authority");
    mqb::process::ProcessSpec program;
    program.executable = root / ".mqb/bin/aot_debug_2222.exe";
    program.working_directory = root;
    const auto execution = runner.run(program);
    if (!execution) write(evidence / "program.launch-error.txt", execution.error().message);
    require(execution.has_value(), "launch produced executable");
    write(evidence / "program.stdout.txt", execution->stdout_text);
    write(evidence / "program.stderr.txt", execution->stderr_text);
    write(evidence / "program.exit.txt", std::to_string(execution->exit_code));
    require(execution->exit_code == 0, "produced program remains correct");
    // A real junction, without requiring symbolic-link privilege.
    const auto external = evidence / "external";
    write(external / "sentinel.txt", "outside must not be enumerated");
    mqb::process::ProcessSpec junction;
    junction.executable = L"C:/Windows/System32/cmd.exe";
    junction.arguments = {"/d", "/c", "mklink", "/J", text(root / ".mqb/junction"), text(external)};
    junction.working_directory = root;
    junction.capture_stdout = junction.capture_stderr = true;
    auto created = runner.run(junction);
    if (!created) write(evidence / "junction.launch-error.txt", created.error().message);
    else {
        write(evidence / "junction.stdout.txt", created->stdout_text);
        write(evidence / "junction.stderr.txt", created->stderr_text);
        write(evidence / "junction.exit.txt", std::to_string(created->exit_code));
    }
    require(created && created->exit_code == 0, "create junction fixture");
    auto refused = call({"storage", "--format", "json"}, "junction-refusal", 1);
    auto json = mqb::json::parse(refused.stdout_text);
    require(json && !json->object.at("complete").boolean, "junction produces valid partial report");
    require(refused.stdout_text.find("sentinel.txt") == std::string::npos, "junction destination not traversed");
    require(fs::exists(external / "sentinel.txt"), "external sentinel retained");
}
#endif
} // namespace

int main(int argc, char* argv[]) {
    try {
        portable_contracts();
#ifdef _WIN32
        require(argc == 2, "provide candidate MQB executable");
        const auto work = fs::current_path();
        require(!fs::exists(work / "storage-fixtures"), "fresh fixture directory required; do not overwrite evidence");
        windows_contracts(work / "storage-fixtures" / "native", work / "storage-evidence" / "pinning");
        cli_lifecycle(fs::absolute(argv[1]), work / "storage-fixtures" / fs::path{L"lifecycle space \u65e5"}, work / "storage-evidence");
        std::cout << "storage fixture and command evidence retained under " << text(work) << '\n';
#else
        (void)argc; (void)argv;
        std::cout << "portable model/report contracts only; Windows/CLI lifecycle NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
