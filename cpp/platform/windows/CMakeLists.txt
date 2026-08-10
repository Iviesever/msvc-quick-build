add_library(mqb_platform_windows STATIC
    src/CommandLine.cpp
    src/WindowsProcessRunner.cpp
)

target_include_directories(mqb_platform_windows
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_platform_windows PUBLIC mqb_process)
target_compile_features(mqb_platform_windows PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_platform_windows PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_platform_windows PRIVATE -Wall -Wextra -Wpedantic)
endif()
