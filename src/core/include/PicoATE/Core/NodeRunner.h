#pragma once

#include "PicoATE/Core/ModuleRuntime.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <memory>

namespace PicoATE::Core {

struct NodeExecutionContext {
    UutId uutId;
    FrameId frameId;
    AttemptId attemptId;
    int attemptIndex = 0;
    QVariantMap variables;
    IModuleRuntimeServices* runtimeServices = nullptr;
};

class INodeHandler {
public:
    virtual ~INodeHandler() = default;
    virtual bool canHandle(const ExecNode& node) const = 0;
    virtual NodeResult run(const ExecNode& node, const NodeExecutionContext& context) = 0;
};

class NodeRunner {
public:
    NodeRunner();

    void registerHandler(std::shared_ptr<INodeHandler> handler);
    bool registerModule(std::shared_ptr<IModule> module);
    void setRuntimeServices(IModuleRuntimeServices* services);
    const ModuleRegistry& modules() const;
    NodeResult run(const ExecNode& node, const NodeExecutionContext& context);

private:
    ModuleRegistry m_modules;
    IModuleRuntimeServices* m_runtimeServices = nullptr;
    QVector<std::shared_ptr<INodeHandler>> m_handlers;
};

class NoopNodeHandler final : public INodeHandler {
public:
    bool canHandle(const ExecNode& node) const override;
    NodeResult run(const ExecNode& node, const NodeExecutionContext& context) override;
};

class WaitNodeHandler final : public INodeHandler {
public:
    bool canHandle(const ExecNode& node) const override;
    NodeResult run(const ExecNode& node, const NodeExecutionContext& context) override;
};

class ActionNodeHandler final : public INodeHandler {
public:
    explicit ActionNodeHandler(ModuleRegistry& modules);

    bool canHandle(const ExecNode& node) const override;
    NodeResult run(const ExecNode& node, const NodeExecutionContext& context) override;

private:
    ModuleRegistry& m_modules;
};

} // namespace PicoATE::Core
