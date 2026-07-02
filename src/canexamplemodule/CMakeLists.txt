add_library(PicoATECanExampleModule SHARED
    CanExampleModule.cpp
)

set_target_properties(PicoATECanExampleModule
    PROPERTIES
        OUTPUT_NAME "PicoATE.CanExampleModule"
)

target_link_libraries(PicoATECanExampleModule
    PRIVATE
        Qt6::Core
)

target_include_directories(PicoATECanExampleModule
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/core/include
)

target_compile_features(PicoATECanExampleModule PRIVATE cxx_std_20)
