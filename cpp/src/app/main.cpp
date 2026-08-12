#include <span>
#include <string_view>
#include <vector>

#include "Application.hpp"

int main(const int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    mqb::app::Application application;
    return application.run(std::span<const std::string_view>{arguments});
}
