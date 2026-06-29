add_executable(PicoATEMockHost
    Main.cpp
)

set_target_properties(PicoATEMockHost
    PROPERTIES
        OUTPUT_NAME "PicoATE.MockHost"
)

target_link_libraries(PicoATEMockHost
    PRIVATE
        Qt6::Core
)

target_compile_features(PicoATEMockHost PRIVATE cxx_std_20)
