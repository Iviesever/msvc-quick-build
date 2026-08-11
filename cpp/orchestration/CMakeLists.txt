add_library(mqb_orchestration STATIC
    src/BoundedWorkScheduler.cpp
    src/MsvcIncrementalArchiveCoordinator.cpp
    src/MsvcIncrementalCompileCoordinator.cpp
    src/MsvcIncrementalLinkCoordinator.cpp
    src/MsvcIncrementalStaticTargetCoordinator.cpp
    src/MsvcIncrementalTargetCoordinator.cpp
    src/MsvcModuleCompileCoordinator.cpp
    src/MsvcModuleTargetCoordinator.cpp
    src/MsvcTargetRouter.cpp
)

target_include_directories(mqb_orchestration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mqb_orchestration
    PUBLIC
        mqb_core
        mqb_modules
        mqb_msvc
        mqb_process
)

target_compile_features(mqb_orchestration PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(mqb_orchestration PRIVATE /W4 /permissive-)
else()
    target_compile_options(mqb_orchestration PRIVATE -Wall -Wextra -Wpedantic)
endif()
