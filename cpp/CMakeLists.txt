cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(mqb VERSION 5.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release candidates are shipped as a standalone mqb.exe. Keep MSVC's runtime
# linked statically so the package does not depend on a separately installed
# Visual C++ redistributable. Debug keeps the matching debug static runtime.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(MQB_VERSION "${PROJECT_VERSION}-dev" CACHE STRING "User-visible MQB version")

option(MQB_BUILD_TESTS "Build MQB tests" ON)
option(MQB_ENABLE_INSTALLED_MSVC_TESTS "Run tests that require an installed Visual Studio toolchain" OFF)

if(MQB_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

add_subdirectory(core)
add_subdirectory(process)
add_subdirectory(json)
add_subdirectory(modules)
add_subdirectory(config)
add_subdirectory(discovery)

if(WIN32)
    add_subdirectory(platform/windows)
    add_subdirectory(backends/msvc)
    add_subdirectory(orchestration)
endif()

add_subdirectory(apps/mqb)

if(MQB_BUILD_TESTS)
    add_subdirectory(tests)
    if(WIN32)
        add_subdirectory(orchestration/tests)
    endif()
endif()
