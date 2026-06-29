#include "PicoATE/Core/NodeRunner.h"
#include "PicoATE/Core/InstrumentAdapterModules.h"
#include "PicoATE/Core/RuntimeVariableResolver.h"

#include <QThread>

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

} // namespace

NodeRunner::NodeRunner()
{
    registerModule(std::make_shared<MockActionModule>("mock.action"));
    registerModule(std::make_shared<MockActionModule>("mock.measurement"));
    registerModule(std::make_shared<ExampleDmmAdapterModule>());
    registerModule(std::make_shared<ExampleCanAdapterModule>());
    registerHandler(std::make_shared<NoopNodeHandler>());
    registerHandler(std::make_shared<WaitNodeHandler>());
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
    variableContext.attemptIndex = context.attemptIndex;
    variableContext.variables = context.variables;

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
