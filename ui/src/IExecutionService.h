#pragma once

#include "PicoATE/Core/ExecutionControl.h"
#include "PicoATE/Core/StopToken.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "UiExecutionTypes.h"

#include <memory>

namespace PicoATE::Ui {

class IExecutionService
{
public:
    virtual ~IExecutionService() = default;

    virtual CompileServiceResult compile(const CompileRequest& request) = 0;
    virtual RunServiceResult run(
        const RunRequest& request,
        const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
        PicoATE::Core::IRuntimeEventSink* eventSink = nullptr,
        const std::shared_ptr<PicoATE::Core::ExecutionControl>& executionControl = {}) = 0;
    virtual DeviceConnectionTestResult testDeviceConnection(
        const DeviceConnectionTestRequest& request,
        const std::shared_ptr<PicoATE::Core::StopToken>& stopToken) = 0;
};

} // namespace PicoATE::Ui
