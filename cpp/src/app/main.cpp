#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Application.hpp"
#include "CompdbCommand.hpp"
#include "PlanCommand.hpp"
#include "mqb/platform/windows/CommandLine.hpp"

int wmain(const int argc, wchar_t* argv[]) {
    std::vector<std::string> argument_storage;
    argument_storage.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) {
        auto encoded = mqb::platform::windows::utf16_to_utf8(argv[index]);
        if (!encoded) {
            std::cerr << "error: failed to decode Windows command-line argument "
                      << index << " as UTF-8: " << encoded.error().message;
            if (encoded.error().native_code != 0) {
                std::cerr << " (Win32 " << encoded.error().native_code << ')';
            }
            std::cerr << '\n';
            return 2;
        }
        argument_storage.push_back(std::move(*encoded));
    }

    std::vector<std::string_view> arguments;
    arguments.reserve(argument_storage.size());
    for (const auto& argument : argument_storage) {
        arguments.emplace_back(argument);
    }

    if (!arguments.empty() && arguments.front() == "compdb") {
        return mqb::app::run_compdb_command(
            std::span<const std::string_view>{arguments}.subspan(1));
    }
    if (!arguments.empty() && arguments.front() == "plan") {
        return mqb::app::run_plan_command(
            std::span<const std::string_view>{arguments}.subspan(1));
    }

    mqb::app::Application application;
    return application.run(std::span<const std::string_view>{arguments});
}
