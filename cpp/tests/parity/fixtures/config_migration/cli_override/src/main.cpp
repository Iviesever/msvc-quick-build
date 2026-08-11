#include "parity_config.hpp"

#include <iostream>

#ifndef PARITY_CONFIG
#error PARITY_CONFIG must remain additive from project config
#endif

#ifndef CLI_OVERRIDE
#error CLI_OVERRIDE must come from the command line
#endif

#if !defined(_MSVC_LANG) || _MSVC_LANG != 201703L
#error project config standard must survive scalar CLI overrides
#endif

#ifndef _DEBUG
#error CLI must override project Release with Debug
#endif

#ifdef NDEBUG
#error CLI Debug override must remove Release NDEBUG behavior
#endif

int main() {
    std::cout << "config=debug;config_define=" << PARITY_CONFIG
              << ";cli_define=" << CLI_OVERRIDE
              << ";include=" << parity_header_value
              << ";std=17\n";
    return 0;
}
