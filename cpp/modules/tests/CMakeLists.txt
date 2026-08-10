add_executable(mqb_p1689_tests p1689_tests.cpp)
target_link_libraries(mqb_p1689_tests PRIVATE mqb_modules)
target_compile_features(mqb_p1689_tests PRIVATE cxx_std_23)

add_executable(mqb_module_dependency_graph_tests module_dependency_graph_tests.cpp)
target_link_libraries(mqb_module_dependency_graph_tests PRIVATE mqb_modules)
target_compile_features(mqb_module_dependency_graph_tests PRIVATE cxx_std_23)

add_executable(mqb_module_dependency_resolution_tests module_dependency_resolution_tests.cpp)
target_link_libraries(mqb_module_dependency_resolution_tests PRIVATE mqb_modules)
target_compile_features(mqb_module_dependency_resolution_tests PRIVATE cxx_std_23)

foreach(test_target IN ITEMS
    mqb_p1689_tests
    mqb_module_dependency_graph_tests
    mqb_module_dependency_resolution_tests
)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

add_test(NAME mqb.modules.p1689_parser COMMAND mqb_p1689_tests)
add_test(NAME mqb.modules.dependency_graph COMMAND mqb_module_dependency_graph_tests)
add_test(NAME mqb.modules.dependency_resolution COMMAND mqb_module_dependency_resolution_tests)
