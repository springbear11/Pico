#include "PicoATE/Core/NodeRunner.h"
#include "PicoATE/Core/DataParserModule.h"
#include "PicoATE/Core/ValueToolsModule.h"
#include "PicoATE/Core/InstrumentAdapterModules.h"
#include "PicoATE/Core/ExecutionControl.h"
#include "PicoATE/Core/ExecutionResultStore.h"
#include "PicoATE/Core/RuntimeVariableResolver.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "PicoATE/Core/StopToken.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <limits>

namespace PicoATE::Core {

namespace {

QString normalizedComparison(QString value)
{
    value = value.trimmed().toLower();
    if (value == ">" || value == ">=" || value == "<" || value == "<=" ||
        value == "==" || value == "!=") {
        return value;
    }
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

bool limitValue(const ExecNode& node, const QString& key, QVariant& value)
{
    const auto inputs = node.payload.value("inputs").toMap();
    if (inputs.contains(key)) {
        value = inputs.value(key);
        return true;
    }
    if (node.payload.contains(key)) {
        value = node.payload.value(key);
        return true;
    }
    return false;
}

bool finiteNumber(const QVariant& value, double& number)
{
    if (!value.isValid() || value.isNull() ||
        value.metaType().id() == QMetaType::Bool ||
        value.metaType().id() == QMetaType::QVariantMap ||
        value.metaType().id() == QMetaType::QVariantList) {
        return false;
    }
    bool ok = false;
    number = value.toDouble(&ok);
    return ok && std::isfinite(number);
}

bool isTextValue(const QVariant& value)
{
    const auto typeId = value.metaType().id();
    return typeId == QMetaType::QString || typeId == QMetaType::QByteArray;
}

bool isUnsafeExactFloatingInteger(const QVariant& value)
{
    const auto typeId = value.metaType().id();
    if (typeId != QMetaType::Double && typeId != QMetaType::Float) {
        return false;
    }
    bool ok = false;
    const auto number = value.toDouble(&ok);
    const auto maximumExactInteger = typeId == QMetaType::Float
        ? 16777216.0
        : 9007199254740992.0;
    return ok && std::isfinite(number) && std::trunc(number) == number &&
           std::abs(number) > maximumExactInteger;
}

struct IntegralValue {
    bool isUnsigned = false;
    qint64 signedValue = 0;
    quint64 unsignedValue = 0;
};

bool integralValue(const QVariant& value, IntegralValue& result)
{
    const auto typeId = value.metaType().id();
    switch (typeId) {
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        result.isUnsigned = true;
        result.unsignedValue = value.toULongLong();
        return true;
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
        result.signedValue = value.toLongLong();
        return true;
    case QMetaType::QString:
    case QMetaType::QByteArray: {
        const auto text = value.toString().trimmed();
        bool ok = false;
        const auto signedValue = text.toLongLong(&ok, 10);
        if (ok) {
            result.signedValue = signedValue;
            return true;
        }
        const auto unsignedValue = text.toULongLong(&ok, 10);
        if (ok) {
            result.isUnsigned = true;
            result.unsignedValue = unsignedValue;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

bool integralDistance(const IntegralValue& left,
                      const IntegralValue& right,
                      quint64& distance)
{
    if (left.isUnsigned && right.isUnsigned) {
        distance = left.unsignedValue >= right.unsignedValue
            ? left.unsignedValue - right.unsignedValue
            : right.unsignedValue - left.unsignedValue;
        return true;
    }
    if (!left.isUnsigned && !right.isUnsigned) {
        constexpr auto signBit = quint64{1} << 63;
        const auto orderedLeft = static_cast<quint64>(left.signedValue) ^ signBit;
        const auto orderedRight = static_cast<quint64>(right.signedValue) ^ signBit;
        distance = orderedLeft >= orderedRight
            ? orderedLeft - orderedRight
            : orderedRight - orderedLeft;
        return true;
    }

    const auto& unsignedSide = left.isUnsigned ? left : right;
    const auto& signedSide = left.isUnsigned ? right : left;
    if (signedSide.signedValue >= 0) {
        const auto signedAsUnsigned = static_cast<quint64>(signedSide.signedValue);
        distance = unsignedSide.unsignedValue >= signedAsUnsigned
            ? unsignedSide.unsignedValue - signedAsUnsigned
            : signedAsUnsigned - unsignedSide.unsignedValue;
        return true;
    }

    const auto magnitude = static_cast<quint64>(-(signedSide.signedValue + 1)) + 1;
    if (unsignedSide.unsignedValue >
        std::numeric_limits<quint64>::max() - magnitude) {
        return false;
    }
    distance = unsignedSide.unsignedValue + magnitude;
    return true;
}

bool integralValuesEqual(const QVariant& actual,
                         const QVariant& expected,
                         double tolerance,
                         bool& equal)
{
    IntegralValue actualInteger;
    IntegralValue expectedInteger;
    if (!integralValue(actual, actualInteger) ||
        !integralValue(expected, expectedInteger)) {
        return false;
    }

    quint64 distance = 0;
    if (!integralDistance(actualInteger, expectedInteger, distance)) {
        equal = false;
        return true;
    }
    if (tolerance >= static_cast<double>(std::numeric_limits<quint64>::max())) {
        equal = true;
    } else {
        equal = distance <= static_cast<quint64>(std::floor(tolerance));
    }
    return true;
}

QString logValueText(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QStringLiteral("<unset>");
    }
    const auto json = QJsonValue::fromVariant(value);
    if (json.isObject()) {
        return QString::fromUtf8(QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
    }
    if (json.isArray()) {
        return QString::fromUtf8(QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
    }
    if (json.isBool()) {
        return json.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return value.toString();
}

void publishLimitLog(const NodeExecutionContext& context, const QString& message)
{
    if (!context.logSink) {
        return;
    }
    ModuleLogRecord record;
    record.timestampUtc = QDateTime::currentDateTimeUtc();
    record.message = message;
    context.logSink->publishModuleLog(record);
}

NodeResult runtimeVariableErrorResult(const ExecNode& node,
                                      const QVector<VariableResolutionError>& errors)
{
    NodeResult result;
    result.nodeId = node.id;
    result.outcome = NodeOutcome::Error;
    result.errorCode = "RuntimeVariableResolutionError";
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;

    if (errors.isEmpty()) {
        result.errorMessage = "Runtime variable resolution failed";
    } else {
        const auto& first = errors.first();
        result.errorMessage = first.path.isEmpty()
            ? first.message
            : QString("%1 at %2").arg(first.message, first.path);
    }

    if (node.kind == ExecNodeKind::Limit) {
        MeasurementResult measurement;
        measurement.name = node.payload.value(
            "measurementName", node.displayName).toString();
        measurement.unit = node.payload.value("unit").toString();
        measurement.status = MeasurementStatus::Error;
        measurement.errorCode = result.errorCode;
        measurement.errorMessage = result.errorMessage;
        applyConfiguredMeasurementLimits(node.payload, measurement);
        result.measurements.push_back(std::move(measurement));
    }
    return result;
}

NodeResult limitErrorResult(const ExecNode& node,
                            const QVariant& actual,
                            const QString& code,
                            const QString& message)
{
    QString effectiveCode = code;
    QString effectiveMessage = message;
    if (node.kind == ExecNodeKind::Break) {
        effectiveCode.replace(QStringLiteral("Limit"),
                              QStringLiteral("Break"));
        effectiveMessage.replace(QStringLiteral("Limit input"),
                                 QStringLiteral("Break input"));
    }

    NodeResult result;
    result.nodeId = node.id;
    result.outcome = NodeOutcome::Error;
    result.errorCode = effectiveCode;
    result.errorMessage = effectiveMessage;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;

    if (node.kind == ExecNodeKind::Limit) {
        MeasurementResult measurement;
        measurement.name = node.payload.value("measurementName", node.displayName).toString();
        measurement.value = actual;
        measurement.rawValue = actual;
        measurement.unit = node.payload.value("unit").toString();
        measurement.status = MeasurementStatus::Error;
        measurement.errorCode = effectiveCode;
        measurement.errorMessage = effectiveMessage;
        applyConfiguredMeasurementLimits(node.payload, measurement);
        result.measurements.push_back(measurement);
    }
    return result;
}

class DeferredControlNodeHandler final : public INodeHandler {
public:
    bool canHandle(const ExecNode& node) const override
    {
        return node.kind == ExecNodeKind::Statement ||
               node.kind == ExecNodeKind::SequenceCall;
    }

    NodeResult run(const ExecNode& node, const NodeExecutionContext&) override
    {
        NodeResult result;
        result.nodeId = node.id;
        result.outcome = NodeOutcome::Error;
        result.startedAt = QDateTime::currentDateTimeUtc();
        if (node.kind == ExecNodeKind::Statement) {
            result.errorCode = "StatementNotImplemented";
            result.errorMessage = "Statement execution is not implemented";
        } else {
            result.errorCode = "SequenceCallNotImplemented";
            result.errorMessage = "SequenceCall execution is not implemented";
        }
        result.finishedAt = QDateTime::currentDateTimeUtc();
        return result;
    }
};

} // namespace

NodeRunner::NodeRunner()
{
    registerModule(std::make_shared<MockActionModule>("mock.action"));
    registerModule(std::make_shared<MockActionModule>("mock.measurement"));
    registerModule(std::make_shared<DataParserModule>());
    registerModule(std::make_shared<ValueToolsModule>());
    registerModule(std::make_shared<ExampleDmmAdapterModule>());
    registerModule(std::make_shared<ExampleCanAdapterModule>());
    registerHandler(std::make_shared<NoopNodeHandler>());
    registerHandler(std::make_shared<WaitNodeHandler>());
    registerHandler(std::make_shared<LimitNodeHandler>());
    registerHandler(std::make_shared<BreakNodeHandler>());
    registerHandler(std::make_shared<CounterNodeHandler>());
    registerHandler(std::make_shared<AggregateNodeHandler>());
    registerHandler(std::make_shared<OperatorPromptNodeHandler>());
    registerHandler(std::make_shared<DeferredControlNodeHandler>());
    registerHandler(std::make_shared<ActionNodeHandler>(m_modules));
}

void NodeRunner::registerHandler(std::shared_ptr<INodeHandler> handler)
{
    m_handlers.push_back(std::move(handler));
}

bool NodeRunner::registerModule(std::shared_ptr<IModule> module)
{
    return m_modules.registerModule(std::move(module));
}

void NodeRunner::setRuntimeServices(IModuleRuntimeServices* services)
{
    m_runtimeServices = services;
}

const ModuleRegistry& NodeRunner::modules() const
{
    return m_modules;
}

NodeResult NodeRunner::run(const ExecNode& node, const NodeExecutionContext& context)
{
    RuntimeVariableContext variableContext;
    variableContext.uutId = context.uutId;
    variableContext.frameId = context.frameId;
    variableContext.attemptId = context.attemptId;
    variableContext.requestId = context.requestId;
    variableContext.currentNodeId = node.id;
    variableContext.attemptIndex = context.attemptIndex;
    variableContext.periodicInvocation = context.periodicInvocation;
    variableContext.periodicIndex = context.periodicIndex;
    variableContext.periodicCounter = context.periodicCounter;
    variableContext.variables = context.variables;
    variableContext.resultStore = context.resultStore;

    RuntimeVariableResolver resolver(variableContext);
    QVector<VariableResolutionError> errors;
    ExecNode resolvedNode = node;
    resolvedNode.payload = resolver.resolveMap(node.payload, errors, QString("%1.payload").arg(node.id));
    if (!errors.isEmpty()) {
        return runtimeVariableErrorResult(node, errors);
    }

    auto resolvedContext = context;
    if (!resolvedContext.runtimeServices) {
        resolvedContext.runtimeServices = m_runtimeServices;
    }

    for (const auto& handler : m_handlers) {
        if (handler->canHandle(resolvedNode)) {
            return handler->run(resolvedNode, resolvedContext);
        }
    }

    NodeResult result;
    result.nodeId = node.id;
    result.outcome = NodeOutcome::Error;
    result.errorMessage = QString("No handler for node kind %1").arg(static_cast<int>(node.kind));
    return result;
}

bool NoopNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Noop ||
           (node.kind == ExecNodeKind::Cleanup && !node.payload.contains("moduleId"));
}

NodeResult NoopNodeHandler::run(const ExecNode& node, const NodeExecutionContext&)
{
    NodeResult result;
    result.nodeId = node.id;
    result.outcome = NodeOutcome::Passed;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;
    return result;
}

bool WaitNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Wait;
}

NodeResult WaitNodeHandler::run(const ExecNode& node, const NodeExecutionContext&)
{
    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.outcome = NodeOutcome::Error;
    result.errorCode = QStringLiteral("WaitRequiresScheduler");
    result.errorMessage = QStringLiteral(
        "Wait nodes must be dispatched by ExecutionGraphScheduler");
    result.finishedAt = QDateTime::currentDateTimeUtc();
    return result;
}

bool OperatorPromptNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::OperatorPrompt;
}

NodeResult OperatorPromptNodeHandler::run(const ExecNode& node,
                                          const NodeExecutionContext& context)
{
    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();

    if (!context.executionControl || !context.stopToken || !context.runtimeEvents ||
        !context.runtimeEvents->hasSink()) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "OperatorPromptResponderUnavailable";
        result.errorMessage = "Operator prompt requires an interactive runtime event responder";
        result.finishedAt = QDateTime::currentDateTimeUtc();
        return result;
    }

    auto& controller = context.executionControl->operatorPrompts();
    if (!controller.responderAvailable()) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "OperatorPromptResponderUnavailable";
        result.errorMessage = "No operator prompt responder is available";
        result.finishedAt = QDateTime::currentDateTimeUtc();
        return result;
    }

    const auto mode = operatorPromptModeFromName(node.payload.value("mode", "confirm").toString());
    const QString instanceId = context.requestId + QStringLiteral(":operator-prompt");
    if (!controller.registerPrompt(instanceId)) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "OperatorPromptRegistrationFailed";
        result.errorMessage = "Unable to register the operator prompt";
        result.finishedAt = QDateTime::currentDateTimeUtc();
        return result;
    }

    QVariantMap promptDetails = node.payload;
    promptDetails.insert("promptInstanceId", instanceId);
    promptDetails.insert("mode", operatorPromptModeName(mode));

    RuntimeEvent requested;
    requested.kind = RuntimeEventKind::OperatorPromptRequested;
    requested.uutId = context.uutId;
    requested.nodeId = node.id;
    requested.nodeDisplayName = node.displayName;
    requested.nodeKind = node.kind;
    requested.attemptId = context.attemptId;
    requested.attemptIndex = context.attemptIndex;
    requested.frameId = context.frameId;
    requested.message = node.payload.value("message").toString();
    requested.details = promptDetails;
    context.runtimeEvents->publish(requested);

    result.outcome = NodeOutcome::Unknown;
    result.outputs = promptDetails;
    return result;
}

bool LimitNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Limit;
}

NodeResult LimitNodeHandler::run(const ExecNode& node,
                                 const NodeExecutionContext& context)
{
    const bool controlPredicate = node.kind == ExecNodeKind::Break;
    QVariant actual;
    if (!limitValue(node, "actual", actual)) {
        return limitErrorResult(node, {}, "LimitActualMissing", "Limit input 'actual' is required");
    }

    const auto comparison = normalizedComparison(
        node.payload.value("comparison", "between").toString());
    QVariant lowerValue;
    QVariant upperValue;
    QVariant expectedValue;
    const bool hasLower = limitValue(node, "lower", lowerValue) ||
                          limitValue(node, "lowerLimit", lowerValue);
    const bool hasUpper = limitValue(node, "upper", upperValue) ||
                          limitValue(node, "upperLimit", upperValue);
    const bool hasExpected = limitValue(node, "expected", expectedValue);

    QVariant toleranceValue;
    double tolerance = 0.0;
    const bool hasTolerance = limitValue(node, "tolerance", toleranceValue);
    publishLimitLog(
        context,
        QStringLiteral("%1_CHECK actual=%2 comparison=%3 expected=%4 lower=%5 upper=%6 tolerance=%7")
            .arg(controlPredicate ? QStringLiteral("BREAK")
                                  : QStringLiteral("LIMIT"),
                 logValueText(actual),
                 comparison,
                 hasExpected ? logValueText(expectedValue) : QStringLiteral("<unset>"),
                 hasLower ? logValueText(lowerValue) : QStringLiteral("<unset>"),
                 hasUpper ? logValueText(upperValue) : QStringLiteral("<unset>"),
                 hasTolerance ? logValueText(toleranceValue) : QStringLiteral("<unset>")));
    if (hasTolerance) {
        if (!finiteNumber(toleranceValue, tolerance) || tolerance < 0.0) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Limit tolerance must be a finite non-negative number");
        }
    }

    bool passed = false;
    QString comparisonMode = QStringLiteral("numeric");
    double actualNumber = 0.0;
    double lower = 0.0;
    double upper = 0.0;
    double expected = 0.0;
    const bool inclusive = node.payload.value("inclusive", true).toBool();

    if (comparison == "between" || comparison == "range") {
        if (hasLower != hasUpper) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Between comparison requires both lower and upper when either is set");
        }
        if (!hasLower && (!hasExpected || !hasTolerance)) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Between comparison requires lower/upper or expected/tolerance");
        }
        if (!finiteNumber(actual, actualNumber)) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitTypeError",
                                    "Between comparison requires a finite numeric actual value");
        }
        if (hasLower) {
            if (!finiteNumber(lowerValue, lower) || !finiteNumber(upperValue, upper)) {
                return limitErrorResult(node,
                                        actual,
                                        "LimitTypeError",
                                        "Lower and upper must be finite numbers");
            }
        } else {
            if (!finiteNumber(expectedValue, expected)) {
                return limitErrorResult(node,
                                        actual,
                                        "LimitTypeError",
                                        "Expected must be a finite number when deriving limits");
            }
            lower = expected - tolerance;
            upper = expected + tolerance;
            if (!std::isfinite(lower) || !std::isfinite(upper)) {
                return limitErrorResult(node,
                                        actual,
                                        "LimitConfigurationError",
                                        "Derived lower and upper limits must be finite numbers");
            }
        }
        if (lower > upper) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Lower limit must not be greater than upper limit");
        }
        passed = inclusive
            ? actualNumber >= lower && actualNumber <= upper
            : actualNumber > lower && actualNumber < upper;
    } else if (comparison == ">" || comparison == "gt" || comparison == "greaterthan" ||
               comparison == ">=" || comparison == "ge" || comparison == "gte" || comparison == "greaterorequal" ||
               comparison == "<" || comparison == "lt" || comparison == "lessthan" ||
               comparison == "<=" || comparison == "le" || comparison == "lte" || comparison == "lessorequal") {
        const bool usesLower = comparison == ">" || comparison == "gt" || comparison == "greaterthan" ||
                               comparison == ">=" || comparison == "ge" || comparison == "gte" || comparison == "greaterorequal";
        const QVariant thresholdValue = hasExpected
            ? expectedValue
            : (usesLower ? lowerValue : upperValue);
        const bool hasThreshold = hasExpected || (usesLower ? hasLower : hasUpper);
        if (!hasThreshold || !finiteNumber(actual, actualNumber) ||
            !finiteNumber(thresholdValue, expected)) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitTypeError",
                                    "Numeric comparison requires finite actual and threshold values");
        }
        if (comparison == ">" || comparison == "gt" || comparison == "greaterthan") passed = actualNumber > expected;
        else if (comparison == ">=" || comparison == "ge" || comparison == "gte" || comparison == "greaterorequal") passed = actualNumber >= expected;
        else if (comparison == "<" || comparison == "lt" || comparison == "lessthan") passed = actualNumber < expected;
        else passed = actualNumber <= expected;
    } else if (comparison == "==" || comparison == "eq" || comparison == "equal" ||
               comparison == "!=" || comparison == "ne" || comparison == "notequal") {
        if (!hasExpected) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Equal comparison requires expected");
        }
        if (tolerance == 0.0 &&
            (isUnsafeExactFloatingInteger(actual) ||
             isUnsafeExactFloatingInteger(expectedValue))) {
            return limitErrorResult(
                node,
                actual,
                "LimitPrecisionError",
                "Exact equality cannot safely compare a floating-point value beyond its exact integer range; use a string identifier or an integer value");
        }
        double expectedNumber = 0.0;
        if (isTextValue(actual) && isTextValue(expectedValue) && tolerance == 0.0) {
            comparisonMode = QStringLiteral("text");
            passed = actual.toString() == expectedValue.toString();
        } else if (integralValuesEqual(actual, expectedValue, tolerance, passed)) {
            comparisonMode = QStringLiteral("integer");
        } else if (finiteNumber(actual, actualNumber) &&
                   finiteNumber(expectedValue, expectedNumber)) {
            passed = std::abs(actualNumber - expectedNumber) <= tolerance;
        } else {
            comparisonMode = QStringLiteral("text");
            passed = actual.toString() == expectedValue.toString();
        }
        if (comparison == "!=" || comparison == "ne" || comparison == "notequal") {
            passed = !passed;
        }
        expected = expectedNumber;
    } else if (comparison == "contains" || comparison == "startswith" || comparison == "endswith") {
        comparisonMode = QStringLiteral("text");
        if (!hasExpected) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "String comparison requires expected");
        }
        const auto actualText = actual.toString();
        const auto expectedText = expectedValue.toString();
        if (comparison == "contains") passed = actualText.contains(expectedText);
        else if (comparison == "startswith") passed = actualText.startsWith(expectedText);
        else passed = actualText.endsWith(expectedText);
    } else if (comparison == "istrue" || comparison == "isfalse") {
        comparisonMode = QStringLiteral("boolean");
        if (actual.metaType().id() != QMetaType::Bool) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitTypeError",
                                    "Boolean comparison requires a bool actual value");
        }
        passed = comparison == "istrue" ? actual.toBool() : !actual.toBool();
    } else {
        return limitErrorResult(node,
                                actual,
                                "UnsupportedLimitComparison",
                                QString("Unsupported limit comparison: %1").arg(comparison));
    }

    NodeResult result;
    result.nodeId = node.id;
    result.outcome = controlPredicate || passed
        ? NodeOutcome::Passed
        : NodeOutcome::Failed;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;
    result.outputs.insert("actual", actual);
    result.outputs.insert("passed", passed);
    result.outputs.insert("matched", passed);
    result.outputs.insert("comparison", comparison);
    result.outputs.insert("comparisonMode", comparisonMode);

    MeasurementResult measurement;
    measurement.name = node.payload.value("measurementName", node.displayName).toString();
    measurement.value = actual;
    measurement.rawValue = actual;
    measurement.unit = node.payload.value("unit").toString();
    measurement.status = passed ? MeasurementStatus::Passed : MeasurementStatus::Failed;
    applyConfiguredMeasurementLimits(node.payload, measurement);
    measurement.attributes.insert("comparisonMode", comparisonMode);
    if (!passed && !controlPredicate) {
        result.errorCode = "LimitFailed";
        result.errorMessage = QString("Measurement %1 failed %2 comparison")
                                  .arg(measurement.name, comparison);
        measurement.errorCode = result.errorCode;
        measurement.errorMessage = result.errorMessage;
    }
    if (!controlPredicate) {
        result.measurements.push_back(measurement);
    }
    const auto effectiveLower = measurement.hasLowerLimit
        ? QString::number(measurement.lowerLimit, 'g', 15)
        : logValueText(measurement.attributes.value("expected"));
    const auto effectiveUpper = measurement.hasUpperLimit
        ? QString::number(measurement.upperLimit, 'g', 15)
        : logValueText(measurement.attributes.value("expected"));
    publishLimitLog(
        context,
        QStringLiteral("%1_RESULT %2 actual=%3 comparison=%4 mode=%5 lower=%6 upper=%7")
            .arg(controlPredicate ? QStringLiteral("BREAK")
                                  : QStringLiteral("LIMIT"),
                  controlPredicate
                      ? (passed ? QStringLiteral("MATCHED")
                                : QStringLiteral("NOT_MATCHED"))
                      : (passed ? QStringLiteral("PASS")
                                : QStringLiteral("FAIL")),
                  logValueText(actual),
                  comparison,
                  comparisonMode,
                  effectiveLower,
                  effectiveUpper));
    return result;
}

bool BreakNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Break;
}

NodeResult BreakNodeHandler::run(const ExecNode& node,
                                 const NodeExecutionContext& context)
{
    auto evaluated = LimitNodeHandler().run(node, context);
    if (evaluated.outcome == NodeOutcome::Error ||
        evaluated.outcome == NodeOutcome::Timeout ||
        evaluated.outcome == NodeOutcome::Cancelled) {
        return evaluated;
    }

    const bool matched = evaluated.outputs.value("matched").toBool();
    evaluated.outputs.insert("breakRequested", matched);
    return evaluated;
}

bool CounterNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Counter;
}

NodeResult CounterNodeHandler::run(const ExecNode& node,
                                   const NodeExecutionContext& context)
{
    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;

    QVariant conditionValue = true;
    limitValue(node, "condition", conditionValue);
    bool condition = false;
    if (conditionValue.metaType().id() == QMetaType::Bool) {
        condition = conditionValue.toBool();
    } else {
        const auto text = conditionValue.toString().trimmed().toLower();
        if (text == "true" || text == "yes" || text == "1" || text == "passed") {
            condition = true;
        } else if (text == "false" || text == "no" || text == "0" ||
                   text == "failed" || text.isEmpty()) {
            condition = false;
        } else {
            result.outcome = NodeOutcome::Error;
            result.errorCode = "CounterConditionNotBoolean";
            result.errorMessage = "Counter condition must resolve to a boolean value";
            return result;
        }
    }

    double start = 0.0;
    double increment = 1.0;
    QVariant configured;
    if (limitValue(node, "start", configured) && !finiteNumber(configured, start)) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "CounterStartNotNumeric";
        result.errorMessage = "Counter start must be numeric";
        return result;
    }
    if (limitValue(node, "increment", configured) && !finiteNumber(configured, increment)) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "CounterIncrementNotNumeric";
        result.errorMessage = "Counter increment must be numeric";
        return result;
    }

    const auto mode = node.payload.value("mode", "consecutive").toString()
                          .trimmed().toLower();
    if (mode != "consecutive" && mode != "total") {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "CounterModeUnsupported";
        result.errorMessage = "Counter mode must be consecutive or total";
        return result;
    }

    double previous = start;
    const bool firstLoopIteration = context.variables.value("loop.number", 1).toInt() <= 1;
    if (!firstLoopIteration && context.resultStore) {
        const auto stored = context.resultStore->latest(context.uutId, context.frameId, node.id);
        if (stored) {
            previous = stored->result.outputs.value("value", start).toDouble();
        }
    }

    double value = previous;
    if (condition) {
        value += increment;
    } else if (mode == "consecutive") {
        value = start;
    }

    result.outcome = NodeOutcome::Passed;
    result.outputs.insert("value", value);
    result.outputs.insert("condition", condition);
    result.outputs.insert("mode", mode);
    return result;
}

bool AggregateNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Aggregate;
}

NodeResult AggregateNodeHandler::run(const ExecNode& node,
                                     const NodeExecutionContext& context)
{
    QVariant sample;
    if (!limitValue(node, "value", sample)) {
        return limitErrorResult(node, {}, "AggregateValueMissing",
                                "Aggregate input 'value' is required");
    }
    double numeric = 0.0;
    if (!finiteNumber(sample, numeric)) {
        return limitErrorResult(node, sample, "AggregateValueNotNumeric",
                                "Aggregate value must resolve to a finite number");
    }

    int count = 0;
    double sum = 0.0;
    double minimum = numeric;
    double maximum = numeric;
    const bool firstLoopIteration = context.variables.value("loop.number", 1).toInt() <= 1;
    if (!firstLoopIteration && context.resultStore) {
        const auto stored = context.resultStore->latest(context.uutId, context.frameId, node.id);
        if (stored) {
            const auto& outputs = stored->result.outputs;
            count = outputs.value("count").toInt();
            sum = outputs.value("sum").toDouble();
            minimum = outputs.value("minimum", numeric).toDouble();
            maximum = outputs.value("maximum", numeric).toDouble();
        }
    }

    ++count;
    sum += numeric;
    minimum = qMin(minimum, numeric);
    maximum = qMax(maximum, numeric);

    NodeResult result;
    result.nodeId = node.id;
    result.outcome = NodeOutcome::Passed;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;
    result.outputs.insert("last", numeric);
    result.outputs.insert("count", count);
    result.outputs.insert("sum", sum);
    result.outputs.insert("minimum", minimum);
    result.outputs.insert("maximum", maximum);
    result.outputs.insert("average", sum / count);
    return result;
}

ActionNodeHandler::ActionNodeHandler(ModuleRegistry& modules)
    : m_modules(modules)
{
}

bool ActionNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Action ||
           (node.kind == ExecNodeKind::Cleanup && node.payload.contains("moduleId"));
}

NodeResult ActionNodeHandler::run(const ExecNode& node, const NodeExecutionContext& context)
{
    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();

    const auto moduleId = node.payload.value("moduleId", "mock.action").toString();
    const auto module = m_modules.module(moduleId);
    if (!module) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "ModuleNotFound";
        result.errorMessage = QString("Module not found: %1").arg(moduleId);
        result.finishedAt = QDateTime::currentDateTimeUtc();
        return result;
    }

    ModuleExecutionContext moduleContext;
    moduleContext.uutId = context.uutId;
    moduleContext.frameId = context.frameId;
    moduleContext.attemptId = context.attemptId;
    moduleContext.requestId = context.requestId;
    moduleContext.attemptIndex = context.attemptIndex;
    moduleContext.variables = context.variables;
    moduleContext.parameters = node.payload;
    moduleContext.inputs = node.payload.value("inputs").toMap();
    moduleContext.runtimeServices = context.runtimeServices;
    moduleContext.logSink = context.logSink;

    const auto functionName = node.payload.value("function").toString();
    const auto moduleResult = module->execute(functionName, moduleContext);

    result.outcome = toNodeOutcome(moduleResult.outcome);
    result.outputs = moduleResult.outputs;
    if (!moduleResult.measurements.isEmpty()) {
        result.measurements = moduleResult.measurements;
        result.outputs.insert("measurements", measurementsToVariant(moduleResult.measurements));
    }
    result.errorCode = moduleResult.errorCode;
    result.errorMessage = moduleResult.errorMessage;
    result.finishedAt = QDateTime::currentDateTimeUtc();
    return result;
}

} // namespace PicoATE::Core
