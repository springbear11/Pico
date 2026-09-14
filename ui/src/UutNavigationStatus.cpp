#include "UutNavigationStatus.h"

#include "ExecutionViewModel.h"
#include "RunnerModels.h"
#include "ScanDialog.h"
#include "UiLanguage.h"

#include <QPainter>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {
namespace {

bool batchActive(UiRunState state)
{
    return state == UiRunState::Starting || state == UiRunState::Running ||
           state == UiRunState::Pausing || state == UiRunState::Paused ||
           state == UiRunState::Stopping;
}

QIcon statusIcon(const QColor& color, bool filled)
{
    QPixmap pixmap(36, 36);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(3.5, 3.5, 11, 11));
    if (filled) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(9, 9), 4.2, 4.2);
    }
    return QIcon(pixmap);
}

} // namespace

UutNavigationStatus::UutNavigationStatus(UutOverviewModel* model, ScanDialog* scanner,
                                       ExecutionViewModel* execution, QObject* parent)
    : QObject(parent), m_model(model), m_execution(execution)
{
    connect(model, &QAbstractItemModel::modelReset, this, &UutNavigationStatus::refresh);
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last) { refreshRange(first.row(), last.row()); });
    connect(scanner, &ScanDialog::batchProgressChanged,
            this, &UutNavigationStatus::scanProgressChanged);
    connect(execution, &ExecutionViewModel::stateChanged, this, [this](UiRunState state) {
        if (state == UiRunState::Starting) {
            m_scanPreview = false;
            m_scanSerials.clear();
            m_scanEnabled.clear();
        }
        refresh();
    });
    connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
            this, &UutNavigationStatus::refresh);
}

void UutNavigationStatus::bind(QPushButton* button, int row)
{
    button->setText(QStringLiteral("UUT%1").arg(row + 1));
    button->setProperty("uutNavigationRow", row);
    button->setIconSize(QSize(18, 18));
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;color:#344048;border:1px solid transparent;"
        "border-radius:4px;min-height:30px;padding:1px 9px;font-weight:700;}"
        "QPushButton:hover{background:#e7f1f7;}"
        "QPushButton:checked{background:#f0f3f5;border-color:#9aa6ad;}"
        "QPushButton:disabled{background:transparent;color:#a0a8ae;border-color:transparent;}"));
    m_buttons.push_back(button);
    refreshRange(row, row);
}

void UutNavigationStatus::scanProgressChanged(const QStringList& serials, const QVector<bool>& enabled)
{
    if (batchActive(m_execution->state())) return;
    bool edited = m_scanPreview;
    for (int row = 0; row < serials.size(); ++row) {
        edited |= enabled.value(row, true) && !serials[row].trimmed().isEmpty();
        const auto entry = m_model->entryAt(row);
        edited |= entry && entry->enabled != enabled.value(row, true);
    }
    // Merely opening the next empty scanner must not erase the previous results.
    if (!edited) return;
    m_scanPreview = true;
    m_scanSerials = serials;
    m_scanEnabled = enabled;
    refresh();
}

void UutNavigationStatus::refresh()
{
    refreshRange(0, m_model->rowCount() - 1);
}

void UutNavigationStatus::refreshRange(int firstRow, int lastRow)
{
    m_buttons.erase(std::remove_if(m_buttons.begin(), m_buttons.end(),
                                   [](const auto& button) { return button.isNull(); }), m_buttons.end());
    const auto session = m_execution->state();
    for (const auto& guarded : std::as_const(m_buttons)) {
        auto* button = guarded.data();
        const int row = button->property("uutNavigationRow").toInt();
        if (row < firstRow || row > lastRow) continue;
        const auto entry = m_model->entryAt(row);
        if (!entry) continue;
        const bool preview = m_scanPreview && !batchActive(session);
        const bool enabled = preview ? m_scanEnabled.value(row, entry->enabled) : entry->enabled;
        const auto serial = preview ? m_scanSerials.value(row).trimmed() : entry->serialNumber.trimmed();
        QString state = QStringLiteral("idle");
        QColor color(QStringLiteral("#202328"));
        const char* caption = "WAITING";
        if (!enabled) {
            state = QStringLiteral("disabled"); color = QColor("#a0a8ae"); caption = "DISABLED";
        } else if (session == UiRunState::Starting) {
            state = QStringLiteral("running"); color = QColor("#d8a61a"); caption = "RUNNING";
        } else if (!preview && !entry->retryActive && entry->state == UutOverviewState::Passed) {
            state = QStringLiteral("passed"); color = QColor("#2f7548"); caption = "PASS";
        } else if (!preview && !entry->retryActive &&
                   (entry->state == UutOverviewState::Failed || entry->state == UutOverviewState::Stopped)) {
            state = QStringLiteral("failed"); color = QColor("#a43838"); caption = "FAIL";
        } else if (!preview && (batchActive(session) || entry->retryActive ||
                   entry->state == UutOverviewState::Running || entry->state == UutOverviewState::Paused)) {
            state = QStringLiteral("running"); color = QColor("#d8a61a"); caption = "RUNNING";
        } else if (!serial.isEmpty()) {
            state = QStringLiteral("scanned"); color = QColor("#397eac"); caption = "SCANNED";
        }
        if (button->property("uutNavigationState").toString() != state) {
            button->setProperty("uutNavigationState", state);
            button->setProperty("uutNavigationColor", color);
            button->setIcon(statusIcon(color, state != "idle" && state != "disabled"));
        }
        button->setEnabled(enabled);
        button->setProperty("slotEnabled", enabled);
        const auto description = button->text() + QStringLiteral(" | ") + uiText(caption);
        const auto tooltip = serial.isEmpty() ? description
            : description + QStringLiteral("\nSN: ") + serial;
        if (button->toolTip() != tooltip) button->setToolTip(tooltip);
        if (button->accessibleName() != description) button->setAccessibleName(description);
    }
}

} // namespace PicoATE::Ui
