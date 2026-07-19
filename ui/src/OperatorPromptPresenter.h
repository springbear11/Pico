#pragma once

#include "PicoATE/Core/RuntimeEvent.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QDialog;
class QWidget;

namespace PicoATE::Ui {

class ExecutionViewModel;

class OperatorPromptPresenter final : public QObject
{
    Q_OBJECT

public:
    OperatorPromptPresenter(ExecutionViewModel* viewModel,
                            QWidget* owner,
                            QObject* parent = nullptr);

    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void closeAll();

private:
    void showPrompt(const PicoATE::Core::RuntimeEvent& event);
    void closePrompt(const QString& instanceId);

    ExecutionViewModel* m_viewModel = nullptr;
    QWidget* m_owner = nullptr;
    QHash<QString, QPointer<QDialog>> m_dialogs;
};

} // namespace PicoATE::Ui
