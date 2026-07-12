#include "ScanDialog.h"

#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace PicoATE::Ui {

ScanDialog::ScanDialog(QWidget* parent)
    : QDialog(parent,
              Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                  Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("scanDialog"));
    setWindowTitle(tr("Scan SN"));
    setWindowModality(Qt::NonModal);
    setModal(false);
    setMinimumSize(460, 230);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(34, 28, 34, 24);
    layout->setSpacing(16);

    auto* title = new QLabel(tr("Scan SN"), this);
    auto font = title->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 10);
    title->setFont(font);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_barcodeEdit = new QLineEdit(this);
    m_barcodeEdit->setObjectName(QStringLiteral("barcodeEdit"));
    m_barcodeEdit->setAlignment(Qt::AlignCenter);
    m_barcodeEdit->setMinimumHeight(54);
    m_barcodeEdit->setPlaceholderText(tr("Scan barcode and press Enter"));
    layout->addWidget(m_barcodeEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("scanErrorLabel"));
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    connect(m_barcodeEdit, &QLineEdit::returnPressed,
            this, &ScanDialog::submitBarcode);
}

void ScanDialog::showForNextScan()
{
    m_errorLabel->hide();
    m_barcodeEdit->clear();
    show();
    raise();
    activateWindow();
    QTimer::singleShot(0, m_barcodeEdit, [edit = m_barcodeEdit] {
        edit->setFocus(Qt::OtherFocusReason);
    });
}

void ScanDialog::closeEvent(QCloseEvent* event)
{
    event->ignore();
}

void ScanDialog::reject()
{
}

void ScanDialog::submitBarcode()
{
    const auto barcode = m_barcodeEdit->text().trimmed();
    if (barcode.isEmpty()) {
        m_errorLabel->setText(tr("SN cannot be empty"));
        m_errorLabel->show();
        m_barcodeEdit->setFocus(Qt::OtherFocusReason);
        return;
    }
    m_errorLabel->hide();
    hide();
    emit barcodeAccepted(barcode);
}

} // namespace PicoATE::Ui
