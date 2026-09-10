add_library(PicoATEUiRuntime STATIC
    ApplicationDiagnostics.cpp
    ApplicationDiagnostics.h
    BufferedRuntimeEventSink.cpp
    BufferedRuntimeEventSink.h
    CoreExecutionService.cpp
    CoreExecutionService.h
    ExecutionViewModel.cpp
    ExecutionViewModel.h
    ExecutionWorker.cpp
    ExecutionWorker.h
    FunctionIconProvider.cpp
    FunctionIconProvider.h
    IExecutionService.h
    RuntimeIntegrity.cpp
    RuntimeIntegrity.h
    MeasurementDisplay.cpp
    MeasurementDisplay.h
    ProjectResourcePaths.cpp
    ProjectResourcePaths.h
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
    UiLanguage.cpp
    UiLanguage.h
)

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_lrelease(
    TS_FILES translations/picoate_zh_CN.ts
    QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations"
    QM_FILES_OUTPUT_VARIABLE PICOATE_UI_QM_FILES
)
qt_add_resources(PicoATEUiRuntime picoate_ui_translations
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}/translations"
    FILES ${PICOATE_UI_QM_FILES}
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

if(WIN32)
    target_link_libraries(PicoATEUiRuntime PRIVATE Dbghelp)
endif()

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
    AdminStartupSplash.cpp
    AdminStartupSplash.h
    FieldDeviceDialog.cpp
    FieldDeviceDialog.h
    FlowTargetSelector.cpp
    FlowTargetSelector.h
    InputWheelGuard.cpp
    InputWheelGuard.h
    IntegrityPage.cpp
    IntegrityPage.h
    LoadingSpinner.cpp
    LoadingSpinner.h
    LoginDialog.cpp
    LoginDialog.h
    Main.cpp
    MainWindow.cpp
    MainWindow.h
    UiTextBinding.cpp
    UiTextBinding.h
    ElidedInfoLabel.h
    TitleBarLanguageButton.cpp
    TitleBarLanguageButton.h
    MultiUutOverviewWidget.cpp
    MultiUutOverviewWidget.h
    OnOffControl.cpp
    OnOffControl.h
    PicoATE.qrc
    PicoATEStyle.cpp
    PicoATEStyle.h
    OperatorPromptPresenter.cpp
    OperatorPromptPresenter.h
    PromptCountdownWidget.cpp
    PromptCountdownWidget.h
    ParserActualDelegate.cpp
    ParserActualDelegate.h
    ProductionWindow.cpp
    ProductionWindow.h
    ProductRoutingDialog.cpp
    ProductRoutingDialog.h
    ProportionalHeaderView.cpp
    ProportionalHeaderView.h
    ScanDialog.cpp
    ScanDialog.h
    UutSlotConfigurationDialog.cpp
    UutSlotConfigurationDialog.h
    SequenceEditorTreeView.cpp
    SequenceEditorTreeView.h
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
        PICOATE_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    target_link_libraries(PicoATEUi PRIVATE dwmapi)
endif()

add_custom_command(TARGET PicoATEUi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:PicoATEUi>/plugins"
    COMMENT "Ensuring the shared plugins directory exists"
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

if(TARGET PicoATECli)
    add_dependencies(PicoATEUi PicoATECli)
    add_custom_command(TARGET PicoATEUi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PicoATECli>"
                "$<TARGET_FILE_DIR:PicoATEUi>"
        COMMENT "Deploying PicoATE.Cli beside PicoATE.UI"
    )
endif()

if(TARGET PicoATERegisterImporter)
    add_dependencies(PicoATEUi PicoATERegisterImporter)
    add_custom_command(TARGET PicoATEUi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PicoATERegisterImporter>"
                "$<TARGET_FILE_DIR:PicoATEUi>"
        COMMENT "Deploying PicoATE.RegisterImporter beside PicoATE.UI"
    )
endif()

# The GCAN ECanVci64 vendor DLL was built with Visual Studio 2013 and loads
# MSVCR120 from the host executable directory on clean production PCs.
set(PICOATE_GCAN_VS2013_RUNTIME_DLL
    "${PICOATE_ROOT_DIR}/templates/dependencies/CAN/GCAN/x64/msvcr120.dll")
if(EXISTS "${PICOATE_GCAN_VS2013_RUNTIME_DLL}")
    add_custom_command(TARGET PicoATEUi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PICOATE_GCAN_VS2013_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:PicoATEUi>"
        COMMENT "Deploying the GCAN Visual Studio 2013 runtime beside PicoATE.UI"
    )
endif()

find_program(PICOATE_UI_WINDEPLOYQT
    NAMES windeployqt
    HINTS "${Qt6_DIR}/../../../bin"
    REQUIRED
)

set(PICOATE_UI_PORTABLE_DIR
    "${CMAKE_BINARY_DIR}/portable/$<CONFIG>/PicoATE.UI")
set(PICOATE_UI_RUNTIME_PAYLOAD_DIR
    "${PICOATE_ROOT_DIR}/packaging/runtime")

set(PICOATE_UI_MSVC_RUNTIME_COPY_COMMAND)
if(MSVC AND CMAKE_GENERATOR_INSTANCE)
    file(GLOB PICOATE_UI_MSVC_RUNTIME_DIRS LIST_DIRECTORIES true
        "${CMAKE_GENERATOR_INSTANCE}/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
    )
    list(SORT PICOATE_UI_MSVC_RUNTIME_DIRS COMPARE NATURAL ORDER DESCENDING)
    if(PICOATE_UI_MSVC_RUNTIME_DIRS)
        list(GET PICOATE_UI_MSVC_RUNTIME_DIRS 0 PICOATE_UI_MSVC_RUNTIME_DIR)
        file(GLOB PICOATE_UI_MSVC_RUNTIME_DLLS
            "${PICOATE_UI_MSVC_RUNTIME_DIR}/*.dll")
        if(PICOATE_UI_MSVC_RUNTIME_DLLS)
            list(APPEND PICOATE_UI_MSVC_RUNTIME_COPY_COMMAND
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        ${PICOATE_UI_MSVC_RUNTIME_DLLS}
                        "${PICOATE_UI_PORTABLE_DIR}/"
            )
        endif()
    endif()
endif()

set(PICOATE_UI_LEGACY_RUNTIME_COPY_COMMAND)
if(EXISTS "${PICOATE_GCAN_VS2013_RUNTIME_DLL}")
    list(APPEND PICOATE_UI_LEGACY_RUNTIME_COPY_COMMAND
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PICOATE_GCAN_VS2013_RUNTIME_DLL}"
                "${PICOATE_UI_PORTABLE_DIR}/"
    )
endif()

add_custom_target(PicoATEUiPortable
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PICOATE_UI_PORTABLE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${PICOATE_UI_PORTABLE_DIR}/plugins"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${PICOATE_UI_PORTABLE_DIR}/log"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${PICOATE_UI_PORTABLE_DIR}/diagnostics"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:PicoATEUi>"
            "${PICOATE_UI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:PicoATECore>"
            "${PICOATE_UI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:PicoATENativeHost>"
            "${PICOATE_UI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:PicoATERegisterImporter>"
            "${PICOATE_UI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:PicoATECli>"
            "${PICOATE_UI_PORTABLE_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${PICOATE_ROOT_DIR}/templates/bin/$<CONFIG>"
            "${PICOATE_UI_PORTABLE_DIR}/plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${PICOATE_UI_RUNTIME_PAYLOAD_DIR}"
            "${PICOATE_UI_PORTABLE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${PICOATE_UI_PORTABLE_DIR}/projects"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${PICOATE_UI_PORTABLE_DIR}/projects"
    ${PICOATE_UI_LEGACY_RUNTIME_COPY_COMMAND}
    ${PICOATE_UI_MSVC_RUNTIME_COPY_COMMAND}
    COMMAND "${PICOATE_UI_WINDEPLOYQT}"
            "--$<IF:$<CONFIG:Debug>,debug,release>"
            --compiler-runtime
            --no-translations
            --dir "${PICOATE_UI_PORTABLE_DIR}"
            "${PICOATE_UI_PORTABLE_DIR}/PicoATE.UI.exe"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${PICOATE_ROOT_DIR}/packaging/GeneratePluginRegistry.ps1"
            -PortableDirectory "${PICOATE_UI_PORTABLE_DIR}"
    DEPENDS PicoATEUi PicoATECore PicoATENativeHost PicoATERegisterImporter PicoATECli
    COMMENT "Creating a self-contained PicoATE UI folder"
    VERBATIM
)

set_target_properties(PicoATEUiPortable PROPERTIES
    FOLDER "Packaging"
)
