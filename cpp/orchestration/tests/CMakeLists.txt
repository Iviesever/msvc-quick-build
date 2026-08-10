add_executable(mqb_orchestration_incremental_tests
    incremental_compile_coordinator_tests.cpp
)

target_link_libraries(mqb_orchestration_incremental_tests
    PRIVATE
        mqb_orchestration
        mqb_msvc
)

target_compile_features(mqb_orchestration_incremental_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_orchestration_incremental_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_orchestration_incremental_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME mqb.orchestration.incremental_compile
    COMMAND mqb_orchestration_incremental_tests
)
