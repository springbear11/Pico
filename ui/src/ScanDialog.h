#pragma once

#include "StartupSupport.h"

#include <QDialog>

class QCloseEvent;
class QLabel;
class QLineEdit;

namespace PicoATE::Ui {

class ScanDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ScanDialog(QWidget* parent = nullptr);

    void setValidationRules(SnValidationRules rules);
    void showForNextScan();

signals:
    void barcodeAccepted(const QString& barcode);

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void submitBarcode();

private:
    QLineEdit* m_barcodeEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    SnValidationRules m_validationRules;
};

} // namespace PicoATE::Ui
