#include "MultiUutOverviewWidget.h"

#include "ProjectResourcePaths.h"
#include "RunnerModels.h"

#include <QAbstractButton>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpacerItem>
#include <QStyle>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace PicoATE::Ui {

namespace {

struct CardPalette {
    QColor background;
    QColor border;
    QColor accent;
};

CardPalette paletteFor(UutOverviewState state)
{
    switch (state) {
    case UutOverviewState::Running:
        return {QColor(QStringLiteral("#fff8df")),
                QColor(QStringLiteral("#dfc36a")),
                QColor(QStringLiteral("#a87500"))};
    case UutOverviewState::Paused:
        return {QColor(QStringLiteral("#edf5fa")),
                QColor(QStringLiteral("#8eb5cc")),
                QColor(QStringLiteral("#35677f"))};
    case UutOverviewState::Passed:
        return {QColor(QStringLiteral("#edf8f0")),
                QColor(QStringLiteral("#8ac097")),
                QColor(QStringLiteral("#2f7548"))};
    case UutOverviewState::Failed:
        return {QColor(QStringLiteral("#fff0f0")),
                QColor(QStringLiteral("#d88e8e")),
                QColor(QStringLiteral("#a43838"))};
    case UutOverviewState::Stopped:
        return {QColor(QStringLiteral("#f5f1f1")),
                QColor(QStringLiteral("#bd9999")),
                QColor(QStringLiteral("#875151"))};
    case UutOverviewState::Waiting:
        return {QColor(QStringLiteral("#f7f9fa")),
                QColor(QStringLiteral("#cbd3d8")),
                QColor(QStringLiteral("#667680"))};
    }
    return {};
}

QString compactDuration(qint64 durationMs)
{
    const auto milliseconds = qMax<qint64>(0, durationMs);
    return QStringLiteral("%1:%2.%3")
        .arg(milliseconds / 60000, 2, 10, QLatin1Char('0'))
        .arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

QString recentStepStateText(PicoATE::Core::ActivationState state)
{
    using PicoATE::Core::ActivationState;
    switch (state) {
    case ActivationState::Passed: return QStringLiteral("PASS");
    case ActivationState::Failed: return QStringLiteral("FAIL");
    case ActivationState::Error: return QStringLiteral("ERROR");
    case ActivationState::Timeout: return QStringLiteral("TIMEOUT");
    case ActivationState::Cancelled: return QStringLiteral("STOP");
    case ActivationState::Skipped: return QStringLiteral("SKIP");
    default: return QStringLiteral("DONE");
    }
}

QColor recentStepStateColor(PicoATE::Core::ActivationState state)
{
    using PicoATE::Core::ActivationState;
    switch (state) {
    case ActivationState::Passed: return QColor(QStringLiteral("#2f7548"));
    case ActivationState::Failed:
    case ActivationState::Error:
    case ActivationState::Timeout:
        return QColor(QStringLiteral("#a43838"));
    case ActivationState::Cancelled: return QColor(QStringLiteral("#875151"));
    case ActivationState::Skipped: return QColor(QStringLiteral("#9a6a00"));
    default: return QColor(QStringLiteral("#667680"));
    }
}

QString promptPresentationKey(const PicoATE::Core::RuntimeEvent& event)
{
    return event.details.value(QStringLiteral("dialogKey"))
        .toString()
        .trimmed();
}

class UutOverviewPromptOverlay final : public QWidget
{
public:
    using ResponseHandler = std::function<void(
        const QString&,
        PicoATE::Core::OperatorPromptResponse,
        const QVariantMap&)>;

    explicit UutOverviewPromptOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("uutOverviewPromptOverlay"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFocusPolicy(Qt::StrongFocus);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 14, 18, 14);
        layout->setSpacing(7);

        m_contextLabel = new QLabel(this);
        m_contextLabel->setObjectName(
            QStringLiteral("uutOverviewPromptContext"));
        m_contextLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_contextLabel);

        m_titleLabel = new QLabel(this);
        m_titleLabel->setObjectName(QStringLiteral("uutOverviewPromptTitle"));
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setWordWrap(true);
        layout->addWidget(m_titleLabel);

        m_messageLabel = new QLabel(this);
        m_messageLabel->setObjectName(
            QStringLiteral("uutOverviewPromptMessage"));
        m_messageLabel->setAlignment(Qt::AlignCenter);
        m_messageLabel->setWordWrap(true);
        m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_messageLabel, 1);

        m_imageLabel = new QLabel(this);
        m_imageLabel->setObjectName(QStringLiteral("uutOverviewPromptImage"));
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setMaximumHeight(80);
        layout->addWidget(m_imageLabel, 0, Qt::AlignCenter);

        m_inputEdit = new QLineEdit(this);
        m_inputEdit->setObjectName(QStringLiteral("uutOverviewPromptInput"));
        m_inputEdit->setAlignment(Qt::AlignCenter);
        m_inputEdit->setMinimumHeight(40);
        layout->addWidget(m_inputEdit);

        m_inputErrorLabel = new QLabel(this);
        m_inputErrorLabel->setObjectName(
            QStringLiteral("uutOverviewPromptInputError"));
        m_inputErrorLabel->setAlignment(Qt::AlignCenter);
        m_inputErrorLabel->setWordWrap(true);
        layout->addWidget(m_inputErrorLabel);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setObjectName(
            QStringLiteral("uutOverviewPromptStatus"));
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setWordWrap(true);
        layout->addWidget(m_statusLabel);

        auto* buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(8);
        buttons->addStretch(1);
        m_failButton = new QPushButton(this);
        m_failButton->setObjectName(
            QStringLiteral("uutOverviewPromptFailButton"));
        m_passButton = new QPushButton(this);
        m_passButton->setObjectName(
            QStringLiteral("uutOverviewPromptPassButton"));
        m_confirmButton = new QPushButton(this);
        m_confirmButton->setObjectName(
            QStringLiteral("uutOverviewPromptConfirmButton"));
        for (auto* button : {m_failButton, m_passButton, m_confirmButton}) {
            button->setDefault(false);
            button->setAutoDefault(false);
            button->setMinimumHeight(32);
            buttons->addWidget(button);
        }
        buttons->addStretch(1);
        layout->addLayout(buttons);

        connect(m_confirmButton, &QPushButton::clicked, this, [this] {
            submit(m_isInput
                       ? PicoATE::Core::OperatorPromptResponse::Submitted
                       : PicoATE::Core::OperatorPromptResponse::Confirmed);
        });
        connect(m_passButton, &QPushButton::clicked, this, [this] {
            submit(PicoATE::Core::OperatorPromptResponse::Passed);
        });
        connect(m_failButton, &QPushButton::clicked, this, [this] {
            submit(PicoATE::Core::OperatorPromptResponse::Failed);
        });
        connect(m_inputEdit, &QLineEdit::returnPressed,
                m_confirmButton, &QPushButton::click);

        setStyleSheet(QStringLiteral(
            "QWidget#uutOverviewPromptOverlay{"
            "background:rgba(226,242,252,250);border:2px solid #6ea8ca;"
            "border-radius:7px;}"
            "QLabel#uutOverviewPromptContext{color:#45677b;font-size:13px;"
            "font-weight:700;}"
            "QLabel#uutOverviewPromptTitle{color:#172b38;font-size:18px;"
            "font-weight:700;}"
            "QLabel#uutOverviewPromptMessage{color:#263e4d;font-size:16px;"
            "font-weight:600;}"
            "QLabel#uutOverviewPromptStatus{color:#506f82;font-size:12px;"
            "font-weight:600;}"
            "QLabel#uutOverviewPromptInputError{color:#a43838;font-size:12px;"
            "font-weight:600;}"
            "QLineEdit#uutOverviewPromptInput{background:#ffffff;color:#202a31;"
            "border:1px solid #8ba9ba;border-radius:4px;padding:0 9px;"
            "font-size:14px;font-weight:600;}"
            "QLineEdit#uutOverviewPromptInput:focus{border-color:#3f5968;}"
            "QLineEdit#uutOverviewPromptInput[invalid=\"true\"]{"
            "border-color:#a43838;background:#fff5f5;}"
            "QPushButton#uutOverviewPromptConfirmButton{min-width:88px;"
            "background:#252b30;color:#ffffff;border:0;border-radius:4px;"
            "padding:0 14px;font-size:14px;font-weight:700;}"
            "QPushButton#uutOverviewPromptPassButton{min-width:82px;"
            "background:#2f7548;color:#ffffff;border:0;border-radius:4px;"
            "padding:0 12px;font-size:14px;font-weight:700;}"
            "QPushButton#uutOverviewPromptFailButton{min-width:82px;"
            "background:#a43838;color:#ffffff;border:0;border-radius:4px;"
            "padding:0 12px;font-size:14px;font-weight:700;}"
            "QPushButton:disabled{background:#aeb6bb;color:#eef1f2;}"));
    }

    void setResponseHandler(ResponseHandler handler)
    {
        m_responseHandler = std::move(handler);
    }

    void configure(const PicoATE::Core::RuntimeEvent& event,
                   const QString& sequencePath,
                   const QString& serialNumber)
    {
        m_instanceId = event.details.value(
            QStringLiteral("promptInstanceId")).toString();
        m_presentationKey = promptPresentationKey(event);
        const auto mode = event.details.value(
            QStringLiteral("mode")).toString().trimmed().toLower();
        const bool notice = mode == QStringLiteral("notice");
        const bool judgment = mode == QStringLiteral("judgment");
        m_isInput = mode == QStringLiteral("input");
        m_inputType = event.details.value(
            QStringLiteral("inputType"), QStringLiteral("text"))
                              .toString()
                              .trimmed()
                              .toLower();

        m_contextLabel->setText(serialNumber.trimmed().isEmpty()
            ? event.uutId
            : tr("%1  |  SN %2").arg(event.uutId, serialNumber.trimmed()));
        const auto title = event.details.value(
            QStringLiteral("title")).toString().trimmed();
        m_titleLabel->setText(title.isEmpty() ? tr("Operator Action") : title);
        m_messageLabel->setText(event.details.value(
            QStringLiteral("message"), event.message).toString());
        updateImage(event.details.value(QStringLiteral("image")).toString(),
                    sequencePath);

        m_inputEdit->setVisible(m_isInput);
        m_inputEdit->setPlaceholderText(event.details.value(
            QStringLiteral("inputPlaceholder")).toString());
        m_inputEdit->setText(event.details.value(
            QStringLiteral("defaultValue")).toString());
        setInputError({});

        m_confirmButton->setVisible(!notice && !judgment);
        m_passButton->setVisible(judgment);
        m_failButton->setVisible(judgment);
        m_confirmButton->setText(event.details.value(
            QStringLiteral("confirmText"),
            m_isInput ? QStringLiteral("Submit") : QStringLiteral("OK"))
                                     .toString());
        m_passButton->setText(event.details.value(
            QStringLiteral("passText"), QStringLiteral("PASS")).toString());
        m_failButton->setText(event.details.value(
            QStringLiteral("failText"), QStringLiteral("FAIL")).toString());
        m_statusLabel->setVisible(notice || judgment);
        m_statusLabel->setText(notice
            ? tr("The test continues while this instruction is displayed.")
            : tr("Select the observed result for this UUT."));
        setResponsePending(false);
    }

    QString currentInstanceId() const { return m_instanceId; }
    QString presentationKey() const { return m_presentationKey; }

    void setResponsePending(bool pending)
    {
        for (auto* button : {m_confirmButton, m_passButton, m_failButton}) {
            button->setEnabled(!pending);
        }
        m_inputEdit->setEnabled(!pending);
        if (pending) {
            m_statusLabel->show();
            m_statusLabel->setText(tr("Recording operator response..."));
        }
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }

private:
    void submit(PicoATE::Core::OperatorPromptResponse response)
    {
        QVariantMap values;
        if (response == PicoATE::Core::OperatorPromptResponse::Submitted &&
            !inputValues(values)) {
            return;
        }
        if (m_responseHandler) {
            m_responseHandler(m_instanceId, response, values);
        }
    }

    bool inputValues(QVariantMap& values)
    {
        if (!m_isInput) {
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
        m_imageLabel->setVisible(!image.trimmed().isEmpty());
        if (image.trimmed().isEmpty()) {
            return;
        }
        const auto path = ProjectResourcePaths::resolveImage(sequencePath, image);
        QPixmap pixmap(path);
        if (pixmap.isNull()) {
            m_imageLabel->setText(tr("Image unavailable: %1").arg(image));
            return;
        }
        m_imageLabel->setPixmap(pixmap.scaled(220, 80,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
    }

    QLabel* m_contextLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLabel* m_imageLabel = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QLabel* m_inputErrorLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QPushButton* m_passButton = nullptr;
    QPushButton* m_failButton = nullptr;
    ResponseHandler m_responseHandler;
    QString m_instanceId;
    QString m_presentationKey;
    QString m_inputType = QStringLiteral("text");
    bool m_isInput = false;
};

class UutOverviewCard final : public QAbstractButton
{
public:
    explicit UutOverviewCard(QWidget* parent = nullptr)
        : QAbstractButton(parent)
    {
        setObjectName(QStringLiteral("uutOverviewCard"));
        setCursor(Qt::PointingHandCursor);
        setCheckable(true);
        setMinimumSize(300, 238);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setAttribute(Qt::WA_Hover);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 17, 20, 16);
        layout->setSpacing(8);

        auto* heading = new QHBoxLayout;
        heading->setSpacing(8);
        m_uutLabel = new QLabel(this);
        m_uutLabel->setObjectName(QStringLiteral("uutOverviewName"));
        auto nameFont = m_uutLabel->font();
        nameFont.setPointSize(nameFont.pointSize() + 4);
        nameFont.setBold(true);
        m_uutLabel->setFont(nameFont);
        m_stateLabel = new QLabel(this);
        m_stateLabel->setObjectName(QStringLiteral("uutOverviewState"));
        m_stateLabel->setAlignment(Qt::AlignCenter);
        m_stateLabel->setMinimumWidth(98);
        auto stateFont = m_stateLabel->font();
        stateFont.setPointSize(stateFont.pointSize() + 2);
        stateFont.setBold(true);
        m_stateLabel->setFont(stateFont);
        heading->addWidget(m_uutLabel);
        heading->addStretch(1);
        heading->addWidget(m_stateLabel);
        layout->addLayout(heading);

        m_serialLabel = new QLabel(this);
        m_serialLabel->setObjectName(QStringLiteral("uutOverviewSerial"));
        layout->addWidget(m_serialLabel);

        auto* stepRow = new QHBoxLayout;
        stepRow->setSpacing(12);
        auto* stepBlock = new QVBoxLayout;
        stepBlock->setSpacing(3);
        m_stepCaptionLabel = new QLabel(this);
        m_stepCaptionLabel->setObjectName(
            QStringLiteral("uutOverviewStepCaption"));
        m_stepLabel = new QLabel(this);
        m_stepLabel->setObjectName(QStringLiteral("uutOverviewStep"));
        m_stepLabel->setWordWrap(true);
        m_stepLabel->setMinimumHeight(28);
        auto* currentStepRow = new QHBoxLayout;
        currentStepRow->setSpacing(8);
        m_currentStateLabel = new QLabel(this);
        m_currentStateLabel->setObjectName(
            QStringLiteral("uutOverviewCurrentState"));
        m_currentStateLabel->setAlignment(Qt::AlignCenter);
        m_currentStateLabel->setFixedWidth(58);
        m_currentStateLabel->setMinimumHeight(22);
        currentStepRow->addWidget(m_currentStateLabel);
        currentStepRow->addWidget(m_stepLabel, 1);
        stepBlock->addWidget(m_stepCaptionLabel);
        stepBlock->addLayout(currentStepRow);
        for (int index = 0; index < static_cast<int>(m_recentStateLabels.size());
             ++index) {
            auto* recentRow = new QHBoxLayout;
            recentRow->setSpacing(7);
            m_recentStateLabels[index] = new QLabel(this);
            m_recentStateLabels[index]->setObjectName(
                QStringLiteral("uutOverviewRecentState_%1").arg(index + 1));
            m_recentStateLabels[index]->setAlignment(Qt::AlignCenter);
            m_recentStateLabels[index]->setFixedWidth(50);
            m_recentStepLabels[index] = new QLabel(this);
            m_recentStepLabels[index]->setObjectName(
                QStringLiteral("uutOverviewRecentStep_%1").arg(index + 1));
            m_recentStepLabels[index]->setMinimumHeight(18);
            recentRow->addWidget(m_recentStateLabels[index]);
            recentRow->addWidget(m_recentStepLabels[index], 1);
            stepBlock->addLayout(recentRow);
            m_recentStateLabels[index]->hide();
            m_recentStepLabels[index]->hide();
        }
        m_progressPercentLabel = new QLabel(this);
        m_progressPercentLabel->setObjectName(
            QStringLiteral("uutOverviewPercent"));
        m_progressPercentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_progressPercentLabel->setMinimumWidth(78);
        auto progressFont = m_progressPercentLabel->font();
        progressFont.setPointSize(progressFont.pointSize() + 11);
        progressFont.setBold(true);
        m_progressPercentLabel->setFont(progressFont);
        auto* progressBlock = new QVBoxLayout;
        progressBlock->setSpacing(0);
        progressBlock->addWidget(m_progressPercentLabel);
        m_retryLabel = new QLabel(this);
        m_retryLabel->setObjectName(QStringLiteral("uutOverviewRetry"));
        m_retryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        progressBlock->addWidget(m_retryLabel);
        stepRow->addLayout(stepBlock, 1);
        stepRow->addLayout(progressBlock);
        layout->addLayout(stepRow, 1);

        m_progress = new QProgressBar(this);
        m_progress->setObjectName(QStringLiteral("uutOverviewProgress"));
        m_progress->setRange(0, 100);
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(9);
        layout->addWidget(m_progress);

        auto* footer = new QGridLayout;
        footer->setContentsMargins(0, 0, 0, 0);
        footer->setHorizontalSpacing(18);
        footer->setVerticalSpacing(2);
        m_completedCaptionLabel = new QLabel(tr("COMPLETED STEPS"), this);
        m_completedCaptionLabel->setObjectName(
            QStringLiteral("uutOverviewCaption"));
        m_progressLabel = new QLabel(this);
        m_progressLabel->setObjectName(QStringLiteral("uutOverviewMetric"));
        m_errorCaptionLabel = new QLabel(tr("ERROR CODE"), this);
        m_errorCaptionLabel->setObjectName(
            QStringLiteral("uutOverviewCaption"));
        m_errorLabel = new QLabel(this);
        m_errorLabel->setObjectName(QStringLiteral("uutOverviewError"));
        m_durationCaptionLabel = new QLabel(tr("DURATION"), this);
        m_durationCaptionLabel->setObjectName(
            QStringLiteral("uutOverviewCaption"));
        m_durationLabel = new QLabel(this);
        m_durationLabel->setObjectName(QStringLiteral("uutOverviewMetric"));
        m_durationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_durationCaptionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        footer->addWidget(m_completedCaptionLabel, 0, 0);
        footer->addWidget(m_errorCaptionLabel, 0, 1);
        footer->addWidget(m_durationCaptionLabel, 0, 2);
        footer->addWidget(m_progressLabel, 1, 0);
        footer->addWidget(m_errorLabel, 1, 1);
        footer->addWidget(m_durationLabel, 1, 2);
        footer->setColumnStretch(0, 1);
        footer->setColumnStretch(1, 1);
        footer->setColumnStretch(2, 1);
        layout->addLayout(footer);

        for (auto* label : {m_uutLabel, m_stateLabel, m_serialLabel,
                            m_stepCaptionLabel, m_currentStateLabel, m_stepLabel,
                            m_progressPercentLabel, m_retryLabel,
                            m_completedCaptionLabel,
                            m_progressLabel, m_errorCaptionLabel, m_errorLabel,
                            m_durationCaptionLabel, m_durationLabel}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        for (int index = 0; index < static_cast<int>(m_recentStateLabels.size());
             ++index) {
            m_recentStateLabels[index]->setAttribute(
                Qt::WA_TransparentForMouseEvents);
            m_recentStepLabels[index]->setAttribute(
                Qt::WA_TransparentForMouseEvents);
        }
    }

    void setCenteredRow(bool centered)
    {
        setSizePolicy(QSizePolicy::Expanding,
                      centered ? QSizePolicy::Preferred
                               : QSizePolicy::Expanding);
        setMaximumHeight(centered ? 280 : QWIDGETSIZE_MAX);
    }

    void showOperatorPrompt(
        const PicoATE::Core::RuntimeEvent& event,
        const QString& sequencePath,
        UutOverviewPromptOverlay::ResponseHandler responseHandler)
    {
        if (!m_promptOverlay) {
            m_promptOverlay = new UutOverviewPromptOverlay(this);
        }
        m_promptOverlay->setResponseHandler(std::move(responseHandler));
        m_promptOverlay->configure(event, sequencePath, m_entry.serialNumber);
        m_promptOverlay->setGeometry(rect().adjusted(2, 2, -2, -2));
        m_promptOverlay->show();
        m_promptOverlay->raise();
        update();
    }

    bool closeOperatorPrompt(const QString& instanceId)
    {
        if (!m_promptOverlay ||
            m_promptOverlay->currentInstanceId() != instanceId) {
            return false;
        }
        m_promptOverlay->hide();
        update();
        return true;
    }

    void clearOperatorPrompt()
    {
        if (m_promptOverlay) {
            m_promptOverlay->hide();
            update();
        }
    }

    bool hasOperatorPrompt() const
    {
        return m_promptOverlay && m_promptOverlay->isVisible();
    }

    QString currentPromptInstanceId() const
    {
        return hasOperatorPrompt()
            ? m_promptOverlay->currentInstanceId()
            : QString{};
    }

    QString currentPromptPresentationKey() const
    {
        return hasOperatorPrompt()
            ? m_promptOverlay->presentationKey()
            : QString{};
    }

    bool setOperatorPromptResponsePending(const QString& instanceId,
                                          bool pending)
    {
        if (!m_promptOverlay ||
            m_promptOverlay->currentInstanceId() != instanceId) {
            return false;
        }
        m_promptOverlay->setResponsePending(pending);
        return true;
    }

    void setEntry(const UutOverviewEntry& entry)
    {
        const bool stateChanged = !m_initialized || m_entry.state != entry.state ||
                                  m_entry.retryActive != entry.retryActive;
        const auto setTextIfChanged = [](QLabel* label, const QString& text) {
            if (label->text() != text) {
                label->setText(text);
            }
        };

        setTextIfChanged(m_uutLabel, entry.uutId);
        setTextIfChanged(m_stateLabel,
                         entry.retryActive
                             ? tr("RETRYING")
                             : uutOverviewStateName(entry.state).toUpper());
        setTextIfChanged(
            m_serialLabel,
            QStringLiteral("SN  %1").arg(entry.serialNumber.isEmpty()
                ? QStringLiteral("--") : entry.serialNumber));
        const bool terminalFailure = entry.state == UutOverviewState::Failed &&
                                     !entry.failedStep.trimmed().isEmpty();
        const auto displayedNodeId = terminalFailure ? entry.failedNodeId
                                                     : entry.currentNodeId;
        const auto displayedStep = terminalFailure ? entry.failedStep
                                                   : entry.currentStep;
        const auto displayedPhase = terminalFailure ? entry.failedPhase
                                                    : entry.currentPhase;
        const auto phaseName = [displayedPhase] {
            switch (displayedPhase) {
            case PicoATE::Core::ExecutionPhase::Setup:
                return QStringLiteral("SETUP");
            case PicoATE::Core::ExecutionPhase::Cleanup:
                return QStringLiteral("CLEANUP");
            case PicoATE::Core::ExecutionPhase::Main:
                return QString{};
            }
            return QString{};
        }();
        QString stepCaption;
        QString stepFallback;
        if (entry.retryActive) {
            stepCaption = phaseName.isEmpty()
                ? tr("RETRYING CURRENT STEP")
                : tr("RETRYING %1 STEP").arg(phaseName);
            stepFallback = tr("Preparing next attempt");
        } else switch (entry.state) {
        case UutOverviewState::Waiting:
            stepCaption = phaseName.isEmpty()
                ? tr("WAITING FOR")
                : tr("WAITING FOR %1 STEP").arg(phaseName);
            stepFallback = tr("Waiting to start");
            break;
        case UutOverviewState::Running:
        case UutOverviewState::Paused:
            stepCaption = phaseName.isEmpty()
                ? tr("CURRENT STEP")
                : tr("%1 STEP").arg(phaseName);
            stepFallback = tr("Preparing");
            break;
        case UutOverviewState::Passed:
            stepCaption = phaseName.isEmpty()
                ? tr("FINAL STEP")
                : tr("FINAL %1 STEP").arg(phaseName);
            stepFallback = tr("Completed");
            break;
        case UutOverviewState::Failed:
            stepCaption = phaseName.isEmpty()
                ? tr("FAILED STEP")
                : tr("FAILED %1 STEP").arg(phaseName);
            stepFallback = tr("Failed");
            break;
        case UutOverviewState::Stopped:
            stepCaption = tr("STOPPED AT");
            stepFallback = tr("Stopped");
            break;
        }
        setTextIfChanged(m_stepCaptionLabel, stepCaption);
        const auto step = displayedStep.isEmpty() ? stepFallback : displayedStep;
        setTextIfChanged(m_stepLabel, step);
        if (m_stepLabel->toolTip() != step) {
            m_stepLabel->setToolTip(step);
        }
        QString currentStateText;
        QColor currentStateColor;
        if (entry.retryActive) {
            currentStateText = QStringLiteral("RETRY");
            currentStateColor = QColor(QStringLiteral("#a87500"));
        } else {
            switch (entry.state) {
            case UutOverviewState::Waiting:
                currentStateText = QStringLiteral("WAIT");
                currentStateColor = QColor(QStringLiteral("#667680"));
                break;
            case UutOverviewState::Running:
                currentStateText = QStringLiteral("RUN");
                currentStateColor = QColor(QStringLiteral("#a87500"));
                break;
            case UutOverviewState::Paused:
                currentStateText = QStringLiteral("PAUSE");
                currentStateColor = QColor(QStringLiteral("#35677f"));
                break;
            case UutOverviewState::Passed:
                currentStateText = QStringLiteral("PASS");
                currentStateColor = QColor(QStringLiteral("#2f7548"));
                break;
            case UutOverviewState::Failed:
                currentStateText = QStringLiteral("FAIL");
                currentStateColor = QColor(QStringLiteral("#a43838"));
                break;
            case UutOverviewState::Stopped:
                currentStateText = QStringLiteral("STOP");
                currentStateColor = QColor(QStringLiteral("#875151"));
                break;
            }
        }
        setTextIfChanged(m_currentStateLabel, currentStateText);
        m_currentStateLabel->setStyleSheet(QStringLiteral(
            "background:%1;color:white;border-radius:3px;"
            "font-size:10px;font-weight:700;padding:2px 4px;")
            .arg(currentStateColor.name()));
        int recentRow = 0;
        for (int index = entry.recentSteps.size() - 1;
             index >= 0 && recentRow < static_cast<int>(m_recentStateLabels.size());
             --index) {
            const auto& recent = entry.recentSteps.at(index);
            if (!displayedNodeId.isEmpty() &&
                recent.nodeId == displayedNodeId) {
                continue;
            }
            auto* stateLabel = m_recentStateLabels[recentRow];
            auto* nameLabel = m_recentStepLabels[recentRow];
            const auto stateText = recentStepStateText(recent.state);
            const auto color = recentStepStateColor(recent.state);
            setTextIfChanged(stateLabel, stateText);
            setTextIfChanged(nameLabel, recent.displayName);
            stateLabel->setStyleSheet(QStringLiteral(
                "background:%1;color:white;border-radius:3px;"
                "font-size:9px;font-weight:700;padding:1px 3px;")
                .arg(color.name()));
            nameLabel->setStyleSheet(
                QStringLiteral("color:#66737b;font-size:10px;font-weight:600;"));
            nameLabel->setToolTip(recent.displayName);
            stateLabel->show();
            nameLabel->show();
            ++recentRow;
        }
        while (recentRow < static_cast<int>(m_recentStateLabels.size())) {
            m_recentStateLabels[recentRow]->hide();
            m_recentStepLabels[recentRow]->hide();
            ++recentRow;
        }
        if (m_progress->value() != entry.progress) {
            m_progress->setValue(entry.progress);
        }
        setTextIfChanged(m_progressPercentLabel,
                         QStringLiteral("%1%").arg(entry.progress));
        const bool showRetry = entry.retryActive && entry.retryAttempt > 0 &&
                               entry.retryMaxAttempts > 1;
        m_retryLabel->setVisible(showRetry);
        setTextIfChanged(
            m_retryLabel,
            showRetry
                ? tr("ATTEMPT %1 / %2")
                      .arg(entry.retryAttempt)
                      .arg(entry.retryMaxAttempts)
                : QString{});
        setTextIfChanged(
            m_progressLabel,
            QStringLiteral("%1 / %2")
                .arg(entry.completedSteps)
                .arg(entry.totalSteps));
        const bool terminal = entry.state == UutOverviewState::Passed ||
                               entry.state == UutOverviewState::Failed ||
                               entry.state == UutOverviewState::Stopped;
        const bool hasError = !entry.errorCode.trimmed().isEmpty();
        m_errorCaptionLabel->show();
        m_errorLabel->show();
        setTextIfChanged(m_errorLabel,
                         hasError ? entry.errorCode : QStringLiteral("--"));
        m_durationCaptionLabel->show();
        m_durationLabel->show();
        if (!entry.message.trimmed().isEmpty()) {
            setToolTip(entry.message);
        } else if (!toolTip().isEmpty()) {
            setToolTip({});
        }
        setTextIfChanged(m_durationLabel,
                          terminal ? compactDuration(entry.durationMs)
                                   : QStringLiteral("--:--.---"));

        m_entry = entry;
        if (stateChanged) {
            const auto colors = paletteFor(entry.retryActive
                                               ? UutOverviewState::Running
                                               : entry.state);
            m_stateLabel->setStyleSheet(QStringLiteral(
                "background:%1;color:%2;border:1px solid %3;"
                "border-radius:4px;padding:5px 10px;font-weight:700;")
                .arg(colors.background.name(), colors.accent.name(),
                     colors.border.name()));
            m_progress->setStyleSheet(QStringLiteral(
                "QProgressBar{background:#e3e8eb;border:0;border-radius:4px;}"
                "QProgressBar::chunk{background:%1;border-radius:4px;}")
                .arg(colors.accent.name()));
            m_progressPercentLabel->setStyleSheet(
                QStringLiteral("color:%1;").arg(colors.accent.name()));
        }
        m_errorLabel->setStyleSheet(
            hasError
                ? QStringLiteral("color:#a43838;font-weight:700;")
                : QStringLiteral("color:#6b7780;font-weight:600;"));
        m_initialized = true;
        update();
    }

    PicoATE::Core::UutId uutId() const { return m_entry.uutId; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QAbstractButton::resizeEvent(event);
        if (m_promptOverlay) {
            m_promptOverlay->setGeometry(rect().adjusted(2, 2, -2, -2));
            if (m_promptOverlay->isVisible()) {
                m_promptOverlay->raise();
            }
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto colors = paletteFor(m_entry.retryActive
                                     ? UutOverviewState::Running
                                     : m_entry.state);
        if (hasOperatorPrompt()) {
            colors.background = QColor(QStringLiteral("#e2f2fc"));
            colors.border = QColor(QStringLiteral("#6ea8ca"));
        } else if (underMouse()) {
            colors.background = colors.background.lighter(102);
        }
        QPen pen(isChecked() ? QColor(QStringLiteral("#3f5968"))
                             : colors.border);
        pen.setWidth(isChecked() ? 2 : 1);
        painter.setPen(pen);
        painter.setBrush(colors.background);
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 7, 7);
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::HoverEnter ||
            event->type() == QEvent::HoverLeave) {
            update();
        }
        return QAbstractButton::event(event);
    }

private:
    UutOverviewEntry m_entry;
    QLabel* m_uutLabel = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_serialLabel = nullptr;
    QLabel* m_stepCaptionLabel = nullptr;
    QLabel* m_currentStateLabel = nullptr;
    QLabel* m_stepLabel = nullptr;
    std::array<QLabel*, 2> m_recentStateLabels{};
    std::array<QLabel*, 2> m_recentStepLabels{};
    QLabel* m_progressPercentLabel = nullptr;
    QLabel* m_retryLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_completedCaptionLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QLabel* m_errorCaptionLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    QLabel* m_durationCaptionLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    UutOverviewPromptOverlay* m_promptOverlay = nullptr;
    bool m_initialized = false;
};

UutOverviewCard* overviewCardForUut(
    const QVector<QAbstractButton*>& cards,
    const PicoATE::Core::UutId& uutId)
{
    for (auto* button : cards) {
        auto* card = static_cast<UutOverviewCard*>(button);
        if (card && card->uutId() == uutId) {
            return card;
        }
    }
    return nullptr;
}

} // namespace

MultiUutOverviewWidget::MultiUutOverviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("multiUutOverview"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("UUT OVERVIEW"), this);
    title->setObjectName(QStringLiteral("multiUutOverviewTitle"));
    auto titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("multiUutOverviewSummary"));
    m_summaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_summaryLabel);
    root->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("multiUutOverviewScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_cardsHost = new QWidget(scroll);
    m_cardsHost->setObjectName(QStringLiteral("multiUutOverviewCards"));
    m_cardsLayout = new QGridLayout(m_cardsHost);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setHorizontalSpacing(14);
    m_cardsLayout->setVerticalSpacing(14);
    m_cardsLayout->setColumnStretch(0, 1);
    m_cardsLayout->setColumnStretch(1, 1);
    scroll->setWidget(m_cardsHost);
    root->addWidget(scroll, 1);

    setStyleSheet(QStringLiteral(
        "QWidget#multiUutOverview{background:#f4f6f7;}"
        "QLabel#multiUutOverviewTitle{color:#263139;}"
        "QLabel#multiUutOverviewSummary{color:#65737c;font-weight:600;}"
        "QLabel#uutOverviewSerial{color:#5e6b73;font-weight:600;}"
        "QLabel#uutOverviewCaption,QLabel#uutOverviewStepCaption{"
        "color:#7b878e;font-size:10px;font-weight:700;}"
        "QLabel#uutOverviewStep{color:#253038;font-weight:600;}"
        "QLabel#uutOverviewRetry{color:#a87500;font-size:10px;font-weight:700;}"
        "QLabel#uutOverviewError{font-weight:700;}"
        "QLabel#uutOverviewMetric{color:#6b7780;font-weight:600;}"));
}

void MultiUutOverviewWidget::setModel(UutOverviewModel* model)
{
    if (m_model == model) {
        return;
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::modelReset,
                this, &MultiUutOverviewWidget::rebuildCards);
        connect(m_model, &QAbstractItemModel::rowsInserted,
                this, &MultiUutOverviewWidget::rebuildCards);
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &MultiUutOverviewWidget::rebuildCards);
        connect(m_model, &QAbstractItemModel::dataChanged,
                this,
                [this](const QModelIndex& topLeft,
                       const QModelIndex& bottomRight) {
                    refreshCardRange(topLeft.row(), bottomRight.row());
                });
    }
    rebuildCards();
}

UutOverviewModel* MultiUutOverviewWidget::model() const
{
    return m_model;
}

void MultiUutOverviewWidget::setSelectedUutId(
    const PicoATE::Core::UutId& uutId)
{
    m_selectedUutId = uutId;
    refreshCards();
}

PicoATE::Core::UutId MultiUutOverviewWidget::selectedUutId() const
{
    return m_selectedUutId;
}

bool MultiUutOverviewWidget::presentOperatorPrompt(
    const PicoATE::Core::RuntimeEvent& event,
    const QString& sequencePath)
{
    const auto instanceId = event.details.value(
        QStringLiteral("promptInstanceId")).toString();
    if (!isVisible() || instanceId.isEmpty() || event.uutId.isEmpty()) {
        return false;
    }

    auto* card = overviewCardForUut(m_cards, event.uutId);
    if (!card) {
        return false;
    }

    const auto currentId = m_currentPromptByUut.value(event.uutId);
    if (!currentId.isEmpty() && currentId != instanceId) {
        const auto current = m_activePrompts.constFind(currentId);
        const auto currentKey = current == m_activePrompts.constEnd()
            ? QString{}
            : promptPresentationKey(current->event);
        const auto nextKey = promptPresentationKey(event);
        if (currentKey.isEmpty() || nextKey.isEmpty() || currentKey != nextKey) {
            return false;
        }
    }

    m_activePrompts.insert(instanceId, ActivePrompt{event, sequencePath});
    m_currentPromptByUut.insert(event.uutId, instanceId);
    card->showOperatorPrompt(
        event,
        sequencePath,
        [this](const QString& responseInstanceId,
               PicoATE::Core::OperatorPromptResponse response,
               const QVariantMap& values) {
            emit operatorPromptResponseRequested(responseInstanceId,
                                                  response,
                                                  values);
        });
    return true;
}

bool MultiUutOverviewWidget::closeOperatorPrompt(const QString& instanceId)
{
    const auto prompt = m_activePrompts.find(instanceId);
    if (prompt == m_activePrompts.end()) {
        return false;
    }
    const auto uutId = prompt->event.uutId;
    m_activePrompts.erase(prompt);
    if (m_currentPromptByUut.value(uutId) != instanceId) {
        return true;
    }

    m_currentPromptByUut.remove(uutId);
    if (auto* card = overviewCardForUut(m_cards, uutId)) {
        card->closeOperatorPrompt(instanceId);
    }
    return true;
}

bool MultiUutOverviewWidget::hasOperatorPrompt(const QString& instanceId) const
{
    return m_activePrompts.contains(instanceId);
}

bool MultiUutOverviewWidget::setOperatorPromptResponsePending(
    const QString& instanceId,
    bool pending)
{
    const auto prompt = m_activePrompts.constFind(instanceId);
    if (prompt == m_activePrompts.constEnd() ||
        m_currentPromptByUut.value(prompt->event.uutId) != instanceId) {
        return false;
    }
    auto* card = overviewCardForUut(m_cards, prompt->event.uutId);
    return card && card->setOperatorPromptResponsePending(instanceId, pending);
}

void MultiUutOverviewWidget::clearOperatorPrompts()
{
    m_activePrompts.clear();
    m_currentPromptByUut.clear();
    for (auto* button : std::as_const(m_cards)) {
        static_cast<UutOverviewCard*>(button)->clearOperatorPrompt();
    }
}

void MultiUutOverviewWidget::rebuildCards()
{
    if (!isVisible()) {
        m_rebuildPending = true;
        return;
    }
    m_rebuildPending = false;
    m_refreshPending = false;
    m_cards.clear();
    while (auto* item = m_cardsLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->setObjectName({});
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (int row = 0; row < m_gridRowCount; ++row) {
        m_cardsLayout->setRowStretch(row, 0);
    }
    for (int column = 0; column < m_gridColumnCount; ++column) {
        m_cardsLayout->setColumnStretch(column, 0);
    }
    m_gridRowCount = 0;
    m_gridColumnCount = 0;
    if (!m_model) {
        updateSummary();
        return;
    }
    const int cardCount = m_model->rowCount();
    const int columns = cardCount <= 4 ? 2 : 3;
    const int gridRows = qMax(1, (cardCount + columns - 1) / columns);
    const bool centeredPair = cardCount == 2;
    const int firstCardRow = centeredPair ? 1 : 0;
    m_cardsLayout->setAlignment({});
    for (int column = 0; column < columns; ++column) {
        m_cardsLayout->setColumnStretch(column, 1);
    }
    if (centeredPair) {
        m_cardsLayout->addItem(
            new QSpacerItem(0, 0,
                            QSizePolicy::Minimum,
                            QSizePolicy::Expanding),
            0, 0, 1, columns);
        m_cardsLayout->setRowStretch(0, 1);
        m_cardsLayout->setRowStretch(1, 0);
        m_cardsLayout->setRowStretch(2, 1);
        m_cardsLayout->addItem(
            new QSpacerItem(0, 0,
                            QSizePolicy::Minimum,
                            QSizePolicy::Expanding),
            2, 0, 1, columns);
    } else {
        for (int row = 0; row < gridRows; ++row) {
            m_cardsLayout->setRowStretch(row, 1);
        }
    }
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto* card = new UutOverviewCard(m_cardsHost);
        card->setCenteredRow(centeredPair);
        card->setObjectName(QStringLiteral("uutOverviewCard_%1").arg(row + 1));
        if (const auto entry = m_model->entryAt(row)) {
            card->setEntry(*entry);
            card->setChecked(entry->uutId == m_selectedUutId);
        }
        connect(card, &QAbstractButton::clicked, this, [this, card] {
            m_selectedUutId = card->uutId();
            refreshCards();
            emit uutActivated(m_selectedUutId);
        });
        restoreOperatorPrompt(card);
        m_cardsLayout->addWidget(card,
                                 firstCardRow + row / columns,
                                 row % columns);
        m_cards.push_back(card);
    }
    m_gridRowCount = centeredPair ? 3 : gridRows;
    m_gridColumnCount = columns;
    updateSummary();
}

void MultiUutOverviewWidget::refreshCards()
{
    if (!m_model) {
        return;
    }
    refreshCardRange(0, m_model->rowCount() - 1);
}

void MultiUutOverviewWidget::refreshCardRange(int firstRow, int lastRow)
{
    if (!isVisible()) {
        m_refreshPending = true;
        return;
    }
    if (!m_model || m_cards.size() != m_model->rowCount()) {
        rebuildCards();
        return;
    }
    m_refreshPending = false;
    firstRow = qBound(0, firstRow, m_model->rowCount());
    lastRow = qBound(-1, lastRow, m_model->rowCount() - 1);
    for (int row = firstRow; row <= lastRow; ++row) {
        auto* card = static_cast<UutOverviewCard*>(m_cards.at(row));
        const auto entry = m_model->entryAt(row);
        if (!card || !entry) {
            continue;
        }
        card->setEntry(*entry);
        card->setChecked(entry->uutId == m_selectedUutId);
    }
    updateSummary();
}

void MultiUutOverviewWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_rebuildPending) {
        rebuildCards();
    } else if (m_refreshPending) {
        refreshCards();
    }
}

void MultiUutOverviewWidget::updateSummary()
{
    int running = 0;
    int passed = 0;
    int failed = 0;
    int waiting = 0;
    if (m_model) {
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const auto entry = m_model->entryAt(row);
            if (!entry) {
                continue;
            }
            if (entry->retryActive) {
                ++running;
                continue;
            }
            switch (entry->state) {
            case UutOverviewState::Running:
            case UutOverviewState::Paused: ++running; break;
            case UutOverviewState::Passed: ++passed; break;
            case UutOverviewState::Failed:
            case UutOverviewState::Stopped: ++failed; break;
            case UutOverviewState::Waiting: ++waiting; break;
            }
        }
    }
    m_summaryLabel->setText(
        tr("RUNNING %1   PASS %2   FAIL %3   WAITING %4")
            .arg(running).arg(passed).arg(failed).arg(waiting));
}

void MultiUutOverviewWidget::restoreOperatorPrompt(QAbstractButton* button)
{
    auto* card = static_cast<UutOverviewCard*>(button);
    if (!card) {
        return;
    }
    const auto instanceId = m_currentPromptByUut.value(card->uutId());
    const auto prompt = m_activePrompts.constFind(instanceId);
    if (instanceId.isEmpty() || prompt == m_activePrompts.constEnd()) {
        return;
    }
    card->showOperatorPrompt(
        prompt->event,
        prompt->sequencePath,
        [this](const QString& responseInstanceId,
               PicoATE::Core::OperatorPromptResponse response,
               const QVariantMap& values) {
            emit operatorPromptResponseRequested(responseInstanceId,
                                                  response,
                                                  values);
        });
}

} // namespace PicoATE::Ui
