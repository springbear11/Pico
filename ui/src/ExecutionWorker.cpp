#include "ExecutionWorker.h"

#include <utility>

namespace PicoATE::Ui {

ExecutionWorker::ExecutionWorker(std::unique_ptr<IExecutionService> service,
                                 QObject* parent)
    : QObject(parent)
    , m_service(std::move(service))
{
}

void ExecutionWorker::compile(const CompileRequest& request)
{
    emit compileFinished(m_service->compile(request));
}

void ExecutionWorker::run(
    const RunRequest& request,
    const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
    const std::shared_ptr<PicoATE::Core::IRuntimeEventSink>& eventSink,
    const std::shared_ptr<PicoATE::Core::ExecutionControl>& executionControl)
{
    emit runStarted(request.requestId);
    emit runFinished(m_service->run(
        request, stopToken, eventSink.get(), executionControl));
}

void ExecutionWorker::testDeviceConnection(
    const DeviceConnectionTestRequest& request,
    const std::shared_ptr<PicoATE::Core::StopToken>& stopToken)
{
    emit deviceConnectionTestStarted(request.requestId, request.deviceId);
    emit deviceConnectionTestFinished(
        m_service->testDeviceConnection(request, stopToken));
}

} // namespace PicoATE::Ui
