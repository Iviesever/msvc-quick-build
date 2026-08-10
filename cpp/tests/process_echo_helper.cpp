#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main(const int argc, char** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "--flood") {
        constexpr std::size_t payload_size = 256 * 1024;
        std::cout << std::string(payload_size, 'O');
        std::cerr << std::string(payload_size, 'E');
        return 17;
    }

    std::cout << "ARGC=" << (argc - 1) << '\n';
    for (int index = 1; index < argc; ++index) {
        std::cout << "ARG=[" << argv[index] << "]\n";
    }

    if (const char* value = std::getenv("MQB_TEST_ENV")) {
        std::cout << "ENV=[" << value << "]\n";
    } else {
        std::cout << "ENV=[<missing>]\n";
    }

    std::cerr << "STDERR-MARKER\n";
    return 23;
}
