#pragma once

#include <array>
#include <cstdint>
#include <sstream>
#include <iterator>
#include <cwctype>

#include "mqb/platform/windows/WindowsWriteDomain.hpp"

// Included after lifetime_tests in windows_process_runner_tests.cpp so the
// existing retained-HANDLE native helper launcher is reused, not reimplemented.
namespace write_domain_tests {
namespace fs = std::filesystem;
using mqb::platform::windows::WindowsWriteDomain;
using mqb::platform::windows::WriteDomainPhase;
using mqb::platform::windows::WriteDomainErrorCode;
using lifetime_tests::Handle;
using lifetime_tests::spawn_native;
using lifetime_tests::self_path;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string utf8(const fs::path& p) {
    auto s = mqb::platform::windows::utf16_to_utf8(p.wstring());
    require(s.has_value(), "test path encoding failed"); return *s;
}
void write(const fs::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary); f << bytes; f.flush(); require(bool(f), "test evidence write failed");
}
std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary); require(bool(f), "test evidence missing");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
WindowsWriteDomain pin(const fs::path& p) {
    auto value = WindowsWriteDomain::open(p);
    if (!value) throw std::runtime_error(value.error().message + " Win32=" + std::to_string(value.error().win32_error));
    return std::move(*value);
}
void reserve(WindowsWriteDomain& domain) {
    auto r = domain.try_reserve();
    if (!r) throw std::runtime_error(r.error().message + " Win32=" + std::to_string(r.error().win32_error)
        + " NT=" + std::to_string(r.error().ntstatus.value_or(0)));
}
int helper(int argc, wchar_t** argv) {
    if (argc < 2) return -1;
    const std::wstring_view mode{argv[1]};
    if (!mode.starts_with(L"--write-domain-")) return -1;
    try {
        if (mode == L"--write-domain-contend") {
            require(argc == 7, "contender arguments");
            Handle start{::OpenEventW(SYNCHRONIZE, FALSE, argv[3])};
            Handle done{::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[4])};
            Handle release{::OpenEventW(SYNCHRONIZE, FALSE, argv[5])};
            require(bool(start) && bool(done) && bool(release), "contender event open");
            auto d = pin(argv[2]);
            require(::WaitForSingleObject(start.value, 15000) == WAIT_OBJECT_0, "contender start watchdog");
            const auto r = d.try_reserve();
            require(r || r.error().code == WriteDomainErrorCode::occupied_or_unresolved, "contender unexpected native failure");
            write(argv[6], r ? "acquired\n" : "blocked\nNT=" + std::to_string(r.error().ntstatus.value_or(0)) + "\n");
            require(::SetEvent(done.value) != FALSE, "contender result signal");
            if (r) {
                require(::WaitForSingleObject(release.value, 15000) == WAIT_OBJECT_0, "contender release watchdog");
                require(d.withdraw_unstarted().has_value(), "unstarted contender withdrawal");
            }
            return 0;
        }
        if (mode == L"--write-domain-abandon") {
            require(argc == 3, "abandon arguments"); auto d = pin(argv[2]); reserve(d);
            return 23; // Normal destructor must ALSO retain an unresolved marker.
        }
        if (mode == L"--write-domain-residual") {
            require(argc == 5, "residual arguments");
            Handle ready{::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3])};
            Handle release{::OpenEventW(SYNCHRONIZE, FALSE, argv[4])};
            Handle file{::CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
            require(bool(file) && bool(ready) && bool(release), "residual setup");
            DWORD n{}; constexpr char first[] = "before-owner-death\n";
            require(::WriteFile(file.value, first, sizeof(first)-1, &n, nullptr) && n == sizeof(first)-1, "residual first write");
            require(::SetEvent(ready.value) != FALSE, "residual ready");
            require(::WaitForSingleObject(release.value, 15000) == WAIT_OBJECT_0, "residual release watchdog");
            constexpr char last[] = "after-owner-death\n";
            require(::WriteFile(file.value, last, sizeof(last)-1, &n, nullptr) && n == sizeof(last)-1
                && ::FlushFileBuffers(file.value), "residual post-owner write");
            return 0;
        }
        if (mode == L"--write-domain-owner") {
            require(argc == 7, "owner arguments");
            auto d = pin(argv[2]); reserve(d); require(d.begin_writes().has_value(), "owner write admission");
            Handle ready{::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3])};
            Handle child_ready{::OpenEventW(SYNCHRONIZE, FALSE, argv[4])};
            auto child = spawn_native(self_path(), {L"--write-domain-residual", (fs::path{argv[2]} / "payload").wstring(), argv[4], argv[5]});
            require(child.has_value(), "owner residual launch");
            FILETIME created{}, end{}, kernel{}, user{};
            require(::GetProcessTimes(child->value, &created, &end, &kernel, &user), "retained residual identity");
            const auto identity = (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
            write(argv[6], std::to_string(::GetProcessId(child->value)) + " " + std::to_string(identity));
            require(::WaitForSingleObject(child_ready.value, 15000) == WAIT_OBJECT_0, "residual not ready");
            require(::SetEvent(ready.value) != FALSE, "owner ready");
            // Parent kills THIS retained fixture process, never a PID-found user
            // service. The independent child has no inherited domain handle.
            ::WaitForSingleObject(child->value, 15000);
            return 99; // Reaching a normal owner return is a failed death control.
        }
        return 91;
    } catch (const std::exception& e) {
        std::cerr << "WRITE_DOMAIN_HELPER_FAILURE " << e.what() << '\n'; return 92;
    }
}
int run() {
    unsigned checks = 0;
    fs::path root;
    bool preserve = false;
    std::vector<std::pair<std::string, bool>> outcomes;
    const auto save_checks = [&](bool passed) {
        std::string result = "{\"passed\":" + std::string(passed ? "true" : "false")
            + ",\"full_project_transaction\":false,\"cli_integrated\":false,\"recovery_authorized\":false,\"checks\":[";
        for (std::size_t i = 0; i < outcomes.size(); ++i) {
            if (i) result += ',';
            result += "{\"name\":\"" + outcomes[i].first + "\",\"passed\":" + (outcomes[i].second ? "true" : "false") + '}';
        }
        write(root / "checks.json", result + "]}\n");
    };
    const auto check = [&](bool ok, const char* name) {
        ++checks; outcomes.emplace_back(name, ok);
        std::cout << "WRITE_DOMAIN_CHECK " << name << ' ' << (ok ? "PASS" : "FAIL") << '\n';
        require(ok, name);
    };
    try {
        LARGE_INTEGER stamp{}; require(::QueryPerformanceCounter(&stamp), "test identity clock");
        const auto id = std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(stamp.QuadPart);
        wchar_t destination[32768]{};
        const auto length = ::GetEnvironmentVariableW(L"MQB_WRITE_DOMAIN_EVIDENCE_ROOT", destination, 32768);
        require(length < 32768, "evidence path too long");
        preserve = length != 0;
        root = preserve ? fs::path{destination} : fs::temp_directory_path() / (L"mqb-write-domain-" + id);
        require(!fs::exists(root), "test root reused");
        fs::create_directories(root / L"Root-\u65e5\u672c" / ".mqb");
        const auto actual = root / L"Root-\u65e5\u672c" / ".mqb";
        const auto marker = actual / WindowsWriteDomain::marker_name;
        fs::create_directory(root / "other");
        check(!WindowsWriteDomain::open("relative-path"), "relative-root-rejected");
        check(!WindowsWriteDomain::open(root / "missing") && !fs::exists(root / "missing"), "missing-root-not-created");
        write(root / "file", "not-directory");
        check(!WindowsWriteDomain::open(root / "file"), "file-not-directory");
        {
            auto d = pin(actual);
            const auto physical = *d.identity();
            std::string identity = std::to_string(physical.volume);
            for (auto b : physical.file) identity += " " + std::to_string(b);
            write(root / "held-directory-identity.txt", identity + "\n");
            check(d.identity().has_value() && d.phase() == WriteDomainPhase::pinned && !fs::exists(marker), "read-only-pin-no-marker");
            auto dots = pin(actual / ".");
            auto upper = actual.wstring(); for (auto& c : upper) if (c >= L'a' && c <= L'z') c = c - L'a' + L'A';
            auto case_alias = pin(upper);
            check(d.identity() == dots.identity() && d.identity() == case_alias.identity(), "dot-case-physical-identity");
            wchar_t system[32768]{}; require(::GetSystemDirectoryW(system, 32768), "system directory");
            mqb::process::ProcessSpec mklink;
            mklink.executable = fs::path{system} / "cmd.exe";
            mklink.arguments = {"/d", "/c", "mklink", "/J", utf8(root / "alias"), utf8(actual)};
            mqb::platform::windows::WindowsProcessRunner runner;
            const auto junction = runner.run(mklink);
            check(junction && junction->exit_code == 0, "real-junction-created");
            auto alias = pin(root / "alias");
            check(d.identity() == alias.identity(), "junction-same-physical-domain");
            fs::remove(root / "alias");
            mklink.arguments.back() = utf8(root / "other");
            const auto retargeted = runner.run(mklink);
            check(retargeted && retargeted->exit_code == 0, "junction-retargeted-after-pin");
            auto now_other = pin(root / "alias");
            check(now_other.identity() != alias.identity(), "replacement-alias-has-distinct-identity");
            reserve(d);
            check(d.phase() == WriteDomainPhase::reserved, "exclusive-reservation-created");
            const auto busy = alias.try_reserve();
            check(!busy && busy.error().code == WriteDomainErrorCode::occupied_or_unresolved && busy.error().ntstatus.has_value(), "alias-cannot-bypass-reservation");
            check(!::MoveFileExW(actual.c_str(), (root / "renamed").c_str(), 0), "pinned-directory-not-renamed");
            check(!::DeleteFileW(marker.c_str()), "held-marker-not-unlinked");
            auto moved = std::move(d);
            check(d.phase() == WriteDomainPhase::empty && !d.identity() && !d.try_reserve(), "moved-from-cannot-grant");
            check(moved.identity() == alias.identity() && moved.phase() == WriteDomainPhase::reserved, "move-preserves-claim");
            check(moved.withdraw_unstarted().has_value() && !fs::exists(marker), "explicit-unstarted-withdrawal");
            check(!moved.withdraw_unstarted() && !moved.begin_writes(), "released-state-cannot-complete-or-write");
            reserve(alias);
            check(fs::exists(marker) && !fs::exists(root / "other" / WindowsWriteDomain::marker_name), "marker-created-relative-to-original-handle");
            check(alias.withdraw_unstarted().has_value(), "alias-reacquires-after-unstarted-withdrawal");
            fs::remove(root / "alias"); mklink.arguments.back() = utf8(actual);
            const auto restored = runner.run(mklink);
            require(restored && restored->exit_code == 0, "restore disposable junction");
        }
        for (const auto& bytes : {std::string{}, std::string{"corrupt or old PID 1 expires 0"}}) {
            write(marker, bytes); auto d = pin(actual); const auto r = d.try_reserve();
            check(!r && read(marker) == bytes, bytes.empty() ? "empty-marker-never-recovered" : "stale-text-never-recovered");
            fs::remove(marker); // Test-only removal: no writer was admitted.
        }
        fs::create_directory(marker);
        { auto d = pin(actual); check(!d.try_reserve() && fs::is_directory(marker), "directory-marker-not-replaced"); }
        fs::remove(marker);
        {
            auto d = pin(actual); reserve(d); check(d.begin_writes().has_value(), "explicit-write-transition");
            const auto rejected = d.withdraw_unstarted();
            check(!rejected && rejected.error().code == WriteDomainErrorCode::writes_started, "started-write-never-cleaned-by-withdrawal");
            check(!d.begin_writes() && !d.try_reserve(), "active-state-no-reentry");
        }
        {
            auto d = pin(actual); check(fs::exists(marker) && !d.try_reserve(), "scope-exit-retains-unresolved-marker");
        }
        fs::remove(marker); // Only this fixture knows no real write was submitted above.
        {
            auto child = spawn_native(self_path(), {L"--write-domain-abandon", actual.wstring()});
            require(child.has_value(), "abandon helper launch");
            require(::WaitForSingleObject(child->value, 15000) == WAIT_OBJECT_0, "abandon helper watchdog");
            DWORD code{}; require(::GetExitCodeProcess(child->value, &code), "abandon exit");
            auto d = pin(actual);
            check(code == 23 && fs::exists(marker) && !d.try_reserve(), "normal-owner-exit-is-not-clean-completion");
        }
        fs::remove(marker);
        // Fixed two-contender experiments: common domain, then two distinct
        // domains. Winners wait until EVERY contender has attempted admission.
        for (unsigned separate = 0; separate != 2; ++separate) {
            const auto prefix = L"Local\\MQB-domain-" + id + L"-" + std::to_wstring(separate);
            const auto start_name = prefix + L"-start", release_name = prefix + L"-release";
            Handle start{::CreateEventW(nullptr, TRUE, FALSE, start_name.c_str())};
            Handle release{::CreateEventW(nullptr, TRUE, FALSE, release_name.c_str())};
            std::array<Handle, 2> done, children;
            struct Release { Handle& event; ~Release() { ::SetEvent(event.value); } } release_on_error{release};
            for (unsigned i = 0; i != 2; ++i) {
                const auto done_name = prefix + L"-done" + std::to_wstring(i);
                done[i] = Handle{::CreateEventW(nullptr, TRUE, FALSE, done_name.c_str())};
                const auto domain = i == 0 ? actual : (separate ? root / "other" : root / "alias");
                auto child = spawn_native(self_path(), {L"--write-domain-contend", domain.wstring(), start_name,
                    done_name, release_name, (root / ("contender-" + std::to_string(separate) + "-" + std::to_string(i))).wstring()});
                require(child.has_value(), "contender launch"); children[i] = std::move(*child);
            }
            require(::SetEvent(start.value), "start contenders");
            for (const auto& event : done) require(::WaitForSingleObject(event.value, 15000) == WAIT_OBJECT_0, "contender watchdog");
            unsigned winners = 0;
            for (unsigned i = 0; i != 2; ++i) winners += read(root / ("contender-" + std::to_string(separate) + "-" + std::to_string(i))).starts_with("acquired");
            check(winners == (separate ? 2U : 1U), separate ? "distinct-physical-domains-concurrent" : "same-domain-one-process-winner");
            require(::SetEvent(release.value), "release contenders");
            for (const auto& child : children) {
                require(::WaitForSingleObject(child.value, 15000) == WAIT_OBJECT_0, "contender return watchdog");
                DWORD code{}; require(::GetExitCodeProcess(child.value, &code), "contender exit read");
                check(code == 0, "contender-original-exit-zero");
            }
        }
        // Owner death with a still-running writer. Native HANDLE retention, not
        // PID reuse or a timeout, supplies the test's process observations.
        {
            const auto prefix = L"Local\\MQB-domain-death-" + id;
            const auto owner_name = prefix + L"-owner", child_name = prefix + L"-child", release_name = prefix + L"-release";
            Handle ready{::CreateEventW(nullptr, TRUE, FALSE, owner_name.c_str())};
            Handle child_ready{::CreateEventW(nullptr, TRUE, FALSE, child_name.c_str())};
            Handle release{::CreateEventW(nullptr, TRUE, FALSE, release_name.c_str())};
            struct Release { Handle& event; ~Release() { ::SetEvent(event.value); } } release_on_error{release};
            auto owner = spawn_native(self_path(), {L"--write-domain-owner", actual.wstring(), owner_name, child_name, release_name, (root / "child-id").wstring()});
            require(owner.has_value(), "owner launch");
            require(::WaitForSingleObject(ready.value, 15000) == WAIT_OBJECT_0, "owner ready watchdog");
            DWORD pid{}; std::uint64_t created{}; std::istringstream recorded{read(root / "child-id")};
            require(bool(recorded >> pid >> created), "residual identity record");
            Handle residual{::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
            FILETIME c{}, e{}, k{}, u{};
            require(bool(residual) && ::GetProcessTimes(residual.value, &c, &e, &k, &u), "residual retained handle");
            check(((std::uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime) == created, "residual-creation-identity-matches");
            require(::TerminateProcess(owner->value, 73) && ::WaitForSingleObject(owner->value, 15000) == WAIT_OBJECT_0, "fixture owner termination failed");
            DWORD code{}; require(::GetExitCodeProcess(owner->value, &code), "owner exit read");
            check(code == 73 && ::WaitForSingleObject(residual.value, 0) == WAIT_TIMEOUT, "owner-dead-writer-still-live");
            {
                Handle inspect{::CreateFileW(marker.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
                check(bool(inspect), "kernel-marker-handle-released-not-inherited");
            }
            auto contender = pin(root / "alias");
            check(!contender.try_reserve(), "dead-owner-does-not-authorize-takeover");
            require(::SetEvent(release.value) && ::WaitForSingleObject(residual.value, 15000) == WAIT_OBJECT_0, "residual release watchdog");
            require(::GetExitCodeProcess(residual.value, &code), "residual exit read");
            check(code == 0 && read(actual / "payload") == "before-owner-death\nafter-owner-death\n", "actual-write-after-owner-death-retained");
            check(!contender.try_reserve(), "writer-exit-alone-still-not-recovery-authority");
        }
        // Only the harness, after all its known children have exited, removes its
        // disposable tree. Product code provides no marker-recovery operation.
        save_checks(true);
        if (preserve) fs::remove(root / "alias"); // Do not upload duplicate junction traversals.
        else fs::remove_all(root);
        std::cout << "windows_write_domain_cases " << checks << " checks passed; full_project_transaction=false\n";
        return 0;
    } catch (const std::exception& e) {
        if (!root.empty() && fs::exists(root)) {
            try { save_checks(false); write(root / "failure.txt", e.what()); } catch (...) {}
        }
        std::cerr << "WRITE_DOMAIN_FAILURE " << e.what() << "; evidence=" << utf8(root) << '\n';
        return 1; // Retain the failed tree rather than manufacturing a clean run.
    }
}
} // namespace write_domain_tests
