#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

namespace PicoATE::Core {

inline constexpr auto BuiltInDataParserModuleId = "builtin.data-parser";

class DataParserModule final : public IModule {
public:
    ModuleId moduleId() const override;
    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override;
};

} // namespace PicoATE::Core
