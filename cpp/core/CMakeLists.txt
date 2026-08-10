add_library(mqb_core STATIC
    src/BuildSignature.cpp
    src/BuildTypes.cpp
    src/CompileCache.cpp
    src/DependencyGraph.cpp
)

target_include_directories(mqb_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mqb_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
