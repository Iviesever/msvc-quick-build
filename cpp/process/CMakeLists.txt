add_library(mqb_process INTERFACE)

target_include_directories(mqb_process
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mqb_process INTERFACE cxx_std_23)
