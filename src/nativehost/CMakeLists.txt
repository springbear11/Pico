add_executable(PicoATENativeHost
    Main.cpp
    NativeHostOutputPump.cpp
    NativeHostOutputPump.h
)

set_target_properties(PicoATENativeHost
    PROPERTIES
        OUTPUT_NAME "PicoATE.NativeHost"
)

target_link_libraries(PicoATENativeHost
    PRIVATE
        PicoATECore
        Qt6::Core
)

target_compile_features(PicoATENativeHost PRIVATE cxx_std_20)
