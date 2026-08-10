add_executable(mqb_core_tests
    build_request_tests.cpp
)

target_link_libraries(mqb_core_tests PRIVATE mqb_core)
target_compile_features(mqb_core_tests PRIVATE cxx_std_23)

add_executable(mqb_build_plan_tests
    build_plan_tests.cpp
)

target_link_libraries(mqb_build_plan_tests PRIVATE mqb_core)
target_compile_features(mqb_build_plan_tests PRIVATE cxx_std_23)

add_executable(mqb_build_signature_tests
    build_signature_tests.cpp
)

target_link_libraries(mqb_build_signature_tests PRIVATE mqb_core)
target_compile_features(mqb_build_signature_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_core_tests PRIVATE /W4 /permissive-)
    target_compile_options(mqb_build_plan_tests PRIVATE /W4 /permissive-)
    target_compile_options(mqb_build_signature_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mqb_build_plan_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mqb_build_signature_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mqb.core.build_request COMMAND mqb_core_tests)
add_test(NAME mqb.core.build_plan COMMAND mqb_build_plan_tests)
add_test(NAME mqb.core.build_signature COMMAND mqb_build_signature_tests)
