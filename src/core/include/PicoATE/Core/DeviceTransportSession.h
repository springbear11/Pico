#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

namespace PicoATE::Core {

class TransportDeviceSession final : public IDeviceSession, public IDeviceCommandSession {
public:
    TransportDeviceSession(DeviceSessionConfig config,
                           std::shared_ptr<IModuleTransport> transport,
                           int timeoutMs = 30000);

    DeviceId deviceId() const override;
    QString deviceType() const override;
    DeviceConnectionState state() const override;
    bool connect(QString& errorMessage) override;
    void disconnect() override;
    QVariantMap metadata() const override;
    bool isHealthy(QString& errorMessage) const override;

    ModuleResult invokeDeviceCommand(const ModuleFunction& functionName,
                                     const QVariantMap& inputs,
                                     const ModuleExecutionContext& context) override;

private:
    ModuleResult callHost(const ModuleFunction& functionName,
                          QVariantMap inputs,
                          const ModuleExecutionContext& context) const;
    QVariantMap deviceInputs(QVariantMap inputs = {}) const;

    DeviceSessionConfig m_config;
    std::shared_ptr<IModuleTransport> m_transport;
    int m_timeoutMs = 30000;
    DeviceConnectionState m_state = DeviceConnectionState::Disconnected;
};

class TransportDeviceSessionFactory final : public IDeviceSessionFactory {
public:
    TransportDeviceSessionFactory(DeviceDriverId driverId,
                                  std::shared_ptr<IModuleTransport> transport,
                                  int timeoutMs = 30000);

    DeviceDriverId driverId() const override;
    std::shared_ptr<IDeviceSession> createSession(const DeviceSessionConfig& config,
                                                  DeviceSessionError& error) override;

private:
    DeviceDriverId m_driverId;
    std::shared_ptr<IModuleTransport> m_transport;
    int m_timeoutMs = 30000;
};

} // namespace PicoATE::Core
