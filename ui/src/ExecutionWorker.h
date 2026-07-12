#pragma once

#include "IExecutionService.h"

#include <QObject>

#include <memory>

namespace PicoATE::Ui {

class ExecutionWorker final : public QObject
{
    Q_OBJECT

public:
    explicit ExecutionWorker(std::unique_ptr<IExecutionService> service,
                             QObject* parent = nullptr);

    void compile(const CompileRequest& request);
    void run(const RunRequest& request,
             const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
             const std::shared_ptr<PicoATE::Core::IRuntimeEventSink>& eventSink,
             const std::shared_ptr<PicoATE::Core::ExecutionControl>& executionControl);
    void testDeviceConnection(
        const DeviceConnectionTestRequest& request,
        const std::shared_ptr<PicoATE::Core::StopToken>& stopToken);

signals:
    void compileFinished(const PicoATE::Ui::CompileServiceResult& result);
    void runStarted(quint64 requestId);
    void runFinished(const PicoATE::Ui::RunServiceResult& result);
    void deviceConnectionTestStarted(quint64 requestId, const QString& deviceId);
    void deviceConnectionTestFinished(
        const PicoATE::Ui::DeviceConnectionTestResult& result);

private:
    std::unique_ptr<IExecutionService> m_service;
};

} // namespace PicoATE::Ui
