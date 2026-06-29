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
             const std::shared_ptr<PicoATE::Core::IRuntimeEventSink>& eventSink);

signals:
    void compileFinished(const PicoATE::Ui::CompileServiceResult& result);
    void runStarted(quint64 requestId);
    void runFinished(const PicoATE::Ui::RunServiceResult& result);

private:
    std::unique_ptr<IExecutionService> m_service;
};

} // namespace PicoATE::Ui
