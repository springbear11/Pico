#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

namespace PicoATE::Core {

inline constexpr auto BuiltInValueToolsModuleId = "builtin.value-tools";

class ValueToolsModule final : public IModule {
public:
    ModuleId moduleId() const override;
    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override;
};

} // namespace PicoATE::Core
