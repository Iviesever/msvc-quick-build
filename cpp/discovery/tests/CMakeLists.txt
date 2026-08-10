add_executable(mqb_source_discovery_tests
    source_discovery_tests.cpp
)
target_link_libraries(mqb_source_discovery_tests PRIVATE mqb_discovery)
target_compile_features(mqb_source_discovery_tests PRIVATE cxx_std_23)

add_executable(mqb_source_discovery_corrections_tests
    source_discovery_corrections_tests.cpp
)
target_link_libraries(mqb_source_discovery_corrections_tests PRIVATE mqb_discovery)
target_compile_features(mqb_source_discovery_corrections_tests PRIVATE cxx_std_23)

add_executable(mqb_module_syntax_tests
    module_syntax_tests.cpp
)
target_link_libraries(mqb_module_syntax_tests PRIVATE mqb_discovery)
target_compile_features(mqb_module_syntax_tests PRIVATE cxx_std_23)

foreach(target IN ITEMS
    mqb_source_discovery_tests
    mqb_source_discovery_corrections_tests
    mqb_module_syntax_tests
)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(NAME mqb.discovery.source_graph COMMAND mqb_source_discovery_tests)
add_test(NAME mqb.discovery.corrections COMMAND mqb_source_discovery_corrections_tests)
add_test(NAME mqb.discovery.module_syntax COMMAND mqb_module_syntax_tests)
