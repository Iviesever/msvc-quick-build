add_executable(mqb_msvc_module_scanner_tests
    module_dependency_scanner_tests.cpp
)
target_link_libraries(mqb_msvc_module_scanner_tests PRIVATE mqb_msvc)
target_compile_features(mqb_msvc_module_scanner_tests PRIVATE cxx_std_23)

add_executable(mqb_msvc_header_unit_compiler_tests
    header_unit_compiler_tests.cpp
)
target_link_libraries(mqb_msvc_header_unit_compiler_tests PRIVATE mqb_msvc)
target_compile_features(mqb_msvc_header_unit_compiler_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_msvc_module_scanner_tests PRIVATE /W4 /permissive-)
    target_compile_options(mqb_msvc_header_unit_compiler_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_msvc_module_scanner_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mqb_msvc_header_unit_compiler_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mqb.msvc.module_scanner COMMAND mqb_msvc_module_scanner_tests)
add_test(NAME mqb.msvc.header_unit_compiler COMMAND mqb_msvc_header_unit_compiler_tests)

if(MQB_ENABLE_INSTALLED_MSVC_TESTS)
    add_executable(mqb_msvc_module_scanner_integration_tests
        module_dependency_scanner_integration_tests.cpp
    )
    target_link_libraries(
        mqb_msvc_module_scanner_integration_tests
        PRIVATE
            mqb_msvc
            mqb_platform_windows
    )
    target_compile_features(mqb_msvc_module_scanner_integration_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_module_compile_integration_tests
        module_compile_integration_tests.cpp
    )
    target_link_libraries(
        mqb_msvc_module_compile_integration_tests
        PRIVATE
            mqb_msvc
            mqb_platform_windows
    )
    target_compile_features(mqb_msvc_module_compile_integration_tests PRIVATE cxx_std_23)

    add_executable(mqb_msvc_header_unit_compile_integration_tests
        header_unit_compile_integration_tests.cpp
    )
    target_link_libraries(
        mqb_msvc_header_unit_compile_integration_tests
        PRIVATE
            mqb_msvc
            mqb_platform_windows
    )
    target_compile_features(mqb_msvc_header_unit_compile_integration_tests PRIVATE cxx_std_23)

    if(MSVC)
        target_compile_options(mqb_msvc_module_scanner_integration_tests PRIVATE /W4 /permissive-)
        target_compile_options(mqb_msvc_module_compile_integration_tests PRIVATE /W4 /permissive-)
        target_compile_options(mqb_msvc_header_unit_compile_integration_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(mqb_msvc_module_scanner_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(mqb_msvc_module_compile_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(mqb_msvc_header_unit_compile_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(
        NAME mqb.msvc.module_scanner_integration
        COMMAND mqb_msvc_module_scanner_integration_tests
    )
    add_test(
        NAME mqb.msvc.module_compile_integration
        COMMAND mqb_msvc_module_compile_integration_tests
    )
    add_test(
        NAME mqb.msvc.header_unit_compile_integration
        COMMAND mqb_msvc_header_unit_compile_integration_tests
    )
endif()
