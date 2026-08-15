#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcDefaultLibraryPolicy.hpp"
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

    const std::vector<std::string> default_policy_args{
        "/DEFAULTLIB:math",
        "/DEFAULTLIB:codec.lib",
        "/NODEFAULTLIB:CODEC",
        "/DEFAULTLIB:nested/direct",
    };
    const auto default_policy = mqb::msvc::MsvcDefaultLibraryPolicy::route(
        default_policy_args,
        work);
    expect(default_policy.has_value(), "valid DEFAULTLIB policy should route");
    if (default_policy) {
        expect(default_policy->passthrough.size() == default_policy_args.size(),
               "DEFAULTLIB observation must preserve raw linker argument count/order");
        expect(default_policy->effective_libraries.size() == 2,
               "NODEFAULTLIB:name should suppress only the matching default library");
        if (default_policy->effective_libraries.size() == 2) {
            expect(default_policy->effective_libraries[0] == "math",
                   "bare DEFAULTLIB name should remain a linker search name");
            expect(default_policy->effective_libraries[1]
                       == path_text((work / "nested/direct").lexically_normal()),
                   "path-bearing DEFAULTLIB should resolve against its supplying layer base");
        }
        expect(default_policy->passthrough.back()
                   == "/DEFAULTLIB:" + path_text((work / "nested/direct").lexically_normal()),
               "path-bearing DEFAULTLIB argv should be normalized without changing its role");
    }

    const std::vector<std::string> ignore_all_args{
        "/NODEFAULTLIB",
        "/DEFAULTLIB:math",
    };
    const auto ignore_all = mqb::msvc::MsvcDefaultLibraryPolicy::route(ignore_all_args);
    expect(ignore_all.has_value() && ignore_all->effective_libraries.empty(),
           "global NODEFAULTLIB should suppress user-declared DEFAULTLIB freshness evidence regardless of order");

    const std::vector<std::string> suppressed_path_args{
        "/DEFAULTLIB:nested/direct.lib",
        "/NODEFAULTLIB:DIRECT",
    };
    const auto suppressed_path = mqb::msvc::MsvcDefaultLibraryPolicy::route(
        suppressed_path_args,
        work);
    expect(suppressed_path.has_value() && suppressed_path->effective_libraries.empty(),
           "NODEFAULTLIB:name should match a path-bearing DEFAULTLIB by case-insensitive file name and optional .lib suffix");

    const std::vector<std::string> invalid_default_args{"/DEFAULTLIB:"};
    const auto invalid_default = mqb::msvc::MsvcDefaultLibraryPolicy::route(invalid_default_args);
    expect(!invalid_default, "empty DEFAULTLIB declaration must fail before LINK");

    const std::vector<std::string> invalid_ignore_args{"/NODEFAULTLIB:"};
    const auto invalid_ignore = mqb::msvc::MsvcDefaultLibraryPolicy::route(invalid_ignore_args);
    expect(!invalid_ignore, "empty NODEFAULTLIB:name declaration must fail before LINK");

    const std::vector<std::string> available_requests{
        "math",
        "does-not-exist",
        "nested/direct",
    };
    const std::vector<fs::path> available_search_dirs{explicit_dir};
    const auto available = mqb::msvc::MsvcLibraryResolver::resolve_available(
        toolchain,
        available_requests,
        available_search_dirs,
        work);
    expect(available.has_value(),
           "available-only default-library evidence should not fail for an unresolved unused library");
    if (available) {
        expect(available->files.size() == 2,
               "available-only resolution should retain only discoverable default-library files");
        if (available->files.size() == 2) {
            expect(available->files[0] == explicit_math.lexically_normal(),
                   "default-library evidence should share explicit library-directory search precedence");
            expect(available->files[1] == direct.lexically_normal(),
                   "default-library evidence should resolve path-bearing requests from the link working directory");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_msvc_library_resolver_tests passed\n";
    return 0;
}
