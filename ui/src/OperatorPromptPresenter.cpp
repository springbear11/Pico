#include "OperatorPromptPresenter.h"

#include "ExecutionViewModel.h"
#include "MultiUutOverviewWidget.h"
#include "ProjectResourcePaths.h"

#include <QCloseEvent>
#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>
#include <cmath>

namespace PicoATE::Ui {

namespace {

class OperatorPromptDialog final : public QDialog
{
public:
    explicit OperatorPromptDialog(QWidget* parent)
        : QDialog(parent,
                  Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                      Qt::WindowStaysOnTopHint)
    {
        setObjectName(QStringLiteral("operatorPromptDialog"));
        setModal(false);
        setMinimumWidth(420);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(20);

        m_messageLabel = new QLabel(this);
        m_messageLabel->setObjectName(QStringLiteral("operatorPromptMessage"));
        m_messageLabel->setWordWrap(true);
        m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_messageLabel);

        m_imageLabel = new QLabel(this);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setWordWrap(true);
        layout->addWidget(m_imageLabel, 0, Qt::AlignCenter);

        m_inputEdit = new QLineEdit(this);
        m_inputEdit->setObjectName(QStringLiteral("operatorPromptInput"));
        m_inputEdit->setMinimumHeight(40);
        layout->addWidget(m_inputEdit);

        m_inputErrorLabel = new QLabel(this);
        m_inputErrorLabel->setObjectName(QStringLiteral("operatorPromptInputError"));
        m_inputErrorLabel->setWordWrap(true);
        layout->addWidget(m_inputErrorLabel);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setObjectName(QStringLiteral("operatorPromptWaitingLabel"));
        layout->addWidget(m_statusLabel);

        auto* buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(10);
        buttons->addStretch(1);
        m_failButton = new QPushButton(this);
        m_failButton->setObjectName(QStringLiteral("operatorPromptFailButton"));
        m_passButton = new QPushButton(this);
        m_passButton->setObjectName(QStringLiteral("operatorPromptPassButton"));
        m_confirmButton = new QPushButton(this);
        m_confirmButton->setObjectName(QStringLiteral("operatorPromptConfirmButton"));
        for (auto* button : {m_failButton, m_passButton, m_confirmButton}) {
            button->setDefault(false);
            button->setAutoDefault(false);
            button->setFocusPolicy(Qt::NoFocus);
            buttons->addWidget(button);
        }
        layout->addLayout(buttons);

        setStyleSheet(QStringLiteral(
            "QDialog#operatorPromptDialog { background: #ffffff; }"
            "QLabel#operatorPromptMessage { color: #172033; font-size: 15px; }"
            "QLabel#operatorPromptImage { background: #f5f7fa; border: 1px solid #dce2e8; }"
            "QLabel#operatorPromptImageError { color: #b42318; font-size: 12px; }"
            "QLabel#operatorPromptWaitingLabel { color: #5f6b7a; font-size: 12px; }"
            "QLabel#operatorPromptInputError { color: #b42318; font-size: 12px; }"
            "QLineEdit#operatorPromptInput { border: 1px solid #b8c2cc; border-radius: 4px; "
            "padding: 0 10px; color: #172033; background: #ffffff; font-size: 14px; }"
            "QLineEdit#operatorPromptInput:focus { border-color: #2f7ed8; }"
            "QLineEdit#operatorPromptInput[invalid=\"true\"] { border-color: #b42318; "
            "background: #fff7f6; }"
            "QPushButton#operatorPromptConfirmButton { min-width: 96px; min-height: 34px; "
            "background: #2f7ed8; color: white; border: 0; border-radius: 4px; padding: 0 18px; }"
            "QPushButton#operatorPromptConfirmButton:hover { background: #246fbe; }"
            "QPushButton#operatorPromptPassButton { min-width: 104px; min-height: 36px; "
            "background: #15803d; color: white; border: 0; border-radius: 4px; padding: 0 20px; }"
            "QPushButton#operatorPromptPassButton:hover { background: #166534; }"
            "QPushButton#operatorPromptFailButton { min-width: 104px; min-height: 36px; "
            "background: #b42318; color: white; border: 0; border-radius: 4px; padding: 0 20px; }"
            "QPushButton#operatorPromptFailButton:hover { background: #912018; }"));
    }

    QPushButton* confirmButton() const { return m_confirmButton; }
    QPushButton* passButton() const { return m_passButton; }
    QPushButton* failButton() const { return m_failButton; }
    QString currentInstanceId() const { return m_currentInstanceId; }

    bool inputValues(QVariantMap& values)
    {
        if (!m_isInput) {
            values.clear();
            return true;
        }

        const auto text = m_inputEdit->text();
        if (text.trimmed().isEmpty()) {
            setInputError(tr("Enter a value."));
            return false;
        }

        QVariant value = text;
        if (m_inputType == QStringLiteral("integer")) {
            bool ok = false;
            const auto parsed = text.trimmed().toLongLong(&ok, 10);
            if (!ok) {
                setInputError(tr("Enter a valid integer."));
                return false;
            }
            value = parsed;
        } else if (m_inputType == QStringLiteral("number")) {
            bool ok = false;
            const auto parsed = text.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(parsed)) {
                setInputError(tr("Enter a valid number."));
                return false;
            }
            value = parsed;
        }

        setInputError({});
        values = {
            {QStringLiteral("value"), value},
            {QStringLiteral("text"), text},
            {QStringLiteral("inputType"), m_inputType}
        };
        return true;
    }

    void focusInput()
    {
        if (m_isInput) {
            m_inputEdit->setFocus(Qt::OtherFocusReason);
            m_inputEdit->selectAll();
        }
    }

    void configure(const PicoATE::Core::RuntimeEvent& event,
                   const QString& sequencePath)
    {
        const auto mode = event.details.value("mode").toString();
        m_currentInstanceId = event.details.value("promptInstanceId").toString();
        setWindowTitle(event.details.value("title").toString().isEmpty()
                           ? tr("Message")
                           : event.details.value("title").toString());
        m_messageLabel->setText(
            event.details.value("message", event.message).toString());
        updateImage(event.details.value("image").toString(), sequencePath);

        const bool notice = mode == QStringLiteral("notice");
        const bool judgment = mode == QStringLiteral("judgment");
        m_isInput = mode == QStringLiteral("input");
        m_inputType = event.details.value("inputType", QStringLiteral("text"))
                          .toString()
                          .trimmed()
                          .toLower();
        m_inputEdit->setVisible(m_isInput);
        m_inputErrorLabel->setVisible(false);
        m_inputEdit->setPlaceholderText(
            event.details.value("inputPlaceholder").toString());
        m_inputEdit->setText(event.details.value("defaultValue").toString());
        setInputError({});
        m_confirmButton->setVisible(!notice && !judgment);
        m_passButton->setVisible(judgment);
        m_failButton->setVisible(judgment);
        m_confirmButton->setText(
            event.details.value(
                "confirmText",
                m_isInput ? QStringLiteral("Submit") : QStringLiteral("OK"))
                .toString());
        m_passButton->setText(
            event.details.value("passText", QStringLiteral("PASS")).toString());
        m_failButton->setText(
            event.details.value("failText", QStringLiteral("FAIL")).toString());
        m_statusLabel->setVisible(notice || judgment);
        m_statusLabel->setText(notice
            ? tr("The test continues while this instruction remains visible.")
            : tr("Select the observed result."));
        setResponsePending(false);
        adjustSize();
    }

    void setResponsePending(bool pending)
    {
        for (auto* button : {m_confirmButton, m_passButton, m_failButton}) {
            button->setEnabled(!pending);
        }
        if (pending) {
            m_statusLabel->setVisible(true);
            m_statusLabel->setText(tr("Recording operator response..."));
        }
    }

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
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            m_isInput) {
            m_confirmButton->click();
            event->accept();
            return;
        }
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
    void setInputError(const QString& message)
    {
        m_inputErrorLabel->setText(message);
        m_inputErrorLabel->setVisible(!message.isEmpty());
        m_inputEdit->setProperty("invalid", !message.isEmpty());
        m_inputEdit->style()->unpolish(m_inputEdit);
        m_inputEdit->style()->polish(m_inputEdit);
        if (!message.isEmpty()) {
            m_inputEdit->setFocus(Qt::OtherFocusReason);
        }
    }

    void updateImage(const QString& image, const QString& sequencePath)
    {
        m_imageLabel->clear();
        m_imageLabel->setToolTip({});
        m_imageLabel->setVisible(!image.trimmed().isEmpty());
        if (image.trimmed().isEmpty()) {
            return;
        }

        const auto imagePath = ProjectResourcePaths::resolveImage(sequencePath, image);
        QPixmap pixmap(imagePath);
        m_imageLabel->setToolTip(imagePath);
        if (pixmap.isNull()) {
            m_imageLabel->setObjectName(QStringLiteral("operatorPromptImageError"));
            m_imageLabel->setText(tr("Image unavailable: %1").arg(image.trimmed()));
            return;
        }

        m_imageLabel->setObjectName(QStringLiteral("operatorPromptImage"));
        constexpr int maximumImageWidth = 760;
        constexpr int maximumImageHeight = 420;
        if (pixmap.width() > maximumImageWidth ||
            pixmap.height() > maximumImageHeight) {
            pixmap = pixmap.scaled(maximumImageWidth,
                                   maximumImageHeight,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        }
        m_imageLabel->setPixmap(pixmap);
    }

    QLabel* m_messageLabel = nullptr;
    QLabel* m_imageLabel = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QLabel* m_inputErrorLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QPushButton* m_passButton = nullptr;
    QPushButton* m_failButton = nullptr;
    QString m_currentInstanceId;
    QString m_inputType = QStringLiteral("text");
    bool m_isInput = false;
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
    if (m_overviewHost) {
        m_overviewHost->clearOperatorPrompts();
    }
    QSet<QDialog*> dialogs;
    for (const auto& dialog : std::as_const(m_dialogs)) {
        if (dialog) {
            dialogs.insert(dialog.data());
        }
    }
    m_dialogs.clear();
    m_dialogsByKey.clear();
    for (auto* dialog : std::as_const(dialogs)) {
        static_cast<OperatorPromptDialog*>(dialog)->dismiss();
    }
}

void OperatorPromptPresenter::setSequencePath(QString sequencePath)
{
    sequencePath = sequencePath.trimmed();
    m_sequencePath = sequencePath.isEmpty()
        ? QString{}
        : QFileInfo(sequencePath).absoluteFilePath();
}

void OperatorPromptPresenter::setOverviewHost(
    MultiUutOverviewWidget* overviewHost)
{
    if (m_overviewHost == overviewHost) {
        return;
    }
    if (m_overviewHost) {
        disconnect(m_overviewHost, nullptr, this, nullptr);
    }
    m_overviewHost = overviewHost;
    if (!m_overviewHost) {
        return;
    }
    connect(m_overviewHost,
            &MultiUutOverviewWidget::operatorPromptResponseRequested,
            this,
            [this](const QString& instanceId,
                   PicoATE::Core::OperatorPromptResponse response,
                   const QVariantMap& values) {
                if (m_viewModel &&
                    m_viewModel->respondToOperatorPrompt(instanceId,
                                                         response,
                                                         values) &&
                    m_overviewHost) {
                    m_overviewHost->setOperatorPromptResponsePending(
                        instanceId, true);
                }
            });
}

void OperatorPromptPresenter::showPrompt(const PicoATE::Core::RuntimeEvent& event)
{
    const auto instanceId = event.details.value("promptInstanceId").toString();
    if (instanceId.isEmpty() || m_dialogs.contains(instanceId) ||
        (m_overviewHost && m_overviewHost->hasOperatorPrompt(instanceId))) {
        return;
    }

    const auto mode = event.details.value("mode").toString();
    const bool notice = mode == QStringLiteral("notice");
    const auto key = presentationKey(event);
    auto* dialog = key.isEmpty()
        ? nullptr
        : static_cast<OperatorPromptDialog*>(m_dialogsByKey.value(key).data());
    if (!dialog && m_overviewHost &&
        m_overviewHost->presentOperatorPrompt(event, m_sequencePath)) {
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
        return;
    }

    if (!dialog) {
        dialog = new OperatorPromptDialog(m_owner);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QObject::destroyed, this, [this] {
            for (auto it = m_dialogs.begin(); it != m_dialogs.end();) {
                if (it.value().isNull()) it = m_dialogs.erase(it);
                else ++it;
            }
            for (auto it = m_dialogsByKey.begin(); it != m_dialogsByKey.end();) {
                if (it.value().isNull()) it = m_dialogsByKey.erase(it);
                else ++it;
            }
        });
    }
    dialog->configure(event, m_sequencePath);
    m_dialogs.insert(instanceId, dialog);
    if (!key.isEmpty()) {
        m_dialogsByKey.insert(key, dialog);
    }

    const auto bindResponse = [this, dialog, instanceId](
                                  QPushButton* button,
                                  PicoATE::Core::OperatorPromptResponse response) {
        QObject::disconnect(button, nullptr, this, nullptr);
        connect(button, &QPushButton::clicked, this,
                [this, instanceId, dialog, response] {
            QVariantMap values;
            if (response == PicoATE::Core::OperatorPromptResponse::Submitted &&
                !dialog->inputValues(values)) {
                return;
            }
            if (m_viewModel && m_viewModel->respondToOperatorPrompt(instanceId,
                                                                    response,
                                                                    values)) {
                dialog->setResponsePending(true);
            }
        });
    };
    bindResponse(dialog->confirmButton(),
                 mode == QStringLiteral("input")
                     ? PicoATE::Core::OperatorPromptResponse::Submitted
                     : PicoATE::Core::OperatorPromptResponse::Confirmed);
    bindResponse(dialog->passButton(),
                 PicoATE::Core::OperatorPromptResponse::Passed);
    bindResponse(dialog->failButton(),
                 PicoATE::Core::OperatorPromptResponse::Failed);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    dialog->focusInput();

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
    if (m_overviewHost && m_overviewHost->closeOperatorPrompt(instanceId)) {
        return;
    }
    const auto dialog = m_dialogs.take(instanceId);
    auto* prompt = static_cast<OperatorPromptDialog*>(dialog.data());
    if (!prompt || prompt->currentInstanceId() != instanceId) {
        return;
    }
    removeDialogMappings(prompt);
    prompt->dismiss();
}

void OperatorPromptPresenter::removeDialogMappings(QDialog* dialog)
{
    for (auto it = m_dialogs.begin(); it != m_dialogs.end();) {
        if (it.value().data() == dialog) it = m_dialogs.erase(it);
        else ++it;
    }
    for (auto it = m_dialogsByKey.begin(); it != m_dialogsByKey.end();) {
        if (it.value().data() == dialog) it = m_dialogsByKey.erase(it);
        else ++it;
    }
}

QString OperatorPromptPresenter::presentationKey(
    const PicoATE::Core::RuntimeEvent& event) const
{
    const auto dialogKey = event.details.value("dialogKey").toString().trimmed();
    if (dialogKey.isEmpty()) {
        return {};
    }
    return event.uutId + QChar(0x001f) + dialogKey;
}

} // namespace PicoATE::Ui
