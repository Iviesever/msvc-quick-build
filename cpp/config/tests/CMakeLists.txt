add_executable(mqb_project_config_tests
    project_config_tests.cpp
)
target_link_libraries(mqb_project_config_tests PRIVATE mqb_config)
target_compile_features(mqb_project_config_tests PRIVATE cxx_std_23)

add_executable(mqb_project_options_tests
    project_options_tests.cpp
)
target_link_libraries(mqb_project_options_tests PRIVATE mqb_config)
target_compile_features(mqb_project_options_tests PRIVATE cxx_std_23)

foreach(target IN ITEMS mqb_project_config_tests mqb_project_options_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(NAME mqb.config.project_config COMMAND mqb_project_config_tests)
add_test(NAME mqb.config.project_options COMMAND mqb_project_options_tests)
