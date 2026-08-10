add_library(mqb_discovery STATIC
    src/ModuleSyntax.cpp
    src/SourceDiscovery.cpp
)

target_include_directories(mqb_discovery
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mqb_discovery PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_discovery PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_discovery PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MQB_BUILD_TESTS)
    add_subdirectory(tests)
endif()
