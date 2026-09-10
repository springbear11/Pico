#pragma once

#include "PicoATE/Core/OperatorPrompt.h"
#include "PicoATE/Core/RuntimeEvent.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QEvent;
class QLabel;
class QGridLayout;
class QShowEvent;
class QTimer;

namespace PicoATE::Ui {

class UutOverviewModel;

class MultiUutOverviewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MultiUutOverviewWidget(QWidget* parent = nullptr);
    ~MultiUutOverviewWidget() override;

    void setModel(UutOverviewModel* model);
    UutOverviewModel* model() const;
    void setSelectedUutId(const PicoATE::Core::UutId& uutId);
    PicoATE::Core::UutId selectedUutId() const;
    void applyRuntimeEvents(
        const QVector<PicoATE::Core::RuntimeEvent>& events);
    void beginStopTransition();
    void resetRuntimeState();
    bool presentOperatorPrompt(const PicoATE::Core::RuntimeEvent& event,
                               const QString& sequencePath);
    bool closeOperatorPrompt(const QString& instanceId);
    bool hasOperatorPrompt(const QString& instanceId) const;
    bool setOperatorPromptResponsePending(const QString& instanceId,
                                          bool pending);
    void clearOperatorPrompts();

signals:
    void uutActivated(const PicoATE::Core::UutId& uutId);
    void operatorPromptResponseRequested(
        const QString& instanceId,
        PicoATE::Core::OperatorPromptResponse response,
        const QVariantMap& values);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void rebuildCards();
    void refreshCards();
    void refreshCardRange(int firstRow, int lastRow);
    void refreshPeriodicCountdowns();
    void refreshSharedPeriodicTasks();
    void refreshSharedResources();
    void updatePairCardHeights();
    void updateSummary();
    void restoreOperatorPrompt(QAbstractButton* card);
    void restoreBatchOperatorPrompt();
    void rememberPromptInput(const QString& instanceId, const QString& text);
    void updateBatchPromptGeometry();
    void setCleanupActive(bool active,
                          const QString& currentStep = {},
                          bool showImmediately = false);
    void updateCleanupOverlayVisibility();
    void updateCleanupOverlayGeometry();

    struct ActivePrompt {
        PicoATE::Core::RuntimeEvent event;
        QString sequencePath;
        bool responsePending = false;
    };

    UutOverviewModel* m_model = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QWidget* m_cardsHost = nullptr;
    QGridLayout* m_cardsLayout = nullptr;
    QWidget* m_sharedPeriodicPanel = nullptr;
    QWidget* m_sharedResourcePanel = nullptr;
    QWidget* m_cleanupOverlayHost = nullptr;
    QWidget* m_cleanupOverlay = nullptr;
    QTimer* m_periodicRefreshTimer = nullptr;
    QTimer* m_cleanupOverlayDelayTimer = nullptr;
    QVector<QAbstractButton*> m_cards;
    QHash<QString, ActivePrompt> m_activePrompts;
    QHash<PicoATE::Core::UutId, QString> m_currentPromptByUut;
    QWidget* m_batchPromptOverlay = nullptr;
    QString m_currentBatchPromptId;
    PicoATE::Core::UutId m_selectedUutId;
    bool m_rebuildPending = false;
    bool m_refreshPending = false;
    bool m_cleanupActive = false;
    bool m_cleanupDelayElapsed = false;
    bool m_stopTransitionRequested = false;
    QString m_cleanupCurrentStep;
    int m_gridRowCount = 0;
    int m_gridColumnCount = 0;
};

} // namespace PicoATE::Ui
