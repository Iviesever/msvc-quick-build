#include "parity_config.hpp"

#include <iostream>

#ifndef PARITY_CONFIG
#error PARITY_CONFIG must come from the project config
#endif

#if !defined(_MSVC_LANG) || _MSVC_LANG != 201703L
#error project config must select C++17
#endif

#ifndef NDEBUG
#error project config must select Release
#endif

#ifdef _DEBUG
#error Release config must not define _DEBUG
#endif

int main() {
    std::cout << "config=release;define=" << PARITY_CONFIG
              << ";include=" << parity_header_value
              << ";std=17\n";
    return 0;
}
