#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include "../../../src/msvc/toolchain/VisualStudioToolchainCache.hpp"
#include "../../../src/msvc/toolchain/VisualStudioToolchainCacheReader.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {
namespace fs = std::filesystem;
int failures = 0;
void expect(bool ok, std::string_view message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
fs::path unique_cache_root() {
    return fs::temp_directory_path() / ("mqb-v9-adoption-" + std::to_string(::GetCurrentProcessId()) + "-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
void restore_cache_text(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
class RejectingRunner final : public mqb::process::ProcessRunner {
public:
    std::size_t calls{};
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        return std::unexpected(mqb::process::ProcessError{
            .code = mqb::process::ProcessErrorCode::launch_failed,
            .message = "cache miss attempted a subprocess"});
    }
};
class ScopedEnvironment final {
    std::optional<std::string> path_;
public:
    ScopedEnvironment() { if (const auto* value = std::getenv("PATH")) path_ = value; }
    ~ScopedEnvironment() { _putenv_s("PATH", path_ ? path_->c_str() : ""); }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    void set(const char* name, const std::string& value) {
        if (std::string_view{name} != "PATH" || _putenv_s(name, value.c_str()) != 0)
            throw std::runtime_error("Cannot change owned PATH test scope");
    }
};
// Exercise the real product cache/locator entry points. No installed tool file
// is modified, no cache/environment values are printed, and misses cannot launch
// subprocesses. Synthetic compiler files below are metadata only, never executed.
void verify_v9_production_admission(const mqb::msvc::DiscoveryOptions& options,
                                    const fs::path& cache_file, const std::string& original) {
    namespace detail = mqb::msvc::detail;
    namespace wire = detail::v9_cache;
    const auto require = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    const auto root = unique_cache_root();
    struct Restore {
        fs::path cache, root;
        const std::string& bytes;
        ~Restore() {
            std::error_code ignored;
            if (fs::is_directory(cache, ignored)) fs::remove(cache, ignored);
            restore_cache_text(cache, bytes);
            fs::remove_all(root, ignored);
        }
    } restore{cache_file, root, original};
    std::size_t cases = 0;
    const auto passed = [&](const char* name) {
        ++cases;
        std::cout << "V9_ADMISSION " << name << " passed=1\n";
    };
    const auto write = [&](const fs::path& file, const std::string& bytes) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        require(bool(out), "V9 fixture write failed");
    };
    const auto replace = [&](std::string bytes, const std::string& from, const std::string& to) {
        const auto at = bytes.find(from);
        require(at != std::string::npos, "V9 fixture marker absent");
        bytes.replace(at, from.size(), to);
        return bytes;
    };
    const auto field = [&](std::string bytes, const std::string& label, const std::string& value) {
        const auto at = bytes.find("\n" + label + " ");
        require(at != std::string::npos, "V9 field marker absent");
        const auto end = bytes.find('\n', at + 1);
        require(end != std::string::npos, "V9 field terminator absent");
        std::ostringstream out;
        out << '\n' << label << ' ' << std::quoted(value);
        bytes.replace(at, end - at, out.str());
        return bytes;
    };
    try {
        fs::create_directories(root);
        const auto canonical = detail::reuse_visual_studio_toolchain_cache(cache_file, options);
        require(canonical.has_value(), "Original real cache not reusable for V9 controls");
        const auto accept = [&](const char* name, const std::string& bytes, wire::Route route) {
            write(cache_file, bytes);
            // The exact private implementation used by the product, not a copy.
            {
                std::ifstream stream(cache_file, std::ios::binary);
                const auto parsed = wire::read_prechecked(stream, fs::file_size(cache_file));
                require(parsed.record.has_value() && parsed.route == route, "Production reader route mismatch");
            }
            const auto reused = detail::reuse_visual_studio_toolchain_cache(cache_file, options);
            require(reused && reused->reused && reused->identity.binary_stamp == canonical->identity.binary_stamp
                && reused->identity.compiler == canonical->identity.compiler
                && reused->environment.size() == canonical->environment.size(), "Product cache reuse changed");
            for (std::size_t i = 0; i < reused->environment.size(); ++i) {
                const auto& a = reused->environment[i]; const auto& b = canonical->environment[i];
                require(a.name == b.name && a.value == b.value && a.remove == b.remove, "Reused environment changed");
            }
            RejectingRunner denied;
            const auto adopted = mqb::msvc::MsvcToolchainLocator{denied}.discover(options);
            require(adopted && adopted->reused && denied.calls == 0, "Locator did not adopt valid V9 cache");
            passed(name);
        };
        accept("canonical", original, wire::Route::canonical);
        accept("unquoted-fallback", replace(original, "target \"x64\"", "target x64"), wire::Route::byte_fallback);
        accept("signed-number-fallback", replace(original, "preference 1\n", "preference +1\n"), wire::Route::byte_fallback);
        accept("trailing-space-fallback", original + " \t\n", wire::Route::byte_fallback);
        require(!original.empty() && original.back() == '\n', "Writer final LF missing");
        accept("missing-final-lf-fallback", original.substr(0, original.size() - 1), wire::Route::byte_fallback);
        const auto reject = [&](const char* name, const std::string& bytes) {
            write(cache_file, bytes);
            require(!detail::reuse_visual_studio_toolchain_cache(cache_file, options), "Invalid V9 cache reused");
            passed(name);
        };
        reject("bad-magic", replace(original, "MQB_TOOLCHAIN_CACHE_V9", "MQB_TOOLCHAIN_CACHE_V8"));
        reject("truncation", original.substr(0, original.size() / 2));
        reject("trailing-junk", original + "unexpected");
        reject("oversize", std::string(static_cast<std::size_t>(wire::max_cache_size + 1), 'x'));
        reject("target-key", field(original, "target", "x86"));
        reject("host-key", field(original, "host", "x86"));
        reject("preference-key", replace(original, "preference 1\n", "preference 0\n"));
        reject("binary-stamp", field(original, "binary_stamp", "stale-identity"));
        reject("ambient-path-record", field(original, "ambient_path", "unmatched-path"));
        reject("empty-effective-path", field(original, "effective_path", ""));
        reject("missing-vc-root", field(original, "vc_tools_root", (root / "absent").generic_string()));
        const auto include_marker = std::string("env_name \"INCLUDE\"\nenv_value ");
        const auto include_at = original.find(include_marker);
        require(include_at != std::string::npos, "Installed INCLUDE field absent");
        auto untrusted = original;
        const auto value_at = include_at + include_marker.size();
        const auto line_end = untrusted.find('\n', value_at);
        require(line_end != std::string::npos, "Installed INCLUDE terminator absent");
        std::ostringstream untrusted_path; untrusted_path << std::quoted(root.generic_string());
        untrusted.replace(value_at, line_end - value_at, untrusted_path.str());
        reject("existing-untrusted-include", untrusted);
        write(cache_file, original);
        const auto timestamp = fs::last_write_time(cache_file);
        fs::last_write_time(cache_file, fs::file_time_type::clock::now() - std::chrono::hours{1});
        require(!detail::reuse_visual_studio_toolchain_cache(cache_file, options), "Expired V9 cache reused"); passed("age-expired");
        fs::last_write_time(cache_file, fs::file_time_type::clock::now() + std::chrono::hours{1});
        require(!detail::reuse_visual_studio_toolchain_cache(cache_file, options), "Future V9 cache reused"); passed("age-future");
        fs::last_write_time(cache_file, timestamp);
        {
            ScopedEnvironment scoped;
            scoped.set("PATH", detail::environment_value("PATH").value_or("") + ";V9-admission-changed");
            require(!detail::reuse_visual_studio_toolchain_cache(cache_file, options), "Changed launching PATH reused"); passed("ambient-path-process");
        }
        require(fs::remove(cache_file), "Owned test cache removal failed");
        require(!detail::reuse_visual_studio_toolchain_cache(cache_file, options), "Missing cache reused"); passed("missing-file");
        fs::create_directory(cache_file);
        require(!detail::reuse_visual_studio_toolchain_cache(cache_file, options), "Directory cache reused"); passed("directory-file");
        require(fs::remove(cache_file), "Owned test directory removal failed");
        write(cache_file, original);

        // A separate complete, trusted synthetic installation permits VC/stamp/
        // executable-existence changes without touching the runner's installation.
        const auto tools = root / "SyntheticVS/VC/Tools/MSVC/1.0";
        const auto bin = tools / "bin/Hostx64/x64";
        fs::create_directories(bin); fs::create_directories(tools / "include"); fs::create_directories(tools / "lib");
        for (const auto name : {"cl.exe", "link.exe", "lib.exe"}) write(bin / name, "synthetic metadata; never executed");
        mqb::msvc::MsvcToolchain synthetic;
        synthetic.vc_tools_root = tools; synthetic.source = mqb::msvc::ToolchainSource::visual_studio;
        synthetic.identity.compiler = bin / "cl.exe"; synthetic.linker = bin / "link.exe"; synthetic.librarian = bin / "lib.exe";
        const auto stamp = detail::binary_stamp(synthetic.identity.compiler);
        require(stamp.has_value(), "Synthetic stamp unavailable"); synthetic.identity.binary_stamp = *stamp;
        synthetic.environment = {{"INCLUDE", (tools / "include").generic_string()}, {"LIB", (tools / "lib").generic_string()},
            {"LIBPATH", (tools / "lib").generic_string()}, {"VCToolsInstallDir", tools.generic_string()}, {"PATH", "synthetic-effective"}};
        const auto synthetic_cache = root / "synthetic.cache";
        detail::save_visual_studio_toolchain_cache_best_effort(synthetic_cache, options, synthetic);
        require(detail::reuse_visual_studio_toolchain_cache(synthetic_cache, options).has_value(), "Synthetic validated cache rejected"); passed("synthetic-valid");
        const auto newer = tools.parent_path() / "2.0"; fs::create_directory(newer);
        require(!detail::reuse_visual_studio_toolchain_cache(synthetic_cache, options), "Newer VC version ignored"); passed("newer-vc");
        require(fs::remove(newer), "Owned VC fixture removal failed");
        for (const auto name : {"cl.exe", "link.exe", "lib.exe"}) {
            const auto path = bin / name; fs::rename(path, bin / "saved-file");
            require(!detail::reuse_visual_studio_toolchain_cache(synthetic_cache, options), "Missing tool executable ignored");
            fs::rename(bin / "saved-file", path); passed(name);
        }
        write(synthetic.identity.compiler, "changed synthetic compiler bytes and size");
        require(!detail::reuse_visual_studio_toolchain_cache(synthetic_cache, options), "Changed compiler stamp ignored"); passed("compiler-file-stamp");
        require(cases == 28, "Unexpected V9 adoption coverage");
        std::cout << "V9_ADMISSION_COMPLETE cases=" << cases << " performance_verified=0\n";
    } catch (const std::exception& error) {
        expect(false, error.what());
    }
}

} // namespace

int main() {
    const auto root = unique_cache_root();
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); }
    } cleanup{root};
    try {
        if (!fs::create_directory(root)) throw std::runtime_error("Fresh test root required");
        mqb::msvc::DiscoveryOptions options;
        options.preference = mqb::msvc::ToolchainPreference::visual_studio;
        options.cache_file = root / "installed.cache";
        mqb::platform::windows::WindowsProcessRunner runner;
        const auto cold = mqb::msvc::MsvcToolchainLocator{runner}.discover(options);
        if (!cold || cold->reused) throw std::runtime_error("Fresh installed toolchain discovery failed");
        std::ifstream input(*options.cache_file, std::ios::binary);
        if (!input) throw std::runtime_error("Initial installed cache missing");
        const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        input.close();
        verify_v9_production_admission(options, *options.cache_file, bytes);
    } catch (const std::exception& error) { expect(false, error.what()); }
    return failures == 0 ? 0 : 1;
}
