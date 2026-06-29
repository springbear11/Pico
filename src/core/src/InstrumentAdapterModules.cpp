#include "PicoATE/Core/InstrumentAdapterModules.h"

#include <utility>

namespace PicoATE::Core {

namespace {

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

DeviceId deviceIdFromContext(const ModuleExecutionContext& context,
                             const QString& primaryKey,
                             const QString& fallback)
{
    const auto primary = context.inputs.value(primaryKey).toString().trimmed();
    if (!primary.isEmpty()) {
        return primary;
    }
    const auto generic = context.inputs.value("deviceId").toString().trimmed();
    return generic.isEmpty() ? fallback : generic;
}

ModuleResult runtimeUnavailable()
{
    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = "RuntimeServicesUnavailable";
    result.errorMessage = "Module runtime services are unavailable";
    return result;
}

ModuleResult fromOpenResult(const DeviceId& deviceId, const DeviceSessionOpenResult& open)
{
    if (!open.ok()) {
        ModuleResult result;
        result.outcome = ModuleOutcome::Error;
        result.errorCode = open.error.errorCode;
        result.errorMessage = open.error.message;
        return result;
    }

    ModuleResult result;
    result.outcome = ModuleOutcome::Passed;
    result.outputs.insert("deviceId", deviceId);
    result.outputs.insert("connected", true);
    result.outputs.insert("reusedExisting", open.reusedExisting);
    if (open.session) {
        result.outputs.insert("deviceType", open.session->deviceType());
        result.outputs.insert("metadata", open.session->metadata());
    }
    return result;
}

ModuleResult fromCloseError(const DeviceId& deviceId, const DeviceSessionError& error)
{
    ModuleResult result;
    result.outputs.insert("deviceId", deviceId);
    result.outputs.insert("connected", false);
    if (error.hasError()) {
        result.outcome = ModuleOutcome::Error;
        result.errorCode = error.errorCode;
        result.errorMessage = error.message;
    }
    return result;
}

void applyLimits(ModuleResult& result, double lowerLimit, bool hasLower, double upperLimit, bool hasUpper)
{
    if (result.measurements.isEmpty()) {
        return;
    }

    auto& measurement = result.measurements.first();
    bool ok = false;
    const auto value = measurement.value.toDouble(&ok);
    if (!ok) {
        return;
    }

    if (hasLower) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = lowerLimit;
    }
    if (hasUpper) {
        measurement.hasUpperLimit = true;
        measurement.upperLimit = upperLimit;
    }
    if ((hasLower && value < lowerLimit) || (hasUpper && value > upperLimit)) {
        measurement.status = MeasurementStatus::Failed;
        result.outcome = ModuleOutcome::Failed;
        result.errorCode = "LimitFailed";
        result.errorMessage = QString("Measurement %1 is outside limits").arg(measurement.name);
    }
}

QVariantMap withDefaultMeasurementName(QVariantMap inputs, const QString& name)
{
    if (!inputs.contains("measurementName")) {
        inputs.insert("measurementName", name);
    }
    return inputs;
}

} // namespace

ExampleDmmAdapterModule::ExampleDmmAdapterModule(ModuleId id)
    : m_id(std::move(id))
{
}

ModuleId ExampleDmmAdapterModule::moduleId() const
{
    return m_id;
}

ModuleResult ExampleDmmAdapterModule::execute(const ModuleFunction& functionName,
                                              const ModuleExecutionContext& context)
{
    if (!context.runtimeServices) {
        return runtimeUnavailable();
    }

    const auto deviceId = deviceIdFromContext(context, "dmm", "DMM1");
    const auto function = normalized(functionName);
    if (function == "connect" || function == "connectdmm") {
        return fromOpenResult(deviceId, context.runtimeServices->openDeviceSession(deviceId));
    }
    if (function == "disconnect" || function == "disconnectdmm") {
        return fromCloseError(deviceId, context.runtimeServices->closeDeviceSession(deviceId));
    }
    if (function == "configuredcv") {
        return context.runtimeServices->invokeDevice(deviceId, "configureDcv", context.inputs, context);
    }
    if (function == "getidentity" || function == "identity") {
        return context.runtimeServices->invokeDevice(deviceId, "identity", context.inputs, context);
    }
    if (function == "read" || function == "readdmm") {
        auto result = context.runtimeServices->invokeDevice(
            deviceId,
            "read",
            withDefaultMeasurementName(context.inputs, "DMM_DCV"),
            context);
        applyLimits(result,
                    context.inputs.value("lowerLimit").toDouble(),
                    context.inputs.contains("lowerLimit"),
                    context.inputs.value("upperLimit").toDouble(),
                    context.inputs.contains("upperLimit"));
        return result;
    }
    if (function == "measuredcv") {
        auto open = context.runtimeServices->openDeviceSession(deviceId);
        if (!open.ok()) {
            return fromOpenResult(deviceId, open);
        }
        auto configure = context.runtimeServices->invokeDevice(deviceId, "configureDcv", context.inputs, context);
        if (configure.outcome != ModuleOutcome::Passed) {
            return configure;
        }
        auto result = context.runtimeServices->invokeDevice(
            deviceId,
            "read",
            withDefaultMeasurementName(context.inputs, "DMM_DCV"),
            context);
        applyLimits(result,
                    context.inputs.value("lowerLimit").toDouble(),
                    context.inputs.contains("lowerLimit"),
                    context.inputs.value("upperLimit").toDouble(),
                    context.inputs.contains("upperLimit"));
        return result;
    }

    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = "UnsupportedDmmFunction";
    result.errorMessage = QString("Unsupported DMM adapter function: %1").arg(functionName);
    return result;
}

ExampleCanAdapterModule::ExampleCanAdapterModule(ModuleId id)
    : m_id(std::move(id))
{
}

ModuleId ExampleCanAdapterModule::moduleId() const
{
    return m_id;
}

ModuleResult ExampleCanAdapterModule::execute(const ModuleFunction& functionName,
                                              const ModuleExecutionContext& context)
{
    if (!context.runtimeServices) {
        return runtimeUnavailable();
    }

    const auto deviceId = deviceIdFromContext(context, "can", "CAN1");
    const auto function = normalized(functionName);
    if (function == "connect" || function == "connectcan") {
        return fromOpenResult(deviceId, context.runtimeServices->openDeviceSession(deviceId));
    }
    if (function == "disconnect" || function == "disconnectcan") {
        return fromCloseError(deviceId, context.runtimeServices->closeDeviceSession(deviceId));
    }
    if (function == "readframe" || function == "readcan") {
        return context.runtimeServices->invokeDevice(deviceId, "readFrame", context.inputs, context);
    }

    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = "UnsupportedCanFunction";
    result.errorMessage = QString("Unsupported CAN adapter function: %1").arg(functionName);
    return result;
}

} // namespace PicoATE::Core
