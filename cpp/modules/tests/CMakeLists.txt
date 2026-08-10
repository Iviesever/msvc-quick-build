add_executable(mqb_p1689_tests p1689_tests.cpp)
target_link_libraries(mqb_p1689_tests PRIVATE mqb_modules)
target_compile_features(mqb_p1689_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb_p1689_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_p1689_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mqb.modules.p1689_parser COMMAND mqb_p1689_tests)
