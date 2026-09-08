#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {
namespace fs = std::filesystem;
int failures = 0;
void expect(bool condition, std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct TempTree {
    fs::path root = fs::temp_directory_path() / ("mqb_toolchain_reader_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempTree() { std::error_code ec; fs::remove_all(root, ec); }
};
class RejectingRunner final : public mqb::process::ProcessRunner {
public:
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec&) override {
        ++calls;
        return std::unexpected(mqb::process::ProcessError{
            .code = mqb::process::ProcessErrorCode::launch_failed,
            .message = "cache miss attempted discovery"});
    }
    unsigned int calls{};
};
}

int main() {
    TempTree tree;
    const fs::path cache = tree.root / ".mqb/cache/toolchain/vs.mqbcache";
    mqb::msvc::DiscoveryOptions options;
    options.preference = mqb::msvc::ToolchainPreference::visual_studio;
    options.target_architecture = mqb::Architecture::x64;
    options.host_architecture = mqb::Architecture::x64;
    options.cache_file = cache;
    mqb::platform::windows::WindowsProcessRunner real_runner;
    mqb::msvc::MsvcToolchainLocator initial_locator{real_runner};
    const auto initial = initial_locator.discover(options);
    expect(initial.has_value(), "real Visual Studio discovery must create the validated fixture");
    if (!initial) { std::cerr << initial.error().message << '\n'; return 1; }
    std::ifstream input{cache, std::ios::binary};
    const std::string valid{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    input.close();
    expect(valid.starts_with("MQB_TOOLCHAIN_CACHE_V9\n"), "fixture must have the current grammar");
    if (valid.empty()) return 1;
    const auto write = [&](const std::string& bytes) {
        std::ofstream out{cache, std::ios::binary | std::ios::trunc};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        expect(static_cast<bool>(out), "fixture replacement must succeed");
    };
    const auto check = [&](bool hit) {
        RejectingRunner runner;
        mqb::msvc::MsvcToolchainLocator locator{runner};
        const auto result = locator.discover(options);
        if (hit) {
            expect(result && result->reused && runner.calls == 0,
                   "valid v9 transport must reuse without a discovery process");
            if (result) {
                expect(result->identity.compiler == initial->identity.compiler
                    && result->identity.binary_stamp == initial->identity.binary_stamp
                    && result->linker == initial->linker && result->librarian == initial->librarian,
                    "bounded transport must preserve selected tools and environment identity");
            }
        } else {
            expect(!result && runner.calls != 0,
                   "invalid transport must request real discovery, not be accepted or suppress fallback");
        }
    };
    const auto malformed = [&](const std::string& bytes) {
        write(bytes); check(false); write(valid); check(true);
    };
    check(true);
    malformed("");
    malformed(valid.substr(0, valid.size() / 2));
    malformed(valid + std::string(1, '\0'));
    malformed(valid + "unexpected");
    auto old_format = valid;
    old_format.replace(0, std::string{"MQB_TOOLCHAIN_CACHE_V9"}.size(), "MQB_TOOLCHAIN_CACHE_V8");
    malformed(old_format);
    constexpr std::size_t limit = 1024u * 1024u;
    expect(valid.size() < limit, "fixture must fit the existing 1 MiB limit");
    if (valid.size() < limit) {
        auto boundary = valid;
        boundary.resize(limit, ' '); // Existing grammar accepts trailing whitespace.
        write(boundary); check(true);
        boundary.push_back(' ');
        malformed(boundary); // Grammar remains valid, transport exceeds the limit.
    }
    write(valid);
    fs::last_write_time(cache, fs::file_time_type::clock::now() - std::chrono::minutes{31});
    check(false);
    fs::last_write_time(cache, fs::file_time_type::clock::now() + std::chrono::minutes{2});
    check(false);
    write(valid); check(true);
    fs::remove(cache); check(false);
    write(valid); check(true);
    fs::remove(cache);
    fs::create_directory(cache);
    check(false); // Keep the ordinary-file guard; a directory never becomes payload.
    fs::remove(cache);
    write(valid); check(true);
    if (failures) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "mqb_toolchain_cache_reader_tests passed\n";
}
