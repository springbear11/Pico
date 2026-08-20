add_library(PicoATERegisterImport STATIC
    RegisterConfigImporter.cpp
    RegisterConfigImporter.h
)

target_include_directories(PicoATERegisterImport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(PicoATERegisterImport
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::CorePrivate
)

target_compile_features(PicoATERegisterImport PUBLIC cxx_std_20)

set_target_properties(PicoATERegisterImport PROPERTIES
    FOLDER "Libraries/Tools"
)

add_executable(PicoATERegisterImporter
    Main.cpp
)

target_link_libraries(PicoATERegisterImporter
    PRIVATE
        PicoATERegisterImport
        Qt6::Core
)

target_compile_features(PicoATERegisterImporter PRIVATE cxx_std_20)

set_target_properties(PicoATERegisterImporter PROPERTIES
    OUTPUT_NAME "PicoATE.RegisterImporter"
    FOLDER "Applications/Tools"
)
