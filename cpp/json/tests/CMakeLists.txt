add_executable(mqb_json_tests json_tests.cpp)
target_link_libraries(mqb_json_tests PRIVATE mqb_json)
target_compile_features(mqb_json_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_json_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_json_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mqb.json.strict_parser COMMAND mqb_json_tests)
