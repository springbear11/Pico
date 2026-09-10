#include "OperatorPromptPresenter.h"
#include "PromptCountdownWidget.h"
#include "UiLanguage.h"

#include "ExecutionViewModel.h"
#include "MultiUutOverviewWidget.h"
#include "ProjectResourcePaths.h"
#include "ElidedInfoLabel.h"

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
#include <QScreen>
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
        connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                this, [this] { retranslatePrompt(); });
        setMinimumWidth(420);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 20);
        layout->setSpacing(14);

        auto* header = new QHBoxLayout;
        header->setSpacing(12);
        auto* contextLabel = new ElidedInfoLabel({}, this);
        contextLabel->setMaximumCharacters(0);
        m_contextLabel = contextLabel;
        m_contextLabel->setObjectName(QStringLiteral("operatorPromptContext"));
        m_countdown = new PromptCountdownWidget(this);
        m_countdown->setCompact(true);
        header->addWidget(m_contextLabel, 1);
        header->addWidget(m_countdown, 0, Qt::AlignRight);
        layout->addLayout(header);

        m_messageLabel = new QLabel(this);
        m_messageLabel->setObjectName(QStringLiteral("operatorPromptMessage"));
        m_messageLabel->setWordWrap(true);
        m_messageLabel->setAlignment(Qt::AlignCenter);
        m_messageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_messageLabel);

        m_imageLabel = new QLabel(this);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setWordWrap(true);
        layout->addWidget(m_imageLabel, 0, Qt::AlignCenter);

        m_inputEdit = new QLineEdit(this);
        m_inputEdit->setObjectName(QStringLiteral("operatorPromptInput"));
        m_inputEdit->setMinimumHeight(40);
        m_inputEdit->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_inputEdit);

        m_inputErrorLabel = new QLabel(this);
        m_inputErrorLabel->setObjectName(QStringLiteral("operatorPromptInputError"));
        m_inputErrorLabel->setWordWrap(true);
        layout->addWidget(m_inputErrorLabel);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setObjectName(QStringLiteral("operatorPromptWaitingLabel"));
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setWordWrap(true);
        layout->addWidget(m_statusLabel);

        auto* buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(10);
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
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            buttons->addWidget(button, 1);
        }
        layout->addLayout(buttons);

        setStyleSheet(QStringLiteral(
            "QDialog#operatorPromptDialog { background: #ffffff; }"
            "QLabel#operatorPromptContext { color: #344048; font-size: 14px; font-weight:600; }"
            "QLabel#operatorPromptMessage { color: #26323a; font-size: 17px; font-weight:600; padding:10px 0; }"
            "QLabel#operatorPromptImage { background: #f5f7fa; border: 1px solid #dce2e8; }"
            "QLabel#operatorPromptImageError { color: #b42318; font-size: 12px; }"
            "QLabel#operatorPromptWaitingLabel { color: #5f6b7a; font-size: 12px; }"
            "QLabel#operatorPromptInputError { color: #b42318; font-size: 12px; }"
            "QLineEdit#operatorPromptInput { border: 1px solid #b8c2cc; border-radius: 4px; "
            "padding: 0 10px; color: #172033; background: #ffffff; font-size: 14px; }"
            "QLineEdit#operatorPromptInput:focus { border-color: #2f7ed8; }"
            "QLineEdit#operatorPromptInput[invalid=\"true\"] { border-color: #b42318; "
            "background: #fff7f6; }"
            "QPushButton#operatorPromptConfirmButton { min-width:0; min-height:38px; max-height:38px; "
            "background:#2f7ed8; color:white; border:0; border-radius:4px; padding:0 14px; font-weight:600; }"
            "QPushButton#operatorPromptConfirmButton:hover { background:#246fbe; }"
            "QPushButton#operatorPromptPassButton { min-width:0; min-height:38px; max-height:38px; "
            "background:#15803d; color:white; border:0; border-radius:4px; padding:0 14px; font-weight:600; }"
            "QPushButton#operatorPromptPassButton:hover { background:#166534; }"
            "QPushButton#operatorPromptPassButton:pressed { background:#14532d; }"
            "QPushButton#operatorPromptFailButton { min-width:0; min-height:38px; max-height:38px; "
            "background:#b42318; color:white; border:0; border-radius:4px; padding:0 14px; font-weight:600; }"
            "QPushButton#operatorPromptFailButton:hover { background:#912018; }"
            "QPushButton#operatorPromptFailButton:pressed { background:#7b1b14; }"
            "QPushButton#operatorPromptConfirmButton:disabled,"
            "QPushButton#operatorPromptPassButton:disabled,"
            "QPushButton#operatorPromptFailButton:disabled { color:#eef1f2; background:#aeb6bb; }"));
    }

    QPushButton* confirmButton() const { return m_confirmButton; }
    QPushButton* passButton() const { return m_passButton; }
    QPushButton* failButton() const { return m_failButton; }
    QString currentInstanceId() const { return m_currentInstanceId; }
    QString inputText() const { return m_inputEdit->text(); }
    bool responsePending() const { return m_responsePending; }

    bool inputValues(QVariantMap& values)
    {
        if (!m_isInput) {
            values.clear();
            return true;
        }

        const auto text = m_inputEdit->text();
        if (text.trimmed().isEmpty()) {
            setInputError("Enter a value.");
            return false;
        }

        QVariant value = text;
        if (m_inputType == QStringLiteral("integer")) {
            bool ok = false;
            const auto parsed = text.trimmed().toLongLong(&ok, 10);
            if (!ok) {
                setInputError("Enter a valid integer.");
                return false;
            }
            value = parsed;
        } else if (m_inputType == QStringLiteral("number")) {
            bool ok = false;
            const auto parsed = text.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(parsed)) {
                setInputError("Enter a valid number.");
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
        m_promptDetails = event.details;
        m_uutId = event.uutId;
        m_countdown->configure(event);
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
        m_statusLabel->hide();
        m_statusLabel->setText(notice
            ? tr("The test continues while this instruction remains visible.")
            : tr("Select the observed result."));
        setResponsePending(false);
        retranslatePrompt();
        adjustSize();
    }

    void setResponsePending(bool pending)
    {
        m_responsePending = pending;
        m_countdown->setResponsePending(pending);
        m_inputEdit->setEnabled(!pending);
        for (auto* button : {m_confirmButton, m_passButton, m_failButton}) {
            button->setEnabled(!pending);
        }
        if (pending) {
            m_statusLabel->setVisible(true);
            m_statusLabel->setText(uiText("Recording operator response..."));
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
    void retranslatePrompt()
    {
        m_inputErrorLabel->setText(m_inputErrorSource
            ? uiText(m_inputErrorSource) : QString{});
        const auto buttonText = [this](const char* key, const char* fallback) {
            const auto text = m_promptDetails.value(QString::fromLatin1(key),
                                                    QString::fromLatin1(fallback)).toString();
            return text == QString::fromLatin1(fallback) ? uiText(fallback) : text;
        };
        m_confirmButton->setText(buttonText("confirmText", m_isInput ? "Submit" : "OK"));
        m_passButton->setText(buttonText("passText", "PASS"));
        m_failButton->setText(buttonText("failText", "FAIL"));
        const auto mode = m_promptDetails.value(QStringLiteral("mode")).toString();
        const auto caption = mode == "judgment" ? uiText("Manual Judgment") : mode == "input"
            ? uiText("Value Input") : mode == "notice" ? uiText("Notice") : uiText("Operator Confirmation");
        m_contextLabel->setText(m_uutId.isEmpty() ? caption : m_uutId + " | " + caption);
        m_contextLabel->setToolTip(m_contextLabel->text());
        const bool notice = m_promptDetails.value(QStringLiteral("mode"))
                                .toString() == QStringLiteral("notice");
        m_statusLabel->setText(m_responsePending
            ? uiText("Recording operator response...")
            : (notice ? uiText("The test continues while this instruction remains visible.")
                      : uiText("Select the observed result.")));
        m_statusLabel->setVisible(m_responsePending);
        if (m_promptDetails.value(QStringLiteral("title")).toString().isEmpty()) {
            setWindowTitle(uiText("Message"));
        }
    }

    void setInputError(const char* source)
    {
        m_inputErrorSource = source;
        const auto message = source ? uiText(source) : QString{};
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
        constexpr int maximumImageWidth = 480;
        const int maximumImageHeight = qMin(260, qMax(80, screen()->availableGeometry().height() - 320));
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
    QLabel* m_contextLabel = nullptr;
    QString m_uutId;
    PromptCountdownWidget* m_countdown = nullptr;
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
    bool m_responsePending = false;
    QVariantMap m_promptDetails;
    const char* m_inputErrorSource = nullptr;
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
    m_activePromptEvents.clear();
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

void OperatorPromptPresenter::rehostActivePromptsInOverview()
{
    if (!m_overviewHost || !m_overviewHost->isVisible()) {
        return;
    }

    QVector<OperatorPromptDialog*> rehostedDialogs;
    for (auto it = m_dialogs.constBegin(); it != m_dialogs.constEnd(); ++it) {
        auto* dialog = static_cast<OperatorPromptDialog*>(it.value().data());
        if (!dialog || dialog->currentInstanceId() != it.key()) {
            continue;
        }
        const auto eventIt = m_activePromptEvents.constFind(it.key());
        if (eventIt == m_activePromptEvents.constEnd()) {
            continue;
        }

        auto event = eventIt.value();
        if (event.details.value(QStringLiteral("mode")).toString() ==
            QStringLiteral("input")) {
            event.details.insert(QStringLiteral("defaultValue"),
                                 dialog->inputText());
        }
        if (!m_overviewHost->presentOperatorPrompt(event, m_sequencePath)) {
            continue;
        }
        m_overviewHost->setOperatorPromptResponsePending(
            it.key(), dialog->responsePending());
        rehostedDialogs.push_back(dialog);
    }

    for (auto* dialog : std::as_const(rehostedDialogs)) {
        removeDialogMappings(dialog);
        dialog->dismiss();
    }
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

void OperatorPromptPresenter::showPrompt(const PicoATE::Core::RuntimeEvent& source)
{
    const auto instanceId = source.details.value("promptInstanceId").toString();
    if (instanceId.isEmpty()) {
        return;
    }
    const auto previous = m_activePromptEvents.constFind(instanceId);
    const auto event = PromptCountdownWidget::withTiming(source,
        previous == m_activePromptEvents.cend() ? nullptr : &previous.value());
    m_activePromptEvents.insert(instanceId, event);
    if (m_dialogs.contains(instanceId) ||
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
    m_activePromptEvents.remove(instanceId);
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
