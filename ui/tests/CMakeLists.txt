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
    ../src/AdminStartupSplash.cpp
    ../src/AdminStartupSplash.h
    ../src/FieldDeviceDialog.cpp
    ../src/FieldDeviceDialog.h
    ../src/FlowTargetSelector.cpp
    ../src/FlowTargetSelector.h
    ../src/InputWheelGuard.cpp
    ../src/InputWheelGuard.h
    ../src/LoadingSpinner.cpp
    ../src/LoadingSpinner.h
    ../src/LoginDialog.cpp
    ../src/LoginDialog.h
    ../src/PicoATE.qrc
    ../src/PicoATEStyle.cpp
    ../src/PicoATEStyle.h
    ../src/ProductionWindow.cpp
    ../src/ProductionWindow.h
    ../src/ProductRoutingDialog.cpp
    ../src/ProductRoutingDialog.h
    ../src/ProportionalHeaderView.cpp
    ../src/ProportionalHeaderView.h
    ../src/OnOffControl.cpp
    ../src/OnOffControl.h
    ../src/OperatorPromptPresenter.cpp
    ../src/OperatorPromptPresenter.h
    ../src/ParserActualDelegate.cpp
    ../src/ParserActualDelegate.h
    ../src/ScanDialog.cpp
    ../src/ScanDialog.h
    ../src/UutSlotConfigurationDialog.cpp
    ../src/UutSlotConfigurationDialog.h
    ../src/SequenceEditorTreeView.cpp
    ../src/SequenceEditorTreeView.h
    ../src/SequenceVariablesDialog.cpp
    ../src/SequenceVariablesDialog.h
    MainWindowLifecycleTests.cpp
    ../src/MainWindow.cpp
    ../src/MainWindow.h
    ../src/MultiUutOverviewWidget.cpp
    ../src/MultiUutOverviewWidget.h
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
        AUTORCC ON
        FOLDER "Tests/UI"
)

add_test(NAME PicoATEUiWindowTests COMMAND PicoATEUiWindowTests)

if(TARGET PicoATEFakeInstrumentHost)
    add_dependencies(PicoATEUiWindowTests PicoATEFakeInstrumentHost)
endif()
