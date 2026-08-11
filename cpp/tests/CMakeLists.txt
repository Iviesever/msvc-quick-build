add_executable(mqb_core_tests build_request_tests.cpp)
target_link_libraries(mqb_core_tests PRIVATE mqb_core)
target_compile_features(mqb_core_tests PRIVATE cxx_std_23)

add_executable(mqb_build_plan_tests build_plan_tests.cpp)
target_link_libraries(mqb_build_plan_tests PRIVATE mqb_core)
target_compile_features(mqb_build_plan_tests PRIVATE cxx_std_23)

add_executable(mqb_build_signature_tests build_signature_tests.cpp)
target_link_libraries(mqb_build_signature_tests PRIVATE mqb_core)
target_compile_features(mqb_build_signature_tests PRIVATE cxx_std_23)

add_executable(mqb_c_compile_signature_tests c_compile_signature_tests.cpp)
target_link_libraries(mqb_c_compile_signature_tests PRIVATE mqb_core)
target_compile_features(mqb_c_compile_signature_tests PRIVATE cxx_std_23)

add_executable(mqb_dependency_graph_tests dependency_graph_tests.cpp)
target_link_libraries(mqb_dependency_graph_tests PRIVATE mqb_core)
target_compile_features(mqb_dependency_graph_tests PRIVATE cxx_std_23)

add_executable(mqb_compile_cache_tests compile_cache_tests.cpp)
target_link_libraries(mqb_compile_cache_tests PRIVATE mqb_core)
target_compile_features(mqb_compile_cache_tests PRIVATE cxx_std_23)

add_executable(mqb_compile_cache_file_tests compile_cache_file_tests.cpp)
target_link_libraries(mqb_compile_cache_file_tests PRIVATE mqb_core)
target_compile_features(mqb_compile_cache_file_tests PRIVATE cxx_std_23)

add_executable(mqb_build_planner_tests build_planner_tests.cpp)
target_link_libraries(mqb_build_planner_tests PRIVATE mqb_core)
target_compile_features(mqb_build_planner_tests PRIVATE cxx_std_23)

add_executable(mqb_link_state_tests link_state_tests.cpp)
target_link_libraries(mqb_link_state_tests PRIVATE mqb_core)
target_compile_features(mqb_link_state_tests PRIVATE cxx_std_23)

add_executable(mqb_link_cache_file_tests link_cache_file_tests.cpp)
target_link_libraries(mqb_link_cache_file_tests PRIVATE mqb_core)
target_compile_features(mqb_link_cache_file_tests PRIVATE cxx_std_23)

add_executable(mqb_project_artifact_layout_tests project_artifact_layout_tests.cpp)
target_link_libraries(mqb_project_artifact_layout_tests PRIVATE mqb_core)
target_compile_features(mqb_project_artifact_layout_tests PRIVATE cxx_std_23)

add_executable(mqb_translation_unit_classifier_tests translation_unit_classifier_tests.cpp)
target_link_libraries(mqb_translation_unit_classifier_tests PRIVATE mqb_core)
target_compile_features(mqb_translation_unit_classifier_tests PRIVATE cxx_std_23)

add_executable(mqb_process_model_tests process_model_tests.cpp)
target_link_libraries(mqb_process_model_tests PRIVATE mqb_process)
target_compile_features(mqb_process_model_tests PRIVATE cxx_std_23)

add_executable(mqb_build_policy_config_tests ../config/tests/build_policy_config_tests.cpp)
target_link_libraries(mqb_build_policy_config_tests PRIVATE mqb_config)
target_compile_features(mqb_build_policy_config_tests PRIVATE cxx_std_23)

set(MQB_TEST_TARGETS
    mqb_core_tests
    mqb_build_plan_tests
    mqb_build_signature_tests
    mqb_c_compile_signature_tests
    mqb_dependency_graph_tests
    mqb_compile_cache_tests
    mqb_compile_cache_file_tests
    mqb_build_planner_tests
    mqb_link_state_tests
    mqb_link_cache_file_tests
    mqb_project_artifact_layout_tests
    mqb_translation_unit_classifier_tests
    mqb_process_model_tests
    mqb_build_policy_config_tests
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

    add_executable(mqb_c_source_recipe_tests c_source_recipe_tests.cpp)
    target_link_libraries(mqb_c_source_recipe_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_c_source_recipe_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_linker_arguments_tests msvc_linker_arguments_tests.cpp)
    target_link_libraries(mqb_msvc_linker_arguments_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_msvc_linker_arguments_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_library_resolver_tests msvc_library_resolver_tests.cpp)
    target_link_libraries(mqb_msvc_library_resolver_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_msvc_library_resolver_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_compile_executor_tests msvc_compile_executor_tests.cpp)
    target_link_libraries(mqb_msvc_compile_executor_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_msvc_compile_executor_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_source_dependencies_tests msvc_source_dependencies_tests.cpp)
    target_link_libraries(mqb_msvc_source_dependencies_tests PRIVATE mqb_msvc)
    target_compile_features(mqb_msvc_source_dependencies_tests PRIVATE cxx_std_23)

    add_executable(mqb_cli_argument_tests cli_argument_tests.cpp)
    target_link_libraries(mqb_cli_argument_tests PRIVATE mqb_cli)
    target_compile_features(mqb_cli_argument_tests PRIVATE cxx_std_23)

    add_executable(mqb_build_policy_cli_tests build_policy_cli_tests.cpp)
    target_link_libraries(mqb_build_policy_cli_tests PRIVATE mqb_cli)
    target_compile_features(mqb_build_policy_cli_tests PRIVATE cxx_std_23)

    list(APPEND MQB_TEST_TARGETS
        mqb_windows_command_line_tests
        mqb_process_echo_helper
        mqb_windows_process_runner_tests
        mqb_msvc_portable_tests
        mqb_msvc_compiler_arguments_tests
        mqb_c_source_recipe_tests
        mqb_msvc_linker_arguments_tests
        mqb_msvc_library_resolver_tests
        mqb_msvc_compile_executor_tests
        mqb_msvc_source_dependencies_tests
        mqb_cli_argument_tests
        mqb_build_policy_cli_tests
    )

    if(MQB_ENABLE_INSTALLED_MSVC_TESTS)
        add_executable(mqb_msvc_visual_studio_tests msvc_visual_studio_tests.cpp)
        target_link_libraries(mqb_msvc_visual_studio_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_msvc_visual_studio_tests PRIVATE cxx_std_23)

        add_executable(mqb_msvc_compile_integration_tests msvc_compile_integration_tests.cpp)
        target_link_libraries(mqb_msvc_compile_integration_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_msvc_compile_integration_tests PRIVATE cxx_std_23)

        add_executable(mqb_msvc_link_integration_tests msvc_link_integration_tests.cpp)
        target_link_libraries(mqb_msvc_link_integration_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_msvc_link_integration_tests PRIVATE cxx_std_23)

        add_executable(mqb_msvc_incremental_loop_tests msvc_incremental_loop_tests.cpp)
        target_link_libraries(mqb_msvc_incremental_loop_tests PRIVATE mqb_core mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_msvc_incremental_loop_tests PRIVATE cxx_std_23)

        add_executable(mqb_cli_e2e_tests mqb_cli_e2e_tests.cpp)
        target_link_libraries(mqb_cli_e2e_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_cli_e2e_tests PRIVATE cxx_std_23)

        add_executable(mqb_project_config_e2e_tests mqb_project_config_e2e_tests.cpp)
        target_link_libraries(mqb_project_config_e2e_tests PRIVATE mqb_msvc mqb_platform_windows)
        target_compile_features(mqb_project_config_e2e_tests PRIVATE cxx_std_23)

        add_executable(mqb_parallel_cli_e2e_tests mqb_parallel_cli_e2e_tests.cpp)
        target_link_libraries(mqb_parallel_cli_e2e_tests PRIVATE mqb_platform_windows)
        target_compile_features(mqb_parallel_cli_e2e_tests PRIVATE cxx_std_23)

        add_executable(mqb_module_cli_e2e_tests mqb_module_cli_e2e_tests.cpp)
        target_link_libraries(mqb_module_cli_e2e_tests PRIVATE mqb_platform_windows)
        target_compile_features(mqb_module_cli_e2e_tests PRIVATE cxx_std_23)

        add_executable(mqb_build_policy_e2e_tests mqb_build_policy_e2e_tests.cpp)
        target_link_libraries(mqb_build_policy_e2e_tests PRIVATE mqb_platform_windows)
        target_compile_features(mqb_build_policy_e2e_tests PRIVATE cxx_std_23)

        add_executable(mqb_c_source_e2e_tests mqb_c_source_e2e_tests.cpp)
        target_link_libraries(mqb_c_source_e2e_tests PRIVATE mqb_platform_windows)
        target_compile_features(mqb_c_source_e2e_tests PRIVATE cxx_std_23)

        add_executable(mqb_runtime_subsystem_config_e2e_tests mqb_runtime_subsystem_config_e2e_tests.cpp)
        target_link_libraries(mqb_runtime_subsystem_config_e2e_tests PRIVATE mqb_platform_windows)
        target_compile_features(mqb_runtime_subsystem_config_e2e_tests PRIVATE cxx_std_23)

        list(APPEND MQB_TEST_TARGETS
            mqb_msvc_visual_studio_tests
            mqb_msvc_compile_integration_tests
            mqb_msvc_link_integration_tests
            mqb_msvc_incremental_loop_tests
            mqb_cli_e2e_tests
            mqb_project_config_e2e_tests
            mqb_parallel_cli_e2e_tests
            mqb_module_cli_e2e_tests
            mqb_build_policy_e2e_tests
            mqb_c_source_e2e_tests
            mqb_runtime_subsystem_config_e2e_tests
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
add_test(NAME mqb.core.c_compile_signature COMMAND mqb_c_compile_signature_tests)
add_test(NAME mqb.core.dependency_graph COMMAND mqb_dependency_graph_tests)
add_test(NAME mqb.core.compile_cache COMMAND mqb_compile_cache_tests)
add_test(NAME mqb.core.compile_cache_file COMMAND mqb_compile_cache_file_tests)
add_test(NAME mqb.core.build_planner COMMAND mqb_build_planner_tests)
add_test(NAME mqb.core.link_state COMMAND mqb_link_state_tests)
add_test(NAME mqb.core.link_cache_file COMMAND mqb_link_cache_file_tests)
add_test(NAME mqb.core.project_artifact_layout COMMAND mqb_project_artifact_layout_tests)
add_test(NAME mqb.core.translation_unit_classifier COMMAND mqb_translation_unit_classifier_tests)
add_test(NAME mqb.process.model COMMAND mqb_process_model_tests)
add_test(NAME mqb.config.build_policy COMMAND mqb_build_policy_config_tests)

if(WIN32)
    add_test(NAME mqb.windows.command_line COMMAND mqb_windows_command_line_tests)
    add_test(
        NAME mqb.windows.process_runner
        COMMAND mqb_windows_process_runner_tests $<TARGET_FILE:mqb_process_echo_helper>
    )
    add_test(NAME mqb.msvc.portable COMMAND mqb_msvc_portable_tests)
    add_test(NAME mqb.msvc.compiler_arguments COMMAND mqb_msvc_compiler_arguments_tests)
    add_test(NAME mqb.msvc.c_source_recipe COMMAND mqb_c_source_recipe_tests)
    add_test(NAME mqb.msvc.linker_arguments COMMAND mqb_msvc_linker_arguments_tests)
    add_test(NAME mqb.msvc.library_resolver COMMAND mqb_msvc_library_resolver_tests)
    add_test(NAME mqb.msvc.compile_executor COMMAND mqb_msvc_compile_executor_tests)
    add_test(NAME mqb.msvc.source_dependencies COMMAND mqb_msvc_source_dependencies_tests)
    add_test(NAME mqb.cli.arguments COMMAND mqb_cli_argument_tests)
    add_test(NAME mqb.cli.build_policy COMMAND mqb_build_policy_cli_tests)

    if(MQB_ENABLE_INSTALLED_MSVC_TESTS)
        add_test(NAME mqb.msvc.visual_studio COMMAND mqb_msvc_visual_studio_tests)
        add_test(NAME mqb.msvc.compile_integration COMMAND mqb_msvc_compile_integration_tests)
        add_test(NAME mqb.msvc.link_integration COMMAND mqb_msvc_link_integration_tests)
        add_test(NAME mqb.msvc.incremental_loop COMMAND mqb_msvc_incremental_loop_tests)
        add_test(
            NAME mqb.cli.target_e2e
            COMMAND mqb_cli_e2e_tests $<TARGET_FILE:mqb>
        )
        add_test(
            NAME mqb.cli.project_config_e2e
            COMMAND mqb_project_config_e2e_tests $<TARGET_FILE:mqb>
        )
        add_test(
            NAME mqb.cli.parallel_e2e
            COMMAND mqb_parallel_cli_e2e_tests $<TARGET_FILE:mqb>
        )
        add_test(
            NAME mqb.cli.module_e2e
            COMMAND mqb_module_cli_e2e_tests $<TARGET_FILE:mqb>
        )
        add_test(
            NAME mqb.cli.build_policy_e2e
            COMMAND mqb_build_policy_e2e_tests $<TARGET_FILE:mqb>
        )
        add_test(
            NAME mqb.cli.c_source_e2e
            COMMAND mqb_c_source_e2e_tests $<TARGET_FILE:mqb>
        )
        add_test(
            NAME mqb.cli.runtime_subsystem_config_e2e
            COMMAND mqb_runtime_subsystem_config_e2e_tests $<TARGET_FILE:mqb>
        )
    endif()
endif()
