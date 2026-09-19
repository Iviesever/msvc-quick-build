#include "StorageCommand.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "StorageReport.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
#include "mqb/platform/windows/StorageInventory.hpp"

namespace mqb::app {
int run_storage_command(std::span<const std::string_view> arguments) {
    constexpr std::string_view usage =
        "Usage: mqb storage [--project <directory>] [--format text|json]\n"
        "Observe exactly <directory>/.mqb (default: current directory); no config search, build or deletion.\n"
        "Legacy cache references do not establish ownership, liveness or configuration.\n"
        "Run with builders stopped: read handles may refuse concurrent writes/renames.\n"
        "Redirect reports outside .mqb; partial observations return exit 1, usage errors exit 2.\n";
    bool json = false, format_seen = false, project_seen = false;
    std::filesystem::path project;
    try {
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto arg = arguments[i];
            if (arg == "--help" || arg == "-h") { std::cout << usage; return std::cout ? 0 : 1; }
            if (arg == "--format" && !format_seen && i + 1 < arguments.size()) {
                format_seen = true;
                const auto value = arguments[++i];
                if (value == "json") json = true;
                else if (value != "text") { std::cerr << usage; return 2; }
            } else if (arg == "--project" && !project_seen && i + 1 < arguments.size()) {
                project_seen = true;
                const auto value = arguments[++i];
                if (value.empty() || value.find('\0') != std::string_view::npos) { std::cerr << usage; return 2; }
                project = std::filesystem::path{std::u8string{
                    reinterpret_cast<const char8_t*>(value.data()), value.size()}};
            } else { std::cerr << "error: invalid storage argument\n" << usage; return 2; }
        }
        if (!project_seen) project = std::filesystem::current_path();
        std::error_code ec;
        project = std::filesystem::absolute(project, ec);
        if (ec || !std::filesystem::is_directory(project, ec) || ec) {
            std::cerr << "error: storage project must be an existing directory\n";
            return 2;
        }
        // Reuse the established artifact-root layout. It canonicalizes the
        // project, not .mqb, so the walker can still refuse a reparse artifact root.
        auto layout = ProjectArtifactLayout::create(project);
        if (!layout) { std::cerr << "error: " << layout.error().message << '\n'; return 2; }
        auto inventory = platform::windows::scan_storage(layout->artifact_root(), observe_storage_cache);
        associate_storage_references(inventory, platform::windows::path_identity_key);
        if (!diagnostics::write_storage_report(std::cout, inventory, json)) {
            std::cerr << "error: storage report output failed\n";
            return 1;
        }
        return inventory.issues.empty() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: storage observation failed: " << error.what() << '\n';
        return 1;
    }
}
} // namespace mqb::app
