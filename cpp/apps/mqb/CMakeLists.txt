if(NOT WIN32)
    message(STATUS "mqb executable is currently available on Windows only")
    return()
endif()

add_library(mqb_cli STATIC
    Cli.cpp
)

target_include_directories(mqb_cli
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mqb_cli
    PUBLIC
        mqb_core
        mqb_msvc
)

target_compile_features(mqb_cli PUBLIC cxx_std_23)

add_executable(mqb
    main.cpp
)

target_link_libraries(mqb
    PRIVATE
        mqb_cli
        mqb_config
        mqb_discovery
        mqb_orchestration
        mqb_platform_windows
)

target_compile_features(mqb PRIVATE cxx_std_23)

foreach(target IN ITEMS mqb_cli mqb)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
