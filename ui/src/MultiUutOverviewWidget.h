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

namespace PicoATE::Ui {

class UutOverviewModel;

class MultiUutOverviewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MultiUutOverviewWidget(QWidget* parent = nullptr);

    void setModel(UutOverviewModel* model);
    UutOverviewModel* model() const;
    void setSelectedUutId(const PicoATE::Core::UutId& uutId);
    PicoATE::Core::UutId selectedUutId() const;
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
    void updateSummary();
    void restoreOperatorPrompt(QAbstractButton* card);
    void restoreBatchOperatorPrompt();
    void updateBatchPromptGeometry();

    struct ActivePrompt {
        PicoATE::Core::RuntimeEvent event;
        QString sequencePath;
    };

    UutOverviewModel* m_model = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QWidget* m_cardsHost = nullptr;
    QGridLayout* m_cardsLayout = nullptr;
    QVector<QAbstractButton*> m_cards;
    QHash<QString, ActivePrompt> m_activePrompts;
    QHash<PicoATE::Core::UutId, QString> m_currentPromptByUut;
    QWidget* m_batchPromptOverlay = nullptr;
    QString m_currentBatchPromptId;
    PicoATE::Core::UutId m_selectedUutId;
    bool m_rebuildPending = false;
    bool m_refreshPending = false;
    int m_gridRowCount = 0;
    int m_gridColumnCount = 0;
};

} // namespace PicoATE::Ui
