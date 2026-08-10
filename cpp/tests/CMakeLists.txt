add_executable(mqb_core_tests build_request_tests.cpp)
target_link_libraries(mqb_core_tests PRIVATE mqb_core)
target_compile_features(mqb_core_tests PRIVATE cxx_std_23)

add_executable(mqb_build_plan_tests build_plan_tests.cpp)
target_link_libraries(mqb_build_plan_tests PRIVATE mqb_core)
target_compile_features(mqb_build_plan_tests PRIVATE cxx_std_23)

add_executable(mqb_build_signature_tests build_signature_tests.cpp)
target_link_libraries(mqb_build_signature_tests PRIVATE mqb_core)
target_compile_features(mqb_build_signature_tests PRIVATE cxx_std_23)

add_executable(mqb_dependency_graph_tests dependency_graph_tests.cpp)
target_link_libraries(mqb_dependency_graph_tests PRIVATE mqb_core)
target_compile_features(mqb_dependency_graph_tests PRIVATE cxx_std_23)

add_executable(mqb_compile_cache_tests compile_cache_tests.cpp)
target_link_libraries(mqb_compile_cache_tests PRIVATE mqb_core)
target_compile_features(mqb_compile_cache_tests PRIVATE cxx_std_23)

add_executable(mqb_build_planner_tests build_planner_tests.cpp)
target_link_libraries(mqb_build_planner_tests PRIVATE mqb_core)
target_compile_features(mqb_build_planner_tests PRIVATE cxx_std_23)

add_executable(mqb_process_model_tests process_model_tests.cpp)
target_link_libraries(mqb_process_model_tests PRIVATE mqb_process)
target_compile_features(mqb_process_model_tests PRIVATE cxx_std_23)

set(MQB_TEST_TARGETS
    mqb_core_tests
    mqb_build_plan_tests
    mqb_build_signature_tests
    mqb_dependency_graph_tests
    mqb_compile_cache_tests
    mqb_build_planner_tests
    mqb_process_model_tests
)

if(WIN32)
    add_executable(mqb_windows_command_line_tests windows_command_line_tests.cpp)
    target_link_libraries(mqb_windows_command_line_tests PRIVATE mqb_platform_windows shell32)
    target_compile_features(mqb_windows_command_line_tests PRIVATE cxx_std_23)

    add_executable(mqb_process_echo_helper process_echo_helper.cpp)
    target_compile_features(mqb_process_echo_helper PRIVATE cxx_std_23)

    add_executable(mqb_windows_process_runner_tests windows_process_runner_tests.cpp)
    target_link_libraries(mqb_windows_process_runner_tests PRIVATE mqb_platform_windows)
    target_compile_features(mqb_windows_process_runner_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_portable_tests msvc_portable_tests.cpp)
    target_link_libraries(mqb_msvc_portable_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_msvc_portable_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_compiler_arguments_tests msvc_compiler_arguments_tests.cpp)
    target_link_libraries(mqb_msvc_compiler_arguments_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_msvc_compiler_arguments_tests PRIVATE cxx_std_23)

    list(APPEND MQB_TEST_TARGETS
        mqb_windows_command_line_tests
        mqb_process_echo_helper
        mqb_windows_process_runner_tests
        mqb_msvc_portable_tests
        mqb_msvc_compiler_arguments_tests
    )

    if(MQB_ENABLE_INSTALLED_MSVC_TESTS)
        add_executable(mqb_msvc_visual_studio_tests msvc_visual_studio_tests.cpp)
        target_link_libraries(mqb_msvc_visual_studio_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_msvc_visual_studio_tests PRIVATE cxx_std_23)

        add_executable(mqb_msvc_compile_integration_tests msvc_compile_integration_tests.cpp)
        target_link_libraries(mqb_msvc_compile_integration_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_msvc_compile_integration_tests PRIVATE cxx_std_23)

        list(APPEND MQB_TEST_TARGETS
            mqb_msvc_visual_studio_tests
            mqb_msvc_compile_integration_tests
        )
    endif()
endif()

foreach(test_target IN LISTS MQB_TEST_TARGETS)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(NAME mqb.core.build_request COMMAND mqb_core_tests)
add_test(NAME mqb.core.build_plan COMMAND mqb_build_plan_tests)
add_test(NAME mqb.core.build_signature COMMAND mqb_build_signature_tests)
add_test(NAME mqb.core.dependency_graph COMMAND mqb_dependency_graph_tests)
add_test(NAME mqb.core.compile_cache COMMAND mqb_compile_cache_tests)
add_test(NAME mqb.core.build_planner COMMAND mqb_build_planner_tests)
add_test(NAME mqb.process.model COMMAND mqb_process_model_tests)

if(WIN32)
    add_test(NAME mqb.windows.command_line COMMAND mqb_windows_command_line_tests)
    add_test(
        NAME mqb.windows.process_runner
        COMMAND mqb_windows_process_runner_tests $<TARGET_FILE:mqb_process_echo_helper>
    )
    add_test(NAME mqb.msvc.portable COMMAND mqb_msvc_portable_tests)
    add_test(NAME mqb.msvc.compiler_arguments COMMAND mqb_msvc_compiler_arguments_tests)

    if(MQB_ENABLE_INSTALLED_MSVC_TESTS)
        add_test(NAME mqb.msvc.visual_studio COMMAND mqb_msvc_visual_studio_tests)
        add_test(NAME mqb.msvc.compile_integration COMMAND mqb_msvc_compile_integration_tests)
    endif()
endif()
