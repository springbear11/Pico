#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

namespace PicoATE::Core {

class ExampleDmmAdapterModule final : public IModule {
public:
    explicit ExampleDmmAdapterModule(ModuleId id = "example.dmm");

    ModuleId moduleId() const override;
    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override;

private:
    ModuleId m_id;
};

class ExampleCanAdapterModule final : public IModule {
public:
    explicit ExampleCanAdapterModule(ModuleId id = "example.can");

    ModuleId moduleId() const override;
    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override;

private:
    ModuleId m_id;
};

} // namespace PicoATE::Core
