#include "OperatorPromptPresenter.h"

#include "ExecutionViewModel.h"

#include <QCloseEvent>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace PicoATE::Ui {

namespace {

class OperatorPromptDialog final : public QDialog
{
public:
    OperatorPromptDialog(const QString& title,
                         const QString& message,
                         const QString& confirmText,
                         bool showConfirmButton,
                         QWidget* parent)
        : QDialog(parent,
                  Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                      Qt::WindowStaysOnTopHint)
    {
        setObjectName(QStringLiteral("operatorPromptDialog"));
        setWindowTitle(title.isEmpty() ? tr("Message") : title);
        setModal(false);
        setMinimumWidth(420);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(20);

        auto* messageLabel = new QLabel(message, this);
        messageLabel->setObjectName(QStringLiteral("operatorPromptMessage"));
        messageLabel->setWordWrap(true);
        messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(messageLabel);

        if (showConfirmButton) {
            m_confirmButton = new QPushButton(
                confirmText.isEmpty() ? tr("OK") : confirmText, this);
            m_confirmButton->setObjectName(QStringLiteral("operatorPromptConfirmButton"));
            m_confirmButton->setDefault(false);
            m_confirmButton->setAutoDefault(false);
            m_confirmButton->setFocusPolicy(Qt::NoFocus);
            layout->addWidget(m_confirmButton, 0, Qt::AlignRight);
        } else {
            auto* status = new QLabel(tr("Waiting for the next condition..."), this);
            status->setObjectName(QStringLiteral("operatorPromptWaitingLabel"));
            layout->addWidget(status);
        }

        setStyleSheet(QStringLiteral(
            "QDialog#operatorPromptDialog { background: #ffffff; }"
            "QLabel#operatorPromptMessage { color: #172033; font-size: 15px; }"
            "QLabel#operatorPromptWaitingLabel { color: #5f6b7a; font-size: 12px; }"
            "QPushButton#operatorPromptConfirmButton { min-width: 96px; min-height: 34px; "
            "background: #2f7ed8; color: white; border: 0; border-radius: 4px; padding: 0 18px; }"
            "QPushButton#operatorPromptConfirmButton:hover { background: #246fbe; }"));
    }

    QPushButton* confirmButton() const { return m_confirmButton; }

    void dismiss()
    {
        m_allowClose = true;
        done(QDialog::Accepted);
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (m_allowClose) {
            QDialog::closeEvent(event);
        } else {
            event->ignore();
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Escape) {
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }

    void reject() override
    {
        if (m_allowClose) {
            QDialog::reject();
        }
    }

private:
    QPushButton* m_confirmButton = nullptr;
    bool m_allowClose = false;
};

} // namespace

OperatorPromptPresenter::OperatorPromptPresenter(ExecutionViewModel* viewModel,
                                                 QWidget* owner,
                                                 QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
    , m_owner(owner)
{
}

void OperatorPromptPresenter::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    for (const auto& event : events) {
        if (event.kind == PicoATE::Core::RuntimeEventKind::OperatorPromptRequested) {
            showPrompt(event);
        } else if (event.kind == PicoATE::Core::RuntimeEventKind::OperatorPromptClosed) {
            closePrompt(event.details.value("promptInstanceId").toString());
        }
    }
}

void OperatorPromptPresenter::closeAll()
{
    const auto dialogs = m_dialogs;
    m_dialogs.clear();
    for (const auto& dialog : dialogs) {
        if (auto* prompt = static_cast<OperatorPromptDialog*>(dialog.data())) {
            prompt->dismiss();
        }
    }
}

void OperatorPromptPresenter::showPrompt(const PicoATE::Core::RuntimeEvent& event)
{
    const auto instanceId = event.details.value("promptInstanceId").toString();
    if (instanceId.isEmpty() || m_dialogs.contains(instanceId)) {
        return;
    }

    const bool notice = event.details.value("mode").toString() == "notice";
    auto* dialog = new OperatorPromptDialog(
        event.details.value("title").toString(),
        event.details.value("message", event.message).toString(),
        event.details.value("confirmText", QStringLiteral("OK")).toString(),
        !notice,
        m_owner);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dialogs.insert(instanceId, dialog);

    connect(dialog, &QObject::destroyed, this, [this, instanceId] {
        m_dialogs.remove(instanceId);
    });
    if (auto* button = dialog->confirmButton()) {
        connect(button, &QPushButton::clicked, this, [this, instanceId, dialog] {
            if (m_viewModel && m_viewModel->respondToOperatorPrompt(
                                   instanceId,
                                   PicoATE::Core::OperatorPromptResponse::Confirmed)) {
                m_dialogs.remove(instanceId);
                dialog->dismiss();
            }
        });
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    if (notice) {
        QPointer<OperatorPromptPresenter> self(this);
        QTimer::singleShot(0, this, [self, instanceId] {
            if (self && self->m_viewModel) {
                self->m_viewModel->respondToOperatorPrompt(
                    instanceId,
                    PicoATE::Core::OperatorPromptResponse::Shown);
            }
        });
    }
}

void OperatorPromptPresenter::closePrompt(const QString& instanceId)
{
    const auto dialog = m_dialogs.take(instanceId);
    if (auto* prompt = static_cast<OperatorPromptDialog*>(dialog.data())) {
        prompt->dismiss();
    }
}

} // namespace PicoATE::Ui
