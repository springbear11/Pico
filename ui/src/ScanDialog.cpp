#include "ScanDialog.h"

#include "UutSlotConfigurationDialog.h"

#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

void applyBarcodeEditAppearance(QLineEdit* edit, bool storedValue)
{
    edit->setStyleSheet(
        QStringLiteral("QLineEdit#barcodeEdit { color: %1; }")
            .arg(storedValue ? QStringLiteral("#7b858c")
                             : QStringLiteral("#20262b")));
    // Reapplying a local style sheet can restore the size constraint captured
    // by QStyleSheetStyle. Set the scanner height after styling so it remains
    // stable with both the native Windows style and PicoATEStyle.
    edit->setMinimumHeight(66);
}

} // namespace

ScanDialog::ScanDialog(QWidget* parent)
    : QDialog(parent,
              Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                  Qt::WindowStaysOnTopHint)
{
    setObjectName(QStringLiteral("scanDialog"));
    setWindowTitle(tr("Scan SN"));
    setWindowModality(Qt::NonModal);
    setModal(false);
    setFixedSize(460, 230);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 16, 28, 14);
    layout->setSpacing(8);

    m_titleLabel = new QLabel(tr("Scan SN"), this);
    m_titleLabel->setObjectName(QStringLiteral("scanTitleLabel"));
    auto font = m_titleLabel->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 8);
    m_titleLabel->setFont(font);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_titleLabel);

    auto* carousel = new QHBoxLayout;
    carousel->setSpacing(10);

    m_previousButton = new QToolButton(this);
    m_previousButton->setObjectName(QStringLiteral("scanPreviousButton"));
    m_previousButton->setText(QStringLiteral("<"));
    m_previousButton->setToolTip(tr("Previous UUT"));
    m_previousButton->setFixedSize(28, 66);
    carousel->addWidget(m_previousButton, 0, Qt::AlignVCenter);

    m_barcodeEdit = new QLineEdit(this);
    m_barcodeEdit->setObjectName(QStringLiteral("barcodeEdit"));
    m_barcodeEdit->setAlignment(Qt::AlignCenter);
    auto barcodeFont = m_barcodeEdit->font();
    barcodeFont.setPointSize(18);
    barcodeFont.setBold(true);
    m_barcodeEdit->setFont(barcodeFont);
    m_barcodeEdit->setMinimumHeight(66);
    m_barcodeEdit->setTextMargins(18, 0, 18, 0);
    m_barcodeEdit->setPlaceholderText(tr("Scan SN and press Enter"));
    m_barcodeEdit->installEventFilter(this);
    carousel->addWidget(m_barcodeEdit, 1);

    m_nextButton = new QToolButton(this);
    m_nextButton->setObjectName(QStringLiteral("scanNextButton"));
    m_nextButton->setText(QStringLiteral(">"));
    m_nextButton->setToolTip(tr("Next UUT"));
    m_nextButton->setFixedSize(28, 66);
    carousel->addWidget(m_nextButton, 0, Qt::AlignVCenter);
    layout->addLayout(carousel);
    setFocusProxy(m_barcodeEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("scanErrorLabel"));
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318;"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto* commands = new QHBoxLayout;
    commands->setSpacing(8);
    m_slotEnabledButton = new QPushButton(this);
    m_slotEnabledButton->setObjectName(
        QStringLiteral("scanSlotEnabledButton"));
    m_slotEnabledButton->setCheckable(true);
    m_slotEnabledButton->setToolTip(
        tr("Enable or disable the current UUT station"));
    commands->addWidget(m_slotEnabledButton);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName(QStringLiteral("scanProgressLabel"));
    m_progressLabel->setStyleSheet(
        QStringLiteral("color: #6b747b; font-weight: 600;"));
    commands->addWidget(m_progressLabel);
    commands->addStretch(1);

    m_undoButton = new QPushButton(
        QIcon(QStringLiteral(":/icons/undo-2.svg")), tr("Undo"), this);
    m_undoButton->setObjectName(QStringLiteral("scanUndoButton"));
    m_undoButton->setToolTip(tr("Undo the most recent scan or replacement"));
    m_clearButton = new QPushButton(
        QIcon(QStringLiteral(":/icons/trash-2.svg")), tr("Clear"), this);
    m_clearButton->setObjectName(QStringLiteral("scanClearButton"));
    m_clearButton->setToolTip(tr("Clear every SN in this batch"));
    commands->addWidget(m_undoButton);
    commands->addWidget(m_clearButton);
    layout->addLayout(commands);

    setStyleSheet(styleSheet() + QStringLiteral(
        "QToolButton#scanPreviousButton, QToolButton#scanNextButton {"
        " border: 0; border-radius: 4px; background: transparent;"
        " color: #3f494f; font-size: 22px; font-weight: 500; padding: 0; }"
        "QToolButton#scanPreviousButton:hover, QToolButton#scanNextButton:hover {"
        " background: #e7f1f7; }"
        "QToolButton#scanPreviousButton:disabled, QToolButton#scanNextButton:disabled {"
        " color: #c7ced2; }"
        "QPushButton#scanUndoButton, QPushButton#scanClearButton {"
        " min-height: 28px; padding: 1px 10px; border: 1px solid #cbd4da;"
        " border-radius: 5px; background: #ffffff; color: #344048; }"
        "QPushButton#scanUndoButton { font-weight: 600; }"
        "QPushButton#scanClearButton { font-weight: 600; }"
        "QPushButton#scanUndoButton:hover, QPushButton#scanClearButton:hover {"
        " background: #eaf3f8; }"
        "QPushButton#scanSlotEnabledButton {"
        " min-height: 28px; min-width: 76px; padding: 1px 10px;"
        " border: 1px solid #b9c3c9; border-radius: 5px;"
        " background: #ffffff; color: #69747b; font-weight: 700; }"
        "QPushButton#scanSlotEnabledButton:checked {"
        " border-color: #252a2f; background: #252a2f; color: #ffffff; }"
        "QPushButton#scanSlotEnabledButton:hover { border-color: #52616b; }"));

    connect(m_barcodeEdit, &QLineEdit::returnPressed,
            this, &ScanDialog::submitBarcode);
    connect(m_barcodeEdit, &QLineEdit::textEdited, this, [this] {
        if (m_updatingEdit) {
            return;
        }
        m_replaceOnNextInput = false;
        m_errorLabel->hide();
        applyBarcodeEditAppearance(m_barcodeEdit, false);
    });
    connect(m_previousButton, &QToolButton::clicked,
            this, &ScanDialog::showPreviousSlot);
    connect(m_nextButton, &QToolButton::clicked,
            this, &ScanDialog::showNextSlot);
    connect(m_undoButton, &QPushButton::clicked,
            this, &ScanDialog::undoLastScan);
    connect(m_clearButton, &QPushButton::clicked,
            this, &ScanDialog::clearBatch);
    connect(m_slotEnabledButton, &QPushButton::toggled,
            this, &ScanDialog::toggleCurrentSlotEnabled);
    updateUi();
}

void ScanDialog::setValidationRules(SnValidationRules rules)
{
    rules.exactLength = qBound(0, rules.exactLength, 256);
    rules.wildcardPattern = rules.wildcardPattern.trimmed();
    rules.allowedRegex = rules.allowedRegex.trimmed();
    m_validationRules = std::move(rules);
}

void ScanDialog::setSubmissionValidator(ScanSubmissionValidator validator)
{
    m_submissionValidator = std::move(validator);
}

void ScanDialog::setSlotCount(int count)
{
    count = qBound(1, count, 64);
    m_configuredSlotCount = count;
    if (count == m_barcodes.size()) {
        m_slotEnabledStates = normalizeUutSlotEnabledStates(
            count, m_slotEnabledStates);
        updateUi();
        return;
    }
    m_barcodes = QStringList(count, QString{});
    m_slotEnabledStates = normalizeUutSlotEnabledStates(
        count, m_slotEnabledStates);
    m_history.clear();
    m_batchContext.clear();
    m_currentSlot = 0;
    updateUi();
}

void ScanDialog::setSlotEnabledStates(const QVector<bool>& enabledStates)
{
    auto normalized = normalizeUutSlotEnabledStates(
        m_barcodes.size(), enabledStates);
    if (enabledUutSlotCount(normalized) == 0) {
        normalized[0] = true;
    }
    m_slotEnabledStates = std::move(normalized);
    updateUi();
}

int ScanDialog::slotCount() const
{
    return m_barcodes.size();
}

QVector<bool> ScanDialog::slotEnabledStates() const
{
    return m_slotEnabledStates;
}

QStringList ScanDialog::barcodes() const
{
    return m_barcodes;
}

void ScanDialog::showForNextScan()
{
    resetBatch();
    show();
    raise();
    activateWindow();
    focusBarcodeEdit();
}

void ScanDialog::cancelCurrentScan()
{
    resetBatch();
    hide();
}

void ScanDialog::closeEvent(QCloseEvent* event)
{
    event->ignore();
}

bool ScanDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_barcodeEdit && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return ||
            keyEvent->key() == Qt::Key_Enter) {
            submitBarcode();
            return true;
        }

        if (!m_replaceOnNextInput) {
            return QDialog::eventFilter(watched, event);
        }
        const bool insertsText = !keyEvent->text().isEmpty() &&
            !(keyEvent->modifiers() & (Qt::ControlModifier |
                                       Qt::AltModifier |
                                       Qt::MetaModifier));
        const bool clearsText = keyEvent->key() == Qt::Key_Backspace ||
                                keyEvent->key() == Qt::Key_Delete;
        const bool pastesText = keyEvent->matches(QKeySequence::Paste);
        if (insertsText || clearsText || pastesText) {
            m_updatingEdit = true;
            m_barcodeEdit->clear();
            m_updatingEdit = false;
            m_replaceOnNextInput = false;
            applyBarcodeEditAppearance(m_barcodeEdit, false);
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ScanDialog::reject()
{
}

void ScanDialog::submitBarcode()
{
    if (!m_slotEnabledStates.value(m_currentSlot, true)) {
        return;
    }
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
    for (int slot = 0; slot < m_barcodes.size(); ++slot) {
        if (slot != m_currentSlot &&
            m_slotEnabledStates.value(slot, true) &&
            m_barcodes[slot].compare(barcode, Qt::CaseSensitive) == 0) {
            m_errorLabel->setText(
                tr("This SN is already assigned to UUT %1").arg(slot + 1));
            m_errorLabel->show();
            m_barcodeEdit->selectAll();
            m_barcodeEdit->setFocus(Qt::OtherFocusReason);
            return;
        }
    }

    if (m_submissionValidator) {
        auto proposedBarcodes = m_barcodes;
        proposedBarcodes[m_currentSlot] = barcode;
        const auto decision = m_submissionValidator(
            proposedBarcodes, m_currentSlot);
        if (!decision.accepted) {
            showSubmissionError(decision.errorMessage);
            return;
        }
        if (decision.requiredSlotCount > 0) {
            resizeSlotsPreservingValues(decision.requiredSlotCount);
        }
        m_batchContext = decision.batchContext.trimmed();
    }

    if (m_barcodes[m_currentSlot] != barcode) {
        m_history.push_back({m_currentSlot, m_barcodes[m_currentSlot]});
        m_barcodes[m_currentSlot] = barcode;
    }
    m_errorLabel->hide();
    updateUi();

    if (batchComplete()) {
        acceptCompletedBatch();
        return;
    }

    const int nextSlot = nextEmptySlot(m_currentSlot);
    showSlot(nextSlot >= 0
                 ? nextSlot
                 : qMin(m_currentSlot + 1, m_barcodes.size() - 1));
}

void ScanDialog::showPreviousSlot()
{
    showSlot(m_currentSlot - 1);
}

void ScanDialog::showNextSlot()
{
    showSlot(m_currentSlot + 1);
}

void ScanDialog::undoLastScan()
{
    if (m_history.isEmpty()) {
        return;
    }
    const auto change = m_history.takeLast();
    if (change.slot < 0 || change.slot >= m_barcodes.size()) {
        return;
    }
    m_barcodes[change.slot] = change.previousValue;
    showSlot(change.slot);
}

void ScanDialog::clearBatch()
{
    resetBatch();
    focusBarcodeEdit();
}

void ScanDialog::toggleCurrentSlotEnabled(bool enabled)
{
    if (m_currentSlot < 0 || m_currentSlot >= m_slotEnabledStates.size()) {
        return;
    }
    if (!enabled && enabledUutSlotCount(m_slotEnabledStates) <= 1) {
        const QSignalBlocker blocker(m_slotEnabledButton);
        m_slotEnabledButton->setChecked(true);
        showSubmissionError(tr("At least one UUT station must remain active"));
        return;
    }

    m_slotEnabledStates[m_currentSlot] = enabled;
    m_errorLabel->hide();
    updateUi();
    if (batchComplete()) {
        acceptCompletedBatch();
        return;
    }
    if (!enabled) {
        const int nextSlot = nextEmptySlot(m_currentSlot);
        if (nextSlot >= 0) {
            showSlot(nextSlot);
        }
    } else {
        focusBarcodeEdit();
    }
}

void ScanDialog::resetBatch()
{
    m_barcodes = QStringList(m_configuredSlotCount, QString{});
    m_slotEnabledStates = normalizeUutSlotEnabledStates(
        m_configuredSlotCount, m_slotEnabledStates);
    if (enabledUutSlotCount(m_slotEnabledStates) == 0) {
        m_slotEnabledStates[0] = true;
    }
    m_history.clear();
    m_batchContext.clear();
    m_currentSlot = nextEmptySlot(-1);
    if (m_currentSlot < 0) {
        m_currentSlot = 0;
    }
    m_errorLabel->hide();
    updateUi();
}

void ScanDialog::showSlot(int slot)
{
    if (slot < 0 || slot >= m_barcodes.size()) {
        return;
    }
    m_currentSlot = slot;
    m_errorLabel->hide();
    updateUi();
    focusBarcodeEdit();
}

void ScanDialog::updateUi()
{
    if (m_barcodes.isEmpty()) {
        m_barcodes.push_back(QString{});
    }
    m_currentSlot = qBound(0, m_currentSlot, m_barcodes.size() - 1);
    const auto stored = m_barcodes[m_currentSlot];
    const bool slotEnabled = m_slotEnabledStates.value(m_currentSlot, true);
    m_updatingEdit = true;
    m_barcodeEdit->setText(stored);
    m_updatingEdit = false;
    m_barcodeEdit->setEnabled(slotEnabled);
    m_barcodeEdit->setPlaceholderText(
        slotEnabled ? tr("Scan SN and press Enter") : tr("UUT disabled"));
    m_replaceOnNextInput = slotEnabled && !stored.isEmpty();
    applyBarcodeEditAppearance(m_barcodeEdit,
                               slotEnabled && !stored.isEmpty());
    const bool batchMode = m_barcodes.size() > 1;
    m_titleLabel->setText(batchMode
                              ? slotEnabled
                                    ? tr("Scan UUT %1 SN").arg(m_currentSlot + 1)
                                    : tr("UUT %1 Disabled").arg(m_currentSlot + 1)
                              : tr("Scan SN"));
    int completed = 0;
    for (int slot = 0; slot < m_barcodes.size(); ++slot) {
        if (m_slotEnabledStates.value(slot, true) &&
            !m_barcodes[slot].isEmpty()) {
            ++completed;
        }
    }
    const int activeSlots = enabledUutSlotCount(m_slotEnabledStates);
    const int disabledSlots = m_barcodes.size() - activeSlots;
    auto progressText =
        tr("%1 / %2 scanned").arg(completed).arg(activeSlots);
    if (disabledSlots > 0) {
        progressText += tr("  |  %1 disabled").arg(disabledSlots);
    }
    if (!m_batchContext.isEmpty()) {
        progressText = tr("%1  |  %2").arg(m_batchContext, progressText);
    }
    m_progressLabel->setText(progressText);
    m_previousButton->setEnabled(m_currentSlot > 0);
    m_nextButton->setEnabled(m_currentSlot + 1 < m_barcodes.size());
    m_undoButton->setEnabled(!m_history.isEmpty());
    m_clearButton->setEnabled(completed > 0);
    {
        const QSignalBlocker blocker(m_slotEnabledButton);
        m_slotEnabledButton->setChecked(slotEnabled);
        m_slotEnabledButton->setText(slotEnabled ? tr("ACTIVE")
                                                 : tr("DISABLED"));
    }
    m_previousButton->setVisible(batchMode);
    m_nextButton->setVisible(batchMode);
    m_progressLabel->setVisible(batchMode);
    m_undoButton->setVisible(batchMode);
    m_clearButton->setVisible(batchMode);
    m_slotEnabledButton->setVisible(batchMode);
}

void ScanDialog::focusBarcodeEdit()
{
    if (!m_barcodeEdit->isEnabled()) {
        return;
    }
    m_barcodeEdit->setCursorPosition(0);
    m_barcodeEdit->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, m_barcodeEdit, [edit = m_barcodeEdit] {
        edit->setCursorPosition(0);
        edit->setFocus(Qt::OtherFocusReason);
    });
}

void ScanDialog::resizeSlotsPreservingValues(int count)
{
    count = qBound(1, count, 64);
    if (count == m_barcodes.size()) {
        return;
    }
    m_barcodes.resize(count);
    m_slotEnabledStates = normalizeUutSlotEnabledStates(
        count, m_slotEnabledStates);
    m_history.erase(
        std::remove_if(m_history.begin(), m_history.end(),
                       [count](const ScanChange& change) {
                           return change.slot >= count;
                       }),
        m_history.end());
    m_currentSlot = qBound(0, m_currentSlot, count - 1);
}

void ScanDialog::acceptCompletedBatch()
{
    auto completedBarcodes = m_barcodes;
    for (int slot = 0; slot < completedBarcodes.size(); ++slot) {
        if (!m_slotEnabledStates.value(slot, true)) {
            completedBarcodes[slot].clear();
        }
    }
    hide();
    emit barcodesAccepted(completedBarcodes);
    if (completedBarcodes.size() == 1) {
        emit barcodeAccepted(completedBarcodes.first());
    }
}

void ScanDialog::showSubmissionError(const QString& message)
{
    const auto text = message.trimmed().isEmpty()
        ? tr("This SN cannot be added to the current batch")
        : message.trimmed();
    m_errorLabel->setText(text);
    m_errorLabel->setToolTip(text);
    m_errorLabel->show();
    m_barcodeEdit->selectAll();
    m_barcodeEdit->setFocus(Qt::OtherFocusReason);
}

int ScanDialog::nextEmptySlot(int afterSlot) const
{
    for (int slot = afterSlot + 1; slot < m_barcodes.size(); ++slot) {
        if (m_slotEnabledStates.value(slot, true) &&
            m_barcodes[slot].isEmpty()) {
            return slot;
        }
    }
    for (int slot = 0; slot <= afterSlot && slot < m_barcodes.size(); ++slot) {
        if (m_slotEnabledStates.value(slot, true) &&
            m_barcodes[slot].isEmpty()) {
            return slot;
        }
    }
    return -1;
}

bool ScanDialog::batchComplete() const
{
    if (enabledUutSlotCount(m_slotEnabledStates) == 0) {
        return false;
    }
    for (int slot = 0; slot < m_barcodes.size(); ++slot) {
        if (m_slotEnabledStates.value(slot, true) &&
            m_barcodes[slot].isEmpty()) {
            return false;
        }
    }
    return true;
}

} // namespace PicoATE::Ui
