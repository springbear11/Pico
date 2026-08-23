#pragma once

#include "StartupSupport.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace PicoATE::Ui {

class ScanDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ScanDialog(QWidget* parent = nullptr);

    void setValidationRules(SnValidationRules rules);
    void setSlotCount(int count);
    int slotCount() const;
    QStringList barcodes() const;
    void showForNextScan();
    void cancelCurrentScan();

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

private:
    struct ScanChange {
        int slot = -1;
        QString previousValue;
    };

    void resetBatch();
    void showSlot(int slot);
    void updateUi();
    void focusBarcodeEdit();
    int nextEmptySlot(int afterSlot) const;
    bool batchComplete() const;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QLineEdit* m_barcodeEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QToolButton* m_previousButton = nullptr;
    QToolButton* m_nextButton = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    SnValidationRules m_validationRules;
    QStringList m_barcodes{QString{}};
    QVector<ScanChange> m_history;
    int m_currentSlot = 0;
    bool m_replaceOnNextInput = false;
    bool m_updatingEdit = false;
};

} // namespace PicoATE::Ui
