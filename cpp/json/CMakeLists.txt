add_library(mqb_json STATIC
    src/Json.cpp
)

target_include_directories(mqb_json
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mqb_json PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_json PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_json PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MQB_BUILD_TESTS)
    add_subdirectory(tests)
endif()
