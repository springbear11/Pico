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
    ReportExporter.cpp
    ReportExporter.h
    ReportHistoryStore.cpp
    ReportHistoryStore.h
    RunnerModels.cpp
    RunnerModels.h
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

qt_add_executable(PicoATEUi WIN32
    Main.cpp
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(PicoATEUi
    PRIVATE
        PicoATEUiRuntime
        Qt6::Core
        Qt6::Widgets
)

set_target_properties(PicoATEUi
    PROPERTIES
        OUTPUT_NAME "PicoATE.UI"
        FOLDER "Applications/UI"
        VS_DEBUGGER_WORKING_DIRECTORY "${PICOATE_ROOT_DIR}"
)

target_compile_features(PicoATEUi PRIVATE cxx_std_20)
