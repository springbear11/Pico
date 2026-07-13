qt_add_executable(PicoATEUiTests
    ExecutionViewModelTests.cpp
)

target_link_libraries(PicoATEUiTests
    PRIVATE
        PicoATEUiRuntime
        Qt6::Core
        Qt6::Test
)

target_compile_definitions(PicoATEUiTests
    PRIVATE
        PICOATE_UI_TEST_PROJECT_DIR="${PICOATE_ROOT_DIR}"
)

if(TARGET PicoATEFakeInstrumentHost)
    add_dependencies(PicoATEUiTests PicoATEFakeInstrumentHost)
endif()
if(TARGET PicoATEMockHost)
    add_dependencies(PicoATEUiTests PicoATEMockHost)
endif()
if(TARGET PicoATENativeHost)
    add_dependencies(PicoATEUiTests PicoATENativeHost)
endif()
if(TARGET PicoATETestDllModule)
    add_dependencies(PicoATEUiTests PicoATETestDllModule)
endif()

set_target_properties(PicoATEUiTests
    PROPERTIES
        FOLDER "Tests/UI"
)

add_test(NAME PicoATEUiTests COMMAND PicoATEUiTests)

qt_add_executable(PicoATEUiWindowTests
    ../src/LoginDialog.cpp
    ../src/LoginDialog.h
    ../src/ProductionWindow.cpp
    ../src/ProductionWindow.h
    ../src/ProportionalHeaderView.cpp
    ../src/ProportionalHeaderView.h
    ../src/OnOffControl.cpp
    ../src/OnOffControl.h
    ../src/ScanDialog.cpp
    ../src/ScanDialog.h
    MainWindowLifecycleTests.cpp
    ../src/MainWindow.cpp
    ../src/MainWindow.h
    ../src/StepPropertyEditor.cpp
    ../src/StepPropertyEditor.h
    ../src/StationPropertyEditor.cpp
    ../src/StationPropertyEditor.h
    ../src/StationSettingsEditor.cpp
    ../src/StationSettingsEditor.h
)

target_link_libraries(PicoATEUiWindowTests
    PRIVATE
        PicoATEUiRuntime
        Qt6::Core
        Qt6::Test
        Qt6::Widgets
)

target_compile_definitions(PicoATEUiWindowTests
    PRIVATE
        PICOATE_UI_TEST_PROJECT_DIR="${PICOATE_ROOT_DIR}"
)

set_target_properties(PicoATEUiWindowTests
    PROPERTIES
        FOLDER "Tests/UI"
)

add_test(NAME PicoATEUiWindowTests COMMAND PicoATEUiWindowTests)

if(TARGET PicoATEFakeInstrumentHost)
    add_dependencies(PicoATEUiWindowTests PicoATEFakeInstrumentHost)
endif()
