#include "PicoATE/Core/NodeRunner.h"
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
#include <QThread>

#include <cmath>

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

void applyConfiguredLimits(const ExecNode& node, MeasurementResult& measurement)
{
    const auto comparison = normalizedComparison(
        node.payload.value("comparison", "between").toString());
    measurement.attributes.insert("comparison", comparison);
    measurement.attributes.insert(
        "inclusive", node.payload.value("inclusive", true).toBool());

    QVariant lowerValue;
    QVariant upperValue;
    QVariant expectedValue;
    QVariant toleranceValue;
    const bool hasLower = limitValue(node, "lower", lowerValue) ||
                          limitValue(node, "lowerLimit", lowerValue);
    const bool hasUpper = limitValue(node, "upper", upperValue) ||
                          limitValue(node, "upperLimit", upperValue);
    const bool hasExpected = limitValue(node, "expected", expectedValue);
    const bool hasTolerance = limitValue(node, "tolerance", toleranceValue);

    double lower = 0.0;
    double upper = 0.0;
    double expected = 0.0;
    double tolerance = 0.0;
    if (hasExpected) {
        measurement.attributes.insert("expected", expectedValue);
    }
    if (hasTolerance) {
        measurement.attributes.insert("tolerance", toleranceValue);
    }

    if ((comparison == "between" || comparison == "range") &&
        hasLower && hasUpper &&
        finiteNumber(lowerValue, lower) && finiteNumber(upperValue, upper)) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = lower;
        measurement.hasUpperLimit = true;
        measurement.upperLimit = upper;
        return;
    }
    if ((comparison == "between" || comparison == "range") &&
        hasExpected && hasTolerance &&
        finiteNumber(expectedValue, expected) && finiteNumber(toleranceValue, tolerance)) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = expected - tolerance;
        measurement.hasUpperLimit = true;
        measurement.upperLimit = expected + tolerance;
        return;
    }

    const bool equality = comparison == "==" || comparison == "eq" ||
        comparison == "equal" || comparison == "!=" || comparison == "ne" ||
        comparison == "notequal";
    if (equality && hasExpected && finiteNumber(expectedValue, expected) &&
        (!hasTolerance || finiteNumber(toleranceValue, tolerance))) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = expected - tolerance;
        measurement.hasUpperLimit = true;
        measurement.upperLimit = expected + tolerance;
        return;
    }

    const bool lowerBound = comparison == ">" || comparison == "gt" ||
        comparison == "greaterthan" || comparison == ">=" || comparison == "ge" ||
        comparison == "gte" || comparison == "greaterorequal";
    const bool upperBound = comparison == "<" || comparison == "lt" ||
        comparison == "lessthan" || comparison == "<=" || comparison == "le" ||
        comparison == "lte" || comparison == "lessorequal";
    const auto thresholdValue = hasExpected
        ? expectedValue
        : (lowerBound ? lowerValue : upperValue);
    if ((lowerBound || upperBound) && finiteNumber(thresholdValue, expected)) {
        measurement.hasLowerLimit = lowerBound;
        measurement.lowerLimit = expected;
        measurement.hasUpperLimit = upperBound;
        measurement.upperLimit = expected;
    }
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
        applyConfiguredLimits(node, measurement);
        result.measurements.push_back(std::move(measurement));
    }
    return result;
}

NodeResult limitErrorResult(const ExecNode& node,
                            const QVariant& actual,
                            const QString& code,
                            const QString& message)
{
    NodeResult result;
    result.nodeId = node.id;
    result.outcome = NodeOutcome::Error;
    result.errorCode = code;
    result.errorMessage = message;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;

    MeasurementResult measurement;
    measurement.name = node.payload.value("measurementName", node.displayName).toString();
    measurement.value = actual;
    measurement.rawValue = actual;
    measurement.unit = node.payload.value("unit").toString();
    measurement.status = MeasurementStatus::Error;
    measurement.errorCode = code;
    measurement.errorMessage = message;
    result.measurements.push_back(measurement);
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
    variableContext.currentNodeId = node.id;
    variableContext.attemptIndex = context.attemptIndex;
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
    const int waitMs = node.payload.value("ms", 0).toInt();
    if (waitMs > 0) {
        QThread::msleep(static_cast<unsigned long>(waitMs));
    }
    result.outcome = NodeOutcome::Passed;
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
    const QString instanceId = context.attemptId + ":operator-prompt";
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

    const auto acceptedResponse = mode == OperatorPromptMode::Notice
        ? OperatorPromptResponse::Shown
        : OperatorPromptResponse::Confirmed;
    int timeoutMs = node.payload.value("timeoutMs", 60000).toInt();
    if (mode == OperatorPromptMode::Notice) {
        timeoutMs = timeoutMs > 0 ? qMin(timeoutMs, 5000) : 5000;
    }
    const auto waitStatus = controller.waitForResponse(instanceId,
                                                       acceptedResponse,
                                                       timeoutMs,
                                                       *context.stopToken);
    switch (waitStatus) {
    case OperatorPromptWaitStatus::Accepted:
        result.outcome = NodeOutcome::Passed;
        result.outputs = promptDetails;
        break;
    case OperatorPromptWaitStatus::Timeout:
        result.outcome = NodeOutcome::Timeout;
        result.errorCode = "OperatorPromptTimeout";
        result.errorMessage = "Operator prompt timed out";
        break;
    case OperatorPromptWaitStatus::Cancelled:
        result.outcome = NodeOutcome::Cancelled;
        result.errorCode = "OperatorPromptCancelled";
        result.errorMessage = "Operator prompt was cancelled";
        break;
    case OperatorPromptWaitStatus::Unavailable:
        result.outcome = NodeOutcome::Error;
        result.errorCode = "OperatorPromptResponderUnavailable";
        result.errorMessage = "Operator prompt responder became unavailable";
        break;
    }

    if (mode == OperatorPromptMode::Confirm || waitStatus != OperatorPromptWaitStatus::Accepted) {
        RuntimeEvent closed = requested;
        closed.kind = RuntimeEventKind::OperatorPromptClosed;
        closed.outcome = result.outcome;
        closed.message = result.outcome == NodeOutcome::Passed
            ? QStringLiteral("operator confirmed")
            : result.errorMessage;
        closed.details.insert("reason",
                              result.outcome == NodeOutcome::Passed
                                  ? QStringLiteral("confirmed")
                                  : QStringLiteral("cancelled"));
        context.runtimeEvents->publish(closed);
    }

    result.finishedAt = QDateTime::currentDateTimeUtc();
    return result;
}

bool LimitNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Limit;
}

NodeResult LimitNodeHandler::run(const ExecNode& node,
                                 const NodeExecutionContext& context)
{
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
        QStringLiteral("LIMIT_CHECK actual=%1 comparison=%2 expected=%3 lower=%4 upper=%5 tolerance=%6")
            .arg(logValueText(actual),
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
    double actualNumber = 0.0;
    double lower = 0.0;
    double upper = 0.0;
    double expected = 0.0;
    bool numericMeasurement = false;
    bool derivedRange = false;
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
            derivedRange = true;
        }
        if (lower > upper) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Lower limit must not be greater than upper limit");
        }
        numericMeasurement = true;
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
        numericMeasurement = true;
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
        double expectedNumber = 0.0;
        if (finiteNumber(actual, actualNumber) && finiteNumber(expectedValue, expectedNumber)) {
            numericMeasurement = true;
            passed = std::abs(actualNumber - expectedNumber) <= tolerance;
        } else {
            passed = actual.toString() == expectedValue.toString();
        }
        if (comparison == "!=" || comparison == "ne" || comparison == "notequal") {
            passed = !passed;
        }
        expected = expectedNumber;
    } else if (comparison == "contains" || comparison == "startswith" || comparison == "endswith") {
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
    result.outcome = passed ? NodeOutcome::Passed : NodeOutcome::Failed;
    result.startedAt = QDateTime::currentDateTimeUtc();
    result.finishedAt = result.startedAt;
    result.outputs.insert("actual", actual);
    result.outputs.insert("passed", passed);
    result.outputs.insert("comparison", comparison);

    MeasurementResult measurement;
    measurement.name = node.payload.value("measurementName", node.displayName).toString();
    measurement.value = actual;
    measurement.rawValue = actual;
    measurement.unit = node.payload.value("unit").toString();
    measurement.status = passed ? MeasurementStatus::Passed : MeasurementStatus::Failed;
    measurement.attributes.insert("comparison", comparison);
    measurement.attributes.insert("inclusive", inclusive);
    if (numericMeasurement && (comparison == "between" || comparison == "range")) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = lower;
        measurement.hasUpperLimit = true;
        measurement.upperLimit = upper;
        if (derivedRange) {
            measurement.attributes.insert("expected", expected);
            measurement.attributes.insert("tolerance", tolerance);
            measurement.attributes.insert("limitsDerived", true);
        }
    } else if (numericMeasurement &&
               (comparison == ">" || comparison == "gt" || comparison == "greaterthan" ||
                comparison == ">=" || comparison == "ge" || comparison == "gte" ||
                comparison == "greaterorequal")) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = expected;
    } else if (numericMeasurement &&
               (comparison == "<" || comparison == "lt" || comparison == "lessthan" ||
                comparison == "<=" || comparison == "le" || comparison == "lte" ||
                comparison == "lessorequal")) {
        measurement.hasUpperLimit = true;
        measurement.upperLimit = expected;
    } else if (numericMeasurement &&
               (comparison == "==" || comparison == "eq" || comparison == "equal" ||
                comparison == "!=" || comparison == "ne" || comparison == "notequal")) {
        measurement.hasLowerLimit = true;
        measurement.lowerLimit = expected - tolerance;
        measurement.hasUpperLimit = true;
        measurement.upperLimit = expected + tolerance;
        measurement.attributes.insert("expected", expectedValue);
        measurement.attributes.insert("tolerance", tolerance);
        measurement.attributes.insert("limitsDerived", true);
    } else if (hasExpected) {
        measurement.attributes.insert("expected", expectedValue);
        if (hasTolerance) {
            measurement.attributes.insert("tolerance", tolerance);
        }
    }
    if (!passed) {
        result.errorCode = "LimitFailed";
        result.errorMessage = QString("Measurement %1 failed %2 comparison")
                                  .arg(measurement.name, comparison);
        measurement.errorCode = result.errorCode;
        measurement.errorMessage = result.errorMessage;
    }
    result.measurements.push_back(measurement);
    const auto effectiveLower = measurement.hasLowerLimit
        ? QString::number(measurement.lowerLimit, 'g', 15)
        : logValueText(measurement.attributes.value("expected"));
    const auto effectiveUpper = measurement.hasUpperLimit
        ? QString::number(measurement.upperLimit, 'g', 15)
        : logValueText(measurement.attributes.value("expected"));
    publishLimitLog(
        context,
        QStringLiteral("LIMIT_RESULT %1 actual=%2 comparison=%3 lower=%4 upper=%5")
            .arg(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                 logValueText(actual),
                 comparison,
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
    ExecNode predicate = node;
    predicate.kind = ExecNodeKind::Limit;
    auto evaluated = LimitNodeHandler().run(predicate, context);
    if (evaluated.outcome == NodeOutcome::Error ||
        evaluated.outcome == NodeOutcome::Timeout ||
        evaluated.outcome == NodeOutcome::Cancelled) {
        return evaluated;
    }

    const bool matched = evaluated.outcome == NodeOutcome::Passed;
    evaluated.outcome = NodeOutcome::Passed;
    evaluated.errorCode.clear();
    evaluated.errorMessage.clear();
    evaluated.measurements.clear();
    evaluated.outputs.insert("breakRequested", matched);
    evaluated.outputs.insert("matched", matched);
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
