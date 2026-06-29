#pragma once

#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <memory>

namespace PicoATE::Core {

using ModuleId = QString;
using ModuleFunction = QString;

class IModuleRuntimeServices;

enum class ModuleOutcome {
    Passed,
    Failed,
    Error,
    Timeout
};

struct ModuleExecutionContext {
    UutId uutId;
    FrameId frameId;
    AttemptId attemptId;
    int attemptIndex = 0;
    QVariantMap inputs;
    QVariantMap parameters;
    QVariantMap variables;
    IModuleRuntimeServices* runtimeServices = nullptr;
};

struct ModuleResult {
    ModuleOutcome outcome = ModuleOutcome::Passed;
    QVariantMap outputs;
    QVector<MeasurementResult> measurements;
    QString errorCode;
    QString errorMessage;
};

class IDeviceCommandSession {
public:
    virtual ~IDeviceCommandSession() = default;
    virtual ModuleResult invokeDeviceCommand(const ModuleFunction& functionName,
                                             const QVariantMap& inputs,
                                             const ModuleExecutionContext& context) = 0;
};

class IModuleRuntimeServices {
public:
    virtual ~IModuleRuntimeServices() = default;

    virtual DeviceSessionOpenResult openDeviceSession(const DeviceId& deviceId) = 0;
    virtual DeviceSessionError closeDeviceSession(const DeviceId& deviceId) = 0;
    virtual std::shared_ptr<IDeviceSession> deviceSession(const DeviceId& deviceId) const = 0;
    virtual ModuleResult invokeDevice(const DeviceId& deviceId,
                                      const ModuleFunction& functionName,
                                      const QVariantMap& inputs,
                                      const ModuleExecutionContext& context) = 0;
};

class ModuleRuntimeServices final : public IModuleRuntimeServices {
public:
    explicit ModuleRuntimeServices(DeviceSessionManager& devices);

    DeviceSessionOpenResult openDeviceSession(const DeviceId& deviceId) override;
    DeviceSessionError closeDeviceSession(const DeviceId& deviceId) override;
    std::shared_ptr<IDeviceSession> deviceSession(const DeviceId& deviceId) const override;
    ModuleResult invokeDevice(const DeviceId& deviceId,
                              const ModuleFunction& functionName,
                              const QVariantMap& inputs,
                              const ModuleExecutionContext& context) override;

private:
    DeviceSessionManager& m_devices;
};

struct ModuleTransportRequest {
    QString traceId;
    ModuleId moduleId;
    ModuleFunction functionName;
    ModuleExecutionContext context;
};

struct ModuleTransportResponse {
    ModuleOutcome outcome = ModuleOutcome::Passed;
    QVariantMap outputs;
    QVector<MeasurementResult> measurements;
    QString errorCode;
    QString errorMessage;
};

enum class ModuleTransportStatus {
    Ok,
    Timeout,
    TransportError
};

class IModuleTransport {
public:
    virtual ~IModuleTransport() = default;
    virtual ModuleTransportStatus call(const ModuleTransportRequest& request,
                                       ModuleTransportResponse& response,
                                       int timeoutMs) = 0;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual ModuleId moduleId() const = 0;
    virtual ModuleResult execute(const ModuleFunction& functionName,
                                 const ModuleExecutionContext& context) = 0;
};

class ModuleRegistry {
public:
    bool registerModule(std::shared_ptr<IModule> module);
    std::shared_ptr<IModule> module(const ModuleId& moduleId) const;
    bool contains(const ModuleId& moduleId) const;
    QVector<ModuleId> moduleIds() const;

private:
    QHash<ModuleId, std::shared_ptr<IModule>> m_modules;
};

class TransportModuleAdapter final : public IModule {
public:
    TransportModuleAdapter(ModuleId moduleId,
                           std::shared_ptr<IModuleTransport> transport,
                           int timeoutMs = 30000);

    ModuleId moduleId() const override;
    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override;

private:
    ModuleId m_moduleId;
    std::shared_ptr<IModuleTransport> m_transport;
    int m_timeoutMs = 30000;
};

class MockActionModule final : public IModule {
public:
    explicit MockActionModule(ModuleId id = "mock.action");

    ModuleId moduleId() const override;
    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override;

private:
    ModuleId m_id;
};

NodeOutcome toNodeOutcome(ModuleOutcome outcome);
MeasurementStatus toMeasurementStatus(ModuleOutcome outcome);

} // namespace PicoATE::Core
