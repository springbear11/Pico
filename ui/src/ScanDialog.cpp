#include "ScanDialog.h"

#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

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
    auto barcodeFont = m_barcodeEdit->font();
    barcodeFont.setPointSize(18);
    barcodeFont.setBold(true);
    m_barcodeEdit->setFont(barcodeFont);
    m_barcodeEdit->setMinimumHeight(66);
    m_barcodeEdit->setTextMargins(18, 0, 18, 0);
    m_barcodeEdit->setPlaceholderText(tr("Scan barcode and press Enter"));
    layout->addWidget(m_barcodeEdit);
    setFocusProxy(m_barcodeEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("scanErrorLabel"));
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    connect(m_barcodeEdit, &QLineEdit::returnPressed,
            this, &ScanDialog::submitBarcode);
}

void ScanDialog::setValidationRules(SnValidationRules rules)
{
    rules.exactLength = qBound(0, rules.exactLength, 256);
    rules.wildcardPattern = rules.wildcardPattern.trimmed();
    rules.allowedRegex = rules.allowedRegex.trimmed();
    m_validationRules = std::move(rules);
}

void ScanDialog::showForNextScan()
{
    m_errorLabel->hide();
    m_barcodeEdit->clear();
    show();
    raise();
    activateWindow();
    QTimer::singleShot(0, m_barcodeEdit, [edit = m_barcodeEdit] {
        edit->setCursorPosition(0);
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
    const auto validation = StartupSupport::validateSerialNumber(
        barcode, m_validationRules);
    if (!validation.ok()) {
        m_errorLabel->setText(validation.errorMessage);
        m_errorLabel->show();
        m_barcodeEdit->selectAll();
        m_barcodeEdit->setFocus(Qt::OtherFocusReason);
        return;
    }
    m_errorLabel->hide();
    hide();
    emit barcodeAccepted(barcode);
}

} // namespace PicoATE::Ui
