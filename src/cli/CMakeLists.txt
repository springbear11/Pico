add_executable(PicoATECli
    Main.cpp
)

set_target_properties(PicoATECli
    PROPERTIES
        OUTPUT_NAME "PicoATE.Cli"
)

target_link_libraries(PicoATECli
    PRIVATE
        PicoATECore
        Qt6::Core
)

target_compile_definitions(PicoATECli
    PRIVATE
        PICOATE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        PICOATE_MOCK_HOST_PATH="$<TARGET_FILE:PicoATEMockHost>"
        PICOATE_FAKE_INSTRUMENT_HOST_PATH="$<TARGET_FILE:PicoATEFakeInstrumentHost>"
        PICOATE_NATIVE_HOST_PATH="$<TARGET_FILE:PicoATENativeHost>"
        PICOATE_TEST_DLL_PATH="$<TARGET_FILE:PicoATETestDllModule>"
        PICOATE_CAN_DLL_PATH="$<TARGET_FILE:PicoATECanExampleModule>"
)

if(Python3_Interpreter_FOUND)
    target_compile_definitions(PicoATECli
        PRIVATE
            PICOATE_PYTHON_EXE="${Python3_EXECUTABLE}"
    )
endif()

add_dependencies(PicoATECli PicoATEMockHost PicoATEFakeInstrumentHost PicoATENativeHost PicoATETestDllModule PicoATECanExampleModule)

add_test(
    NAME PicoATECliSimpleExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/simple_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliBasicExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/basic_sequence.json" --uuts 2
)

add_test(
    NAME PicoATECliCustomDisabledExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/custom_disabled_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliForLoopExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/for_loop_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliTestItemExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/test_item_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliExternalEchoExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/external_echo_sequence.json" --uuts 1
)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME PicoATECliPythonEchoExample
        COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/python_echo_sequence.json" --uuts 1
    )
endif()

add_test(
    NAME PicoATECliNativeHostDllExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/nativehost_dll_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliCanDllExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/can_dll_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliStationConfigExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/simple_sequence.json" --uuts 1 --station "${CMAKE_SOURCE_DIR}/examples/stations/basic_station.json"
)

set_tests_properties(PicoATECliStationConfigExample
    PROPERTIES
        ENVIRONMENT "DMM1_ADDRESS=USB0::0x0957::0x0607::MY59001234::INSTR"
)

add_test(
    NAME PicoATECliPersistentInstrumentExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/persistent_instrument_sequence.json" --uuts 1
)

add_test(
    NAME PicoATECliDmmCanAdapterExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/dmm_can_adapter_sequence.json" --uuts 1 --station "${CMAKE_SOURCE_DIR}/examples/stations/basic_station.json"
)

set_tests_properties(PicoATECliDmmCanAdapterExample
    PROPERTIES
        ENVIRONMENT "DMM1_ADDRESS=USB0::0x0957::0x0607::MY59001234::INSTR"
)
