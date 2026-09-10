#pragma once

#include "PicoATE/Core/RuntimeEvent.h"

#include <QTimer>
#include <QWidget>

class QLabel;
class QProgressBar;

namespace PicoATE::Ui {

class PromptCountdownWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PromptCountdownWidget(QWidget* parent = nullptr);
    static PicoATE::Core::RuntimeEvent withTiming(
        PicoATE::Core::RuntimeEvent event,
        const PicoATE::Core::RuntimeEvent* previous = nullptr);
    void configure(const PicoATE::Core::RuntimeEvent& event);
    void setResponsePending(bool pending);
    void setCompact(bool compact);
    qint64 remainingMs() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void updateTimer();

    QTimer m_timer;
    QLabel* m_label = nullptr;
    QLabel* m_icon = nullptr;
    QProgressBar* m_progress = nullptr;
    qint64 m_durationMs = 0;
    qint64 m_deadlineMs = 0;
    qint64 m_frozenRemainingMs = 0;
    bool m_conditionClosed = false;
    bool m_responsePending = false;
    bool m_compact = false;
    int m_tone = -1;
};

} // namespace PicoATE::Ui
