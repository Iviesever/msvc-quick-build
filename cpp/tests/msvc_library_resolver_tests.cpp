#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcLibraryResolver.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void touch(const fs::path& file) {
    fs::create_directories(file.parent_path());
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    stream << "lib";
}

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
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
        .root = fs::temp_directory_path() / ("mqb_library_resolver_" + std::to_string(unique)),
    };
    const fs::path work = tree.root / "work";
    const fs::path explicit_dir = tree.root / "explicit";
    const fs::path env_dir = tree.root / "env libs";

    const fs::path explicit_math = explicit_dir / "math.lib";
    const fs::path env_math = env_dir / "math.lib";
    const fs::path env_codec = env_dir / "codec.lib";
    const fs::path direct = work / "nested" / "direct.lib";
    touch(explicit_math);
    touch(env_math);
    touch(env_codec);
    touch(direct);
    fs::create_directories(work);

    mqb::msvc::MsvcToolchain toolchain;
    toolchain.environment.push_back({
        .name = "Lib",
        .value = path_text(env_dir),
    });

    mqb::LinkOptions options;
    options.library_directories = {explicit_dir};
    options.libraries = {"math", "codec.lib", "nested/direct"};

    const auto resolved = mqb::msvc::MsvcLibraryResolver::resolve(toolchain, options, work);
    expect(resolved.has_value(), "valid requested libraries should resolve");
    if (resolved) {
        expect(resolved->files.size() == 3,
               "resolver should preserve one resolved file per requested library");
        if (resolved->files.size() == 3) {
            expect(resolved->files[0] == explicit_math.lexically_normal(),
                   "explicit -L directory should take precedence over toolchain LIB");
            expect(resolved->files[1] == env_codec.lexically_normal(),
                   "toolchain LIB environment should resolve bare library names");
            expect(resolved->files[2] == direct.lexically_normal(),
                   "relative explicit library path should resolve from link working directory");
        }
    }

    mqb::LinkOptions current_directory_options;
    touch(work / "local.lib");
    current_directory_options.libraries = {"local"};
    const auto from_current = mqb::msvc::MsvcLibraryResolver::resolve(
        toolchain, current_directory_options, work);
    expect(from_current.has_value()
               && from_current->files.size() == 1
               && from_current->files.front() == (work / "local.lib").lexically_normal(),
           "working directory should be searched before toolchain LIB fallback");

    mqb::LinkOptions missing_options;
    missing_options.libraries = {"does-not-exist"};
    const auto missing = mqb::msvc::MsvcLibraryResolver::resolve(
        toolchain, missing_options, work);
    expect(!missing, "unresolved requested library must fail before link.exe runs");
    if (!missing) {
        expect(missing.error().code == mqb::msvc::LibraryResolutionErrorCode::library_not_found,
               "missing library should report library_not_found");
    }

    mqb::LinkOptions empty_options;
    empty_options.libraries = {""};
    const auto empty = mqb::msvc::MsvcLibraryResolver::resolve(toolchain, empty_options, work);
    expect(!empty, "empty requested library should be rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_msvc_library_resolver_tests passed\n";
    return 0;
}
