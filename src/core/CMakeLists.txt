add_library(PicoATECore STATIC
    src/BarrierController.cpp
    src/DllBridgeInvoker.cpp
    src/DeviceSessionManager.cpp
    src/DeviceTransportSession.cpp
    src/ErrorPolicyEngine.cpp
    src/ExecutionGraphScheduler.cpp
    src/ExecutionSession.cpp
    src/ExecutionPlan.cpp
    src/ExecutionReportJson.cpp
    src/InstrumentAdapterModules.cpp
    src/LoopController.cpp
    src/MeasurementTypes.cpp
    src/ModuleBindingRegistrar.cpp
    src/ModuleRuntime.cpp
    src/ModuleTransportJson.cpp
    src/NativeHostManifest.cpp
    src/NodeRunner.cpp
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
    src/StationRuntime.cpp
    src/StopToken.cpp
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
