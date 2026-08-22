add_executable(PicoATECoreTests
    CoreTests.cpp
)

target_link_libraries(PicoATECoreTests
    PRIVATE
        PicoATECore
        Qt6::Test
)

target_compile_definitions(PicoATECoreTests
    PRIVATE
        PICOATE_MOCK_HOST_PATH="$<TARGET_FILE:PicoATEMockHost>"
        PICOATE_FAKE_INSTRUMENT_HOST_PATH="$<TARGET_FILE:PicoATEFakeInstrumentHost>"
        PICOATE_NATIVE_HOST_PATH="$<TARGET_FILE:PicoATENativeHost>"
        PICOATE_TEST_DLL_PATH="$<TARGET_FILE:PicoATETestDllModule>"
        PICOATE_CAN_DLL_PATH="$<TARGET_FILE:PicoATECanExampleModule>"
)

if(Python3_Interpreter_FOUND)
    target_compile_definitions(PicoATECoreTests
        PRIVATE
            PICOATE_PYTHON_EXE="${Python3_EXECUTABLE}"
    )
endif()

add_dependencies(PicoATECoreTests PicoATEMockHost PicoATEFakeInstrumentHost PicoATENativeHost PicoATETestDllModule PicoATECanExampleModule)

add_test(NAME PicoATECoreTests COMMAND PicoATECoreTests)

if(WIN32)
    add_executable(PicoATESerialPortDiscoveryTests
        SerialPortDiscoveryTests.cpp
        ${PROJECT_SOURCE_DIR}/src/core/src/WindowsSerialPortDiscovery.cpp
    )

    target_include_directories(PicoATESerialPortDiscoveryTests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/core/src
    )

    target_link_libraries(PicoATESerialPortDiscoveryTests
        PRIVATE
            Qt6::Core
            Qt6::Test
            Advapi32
    )

    target_compile_features(PicoATESerialPortDiscoveryTests PRIVATE cxx_std_20)
    set_target_properties(PicoATESerialPortDiscoveryTests PROPERTIES AUTOMOC ON)
    add_test(NAME PicoATESerialPortDiscoveryTests COMMAND PicoATESerialPortDiscoveryTests)
endif()

add_executable(PicoATERegisterConfigImporterTests
    RegisterConfigImporterTests.cpp
    RegisterConfigImporterTests.h
)

target_link_libraries(PicoATERegisterConfigImporterTests
    PRIVATE
        PicoATERegisterImport
        PicoATECore
        Qt6::Core
        Qt6::CorePrivate
        Qt6::Test
)

target_compile_features(PicoATERegisterConfigImporterTests PRIVATE cxx_std_20)

set_target_properties(PicoATERegisterConfigImporterTests PROPERTIES
    AUTOMOC ON
    FOLDER "Tests/Tools"
)

add_test(NAME PicoATERegisterConfigImporterTests
         COMMAND PicoATERegisterConfigImporterTests)
