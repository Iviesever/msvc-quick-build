cmake_minimum_required(VERSION 3.25)

project(mqb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MQB_BUILD_TESTS "Build MQB tests" ON)

add_subdirectory(core)
add_subdirectory(process)

if(WIN32)
    add_subdirectory(platform/windows)
endif()

add_subdirectory(apps/mqb)

if(MQB_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
