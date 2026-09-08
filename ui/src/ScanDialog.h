#pragma once

#include "StartupSupport.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

#include <functional>

class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace PicoATE::Ui {

struct ScanSubmissionDecision {
    bool accepted = true;
    QString errorMessage;
    int requiredSlotCount = 0;
    QString batchContext;
};

using ScanSubmissionValidator = std::function<ScanSubmissionDecision(
    const QStringList& proposedBarcodes,
    int currentSlot)>;

class ScanDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ScanDialog(QWidget* parent = nullptr);

    void setValidationRules(SnValidationRules rules);
    void setSubmissionValidator(ScanSubmissionValidator validator);
    void setSlotCount(int count);
    void setSlotEnabledStates(const QVector<bool>& enabledStates);
    int slotCount() const;
    QVector<bool> slotEnabledStates() const;
    QStringList barcodes() const;
    void showForNextScan();
    void cancelCurrentScan();
    bool isScanRequested() const { return m_visibilityRequested; }
    void setVisible(bool visible) override;

signals:
    void barcodeAccepted(const QString& barcode);
    void barcodesAccepted(const QStringList& barcodes);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void reject() override;

private slots:
    void submitBarcode();
    void showPreviousSlot();
    void showNextSlot();
    void undoLastScan();
    void clearBatch();
    void toggleCurrentSlotEnabled(bool enabled);

private:
    struct ScanChange {
        int slot = -1;
        QString previousValue;
    };

    void resetBatch();
    void showSlot(int slot);
    void updateUi();
    void retranslateUi();
    void focusBarcodeEdit();
    void resizeSlotsPreservingValues(int count);
    void showSubmissionError(const QString& message);
    void acceptCompletedBatch();
    int nextEmptySlot(int afterSlot) const;
    bool batchComplete() const;
    bool blocksScanner(const QWidget* window) const;
    bool hasBlockingModal() const;
    void scheduleVisibilityCheck();
    void refreshVisibility();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QPushButton* m_slotEnabledButton = nullptr;
    QLineEdit* m_barcodeEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QToolButton* m_previousButton = nullptr;
    QToolButton* m_nextButton = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    SnValidationRules m_validationRules;
    ScanSubmissionValidator m_submissionValidator;
    QStringList m_barcodes{QString{}};
    QVector<bool> m_slotEnabledStates{true};
    QVector<ScanChange> m_history;
    QString m_batchContext;
    int m_configuredSlotCount = 1;
    int m_currentSlot = 0;
    bool m_replaceOnNextInput = false;
    bool m_updatingEdit = false;
    bool m_visibilityRequested = false;
    bool m_ownerBlocked = false;
    bool m_visibilityCheckQueued = false;
};

} // namespace PicoATE::Ui
