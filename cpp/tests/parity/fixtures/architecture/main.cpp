#include <iostream>

int main() {
#if defined(_M_X64)
    std::cout << "arch=x64\n";
#elif defined(_M_IX86)
    std::cout << "arch=x86\n";
#else
    std::cout << "arch=unknown\n";
#endif
    return 0;
}
