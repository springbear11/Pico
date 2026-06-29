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
