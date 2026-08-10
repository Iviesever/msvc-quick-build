add_library(mqb_modules STATIC
    src/P1689.cpp
)

target_include_directories(mqb_modules
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_modules
    PUBLIC
        mqb_json
)

target_compile_features(mqb_modules PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_modules PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_modules PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MQB_BUILD_TESTS)
    add_subdirectory(tests)
endif()
