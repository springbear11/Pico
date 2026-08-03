add_library(PicoATECore SHARED
    include/PicoATE/Core/CoreExport.h
    src/BarrierController.cpp
    src/DllBridgeInvoker.cpp
    src/DeviceDiscovery.cpp
    src/DeviceSessionManager.cpp
    src/DeviceTransportSession.cpp
    src/ErrorPolicyEngine.cpp
    src/ExecutionControl.cpp
    src/ExecutionDebug.cpp
    src/ExecutionGraphScheduler.cpp
    src/ExecutionSession.cpp
    src/ExecutionPlan.cpp
    src/ExecutionRequest.cpp
    src/ExecutionReportJson.cpp
    src/ExecutionResultStore.cpp
    src/InstrumentAdapterModules.cpp
    src/LoopController.cpp
    src/MeasurementTypes.cpp
    src/ModuleBindingRegistrar.cpp
    src/ModuleRuntime.cpp
    src/DataParserModule.cpp
    src/ModuleTransportJson.cpp
    src/NativeHostManifest.cpp
    src/NodeRunner.cpp
    src/OperatorPrompt.cpp
    src/PeriodicTaskController.cpp
    src/PlanBuilder.cpp
    src/PlanCache.cpp
    src/PersistentQProcessTransport.cpp
    src/QProcessTransport.cpp
    src/ResourceManager.cpp
    src/RuntimeEvent.cpp
    src/RuntimeVariableResolver.cpp
    src/SequenceCompiler.cpp
    src/SequenceDef.cpp
    src/StationConfig.cpp
    src/StationRunPreparation.cpp
    src/StationRuntime.cpp
    src/StopToken.cpp
    src/TimerService.cpp
    src/VariableResolver.cpp
)

target_include_directories(PicoATECore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PicoATECore
    PUBLIC
        Qt6::Core
)

target_compile_features(PicoATECore PUBLIC cxx_std_20)

target_compile_definitions(PicoATECore
    PRIVATE
        PICOATE_CORE_BUILD
)

set_target_properties(PicoATECore
    PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Keep the existing executable layout while allowing PicoATECore to be built
# independently from its consumers. Building only PicoATECore refreshes the
# runtime DLL beside every executable that may already exist in this build tree.
if(WIN32)
    set(PICOATE_CORE_RUNTIME_DIRECTORIES
        "${CMAKE_BINARY_DIR}/src/cli/$<CONFIG>"
        "${CMAKE_BINARY_DIR}/src/nativehost/$<CONFIG>"
        "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
        "${CMAKE_BINARY_DIR}/ui/src/$<CONFIG>"
        "${CMAKE_BINARY_DIR}/ui/tests/$<CONFIG>"
        # Paths used when ui/CMakeLists.txt is configured as a standalone project.
        "${CMAKE_BINARY_DIR}/src/$<CONFIG>"
    )

    set(PICOATE_CORE_DEPLOY_COMMANDS)
    foreach(PICOATE_CORE_RUNTIME_DIRECTORY IN LISTS PICOATE_CORE_RUNTIME_DIRECTORIES)
        list(APPEND PICOATE_CORE_DEPLOY_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${PICOATE_CORE_RUNTIME_DIRECTORY}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:PicoATECore>"
                    "${PICOATE_CORE_RUNTIME_DIRECTORY}/"
        )
    endforeach()

    add_custom_command(TARGET PicoATECore POST_BUILD
        ${PICOATE_CORE_DEPLOY_COMMANDS}
        COMMENT "Deploying PicoATECore DLL to Debug/Release runtime directories"
        VERBATIM
    )
endif()
