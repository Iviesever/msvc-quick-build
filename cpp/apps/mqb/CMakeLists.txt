add_executable(mqb
    main.cpp
)

target_link_libraries(mqb PRIVATE mqb_core)
target_compile_features(mqb PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(mqb PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb PRIVATE -Wall -Wextra -Wpedantic)
endif()
