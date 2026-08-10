add_library(mqb_orchestration STATIC
    src/MsvcIncrementalCompileCoordinator.cpp
    src/MsvcIncrementalLinkCoordinator.cpp
)

target_include_directories(mqb_orchestration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_orchestration
    PUBLIC
        mqb_core
        mqb_msvc
        mqb_process
)

target_compile_features(mqb_orchestration PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_orchestration PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_orchestration PRIVATE -Wall -Wextra -Wpedantic)
endif()
