add_library(mqb_config STATIC
    src/ProjectConfig.cpp
)

target_include_directories(mqb_config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_config PUBLIC mqb_core)
target_compile_features(mqb_config PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_config PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_config PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MQB_BUILD_TESTS)
    add_subdirectory(tests)
endif()
