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

find_program(PICOATE_WINDEPLOYQT
    NAMES windeployqt
    HINTS "${Qt6_DIR}/../../../bin"
    REQUIRED
)

set(PICOATE_CLI_PORTABLE_DIR "${CMAKE_BINARY_DIR}/portable/$<CONFIG>/PicoATE.Cli")

set(PICOATE_MSVC_RUNTIME_COPY_COMMAND)
if(MSVC AND CMAKE_GENERATOR_INSTANCE)
    file(GLOB PICOATE_MSVC_RUNTIME_DIRS LIST_DIRECTORIES true
        "${CMAKE_GENERATOR_INSTANCE}/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
    )
    list(SORT PICOATE_MSVC_RUNTIME_DIRS COMPARE NATURAL ORDER DESCENDING)
    if(PICOATE_MSVC_RUNTIME_DIRS)
        list(GET PICOATE_MSVC_RUNTIME_DIRS 0 PICOATE_MSVC_RUNTIME_DIR)
        file(GLOB PICOATE_MSVC_RUNTIME_DLLS "${PICOATE_MSVC_RUNTIME_DIR}/*.dll")
        if(PICOATE_MSVC_RUNTIME_DLLS)
            list(APPEND PICOATE_MSVC_RUNTIME_COPY_COMMAND
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        ${PICOATE_MSVC_RUNTIME_DLLS}
                        "${PICOATE_CLI_PORTABLE_DIR}/"
            )
        endif()
    endif()
endif()

add_custom_target(PicoATECliPortable
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PICOATE_CLI_PORTABLE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PicoATECli>" "${PICOATE_CLI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PicoATEMockHost>" "${PICOATE_CLI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PicoATEFakeInstrumentHost>" "${PICOATE_CLI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PicoATENativeHost>" "${PICOATE_CLI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PicoATETestDllModule>" "${PICOATE_CLI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PicoATECanExampleModule>" "${PICOATE_CLI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/examples" "${PICOATE_CLI_PORTABLE_DIR}/examples"
    ${PICOATE_MSVC_RUNTIME_COPY_COMMAND}
    COMMAND "${PICOATE_WINDEPLOYQT}"
            "--$<IF:$<CONFIG:Debug>,debug,release>"
            --no-translations
            --no-opengl-sw
            --dir "${PICOATE_CLI_PORTABLE_DIR}"
            "$<TARGET_FILE:PicoATECli>"
    DEPENDS PicoATECli PicoATEMockHost PicoATEFakeInstrumentHost PicoATENativeHost PicoATETestDllModule PicoATECanExampleModule
    COMMENT "Creating a self-contained PicoATE CLI folder"
    VERBATIM
)

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
    NAME PicoATECliScopedResultExample
    COMMAND PicoATECli run "${CMAKE_SOURCE_DIR}/examples/scoped_result_sequence.json" --uuts 2
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
