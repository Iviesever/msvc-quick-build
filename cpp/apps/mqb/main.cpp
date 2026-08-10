#include <filesystem>
#include <iostream>

#include "mqb/core/BuildRequest.hpp"
#include "mqb/core/BuildTypes.hpp"

int main(const int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: mqb <source-file>\n";
        return 2;
    }

    mqb::BuildRequest request;
    request.entry = std::filesystem::path{argv[1]};

    std::cout << "MQB C++ v2 scaffold\n"
              << "entry: " << request.entry.string() << '\n'
              << "config: " << mqb::to_string(request.configuration) << '\n'
              << "arch: " << mqb::to_string(request.architecture) << '\n'
              << "std: " << mqb::to_string(request.standard) << '\n';

    return 0;
}
