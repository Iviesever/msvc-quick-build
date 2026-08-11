#include <iostream>

extern "C" int parity_library_value();

int main() {
    std::cout << "library=" << parity_library_value() << '\n';
    return 0;
}
