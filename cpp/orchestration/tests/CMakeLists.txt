add_executable(mqb_orchestration_incremental_tests
    incremental_compile_coordinator_tests.cpp
)

target_link_libraries(mqb_orchestration_incremental_tests
    PRIVATE
        mqb_orchestration
        mqb_msvc
)

target_compile_features(mqb_orchestration_incremental_tests PRIVATE cxx_std_23)

add_executable(mqb_orchestration_incremental_link_tests
    incremental_link_coordinator_tests.cpp
)

target_link_libraries(mqb_orchestration_incremental_link_tests
    PRIVATE
        mqb_orchestration
        mqb_msvc
)

target_compile_features(mqb_orchestration_incremental_link_tests PRIVATE cxx_std_23)

add_executable(mqb_bounded_work_scheduler_tests
    bounded_work_scheduler_tests.cpp
)

target_link_libraries(mqb_bounded_work_scheduler_tests PRIVATE mqb_orchestration)
target_compile_features(mqb_bounded_work_scheduler_tests PRIVATE cxx_std_23)

foreach(test_target IN ITEMS
    mqb_orchestration_incremental_tests
    mqb_orchestration_incremental_link_tests
    mqb_bounded_work_scheduler_tests
)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(
    NAME mqb.orchestration.incremental_compile
    COMMAND mqb_orchestration_incremental_tests
)

add_test(
    NAME mqb.orchestration.incremental_link
    COMMAND mqb_orchestration_incremental_link_tests
)

add_test(
    NAME mqb.orchestration.bounded_work_scheduler
    COMMAND mqb_bounded_work_scheduler_tests
)
