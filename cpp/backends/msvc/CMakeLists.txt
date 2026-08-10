add_library(mqb_msvc STATIC
    src/MsvcToolchainLocator.cpp
)

target_include_directories(mqb_msvc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_msvc
    PUBLIC
        mqb_core
        mqb_process
)

target_compile_features(mqb_msvc PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_msvc PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_msvc PRIVATE -Wall -Wextra -Wpedantic)
endif()
