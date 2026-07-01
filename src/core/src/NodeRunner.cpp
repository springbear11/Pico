#include "PicoATE/Core/NodeRunner.h"
#include "PicoATE/Core/InstrumentAdapterModules.h"
#include "PicoATE/Core/RuntimeVariableResolver.h"

#include <QThread>

#include <cmath>

namespace PicoATE::Core {

namespace {

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
        return result;
    }

    const auto& first = errors.first();
    result.errorMessage = first.path.isEmpty()
        ? first.message
        : QString("%1 at %2").arg(first.message, first.path);
    return result;
}

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

bool LimitNodeHandler::canHandle(const ExecNode& node) const
{
    return node.kind == ExecNodeKind::Limit;
}

NodeResult LimitNodeHandler::run(const ExecNode& node, const NodeExecutionContext&)
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
    if (limitValue(node, "tolerance", toleranceValue)) {
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
    const bool inclusive = node.payload.value("inclusive", true).toBool();

    if (comparison == "between" || comparison == "range") {
        if (!hasLower || !hasUpper) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitConfigurationError",
                                    "Between comparison requires lower and upper limits");
        }
        if (!finiteNumber(actual, actualNumber) ||
            !finiteNumber(lowerValue, lower) ||
            !finiteNumber(upperValue, upper)) {
            return limitErrorResult(node,
                                    actual,
                                    "LimitTypeError",
                                    "Actual, lower, and upper must be finite numbers");
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
    } else if (numericMeasurement && hasExpected) {
        measurement.attributes.insert("expected", expectedValue);
        measurement.attributes.insert("tolerance", tolerance);
    }
    if (!passed) {
        result.errorCode = "LimitFailed";
        result.errorMessage = QString("Measurement %1 failed %2 comparison")
                                  .arg(measurement.name, comparison);
        measurement.errorCode = result.errorCode;
        measurement.errorMessage = result.errorMessage;
    }
    result.measurements.push_back(measurement);
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
