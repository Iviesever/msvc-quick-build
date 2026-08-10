add_library(mqb_application STATIC
    src/IncrementalCompileCoordinator.cpp
)

target_include_directories(mqb_application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_application
    PUBLIC
        mqb_core
        mqb_msvc
        mqb_process
)

target_compile_features(mqb_application PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_application PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_application PRIVATE -Wall -Wextra -Wpedantic)
endif()
