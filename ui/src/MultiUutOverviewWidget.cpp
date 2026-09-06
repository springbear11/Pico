#include "UiTextBinding.h"
#include "MultiUutOverviewWidget.h"

#include "LoadingSpinner.h"
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
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

using PicoATE::Ui::uiText;
using PicoATE::Ui::uiStateText;

namespace PicoATE::Ui {

namespace {

constexpr int PairCardMinimumHeight = 280;
constexpr int PairCardMaximumHeight = 460;
constexpr double PairCardHeightRatio = 0.82;

struct CardPalette {
    QColor background;
    QColor border;
    QColor accent;
};

CardPalette paletteFor(UutOverviewState state)
{
    switch (state) {
    case UutOverviewState::Disabled:
        return {QColor(QStringLiteral("#eef1f2")),
                QColor(QStringLiteral("#c7ced2")),
                QColor(QStringLiteral("#7d888f"))};
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

void setTextIfChanged(QLabel* label, const QString& text)
{
    if (label && label->text() != text) {
        label->setText(text);
    }
}

QString recentStepStateText(PicoATE::Core::ActivationState state)
{
    using PicoATE::Core::ActivationState;
    switch (state) {
    case ActivationState::Passed: return uiText("PASS");
    case ActivationState::Failed: return uiText("FAIL");
    case ActivationState::Error: return uiText("ERROR");
    case ActivationState::Timeout: return uiText("TIMEOUT");
    case ActivationState::Cancelled: return uiText("STOP");
    case ActivationState::Skipped: return uiText("SKIP");
    default: return uiText("DONE");
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

int periodicTaskPriority(PicoATE::Core::PeriodicTaskState state)
{
    using PicoATE::Core::PeriodicTaskState;
    switch (state) {
    case PeriodicTaskState::Running: return 5;
    case PeriodicTaskState::Waiting: return 3;
    case PeriodicTaskState::Passed:
    case PeriodicTaskState::Failed:
        return 3;
    case PeriodicTaskState::Stopped: return 1;
    }
    return 0;
}

QString periodicStateText(const PeriodicTaskOverviewEntry& task)
{
    using PicoATE::Core::PeriodicTaskState;
    switch (task.state) {
    case PeriodicTaskState::Waiting: return uiText("WAITING");
    case PeriodicTaskState::Running: return uiText("RUNNING");
    case PeriodicTaskState::Passed:
    case PeriodicTaskState::Failed:
        return uiText("WAITING");
    case PeriodicTaskState::Stopped: return uiText("STOPPED");
    }
    return uiText("WAITING");
}

QColor periodicStateColor(PicoATE::Core::PeriodicTaskState state)
{
    using PicoATE::Core::PeriodicTaskState;
    switch (state) {
    case PeriodicTaskState::Running: return QColor(QStringLiteral("#a87500"));
    case PeriodicTaskState::Passed:
    case PeriodicTaskState::Failed:
        return QColor(QStringLiteral("#35677f"));
    case PeriodicTaskState::Stopped: return QColor(QStringLiteral("#667680"));
    case PeriodicTaskState::Waiting: return QColor(QStringLiteral("#35677f"));
    }
    return QColor(QStringLiteral("#667680"));
}

QString compactCountdown(qint64 remainingMs)
{
    remainingMs = qMax<qint64>(0, remainingMs);
    if (remainingMs < 60000) {
        return QStringLiteral("%1 s").arg(remainingMs / 1000.0, 0, 'f', 1);
    }
    const auto tenths = (remainingMs % 1000) / 100;
    const auto totalSeconds = remainingMs / 1000;
    const auto seconds = totalSeconds % 60;
    const auto minutes = totalSeconds / 60;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(tenths);
}

QString periodicResultText(PicoATE::Core::NodeOutcome outcome)
{
    using PicoATE::Core::NodeOutcome;
    switch (outcome) {
    case NodeOutcome::Passed: return uiText("LAST PASS");
    case NodeOutcome::Failed:
    case NodeOutcome::Error:
    case NodeOutcome::Timeout:
        return uiText("LAST FAIL");
    default:
        return {};
    }
}

QColor periodicResultColor(PicoATE::Core::NodeOutcome outcome)
{
    return outcome == PicoATE::Core::NodeOutcome::Passed
        ? QColor(QStringLiteral("#2f7548"))
        : QColor(QStringLiteral("#a43838"));
}

class PeriodicTaskStatusPanel final : public QFrame
{
public:
    explicit PeriodicTaskStatusPanel(bool shared, QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(shared ? QStringLiteral("sharedPeriodicTaskPanel")
                             : QStringLiteral("uutOverviewPeriodicPanel"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFrameShape(QFrame::NoFrame);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(shared ? 12 : 9, shared ? 7 : 5,
                                 shared ? 12 : 9, shared ? 7 : 5);
        root->setSpacing(3);
        m_captionLabel = makeUiLabel(
            shared ? "SHARED PERIODIC TASK" : "PERIODIC TASK", this);
        m_captionLabel->setObjectName(QStringLiteral("periodicTaskCaption"));
        root->addWidget(m_captionLabel);

        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        m_nameLabel = new QLabel(this);
        m_nameLabel->setObjectName(shared
            ? QStringLiteral("sharedPeriodicTaskName")
            : QStringLiteral("uutOverviewPeriodicName"));
        m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_stateLabel = new QLabel(this);
        m_stateLabel->setObjectName(shared
            ? QStringLiteral("sharedPeriodicTaskState")
            : QStringLiteral("uutOverviewPeriodicState"));
        m_stateLabel->setAlignment(Qt::AlignCenter);
        m_resultLabel = new QLabel(this);
        m_resultLabel->setObjectName(shared
            ? QStringLiteral("sharedPeriodicTaskResult")
            : QStringLiteral("uutOverviewPeriodicResult"));
        m_resultLabel->setAlignment(Qt::AlignCenter);
        m_detailLabel = new QLabel(this);
        m_detailLabel->setObjectName(shared
            ? QStringLiteral("sharedPeriodicTaskCountdown")
            : QStringLiteral("uutOverviewPeriodicCountdown"));
        m_detailLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_detailLabel->setMinimumWidth(92);
        row->addWidget(m_nameLabel, 1);
        row->addWidget(m_stateLabel);
        row->addWidget(m_resultLabel);
        row->addWidget(m_detailLabel);
        root->addLayout(row);

        setStyleSheet(QStringLiteral(
            "QFrame#sharedPeriodicTaskPanel,QFrame#uutOverviewPeriodicPanel{"
            "background:#eef3f6;border:1px solid #d4dde2;border-radius:5px;}"
            "QLabel#periodicTaskCaption{color:#718089;font-size:9px;"
            "font-weight:700;}"
            "QLabel#sharedPeriodicTaskName,QLabel#uutOverviewPeriodicName{"
            "color:#263139;font-weight:650;}"
            "QLabel#sharedPeriodicTaskCountdown,QLabel#uutOverviewPeriodicCountdown{"
            "color:#52636d;font-size:10px;font-weight:700;}"));
        hide();
    }

    void setTasks(QVector<PeriodicTaskOverviewEntry> tasks)
    {
        m_tasks = std::move(tasks);
        m_featuredIndex = -1;
        for (int index = 0; index < m_tasks.size(); ++index) {
            if (m_featuredIndex < 0 ||
                periodicTaskPriority(m_tasks[index].state) >
                    periodicTaskPriority(m_tasks[m_featuredIndex].state) ||
                (periodicTaskPriority(m_tasks[index].state) ==
                     periodicTaskPriority(m_tasks[m_featuredIndex].state) &&
                 m_tasks[index].updatedAtUtc >
                     m_tasks[m_featuredIndex].updatedAtUtc)) {
                m_featuredIndex = index;
            }
        }

        setVisible(m_featuredIndex >= 0);
        if (m_featuredIndex < 0) {
            return;
        }

        const auto& task = m_tasks.at(m_featuredIndex);
        auto name = task.displayName.trimmed().isEmpty() ? task.nodeId
                                                        : task.displayName;
        if (m_tasks.size() > 1) {
            name += uiText("  +%1").arg(m_tasks.size() - 1);
        }
        setTextIfChanged(m_nameLabel, name);
        setTextIfChanged(m_stateLabel, periodicStateText(task));
        const auto color = periodicStateColor(task.state);
        m_stateLabel->setStyleSheet(QStringLiteral(
            "background:%1;color:white;border-radius:3px;"
            "font-size:9px;font-weight:700;padding:2px 5px;")
            .arg(color.name()));
        const auto resultText = periodicResultText(task.lastOutcome);
        setTextIfChanged(m_resultLabel, resultText);
        m_resultLabel->setVisible(!resultText.isEmpty());
        if (!resultText.isEmpty()) {
            m_resultLabel->setStyleSheet(QStringLiteral(
                "background:%1;color:white;border-radius:3px;"
                "font-size:9px;font-weight:700;padding:2px 5px;")
                .arg(periodicResultColor(task.lastOutcome).name()));
        }
        setToolTip(task.message.trimmed().isEmpty()
            ? task.errorCode
            : QStringLiteral("%1%2%3")
                  .arg(task.errorCode,
                       task.errorCode.isEmpty() ? QString{} : QStringLiteral(" | "),
                       task.message));
        refreshCountdown();
    }

    void refreshCountdown()
    {
        if (m_featuredIndex < 0 || m_featuredIndex >= m_tasks.size()) {
            return;
        }
        const auto& task = m_tasks.at(m_featuredIndex);
        QString detail;
        if (task.state == PicoATE::Core::PeriodicTaskState::Running) {
            detail = task.counter != 0
                ? uiText("COUNT %1").arg(task.counter)
                : uiText("RUN %1").arg(qMax(1, task.invocationIndex));
        } else if (task.nextDueAtUtc.isValid()) {
            const auto remainingMs =
                QDateTime::currentDateTimeUtc().msecsTo(task.nextDueAtUtc);
            detail = remainingMs <= 0
                ? uiText("DUE")
                : uiText("NEXT IN %1").arg(compactCountdown(remainingMs));
        }
        setTextIfChanged(m_detailLabel, detail);
    }

private:
    QVector<PeriodicTaskOverviewEntry> m_tasks;
    int m_featuredIndex = -1;
    QLabel* m_captionLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_detailLabel = nullptr;
};

QString compactResourceIds(
    const QVector<ResourceUsageOverviewEntry>& entries)
{
    QStringList ids;
    for (const auto& entry : entries) {
        for (const auto& resourceId : entry.resourceIds) {
            if (!ids.contains(resourceId)) {
                ids.push_back(resourceId);
            }
        }
    }
    ids.sort();
    constexpr int maximumVisible = 3;
    const int hidden = qMax(0, ids.size() - maximumVisible);
    if (hidden > 0) {
        ids = ids.mid(0, maximumVisible);
        ids.push_back(uiText("+%1").arg(hidden));
    }
    return ids.join(QStringLiteral("  |  "));
}

QString compactResourceBadgeText(
    const QVector<ResourceUsageOverviewEntry>& entries)
{
    QStringList ids;
    for (const auto& entry : entries) {
        for (const auto& resourceId : entry.resourceIds) {
            if (!resourceId.isEmpty() && !ids.contains(resourceId)) {
                ids.push_back(resourceId);
            }
        }
    }
    ids.sort();
    if (ids.isEmpty()) {
        return {};
    }
    auto text = ids.front();
    if (ids.size() > 1) {
        text += uiText(" +%1").arg(ids.size() - 1);
    }
    return text;
}

QString compactUutIds(const QVector<ResourceUsageOverviewEntry>& entries,
                      bool blockers)
{
    QStringList ids;
    for (const auto& entry : entries) {
        const auto values = blockers ? entry.blockingUutIds
                                     : QVector<PicoATE::Core::UutId>{entry.uutId};
        for (const auto& uutId : values) {
            if (!uutId.isEmpty() && !ids.contains(uutId)) {
                ids.push_back(uutId);
            }
        }
    }
    ids.sort();
    return ids.join(QStringLiteral(", "));
}

class ResourceStatusPanel final : public QFrame
{
public:
    explicit ResourceStatusPanel(bool shared, QWidget* parent = nullptr)
        : QFrame(parent)
        , m_shared(shared)
    {
        setObjectName(shared ? QStringLiteral("sharedResourcePanel")
                             : QStringLiteral("uutOverviewResourcePanel"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFrameShape(QFrame::NoFrame);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(shared ? 12 : 9, shared ? 7 : 5,
                                 shared ? 12 : 9, shared ? 7 : 5);
        root->setSpacing(3);
        auto* caption = makeUiLabel(
            shared ? "SHARED RESOURCES" : "RESOURCES", this);
        caption->setObjectName(QStringLiteral("resourceStatusCaption"));
        root->addWidget(caption);

        const auto addRow = [this, root](const char* badgeText,
                                        const QString& badgeObject,
                                        const QString& detailObject,
                                        QLabel*& row,
                                        QLabel*& detail) {
            row = new QLabel(this);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            auto* badge = makeUiLabel(badgeText, row);
            badge->setObjectName(badgeObject);
            badge->setAlignment(Qt::AlignCenter);
            badge->setFixedWidth(58);
            detail = new QLabel(row);
            detail->setObjectName(detailObject);
            detail->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Preferred);
            layout->addWidget(badge);
            layout->addWidget(detail, 1);
            root->addWidget(row);
        };
        addRow("USING", QStringLiteral("resourceUsingBadge"),
               shared ? QStringLiteral("sharedResourceUsing")
                      : QStringLiteral("uutOverviewResourceUsing"),
               m_usingRow, m_usingLabel);
        addRow("WAIT", QStringLiteral("resourceWaitingBadge"),
               shared ? QStringLiteral("sharedResourceWaiting")
                      : QStringLiteral("uutOverviewResourceWaiting"),
               m_waitingRow, m_waitingLabel);

        setStyleSheet(QStringLiteral(
            "QFrame#sharedResourcePanel,QFrame#uutOverviewResourcePanel{"
            "background:#eef3f6;border:1px solid #d4dde2;border-radius:5px;}"
            "QLabel#resourceStatusCaption{color:#718089;font-size:9px;"
            "font-weight:700;}"
            "QLabel#resourceUsingBadge{background:#35677f;color:white;"
            "border-radius:3px;font-size:9px;font-weight:700;padding:2px 4px;}"
            "QLabel#resourceWaitingBadge{background:#a87500;color:white;"
            "border-radius:3px;font-size:9px;font-weight:700;padding:2px 4px;}"
            "QLabel#sharedResourceUsing,QLabel#uutOverviewResourceUsing,"
            "QLabel#sharedResourceWaiting,QLabel#uutOverviewResourceWaiting{"
            "color:#34434c;font-size:10px;font-weight:650;}"));
        hide();
    }

    void setResources(QVector<ResourceUsageOverviewEntry> held,
                      QVector<ResourceUsageOverviewEntry> waiting)
    {
        m_held = std::move(held);
        m_waiting = std::move(waiting);
        m_usingRow->setVisible(!m_held.isEmpty());
        m_waitingRow->setVisible(!m_waiting.isEmpty());
        setVisible(!m_held.isEmpty() || !m_waiting.isEmpty());

        auto usingText = compactResourceIds(m_held);
        if (m_shared) {
            const auto executors = compactUutIds(m_held, false);
            if (!executors.isEmpty()) {
                usingText += uiText("  |  EXECUTOR %1").arg(executors);
            }
        }
        setTextIfChanged(m_usingLabel, usingText);
        refreshWaitDuration();
    }

    void refreshWaitDuration()
    {
        if (m_waiting.isEmpty()) {
            return;
        }
        auto waitingText = compactResourceIds(m_waiting);
        const auto blockers = compactUutIds(m_waiting, true);
        if (!blockers.isEmpty()) {
            waitingText += uiText("  |  HELD BY %1").arg(blockers);
        }
        QDateTime oldest;
        for (const auto& entry : m_waiting) {
            if (entry.waitingSinceUtc.isValid() &&
                (!oldest.isValid() || entry.waitingSinceUtc < oldest)) {
                oldest = entry.waitingSinceUtc;
            }
        }
        if (oldest.isValid()) {
            const auto elapsed = qMax<qint64>(
                0, oldest.msecsTo(QDateTime::currentDateTimeUtc()));
            waitingText += uiText("  |  %1 s").arg(elapsed / 1000.0, 0, 'f', 1);
        }
        setTextIfChanged(m_waitingLabel, waitingText);
    }

private:
    bool m_shared = false;
    QVector<ResourceUsageOverviewEntry> m_held;
    QVector<ResourceUsageOverviewEntry> m_waiting;
    QLabel* m_usingRow = nullptr;
    QLabel* m_waitingRow = nullptr;
    QLabel* m_usingLabel = nullptr;
    QLabel* m_waitingLabel = nullptr;
};

class ResourceStatusBadge final : public QFrame
{
public:
    explicit ResourceStatusBadge(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("uutOverviewResourceBadge"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFrameShape(QFrame::NoFrame);
        setFixedSize(118, 48);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(7, 4, 7, 4);
        layout->setSpacing(1);

        m_usingLabel = new QLabel(this);
        m_usingLabel->setObjectName(
            QStringLiteral("uutOverviewResourceUsing"));
        m_usingLabel->setAlignment(Qt::AlignCenter);
        m_waitingLabel = new QLabel(this);
        m_waitingLabel->setObjectName(
            QStringLiteral("uutOverviewResourceWaiting"));
        m_waitingLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_usingLabel);
        layout->addWidget(m_waitingLabel);

        setStyleSheet(QStringLiteral(
            "QFrame#uutOverviewResourceBadge{background:#eef3f6;"
            "border:1px solid #cbd8df;border-radius:4px;}"
            "QLabel#uutOverviewResourceUsing{color:#35677f;font-size:9px;"
            "font-weight:700;}"
            "QLabel#uutOverviewResourceWaiting{color:#a87500;font-size:9px;"
            "font-weight:700;}"));
        hide();
    }

    void setResources(QVector<ResourceUsageOverviewEntry> held,
                      QVector<ResourceUsageOverviewEntry> waiting)
    {
        m_held = std::move(held);
        m_waiting = std::move(waiting);
        m_usingLabel->setVisible(!m_held.isEmpty());
        m_waitingLabel->setVisible(!m_waiting.isEmpty());
        setTextIfChanged(
            m_usingLabel,
            m_held.isEmpty()
                ? QString{}
                : uiText("USE  %1").arg(compactResourceBadgeText(m_held)));
        setTextIfChanged(
            m_waitingLabel,
            m_waiting.isEmpty()
                ? QString{}
                : uiText("WAIT  %1").arg(compactResourceBadgeText(m_waiting)));
        setVisible(!m_held.isEmpty() || !m_waiting.isEmpty());
        refreshWaitDuration();
    }

    void refreshWaitDuration()
    {
        QStringList details;
        if (!m_held.isEmpty()) {
            details.push_back(uiText("USING: %1").arg(compactResourceIds(m_held)));
        }
        if (!m_waiting.isEmpty()) {
            auto waitingText =
                uiText("WAITING: %1").arg(compactResourceIds(m_waiting));
            const auto blockers = compactUutIds(m_waiting, true);
            if (!blockers.isEmpty()) {
                waitingText += uiText("\nHELD BY: %1").arg(blockers);
            }
            QDateTime oldest;
            for (const auto& entry : m_waiting) {
                if (entry.waitingSinceUtc.isValid() &&
                    (!oldest.isValid() || entry.waitingSinceUtc < oldest)) {
                    oldest = entry.waitingSinceUtc;
                }
            }
            if (oldest.isValid()) {
                const auto elapsed = qMax<qint64>(
                    0, oldest.msecsTo(QDateTime::currentDateTimeUtc()));
                waitingText +=
                    uiText("\nWAIT TIME: %1 s").arg(elapsed / 1000.0, 0, 'f', 1);
            }
            details.push_back(waitingText);
        }
        setToolTip(details.join(QStringLiteral("\n")));
    }

private:
    QVector<ResourceUsageOverviewEntry> m_held;
    QVector<ResourceUsageOverviewEntry> m_waiting;
    QLabel* m_usingLabel = nullptr;
    QLabel* m_waitingLabel = nullptr;
};

QString promptPresentationKey(const PicoATE::Core::RuntimeEvent& event)
{
    return event.details.value(QStringLiteral("dialogKey"))
        .toString()
        .trimmed();
}

QString promptMode(const PicoATE::Core::RuntimeEvent& event)
{
    return event.details.value(QStringLiteral("mode"))
        .toString()
        .trimmed()
        .toLower();
}

bool mayReplaceDisplayedPrompt(
    const PicoATE::Core::RuntimeEvent& current,
    const PicoATE::Core::RuntimeEvent& next)
{
    const auto currentKey = promptPresentationKey(current);
    const auto nextKey = promptPresentationKey(next);
    if (!currentKey.isEmpty() && currentKey == nextKey) {
        return true;
    }

    // A condition-close notice does not require an operator response. If the
    // same UUT reaches an interactive prompt before its close event is
    // presented, the interactive prompt must take over the card instead of
    // falling back to a window-level dialog.
    return promptMode(current) == QStringLiteral("notice") &&
           promptMode(next) != QStringLiteral("notice");
}

bool isOncePerBatchPrompt(const PicoATE::Core::RuntimeEvent& event)
{
    auto scope = event.details.value(QStringLiteral("executionScope"))
                     .toString()
                     .trimmed()
                     .toLower();
    scope.remove(QLatin1Char('-'));
    scope.remove(QLatin1Char('_'));
    scope.remove(QLatin1Char(' '));
    return scope == QStringLiteral("onceperbatch");
}

class CleanupProgressOverlay final : public QWidget
{
public:
    enum class Stage {
        Stopping,
        CleaningUp
    };

    explicit CleanupProgressOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("multiUutCleanupOverlay"));
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(20, 20, 20, 20);
        root->addStretch(1);

        auto* row = new QHBoxLayout;
        row->addStretch(1);
        auto* card = new QFrame(this);
        card->setObjectName(QStringLiteral("multiUutCleanupCard"));
        card->setMinimumSize(320, 150);
        card->setMaximumSize(390, 174);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(28, 22, 28, 22);
        cardLayout->setSpacing(8);

        m_spinner = new LoadingSpinner(card);
        m_spinner->setObjectName(QStringLiteral("multiUutCleanupSpinner"));
        m_spinner->setFixedSize(34, 34);
        m_spinner->setColor(QColor(QStringLiteral("#3f4a54")));
        cardLayout->addWidget(m_spinner, 0, Qt::AlignHCenter);

        m_titleLabel = new QLabel(card);
        m_titleLabel->setObjectName(QStringLiteral("multiUutCleanupTitle"));
        m_titleLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(m_titleLabel);

        m_statusLabel = new QLabel(card);
        m_statusLabel->setObjectName(QStringLiteral("multiUutCleanupStatus"));
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setWordWrap(true);
        cardLayout->addWidget(m_statusLabel);

        m_stepLabel = new QLabel(card);
        m_stepLabel->setObjectName(QStringLiteral("multiUutCleanupStep"));
        m_stepLabel->setAlignment(Qt::AlignCenter);
        m_stepLabel->setWordWrap(true);
        cardLayout->addWidget(m_stepLabel);
        row->addWidget(card);
        row->addStretch(1);
        root->addLayout(row);
        root->addStretch(1);

        setStyleSheet(QStringLiteral(R"css(
            QWidget#multiUutCleanupOverlay {
                background: rgba(244, 247, 250, 235);
            }
            QFrame#multiUutCleanupCard {
                background: #ffffff;
                border: 1px solid #d6dfe8;
                border-radius: 8px;
            }
            QLabel#multiUutCleanupTitle {
                color: #263139;
                font-size: 14px;
                font-weight: 800;
            }
            QLabel#multiUutCleanupStatus {
                color: #405160;
                font-size: 10px;
                font-weight: 600;
            }
            QLabel#multiUutCleanupStep {
                color: #6a7881;
                font-size: 9px;
                font-weight: 600;
            }
        )css"));
        setStage(Stage::CleaningUp);
        connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                this, [this] { setStage(m_stage); });
        hide();
    }

    void setStage(Stage stage)
    {
        m_stage = stage;
        if (stage == Stage::Stopping) {
            m_titleLabel->setText(uiText("STOPPING"));
            m_statusLabel->setText(
                uiText("Stopping active work before cleanup..."));
        } else {
            m_titleLabel->setText(uiText("CLEANING UP"));
            m_statusLabel->setText(
                uiText("Closing devices and releasing resources..."));
        }
        setCurrentStep(m_currentStep);
    }

    void setCurrentStep(const QString& currentStep)
    {
        const auto step = currentStep.trimmed();
        m_currentStep = step;
        m_stepLabel->setText(step.isEmpty()
            ? (m_stage == Stage::Stopping
                   ? uiText("Preparing cleanup for all UUTs")
                   : uiText("Finalizing all UUTs"))
            : uiText("Current: %1").arg(step));
        m_stepLabel->setToolTip(step);
    }

    void setRunning(bool running)
    {
        m_spinner->setRunning(running);
        setVisible(running);
    }

private:
    LoadingSpinner* m_spinner = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_stepLabel = nullptr;
    Stage m_stage = Stage::CleaningUp;
    QString m_currentStep;
};

class UutOverviewPromptOverlay final : public QWidget
{
public:
    using ResponseHandler = std::function<void(
        const QString&,
        PicoATE::Core::OperatorPromptResponse,
        const QVariantMap&)>;

    explicit UutOverviewPromptOverlay(
        QWidget* parent = nullptr,
        const QString& objectName = QStringLiteral("uutOverviewPromptOverlay"))
        : QWidget(parent)
    {
        setObjectName(objectName);
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
            "QWidget#uutOverviewPromptOverlay,"
            "QWidget#multiUutOverviewPromptOverlay{"
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

    void retranslatePrompt()
    {
        m_inputErrorLabel->setText(m_inputErrorSource
            ? uiText(m_inputErrorSource) : QString{});
        if (m_batchParticipantCount > 0) {
            m_contextLabel->setText(uiText("ALL %1 UUTs  |  ONCE PER BATCH")
                .arg(m_batchParticipantCount));
        }
        const auto buttonText = [this](const char* key, const char* fallback) {
            const auto text = m_promptDetails.value(QString::fromLatin1(key),
                                                    QString::fromLatin1(fallback)).toString();
            return text == QString::fromLatin1(fallback) ? uiText(fallback) : text;
        };
        m_confirmButton->setText(buttonText("confirmText", m_isInput ? "Submit" : "OK"));
        m_passButton->setText(buttonText("passText", "PASS"));
        m_failButton->setText(buttonText("failText", "FAIL"));
        const bool notice = m_promptDetails.value(QStringLiteral("mode"))
                                .toString().trimmed().toLower() == QStringLiteral("notice");
        m_statusLabel->setText(m_responsePending
            ? uiText("Recording operator response...")
            : (notice ? uiText("The test continues while this instruction is displayed.")
                      : uiText("Select the observed result for this UUT.")));
        if (m_promptDetails.value(QStringLiteral("title")).toString().trimmed().isEmpty()) {
            m_titleLabel->setText(uiText("Operator Action"));
        }
    }

    void configure(const PicoATE::Core::RuntimeEvent& event,
                   const QString& sequencePath,
                   const QString& serialNumber,
                   int batchParticipantCount = 0)
    {
        m_promptDetails = event.details;
        m_batchParticipantCount = batchParticipantCount;
        if (!m_languageConnected) {
            connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                    this, [this] { retranslatePrompt(); });
            m_languageConnected = true;
        }
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

        m_contextLabel->setText(batchParticipantCount > 0
            ? uiText("ALL %1 UUTs  |  ONCE PER BATCH").arg(batchParticipantCount)
            : (serialNumber.trimmed().isEmpty()
                   ? event.uutId
                   : uiText("%1  |  SN %2").arg(event.uutId,
                                               serialNumber.trimmed())));
        const auto title = event.details.value(
            QStringLiteral("title")).toString().trimmed();
        m_titleLabel->setText(title.isEmpty() ? uiText("Operator Action") : title);
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
            QStringLiteral("passText"), uiText("PASS")).toString());
        m_failButton->setText(event.details.value(
            QStringLiteral("failText"), uiText("FAIL")).toString());
        m_statusLabel->setVisible(notice || judgment);
        m_statusLabel->setText(notice
            ? uiText("The test continues while this instruction is displayed.")
            : uiText("Select the observed result for this UUT."));
        setResponsePending(false);
        retranslatePrompt();
    }

    QString currentInstanceId() const { return m_instanceId; }
    QString presentationKey() const { return m_presentationKey; }

    void setResponsePending(bool pending)
    {
        m_responsePending = pending;
        for (auto* button : {m_confirmButton, m_passButton, m_failButton}) {
            button->setEnabled(!pending);
        }
        m_inputEdit->setEnabled(!pending);
        if (pending) {
            m_statusLabel->show();
            m_statusLabel->setText(uiText("Recording operator response..."));
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
        m_imageLabel->setVisible(!image.trimmed().isEmpty());
        if (image.trimmed().isEmpty()) {
            return;
        }
        const auto path = ProjectResourcePaths::resolveImage(sequencePath, image);
        QPixmap pixmap(path);
        if (pixmap.isNull()) {
            m_imageLabel->setText(uiText("Image unavailable: %1").arg(image));
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
    bool m_responsePending = false;
    bool m_languageConnected = false;
    QVariantMap m_promptDetails;
    const char* m_inputErrorSource = nullptr;
    int m_batchParticipantCount = 0;
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
        setMinimumSize(300, 260);
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
        m_resourceBadge = new ResourceStatusBadge(this);
        heading->addWidget(m_resourceBadge, 0, Qt::AlignVCenter);
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

        m_periodicPanel = new PeriodicTaskStatusPanel(false, this);
        layout->addWidget(m_periodicPanel);

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
        m_completedCaptionLabel = makeUiLabel("COMPLETED STEPS", this);
        m_completedCaptionLabel->setObjectName(
            QStringLiteral("uutOverviewCaption"));
        m_progressLabel = new QLabel(this);
        m_progressLabel->setObjectName(QStringLiteral("uutOverviewMetric"));
        m_errorCaptionLabel = makeUiLabel("ERROR CODE", this);
        m_errorCaptionLabel->setObjectName(
            QStringLiteral("uutOverviewCaption"));
        m_errorLabel = new QLabel(this);
        m_errorLabel->setObjectName(QStringLiteral("uutOverviewError"));
        m_durationCaptionLabel = makeUiLabel("DURATION", this);
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
                      centered ? QSizePolicy::Fixed
                               : QSizePolicy::Expanding);
        if (!centered) {
            setMinimumHeight(260);
            setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }

    void setPairLayoutHeight(int availableHeight)
    {
        const int targetHeight = qBound(
            PairCardMinimumHeight,
            qRound(qMax(0, availableHeight) * PairCardHeightRatio),
            PairCardMaximumHeight);
        if (minimumHeight() == targetHeight &&
            maximumHeight() == targetHeight) {
            return;
        }
        setMinimumHeight(targetHeight);
        setMaximumHeight(targetHeight);
        updateGeometry();
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
                             ? uiText("RETRYING")
                             : uiStateText(uutOverviewStateName(entry.state).toUpper()));
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
                return uiText("SETUP");
            case PicoATE::Core::ExecutionPhase::Cleanup:
                return uiText("CLEANUP");
            case PicoATE::Core::ExecutionPhase::Main:
                return QString{};
            }
            return QString{};
        }();
        QString stepCaption;
        QString stepFallback;
        if (entry.retryActive) {
            stepCaption = phaseName.isEmpty()
                ? uiText("RETRYING CURRENT STEP")
                : uiText("RETRYING %1 STEP").arg(phaseName);
            stepFallback = uiText("Preparing next attempt");
        } else switch (entry.state) {
        case UutOverviewState::Disabled:
            stepCaption = uiText("UUT SLOT");
            stepFallback = uiText("Disabled for this run");
            break;
        case UutOverviewState::Waiting:
            stepCaption = phaseName.isEmpty()
                ? uiText("WAITING FOR")
                : uiText("WAITING FOR %1 STEP").arg(phaseName);
            stepFallback = uiText("Waiting to start");
            break;
        case UutOverviewState::Running:
        case UutOverviewState::Paused:
            stepCaption = phaseName.isEmpty()
                ? uiText("CURRENT STEP")
                : uiText("%1 STEP").arg(phaseName);
            stepFallback = uiText("Preparing");
            break;
        case UutOverviewState::Passed:
            stepCaption = phaseName.isEmpty()
                ? uiText("FINAL STEP")
                : uiText("FINAL %1 STEP").arg(phaseName);
            stepFallback = uiText("Completed");
            break;
        case UutOverviewState::Failed:
            stepCaption = phaseName.isEmpty()
                ? uiText("FAILED STEP")
                : uiText("FAILED %1 STEP").arg(phaseName);
            stepFallback = uiText("Failed");
            break;
        case UutOverviewState::Stopped:
            stepCaption = uiText("STOPPED AT");
            stepFallback = uiText("Stopped");
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
            currentStateText = uiText("RETRY");
            currentStateColor = QColor(QStringLiteral("#a87500"));
        } else {
            switch (entry.state) {
            case UutOverviewState::Disabled:
                currentStateText = uiText("OFF");
                currentStateColor = QColor(QStringLiteral("#7d888f"));
                break;
            case UutOverviewState::Waiting:
                currentStateText = uiText("WAIT");
                currentStateColor = QColor(QStringLiteral("#667680"));
                break;
            case UutOverviewState::Running:
                currentStateText = uiText("RUN");
                currentStateColor = QColor(QStringLiteral("#a87500"));
                break;
            case UutOverviewState::Paused:
                currentStateText = uiText("PAUSE");
                currentStateColor = QColor(QStringLiteral("#35677f"));
                break;
            case UutOverviewState::Passed:
                currentStateText = uiText("PASS");
                currentStateColor = QColor(QStringLiteral("#2f7548"));
                break;
            case UutOverviewState::Failed:
                currentStateText = uiText("FAIL");
                currentStateColor = QColor(QStringLiteral("#a43838"));
                break;
            case UutOverviewState::Stopped:
                currentStateText = uiText("STOP");
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
                ? uiText("ATTEMPT %1 / %2")
                      .arg(entry.retryAttempt)
                      .arg(entry.retryMaxAttempts)
                : QString{});
        setTextIfChanged(
            m_progressLabel,
            QStringLiteral("%1 / %2")
                .arg(entry.completedSteps)
                .arg(entry.totalSteps));
        m_periodicPanel->setTasks(entry.periodicTasks);
        m_resourceBadge->setResources(entry.heldResources,
                                      entry.waitingResources);
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

    void refreshPeriodicCountdown()
    {
        if (m_resourceBadge) {
            m_resourceBadge->refreshWaitDuration();
        }
        if (m_periodicPanel) {
            m_periodicPanel->refreshCountdown();
        }
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
    ResourceStatusBadge* m_resourceBadge = nullptr;
    PeriodicTaskStatusPanel* m_periodicPanel = nullptr;
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
    auto* title = makeUiLabel("UUT OVERVIEW", this);
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

    m_sharedResourcePanel = new ResourceStatusPanel(true, this);
    root->addWidget(m_sharedResourcePanel);

    m_sharedPeriodicPanel = new PeriodicTaskStatusPanel(true, this);
    root->addWidget(m_sharedPeriodicPanel);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("multiUutOverviewScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_cardsHost = new QWidget(scroll);
    m_cardsHost->setObjectName(QStringLiteral("multiUutOverviewCards"));
    m_cardsHost->installEventFilter(this);
    m_cardsLayout = new QGridLayout(m_cardsHost);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setHorizontalSpacing(14);
    m_cardsLayout->setVerticalSpacing(14);
    m_cardsLayout->setColumnStretch(0, 1);
    m_cardsLayout->setColumnStretch(1, 1);
    scroll->setWidget(m_cardsHost);
    m_cleanupOverlayHost = scroll->viewport();
    m_cleanupOverlayHost->installEventFilter(this);
    m_cleanupOverlay = new CleanupProgressOverlay(m_cleanupOverlayHost);
    root->addWidget(scroll, 1);

    m_periodicRefreshTimer = new QTimer(this);
    m_periodicRefreshTimer->setInterval(250);
    connect(m_periodicRefreshTimer, &QTimer::timeout,
             this, &MultiUutOverviewWidget::refreshPeriodicCountdowns);
    m_periodicRefreshTimer->start();

    m_cleanupOverlayDelayTimer = new QTimer(this);
    m_cleanupOverlayDelayTimer->setSingleShot(true);
    m_cleanupOverlayDelayTimer->setInterval(300);
    connect(m_cleanupOverlayDelayTimer, &QTimer::timeout, this, [this] {
        m_cleanupDelayElapsed = true;
        updateCleanupOverlayVisibility();
    });
    updateCleanupOverlayGeometry();
    connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
            this, [this] {
        refreshCards();
        refreshSharedPeriodicTasks();
        refreshSharedResources();
        updateSummary();
    }, Qt::QueuedConnection);

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
        connect(m_model, &UutOverviewModel::sharedPeriodicTasksChanged,
                this, &MultiUutOverviewWidget::refreshSharedPeriodicTasks);
        connect(m_model, &UutOverviewModel::sharedResourcesChanged,
                this, &MultiUutOverviewWidget::refreshSharedResources);
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

void MultiUutOverviewWidget::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    bool cleanupActive = m_cleanupActive;
    bool cleanupObserved = false;
    bool terminalSession = false;
    auto currentStep = m_cleanupCurrentStep;

    for (const auto& event : events) {
        if (event.kind == PicoATE::Core::RuntimeEventKind::SessionStateChanged) {
            switch (event.executionState) {
            case PicoATE::Core::ExecutionState::CleaningUp:
                cleanupActive = true;
                cleanupObserved = true;
                break;
            case PicoATE::Core::ExecutionState::Completed:
            case PicoATE::Core::ExecutionState::CompletedWithError:
            case PicoATE::Core::ExecutionState::Aborted:
                terminalSession = true;
                break;
            case PicoATE::Core::ExecutionState::Idle:
            case PicoATE::Core::ExecutionState::Starting:
                cleanupActive = false;
                m_stopTransitionRequested = false;
                currentStep.clear();
                break;
            default:
                break;
            }
        }

        const bool cleanupNode =
            event.nodePhase == PicoATE::Core::ExecutionPhase::Cleanup &&
            !event.nodeId.isEmpty();
        if (event.kind == PicoATE::Core::RuntimeEventKind::CleanupActivated ||
            (cleanupNode &&
             event.activationState != PicoATE::Core::ActivationState::Skipped)) {
            cleanupActive = true;
            cleanupObserved = true;
        }
        if (cleanupNode &&
            event.activationState != PicoATE::Core::ActivationState::Skipped) {
            currentStep = event.nodeDisplayName.trimmed();
            if (currentStep.isEmpty()) {
                currentStep = event.nodeLocalId.trimmed();
            }
            if (currentStep.isEmpty()) {
                currentStep = event.nodeId;
            }
        }
    }

    if (terminalSession) {
        cleanupActive = false;
        m_stopTransitionRequested = false;
        currentStep.clear();
    }
    if (cleanupObserved) {
        if (auto* overlay = static_cast<CleanupProgressOverlay*>(
                m_cleanupOverlay)) {
            overlay->setStage(CleanupProgressOverlay::Stage::CleaningUp);
        }
    }
    setCleanupActive(cleanupActive, currentStep);
}

void MultiUutOverviewWidget::beginStopTransition()
{
    m_stopTransitionRequested = true;
    if (auto* overlay = static_cast<CleanupProgressOverlay*>(
            m_cleanupOverlay)) {
        overlay->setStage(CleanupProgressOverlay::Stage::Stopping);
    }
    setCleanupActive(true, {}, true);
}

void MultiUutOverviewWidget::resetRuntimeState()
{
    m_stopTransitionRequested = false;
    setCleanupActive(false);
    if (auto* overlay = static_cast<CleanupProgressOverlay*>(
            m_cleanupOverlay)) {
        overlay->setStage(CleanupProgressOverlay::Stage::CleaningUp);
    }
}

bool MultiUutOverviewWidget::presentOperatorPrompt(
    const PicoATE::Core::RuntimeEvent& event,
    const QString& sequencePath)
{
    const auto instanceId = event.details.value(
        QStringLiteral("promptInstanceId")).toString();
    if (!isVisible() || instanceId.isEmpty()) {
        return false;
    }

    if (isOncePerBatchPrompt(event)) {
        const auto currentId = m_currentBatchPromptId;
        if (!currentId.isEmpty() && currentId != instanceId) {
            const auto current = m_activePrompts.constFind(currentId);
            if (current == m_activePrompts.constEnd() ||
                !mayReplaceDisplayedPrompt(current->event, event)) {
                return false;
            }
        }

        m_activePrompts.insert(instanceId, ActivePrompt{event, sequencePath});
        m_currentBatchPromptId = instanceId;
        updateCleanupOverlayVisibility();
        if (!m_batchPromptOverlay) {
            m_batchPromptOverlay = new UutOverviewPromptOverlay(
                m_cardsHost,
                QStringLiteral("multiUutOverviewPromptOverlay"));
        }
        auto* overlay = static_cast<UutOverviewPromptOverlay*>(
            m_batchPromptOverlay);
        overlay->setResponseHandler(
            [this](const QString& responseInstanceId,
                   PicoATE::Core::OperatorPromptResponse response,
                   const QVariantMap& values) {
                emit operatorPromptResponseRequested(responseInstanceId,
                                                      response,
                                                      values);
            });
        int participantCount = 0;
        if (m_model) {
            for (int row = 0; row < m_model->rowCount(); ++row) {
                const auto entry = m_model->entryAt(row);
                if (entry && entry->enabled) {
                    ++participantCount;
                }
            }
        } else {
            participantCount = m_cards.size();
        }
        overlay->configure(
            event,
            sequencePath,
            {},
            qMax(1, participantCount));
        updateBatchPromptGeometry();
        overlay->show();
        overlay->raise();
        return true;
    }

    if (event.uutId.isEmpty()) {
        return false;
    }

    auto* card = overviewCardForUut(m_cards, event.uutId);
    if (!card) {
        return false;
    }

    const auto currentId = m_currentPromptByUut.value(event.uutId);
    if (!currentId.isEmpty() && currentId != instanceId) {
        const auto current = m_activePrompts.constFind(currentId);
        if (current == m_activePrompts.constEnd() ||
            !mayReplaceDisplayedPrompt(current->event, event)) {
            return false;
        }
    }

    m_activePrompts.insert(instanceId, ActivePrompt{event, sequencePath});
    m_currentPromptByUut.insert(event.uutId, instanceId);
    updateCleanupOverlayVisibility();
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
    const bool batchPrompt = isOncePerBatchPrompt(prompt->event);
    const auto uutId = prompt->event.uutId;
    m_activePrompts.erase(prompt);
    updateCleanupOverlayVisibility();
    if (batchPrompt) {
        if (m_currentBatchPromptId != instanceId) {
            return true;
        }
        m_currentBatchPromptId.clear();
        if (m_batchPromptOverlay) {
            m_batchPromptOverlay->hide();
        }
        return true;
    }
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
    if (prompt == m_activePrompts.constEnd()) {
        return false;
    }
    if (isOncePerBatchPrompt(prompt->event)) {
        if (m_currentBatchPromptId != instanceId || !m_batchPromptOverlay) {
            return false;
        }
        static_cast<UutOverviewPromptOverlay*>(m_batchPromptOverlay)
            ->setResponsePending(pending);
        return true;
    }
    if (m_currentPromptByUut.value(prompt->event.uutId) != instanceId) {
        return false;
    }
    auto* card = overviewCardForUut(m_cards, prompt->event.uutId);
    return card && card->setOperatorPromptResponsePending(instanceId, pending);
}

void MultiUutOverviewWidget::clearOperatorPrompts()
{
    m_activePrompts.clear();
    m_currentPromptByUut.clear();
    m_currentBatchPromptId.clear();
    if (m_batchPromptOverlay) {
        m_batchPromptOverlay->hide();
    }
    for (auto* button : std::as_const(m_cards)) {
        static_cast<UutOverviewCard*>(button)->clearOperatorPrompt();
    }
    updateCleanupOverlayVisibility();
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
        refreshSharedPeriodicTasks();
        refreshSharedResources();
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
            card->setEnabled(entry->enabled);
        }
        connect(card, &QAbstractButton::clicked, this, [this, card] {
            if (!card->isEnabled()) {
                return;
            }
            m_selectedUutId = card->uutId();
            refreshCards();
            emit uutActivated(m_selectedUutId);
        });
        restoreOperatorPrompt(card);
        m_cardsLayout->addWidget(card,
                                 firstCardRow + row / columns,
                                 row % columns);
        card->show();
        m_cards.push_back(card);
    }
    m_gridRowCount = centeredPair ? 3 : gridRows;
    m_gridColumnCount = columns;
    updatePairCardHeights();
    restoreBatchOperatorPrompt();
    updateSummary();
    refreshSharedPeriodicTasks();
    refreshSharedResources();
    updateCleanupOverlayGeometry();
    updateCleanupOverlayVisibility();
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
        card->setEnabled(entry->enabled);
    }
    updateSummary();
}

void MultiUutOverviewWidget::refreshPeriodicCountdowns()
{
    if (m_sharedPeriodicPanel) {
        static_cast<PeriodicTaskStatusPanel*>(m_sharedPeriodicPanel)
            ->refreshCountdown();
    }
    if (m_sharedResourcePanel) {
        static_cast<ResourceStatusPanel*>(m_sharedResourcePanel)
            ->refreshWaitDuration();
    }
    for (auto* button : std::as_const(m_cards)) {
        if (auto* card = static_cast<UutOverviewCard*>(button)) {
            card->refreshPeriodicCountdown();
        }
    }
}

void MultiUutOverviewWidget::refreshSharedPeriodicTasks()
{
    if (!m_sharedPeriodicPanel) {
        return;
    }
    static_cast<PeriodicTaskStatusPanel*>(m_sharedPeriodicPanel)
        ->setTasks(m_model ? m_model->sharedPeriodicTasks()
                           : QVector<PeriodicTaskOverviewEntry>{});
}

void MultiUutOverviewWidget::refreshSharedResources()
{
    if (!m_sharedResourcePanel) {
        return;
    }
    static_cast<ResourceStatusPanel*>(m_sharedResourcePanel)
        ->setResources(m_model ? m_model->sharedHeldResources()
                              : QVector<ResourceUsageOverviewEntry>{},
                       m_model ? m_model->sharedWaitingResources()
                               : QVector<ResourceUsageOverviewEntry>{});
}

bool MultiUutOverviewWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_cleanupOverlayHost &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateCleanupOverlayGeometry();
    }
    if (watched == m_cardsHost &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
         event->type() == QEvent::LayoutRequest)) {
        updateBatchPromptGeometry();
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            updatePairCardHeights();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MultiUutOverviewWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_rebuildPending) {
        rebuildCards();
    } else if (m_refreshPending) {
        refreshCards();
    }
    updateCleanupOverlayGeometry();
    updateCleanupOverlayVisibility();
    updateBatchPromptGeometry();
    updatePairCardHeights();
}

void MultiUutOverviewWidget::updatePairCardHeights()
{
    if (!m_cardsHost || m_cards.size() != 2) {
        return;
    }
    const int availableHeight = m_cardsHost->height();
    for (auto* button : std::as_const(m_cards)) {
        static_cast<UutOverviewCard*>(button)->setPairLayoutHeight(
            availableHeight);
    }
}

void MultiUutOverviewWidget::updateSummary()
{
    int running = 0;
    int passed = 0;
    int failed = 0;
    int waiting = 0;
    int disabled = 0;
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
            case UutOverviewState::Disabled: ++disabled; break;
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
        disabled > 0
            ? uiText("RUNNING %1   PASS %2   FAIL %3   WAITING %4   DISABLED %5")
                  .arg(running).arg(passed).arg(failed).arg(waiting)
                  .arg(disabled)
            : uiText("RUNNING %1   PASS %2   FAIL %3   WAITING %4")
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

void MultiUutOverviewWidget::restoreBatchOperatorPrompt()
{
    if (m_currentBatchPromptId.isEmpty()) {
        return;
    }
    const auto prompt = m_activePrompts.constFind(m_currentBatchPromptId);
    if (prompt == m_activePrompts.constEnd()) {
        m_currentBatchPromptId.clear();
        if (m_batchPromptOverlay) {
            m_batchPromptOverlay->hide();
        }
        return;
    }
    if (!m_batchPromptOverlay) {
        m_batchPromptOverlay = new UutOverviewPromptOverlay(
            m_cardsHost,
            QStringLiteral("multiUutOverviewPromptOverlay"));
    }
    auto* overlay = static_cast<UutOverviewPromptOverlay*>(m_batchPromptOverlay);
    overlay->setResponseHandler(
        [this](const QString& responseInstanceId,
               PicoATE::Core::OperatorPromptResponse response,
               const QVariantMap& values) {
            emit operatorPromptResponseRequested(responseInstanceId,
                                                  response,
                                                  values);
        });
    const int participantCount = m_model ? m_model->rowCount() : m_cards.size();
    overlay->configure(
        prompt->event,
        prompt->sequencePath,
        {},
        qMax(1, participantCount));
    updateBatchPromptGeometry();
    overlay->show();
    overlay->raise();
}

void MultiUutOverviewWidget::setCleanupActive(
    bool active,
    const QString& currentStep,
    bool showImmediately)
{
    const auto normalizedStep = currentStep.trimmed();
    const bool stateChanged = m_cleanupActive != active;
    m_cleanupActive = active;
    m_cleanupCurrentStep = active ? normalizedStep : QString{};

    if (auto* overlay = static_cast<CleanupProgressOverlay*>(
            m_cleanupOverlay)) {
        overlay->setCurrentStep(m_cleanupCurrentStep);
    }

    if (!active) {
        if (m_cleanupOverlayDelayTimer) {
            m_cleanupOverlayDelayTimer->stop();
        }
        m_cleanupDelayElapsed = false;
        updateCleanupOverlayVisibility();
        return;
    }

    if (showImmediately) {
        if (m_cleanupOverlayDelayTimer) {
            m_cleanupOverlayDelayTimer->stop();
        }
        m_cleanupDelayElapsed = true;
    } else if (stateChanged) {
        m_cleanupDelayElapsed = false;
        if (m_cleanupOverlayDelayTimer) {
            m_cleanupOverlayDelayTimer->start();
        }
    }
    updateCleanupOverlayVisibility();
}

void MultiUutOverviewWidget::updateCleanupOverlayVisibility()
{
    auto* overlay = static_cast<CleanupProgressOverlay*>(m_cleanupOverlay);
    if (!overlay) {
        return;
    }
    const bool promptAllowsOverlay = m_stopTransitionRequested ||
                                     m_activePrompts.isEmpty();
    const bool showOverlay = m_cleanupActive && m_cleanupDelayElapsed &&
                             promptAllowsOverlay && isVisible();
    overlay->setRunning(showOverlay);
    if (!showOverlay) {
        return;
    }
    updateCleanupOverlayGeometry();
    overlay->raise();
    if (!m_stopTransitionRequested && m_batchPromptOverlay &&
        m_batchPromptOverlay->isVisible()) {
        m_batchPromptOverlay->raise();
    }
}

void MultiUutOverviewWidget::updateCleanupOverlayGeometry()
{
    if (!m_cleanupOverlay || !m_cleanupOverlayHost) {
        return;
    }
    m_cleanupOverlay->setGeometry(m_cleanupOverlayHost->rect());
    if (m_cleanupOverlay->isVisible()) {
        m_cleanupOverlay->raise();
    }
}

void MultiUutOverviewWidget::updateBatchPromptGeometry()
{
    if (!m_batchPromptOverlay || !m_cardsHost) {
        return;
    }
    m_batchPromptOverlay->setGeometry(m_cardsHost->rect());
    m_batchPromptOverlay->raise();
}

} // namespace PicoATE::Ui
