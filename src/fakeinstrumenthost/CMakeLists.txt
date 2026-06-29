add_executable(PicoATEFakeInstrumentHost
    Main.cpp
)

set_target_properties(PicoATEFakeInstrumentHost
    PROPERTIES
        OUTPUT_NAME "PicoATE.FakeInstrumentHost"
)

target_link_libraries(PicoATEFakeInstrumentHost
    PRIVATE
        Qt6::Core
)

target_compile_features(PicoATEFakeInstrumentHost PRIVATE cxx_std_20)
