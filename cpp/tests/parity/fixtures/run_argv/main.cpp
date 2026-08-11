#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "argv=";
    for (int index = 1; index < argc; ++index) {
        if (index != 1) std::cout << '|';
        std::cout << argv[index];
    }
    std::cout << '\n';
    return 0;
}
