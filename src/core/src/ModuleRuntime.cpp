#include "PicoATE/Core/ModuleRuntime.h"

#include <QUuid>
#include <utility>

namespace PicoATE::Core {

namespace {

QString makeTraceId(const ModuleId& moduleId)
{
    return QString("%1:%2").arg(moduleId, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString stringFromMaps(const QVariantMap& primary,
                       const QVariantMap& fallback,
                       const QString& key,
                       const QString& defaultValue = {})
{
    if (primary.contains(key)) {
        return primary.value(key).toString();
    }
    if (fallback.contains(key)) {
        return fallback.value(key).toString();
    }
    return defaultValue;
}

int intFromMaps(const QVariantMap& primary,
                const QVariantMap& fallback,
                const QString& key,
                int defaultValue)
{
    if (primary.contains(key)) {
        return primary.value(key).toInt();
    }
    if (fallback.contains(key)) {
        return fallback.value(key).toInt();
    }
    return defaultValue;
}

QVariantMap mapFromMaps(const QVariantMap& primary,
                        const QVariantMap& fallback,
                        const QString& key)
{
    if (primary.contains(key)) {
        return primary.value(key).toMap();
    }
    if (fallback.contains(key)) {
        return fallback.value(key).toMap();
    }
    return {};
}

ModuleResult moduleResultFromDeviceError(const DeviceSessionError& error)
{
    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = error.errorCode.isEmpty() ? QString("DeviceSessionError") : error.errorCode;
    result.errorMessage = error.message;
    return result;
}

} // namespace

ModuleRuntimeServices::ModuleRuntimeServices(DeviceSessionManager& devices)
    : m_devices(devices)
{
}

DeviceSessionOpenResult ModuleRuntimeServices::openDeviceSession(const DeviceId& deviceId)
{
    return m_devices.openSession(deviceId);
}

DeviceSessionError ModuleRuntimeServices::closeDeviceSession(const DeviceId& deviceId)
{
    return m_devices.closeSession(deviceId);
}

std::shared_ptr<IDeviceSession> ModuleRuntimeServices::deviceSession(const DeviceId& deviceId) const
{
    return m_devices.session(deviceId);
}

ModuleResult ModuleRuntimeServices::invokeDevice(const DeviceId& deviceId,
                                                 const ModuleFunction& functionName,
                                                 const QVariantMap& inputs,
                                                 const ModuleExecutionContext& context)
{
    const auto open = m_devices.openSession(deviceId);
    if (!open.ok()) {
        return moduleResultFromDeviceError(open.error);
    }

    const auto commandSession = std::dynamic_pointer_cast<IDeviceCommandSession>(open.session);
    if (!commandSession) {
        ModuleResult result;
        result.outcome = ModuleOutcome::Error;
        result.errorCode = "DeviceCommandUnsupported";
        result.errorMessage = QString("Device session does not support commands: %1").arg(deviceId);
        return result;
    }

    return commandSession->invokeDeviceCommand(functionName, inputs, context);
}

bool ModuleRegistry::registerModule(std::shared_ptr<IModule> module)
{
    if (!module || module->moduleId().trimmed().isEmpty()) {
        return false;
    }

    const auto id = module->moduleId();
    if (m_modules.contains(id)) {
        return false;
    }

    m_modules.insert(id, std::move(module));
    return true;
}

std::shared_ptr<IModule> ModuleRegistry::module(const ModuleId& moduleId) const
{
    const auto it = m_modules.constFind(moduleId);
    if (it == m_modules.constEnd()) {
        return {};
    }
    return it.value();
}

bool ModuleRegistry::contains(const ModuleId& moduleId) const
{
    return m_modules.contains(moduleId);
}

QVector<ModuleId> ModuleRegistry::moduleIds() const
{
    QVector<ModuleId> ids;
    ids.reserve(m_modules.size());
    for (auto it = m_modules.constBegin(); it != m_modules.constEnd(); ++it) {
        ids.push_back(it.key());
    }
    return ids;
}

TransportModuleAdapter::TransportModuleAdapter(ModuleId moduleId,
                                               std::shared_ptr<IModuleTransport> transport,
                                               int timeoutMs)
    : m_moduleId(std::move(moduleId))
    , m_transport(std::move(transport))
    , m_timeoutMs(timeoutMs)
{
}

ModuleId TransportModuleAdapter::moduleId() const
{
    return m_moduleId;
}

ModuleResult TransportModuleAdapter::execute(const ModuleFunction& functionName,
                                             const ModuleExecutionContext& context)
{
    ModuleResult result;
    if (!m_transport) {
        result.outcome = ModuleOutcome::Error;
        result.errorCode = "TransportUnavailable";
        result.errorMessage = "Module transport is unavailable";
        return result;
    }

    ModuleTransportRequest request;
    request.traceId = makeTraceId(m_moduleId);
    request.moduleId = m_moduleId;
    request.functionName = functionName;
    request.context = context;

    ModuleTransportResponse response;
    const auto status = m_transport->call(request, response, m_timeoutMs);
    switch (status) {
    case ModuleTransportStatus::Ok:
        result.outcome = response.outcome;
        result.outputs = response.outputs;
        result.measurements = response.measurements;
        result.errorCode = response.errorCode;
        result.errorMessage = response.errorMessage;
        return result;
    case ModuleTransportStatus::Timeout:
        result.outcome = ModuleOutcome::Timeout;
        result.errorCode = "TransportTimeout";
        result.errorMessage = "Module transport timed out";
        return result;
    case ModuleTransportStatus::TransportError:
        result.outcome = ModuleOutcome::Error;
        result.errorCode = response.errorCode.isEmpty() ? QString("TransportError") : response.errorCode;
        result.errorMessage = response.errorMessage.isEmpty()
            ? QString("Module transport call failed")
            : response.errorMessage;
        return result;
    }

    result.outcome = ModuleOutcome::Error;
    result.errorCode = "TransportError";
    result.errorMessage = "Unknown module transport status";
    return result;
}

MockActionModule::MockActionModule(ModuleId id)
    : m_id(std::move(id))
{
}

ModuleId MockActionModule::moduleId() const
{
    return m_id;
}

ModuleResult MockActionModule::execute(const ModuleFunction&,
                                       const ModuleExecutionContext& context)
{
    ModuleResult result;

    const auto failForUut = stringFromMaps(context.inputs, context.parameters, "failForUut");
    if (!failForUut.isEmpty() && failForUut == context.uutId) {
        result.outcome = ModuleOutcome::Failed;
        result.errorMessage = "Mock failure for UUT";
        return result;
    }

    const auto failUntilAttempt = intFromMaps(context.inputs, context.parameters, "failUntilAttempt", -1);
    if (context.attemptIndex <= failUntilAttempt) {
        result.outcome = ModuleOutcome::Failed;
        result.errorMessage = "Mock failure";
        return result;
    }

    const auto outcome = stringFromMaps(context.inputs, context.parameters, "outcome", "Passed");
    if (outcome.compare("Failed", Qt::CaseInsensitive) == 0) {
        result.outcome = ModuleOutcome::Failed;
    } else if (outcome.compare("Error", Qt::CaseInsensitive) == 0) {
        result.outcome = ModuleOutcome::Error;
    } else if (outcome.compare("Timeout", Qt::CaseInsensitive) == 0) {
        result.outcome = ModuleOutcome::Timeout;
    } else {
        result.outcome = ModuleOutcome::Passed;
    }

    result.errorCode = stringFromMaps(context.inputs, context.parameters, "errorCode");
    result.errorMessage = stringFromMaps(context.inputs, context.parameters, "errorMessage");
    result.outputs = mapFromMaps(context.inputs, context.parameters, "outputs");
    result.measurements = measurementsFromVariant(
        mapFromMaps(context.inputs, context.parameters, "measurements"),
        toMeasurementStatus(result.outcome));
    return result;
}

NodeOutcome toNodeOutcome(ModuleOutcome outcome)
{
    switch (outcome) {
    case ModuleOutcome::Passed:
        return NodeOutcome::Passed;
    case ModuleOutcome::Failed:
        return NodeOutcome::Failed;
    case ModuleOutcome::Error:
        return NodeOutcome::Error;
    case ModuleOutcome::Timeout:
        return NodeOutcome::Timeout;
    }
    return NodeOutcome::Error;
}

MeasurementStatus toMeasurementStatus(ModuleOutcome outcome)
{
    switch (outcome) {
    case ModuleOutcome::Passed:
        return MeasurementStatus::Passed;
    case ModuleOutcome::Failed:
        return MeasurementStatus::Failed;
    case ModuleOutcome::Error:
    case ModuleOutcome::Timeout:
        return MeasurementStatus::Error;
    }
    return MeasurementStatus::Unknown;
}

} // namespace PicoATE::Core
