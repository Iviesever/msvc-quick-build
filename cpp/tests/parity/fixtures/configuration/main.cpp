#include <iostream>

int main() {
#if defined(_DEBUG)
    std::cout << "config=debug\n";
#elif defined(NDEBUG)
    std::cout << "config=release\n";
#else
    std::cout << "config=unknown\n";
#endif
    return 0;
}
