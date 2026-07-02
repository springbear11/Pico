add_library(PicoATETestDllModule SHARED
    TestDllModule.cpp
)

set_target_properties(PicoATETestDllModule
    PROPERTIES
        OUTPUT_NAME "PicoATE.TestDllModule"
)

target_link_libraries(PicoATETestDllModule
    PRIVATE
        Qt6::Core
)

target_include_directories(PicoATETestDllModule
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/core/include
)

target_compile_features(PicoATETestDllModule PRIVATE cxx_std_20)
