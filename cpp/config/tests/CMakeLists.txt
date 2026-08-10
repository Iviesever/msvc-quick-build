add_executable(mqb_project_config_tests
    project_config_tests.cpp
)

target_link_libraries(mqb_project_config_tests PRIVATE mqb_config)
target_compile_features(mqb_project_config_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_project_config_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_project_config_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mqb.config.project_config COMMAND mqb_project_config_tests)
