add_executable(mqb_source_discovery_tests
    source_discovery_tests.cpp
)

target_link_libraries(mqb_source_discovery_tests PRIVATE mqb_discovery)
target_compile_features(mqb_source_discovery_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_source_discovery_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_source_discovery_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mqb.discovery.source_graph COMMAND mqb_source_discovery_tests)
