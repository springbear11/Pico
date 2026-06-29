#include "PicoATE/Core/DeviceTransportSession.h"

#include <QUuid>

#include <utility>

namespace PicoATE::Core {

namespace {

QString traceIdForDevice(const DeviceId& deviceId, const ModuleFunction& functionName)
{
    return QString("%1:%2:%3").arg(deviceId,
                                   functionName,
                                   QUuid::createUuid().toString(QUuid::WithoutBraces));
}

ModuleResult transportUnavailableResult()
{
    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = "DeviceTransportUnavailable";
    result.errorMessage = "Device transport is unavailable";
    return result;
}

ModuleResult timeoutResult()
{
    ModuleResult result;
    result.outcome = ModuleOutcome::Timeout;
    result.errorCode = "DeviceTransportTimeout";
    result.errorMessage = "Device transport timed out";
    return result;
}

ModuleResult transportErrorResult(const ModuleTransportResponse& response)
{
    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = response.errorCode.isEmpty() ? QString("DeviceTransportError") : response.errorCode;
    result.errorMessage = response.errorMessage.isEmpty()
        ? QString("Device transport call failed")
        : response.errorMessage;
    return result;
}

} // namespace

TransportDeviceSession::TransportDeviceSession(DeviceSessionConfig config,
                                               std::shared_ptr<IModuleTransport> transport,
                                               int timeoutMs)
    : m_config(std::move(config))
    , m_transport(std::move(transport))
    , m_timeoutMs(timeoutMs)
{
}

DeviceId TransportDeviceSession::deviceId() const
{
    return m_config.deviceId;
}

QString TransportDeviceSession::deviceType() const
{
    return m_config.deviceType;
}

DeviceConnectionState TransportDeviceSession::state() const
{
    return m_state;
}

bool TransportDeviceSession::connect(QString& errorMessage)
{
    m_state = DeviceConnectionState::Connecting;
    ModuleExecutionContext context;
    const auto result = callHost("open", {}, context);
    if (result.outcome != ModuleOutcome::Passed) {
        m_state = DeviceConnectionState::Error;
        errorMessage = result.errorMessage.isEmpty() ? result.errorCode : result.errorMessage;
        return false;
    }

    errorMessage.clear();
    m_state = DeviceConnectionState::Connected;
    return true;
}

void TransportDeviceSession::disconnect()
{
    ModuleExecutionContext context;
    callHost("close", {}, context);
    m_state = DeviceConnectionState::Disconnected;
}

QVariantMap TransportDeviceSession::metadata() const
{
    auto metadata = m_config.options;
    metadata.insert("address", m_config.address);
    metadata.insert("driverId", m_config.driverId);
    metadata.insert("lifetime", deviceSessionLifetimeName(m_config.lifetime));
    return metadata;
}

bool TransportDeviceSession::isHealthy(QString& errorMessage) const
{
    ModuleExecutionContext context;
    const auto result = callHost("health", {}, context);
    if (result.outcome == ModuleOutcome::Passed &&
        result.outputs.value("healthy", true).toBool()) {
        errorMessage.clear();
        return true;
    }

    errorMessage = result.errorMessage.isEmpty()
        ? QString("device health check failed")
        : result.errorMessage;
    return false;
}

ModuleResult TransportDeviceSession::invokeDeviceCommand(const ModuleFunction& functionName,
                                                         const QVariantMap& inputs,
                                                         const ModuleExecutionContext& context)
{
    return callHost(functionName, inputs, context);
}

ModuleResult TransportDeviceSession::callHost(const ModuleFunction& functionName,
                                              QVariantMap inputs,
                                              const ModuleExecutionContext& context) const
{
    if (!m_transport) {
        return transportUnavailableResult();
    }

    ModuleTransportRequest request;
    request.traceId = traceIdForDevice(m_config.deviceId, functionName);
    request.moduleId = m_config.driverId;
    request.functionName = functionName;
    request.context = context;
    request.context.inputs = deviceInputs(std::move(inputs));

    ModuleTransportResponse response;
    const auto status = m_transport->call(request, response, m_timeoutMs);
    switch (status) {
    case ModuleTransportStatus::Ok: {
        ModuleResult result;
        result.outcome = response.outcome;
        result.outputs = response.outputs;
        result.measurements = response.measurements;
        result.errorCode = response.errorCode;
        result.errorMessage = response.errorMessage;
        return result;
    }
    case ModuleTransportStatus::Timeout:
        return timeoutResult();
    case ModuleTransportStatus::TransportError:
        return transportErrorResult(response);
    }

    return transportErrorResult(response);
}

QVariantMap TransportDeviceSession::deviceInputs(QVariantMap inputs) const
{
    if (!inputs.contains("deviceId")) {
        inputs.insert("deviceId", m_config.deviceId);
    }
    if (!inputs.contains("deviceType")) {
        inputs.insert("deviceType", m_config.deviceType);
    }
    if (!inputs.contains("address")) {
        inputs.insert("address", m_config.address);
    }
    for (auto it = m_config.options.constBegin(); it != m_config.options.constEnd(); ++it) {
        if (!inputs.contains(it.key())) {
            inputs.insert(it.key(), it.value());
        }
    }
    return inputs;
}

TransportDeviceSessionFactory::TransportDeviceSessionFactory(DeviceDriverId driverId,
                                                             std::shared_ptr<IModuleTransport> transport,
                                                             int timeoutMs)
    : m_driverId(std::move(driverId))
    , m_transport(std::move(transport))
    , m_timeoutMs(timeoutMs)
{
}

DeviceDriverId TransportDeviceSessionFactory::driverId() const
{
    return m_driverId;
}

std::shared_ptr<IDeviceSession> TransportDeviceSessionFactory::createSession(const DeviceSessionConfig& config,
                                                                             DeviceSessionError& error)
{
    if (!m_transport) {
        error.deviceId = config.deviceId;
        error.errorCode = "DeviceTransportUnavailable";
        error.message = "Device transport is unavailable";
        return {};
    }

    return std::make_shared<TransportDeviceSession>(config, m_transport, m_timeoutMs);
}

} // namespace PicoATE::Core
