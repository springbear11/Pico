add_library(PicoATEUiRuntime STATIC
    BufferedRuntimeEventSink.cpp
    BufferedRuntimeEventSink.h
    CoreExecutionService.cpp
    CoreExecutionService.h
    ExecutionViewModel.cpp
    ExecutionViewModel.h
    ExecutionWorker.cpp
    ExecutionWorker.h
    IExecutionService.h
    PluginCatalog.cpp
    PluginCatalog.h
    PluginFunctionModel.cpp
    PluginFunctionModel.h
    ReportExporter.cpp
    ReportExporter.h
    ReportHistoryStore.cpp
    ReportHistoryStore.h
    RunArtifactWriter.cpp
    RunArtifactWriter.h
    RunnerModels.cpp
    RunnerModels.h
    SequenceDocument.cpp
    SequenceDocument.h
    SequenceTreeModel.cpp
    SequenceTreeModel.h
    SimpleXlsxWriter.cpp
    SimpleXlsxWriter.h
    StationDeviceModel.cpp
    StationDeviceModel.h
    StationDocument.cpp
    StationDocument.h
    StepOutputCatalog.cpp
    StepOutputCatalog.h
    StartupSupport.cpp
    StartupSupport.h
    UiExecutionTypes.cpp
    UiExecutionTypes.h
)

target_include_directories(PicoATEUiRuntime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(PicoATEUiRuntime
    PUBLIC
        PicoATECore
        Qt6::Core
        Qt6::Gui
)

target_compile_definitions(PicoATEUiRuntime
    PRIVATE
        PICOATE_PROJECT_DIR="${PICOATE_ROOT_DIR}"
)

target_compile_features(PicoATEUiRuntime PUBLIC cxx_std_20)

set_target_properties(PicoATEUiRuntime
    PROPERTIES
        FOLDER "Libraries/UI"
)

set(PICOATE_UI_PLATFORM_RESOURCES)
if(WIN32)
    list(APPEND PICOATE_UI_PLATFORM_RESOURCES PicoATE.rc)
endif()

qt_add_executable(PicoATEUi WIN32
    ${PICOATE_UI_PLATFORM_RESOURCES}
    FieldDeviceDialog.cpp
    FieldDeviceDialog.h
    FlowTargetSelector.cpp
    FlowTargetSelector.h
    LoadingSpinner.cpp
    LoadingSpinner.h
    LoginDialog.cpp
    LoginDialog.h
    Main.cpp
    MainWindow.cpp
    MainWindow.h
    OnOffControl.cpp
    OnOffControl.h
    PicoATE.qrc
    OperatorPromptPresenter.cpp
    OperatorPromptPresenter.h
    ProductionWindow.cpp
    ProductionWindow.h
    ProportionalHeaderView.cpp
    ProportionalHeaderView.h
    ScanDialog.cpp
    ScanDialog.h
    SequenceVariablesDialog.cpp
    SequenceVariablesDialog.h
    StepPropertyEditor.cpp
    StepPropertyEditor.h
    StationPropertyEditor.cpp
    StationPropertyEditor.h
    StationSettingsEditor.cpp
    StationSettingsEditor.h
)

target_link_libraries(PicoATEUi
    PRIVATE
        PicoATEUiRuntime
        Qt6::Core
        Qt6::Widgets
)

set_target_properties(PicoATEUi
    PROPERTIES
        AUTORCC ON
        OUTPUT_NAME "PicoATE.UI"
        FOLDER "Applications/UI"
        VS_DEBUGGER_WORKING_DIRECTORY "${PICOATE_ROOT_DIR}"
)

target_compile_features(PicoATEUi PRIVATE cxx_std_20)

target_compile_definitions(PicoATEUi
    PRIVATE
        PICOATE_PROJECT_DIR="${PICOATE_ROOT_DIR}"
)

add_custom_command(TARGET PicoATEUi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:PicoATEUi>/plugins"
    COMMENT "Ensuring the shared plugins directory exists"
)

add_custom_command(TARGET PicoATEUi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:PicoATEUi>/image"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PICOATE_ROOT_DIR}/image"
            "$<TARGET_FILE_DIR:PicoATEUi>/image"
    COMMENT "Deploying MessageBox images beside PicoATE.UI"
)

if(TARGET PicoATENativeHost)
    add_dependencies(PicoATEUi PicoATENativeHost)
    add_custom_command(TARGET PicoATEUi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PicoATENativeHost>"
                "$<TARGET_FILE_DIR:PicoATEUi>"
        COMMENT "Deploying PicoATE.NativeHost beside PicoATE.UI"
    )
endif()
