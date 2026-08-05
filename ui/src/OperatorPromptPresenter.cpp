#include "OperatorPromptPresenter.h"

#include "ExecutionViewModel.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace PicoATE::Ui {

namespace {

QString resolvePromptImagePath(const QString& configuredImage)
{
    const auto configured = configuredImage.trimmed();
    if (configured.isEmpty()) {
        return {};
    }

    const QFileInfo direct(configured);
    if (direct.isAbsolute()) {
        return direct.absoluteFilePath();
    }

    const auto normalized = QDir::fromNativeSeparators(configured);
    const bool includesImageFolder =
        normalized.compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0 ||
        normalized.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive);
    const QStringList roots = {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath(),
    };

    QString firstCandidate;
    for (const auto& root : roots) {
        const auto candidate = QDir(root).absoluteFilePath(
            includesImageFolder
                ? normalized
                : QStringLiteral("image/%1").arg(normalized));
        if (firstCandidate.isEmpty()) {
            firstCandidate = candidate;
        }
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return firstCandidate;
}

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

    void configure(const PicoATE::Core::RuntimeEvent& event)
    {
        const auto mode = event.details.value("mode").toString();
        m_currentInstanceId = event.details.value("promptInstanceId").toString();
        setWindowTitle(event.details.value("title").toString().isEmpty()
                           ? tr("Message")
                           : event.details.value("title").toString());
        m_messageLabel->setText(
            event.details.value("message", event.message).toString());
        updateImage(event.details.value("image").toString());

        const bool notice = mode == QStringLiteral("notice");
        const bool judgment = mode == QStringLiteral("judgment");
        m_confirmButton->setVisible(!notice && !judgment);
        m_passButton->setVisible(judgment);
        m_failButton->setVisible(judgment);
        m_confirmButton->setText(
            event.details.value("confirmText", QStringLiteral("OK")).toString());
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
    void updateImage(const QString& image)
    {
        m_imageLabel->clear();
        m_imageLabel->setToolTip({});
        m_imageLabel->setVisible(!image.trimmed().isEmpty());
        if (image.trimmed().isEmpty()) {
            return;
        }

        const auto imagePath = resolvePromptImagePath(image);
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
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QPushButton* m_passButton = nullptr;
    QPushButton* m_failButton = nullptr;
    QString m_currentInstanceId;
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

void OperatorPromptPresenter::showPrompt(const PicoATE::Core::RuntimeEvent& event)
{
    const auto instanceId = event.details.value("promptInstanceId").toString();
    if (instanceId.isEmpty() || m_dialogs.contains(instanceId)) {
        return;
    }

    const auto mode = event.details.value("mode").toString();
    const bool notice = mode == QStringLiteral("notice");
    const auto key = presentationKey(event);
    auto* dialog = key.isEmpty()
        ? nullptr
        : static_cast<OperatorPromptDialog*>(m_dialogsByKey.value(key).data());
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
    dialog->configure(event);
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
            if (m_viewModel && m_viewModel->respondToOperatorPrompt(instanceId,
                                                                    response)) {
                dialog->setResponsePending(true);
            }
        });
    };
    bindResponse(dialog->confirmButton(),
                 PicoATE::Core::OperatorPromptResponse::Confirmed);
    bindResponse(dialog->passButton(),
                 PicoATE::Core::OperatorPromptResponse::Passed);
    bindResponse(dialog->failButton(),
                 PicoATE::Core::OperatorPromptResponse::Failed);

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
