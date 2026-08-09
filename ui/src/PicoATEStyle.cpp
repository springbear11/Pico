#include "PicoATEStyle.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleOption>

#include <algorithm>

namespace PicoATE::Ui {

namespace {

QFont picoATEInterfaceFont()
{
    const QStringList candidates = {
        QStringLiteral("Segoe UI Variable Text"),
        QStringLiteral("Segoe UI"),
        QStringLiteral("Microsoft YaHei UI")};
    QFont font;
    for (const auto& candidate : candidates) {
        if (QFontDatabase::hasFamily(candidate)) {
            font.setFamily(candidate);
            break;
        }
    }
    font.setPointSizeF(10.0);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QString picoATEStyleSheet()
{
    return QStringLiteral(R"css(
        QMainWindow, QDialog {
            background: #f4f6f7;
            color: #20262b;
        }
        QMenuBar {
            background: #ffffff;
            color: #2b3238;
            border-bottom: 1px solid #dce1e5;
            padding: 2px 6px;
        }
        QMenuBar::item {
            background: transparent;
            border-radius: 4px;
            padding: 5px 9px;
        }
        QMenuBar::item:selected,
        QMenuBar::item:pressed {
            background: #edf2f5;
        }
        QMenu {
            background: #ffffff;
            color: #252c31;
            border: 1px solid #cfd5da;
            padding: 5px;
        }
        QMenu::item {
            border-radius: 4px;
            padding: 7px 26px 7px 10px;
        }
        QMenu::item:selected {
            background: #e5f0f7;
            color: #1f2b33;
        }
        QMenu::separator {
            height: 1px;
            background: #e1e5e8;
            margin: 5px 7px;
        }
        QToolBar {
            background: #ffffff;
            border: 0;
            border-bottom: 1px solid #dce1e5;
            spacing: 3px;
            padding: 4px 6px;
        }
        QToolBar::separator {
            background: #dce1e5;
            width: 1px;
            margin: 5px 6px;
        }
        QPushButton {
            background: #ffffff;
            color: #273038;
            border: 1px solid #c8d0d6;
            border-radius: 5px;
            min-height: 28px;
            padding: 3px 13px;
        }
        QPushButton:hover {
            background: #f2f5f7;
            border-color: #9faab2;
        }
        QPushButton:pressed {
            background: #e5eaed;
            border-color: #89959e;
        }
        QPushButton:default,
        QPushButton[primary="true"] {
            background: #252b30;
            color: #ffffff;
            border-color: #252b30;
            font-weight: 600;
        }
        QPushButton:default:hover,
        QPushButton[primary="true"]:hover {
            background: #363e44;
            border-color: #363e44;
        }
        QPushButton:disabled {
            background: #f0f2f3;
            color: #a0a8ae;
            border-color: #dfe3e6;
        }
        QToolButton {
            background: transparent;
            color: #30383e;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 4px 6px;
        }
        QToolButton:hover {
            background: #edf2f5;
            border-color: #dbe2e7;
        }
        QToolButton:pressed {
            background: #dde5ea;
        }
        QToolButton:checked {
            background: #dcecf6;
            border-color: #afcadb;
            color: #203846;
        }
        QToolButton:disabled {
            color: #a8afb5;
            background: transparent;
            border-color: transparent;
        }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox,
        QDateEdit, QTimeEdit, QDateTimeEdit,
        QTextEdit, QPlainTextEdit {
            background: #ffffff;
            color: #222a30;
            border: 1px solid #c8d0d6;
            border-radius: 5px;
            selection-background-color: #cfe4f1;
            selection-color: #20262b;
        }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox,
        QDateEdit, QTimeEdit, QDateTimeEdit {
            min-height: 28px;
            padding: 1px 8px;
        }
        QTextEdit, QPlainTextEdit {
            padding: 6px 8px;
        }
        QLineEdit:hover, QComboBox:hover, QSpinBox:hover,
        QDoubleSpinBox:hover, QDateEdit:hover, QTimeEdit:hover,
        QDateTimeEdit:hover, QTextEdit:hover, QPlainTextEdit:hover {
            border-color: #a5afb7;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus,
        QDoubleSpinBox:focus, QDateEdit:focus, QTimeEdit:focus,
        QDateTimeEdit:focus, QTextEdit:focus, QPlainTextEdit:focus {
            border-color: #5d8ba8;
        }
        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled,
        QDoubleSpinBox:disabled, QTextEdit:disabled, QPlainTextEdit:disabled {
            background: #f1f3f4;
            color: #9ea6ac;
            border-color: #dce1e4;
        }
        QLineEdit[readOnly="true"], QTextEdit[readOnly="true"],
        QPlainTextEdit[readOnly="true"] {
            background: #f5f6f7;
            color: #59636b;
        }
        QComboBox {
            padding-right: 28px;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 26px;
            border: 0;
        }
        QComboBox QAbstractItemView {
            background: #ffffff;
            color: #252c31;
            border: 1px solid #cfd5da;
            selection-background-color: #dcecf6;
            selection-color: #20262b;
            outline: 0;
        }
        QCheckBox, QRadioButton {
            color: #2c343a;
            spacing: 7px;
        }
        QCheckBox:disabled, QRadioButton:disabled {
            color: #9ca4aa;
        }
        QGroupBox {
            background: #ffffff;
            border: 1px solid #d8dde1;
            border-radius: 6px;
            margin-top: 11px;
            padding: 12px 9px 9px 9px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 5px;
            color: #3c464e;
        }
        QTabWidget::pane {
            background: #ffffff;
            border: 1px solid #d9dee2;
            border-radius: 5px;
            top: -1px;
        }
        QTabBar::tab {
            background: transparent;
            color: #67727a;
            border: 0;
            border-bottom: 2px solid transparent;
            min-height: 28px;
            padding: 5px 13px;
        }
        QTabBar::tab:hover {
            background: #eef2f4;
            color: #343d43;
        }
        QTabBar::tab:selected {
            background: #ffffff;
            color: #20262b;
            border-bottom-color: #30383e;
            font-weight: 600;
        }
        QTableView, QTreeView, QListView, QListWidget,
        QTableWidget, QTreeWidget {
            background: #ffffff;
            alternate-background-color: #f7f8f9;
            color: #293138;
            border: 1px solid #d8dde1;
            border-radius: 4px;
            gridline-color: #e2e6e9;
            selection-background-color: #dcecf6;
            selection-color: #20262b;
            outline: 0;
        }
        QTableView::item, QTreeView::item, QListView::item,
        QListWidget::item, QTableWidget::item, QTreeWidget::item {
            padding: 3px 6px;
        }
        QTableView::item:hover, QTreeView::item:hover,
        QListView::item:hover, QListWidget::item:hover,
        QTableWidget::item:hover, QTreeWidget::item:hover {
            background: #eef5f9;
        }
        QHeaderView {
            background: #eef1f3;
            color: #364149;
        }
        QHeaderView::section {
            background: #eef1f3;
            color: #364149;
            border: 0;
            border-right: 1px solid #d8dde1;
            border-bottom: 1px solid #cbd2d7;
            padding: 7px 7px;
            font-weight: 600;
        }
        QTableCornerButton::section {
            background: #eef1f3;
            border: 0;
            border-right: 1px solid #d8dde1;
            border-bottom: 1px solid #cbd2d7;
        }
        QProgressBar {
            background: #e8ecef;
            color: #273038;
            border: 1px solid #c9d0d5;
            border-radius: 4px;
            min-height: 20px;
            text-align: center;
        }
        QProgressBar::chunk {
            background: #4f7d5d;
            border-radius: 3px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 11px;
            margin: 1px;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 11px;
            margin: 1px;
        }
        QScrollBar::handle {
            background: #c3cbd1;
            border-radius: 5px;
            min-height: 28px;
            min-width: 28px;
        }
        QScrollBar::handle:hover {
            background: #9faab2;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0;
            height: 0;
            background: transparent;
            border: 0;
        }
        QScrollBar::add-page, QScrollBar::sub-page {
            background: transparent;
        }
        QSplitter::handle {
            background: transparent;
        }
        QSplitter::handle:hover {
            background: #dce3e7;
        }
        QSplitter::handle:horizontal { width: 5px; }
        QSplitter::handle:vertical { height: 5px; }
        QStatusBar {
            background: #f8f9fa;
            color: #59646c;
            border-top: 1px solid #dce1e4;
        }
        QStatusBar::item { border: 0; }
        QToolTip {
            background: #252b30;
            color: #ffffff;
            border: 1px solid #252b30;
            padding: 5px 7px;
        }
    )css");
}

bool usesNeutralToolbarIcon(QStyle::StandardPixmap icon)
{
    switch (icon) {
    case QStyle::SP_DialogOpenButton:
    case QStyle::SP_DialogSaveButton:
    case QStyle::SP_DialogApplyButton:
    case QStyle::SP_DialogCancelButton:
    case QStyle::SP_DialogYesButton:
    case QStyle::SP_ArrowBack:
    case QStyle::SP_ArrowForward:
    case QStyle::SP_ArrowUp:
    case QStyle::SP_ArrowDown:
    case QStyle::SP_FileIcon:
    case QStyle::SP_FileLinkIcon:
    case QStyle::SP_FileDialogDetailedView:
    case QStyle::SP_DirIcon:
    case QStyle::SP_DirOpenIcon:
    case QStyle::SP_TrashIcon:
    case QStyle::SP_BrowserReload:
    case QStyle::SP_MediaPlay:
    case QStyle::SP_MediaPause:
    case QStyle::SP_MediaStop:
    case QStyle::SP_DriveNetIcon:
        return true;
    default:
        return false;
    }
}

QPixmap tintedIconPixmap(const QPixmap& source, const QColor& color)
{
    if (source.isNull()) {
        return {};
    }
    QPixmap result(source.size());
    result.setDevicePixelRatio(source.devicePixelRatio());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    return result;
}

} // namespace

PicoATEStyle::PicoATEStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
}

int PicoATEStyle::pixelMetric(PixelMetric metric,
                              const QStyleOption* option,
                              const QWidget* widget) const
{
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) {
        return 18;
    }
    if (metric == PM_ToolBarIconSize) {
        return 20;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

QIcon PicoATEStyle::standardIcon(StandardPixmap standardIcon,
                                 const QStyleOption* option,
                                 const QWidget* widget) const
{
    const auto source = QProxyStyle::standardIcon(standardIcon, option, widget);
    if (source.isNull() || !usesNeutralToolbarIcon(standardIcon)) {
        return source;
    }

    QIcon result;
    const QList<QSize> sizes = {
        QSize(16, 16), QSize(20, 20), QSize(24, 24), QSize(32, 32)};
    for (const auto& size : sizes) {
        result.addPixmap(
            tintedIconPixmap(source.pixmap(size, QIcon::Normal, QIcon::Off),
                             QColor(QStringLiteral("#30383e"))),
            QIcon::Normal,
            QIcon::Off);
        result.addPixmap(
            tintedIconPixmap(source.pixmap(size, QIcon::Active, QIcon::Off),
                             QColor(QStringLiteral("#20262b"))),
            QIcon::Active,
            QIcon::Off);
        result.addPixmap(
            tintedIconPixmap(source.pixmap(size, QIcon::Disabled, QIcon::Off),
                             QColor(QStringLiteral("#a8afb5"))),
            QIcon::Disabled,
            QIcon::Off);
        result.addPixmap(
            tintedIconPixmap(source.pixmap(size, QIcon::Normal, QIcon::On),
                             QColor(QStringLiteral("#356f98"))),
            QIcon::Normal,
            QIcon::On);
    }
    return result;
}

void PicoATEStyle::drawPrimitive(PrimitiveElement element,
                                 const QStyleOption* option,
                                 QPainter* painter,
                                 const QWidget* widget) const
{
    if (element != PE_IndicatorCheckBox &&
        element != PE_IndicatorItemViewItemCheck) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    if (!option || !painter || option->rect.isEmpty()) {
        return;
    }

    const int side = std::min({18, option->rect.width(), option->rect.height()});
    QRectF box(0.0, 0.0, side, side);
    box.moveCenter(QRectF(option->rect).center());
    const bool enabled = option->state.testFlag(State_Enabled);
    const bool checked = option->state.testFlag(State_On);
    const bool partial = option->state.testFlag(State_NoChange);
    const bool hovered = option->state.testFlag(State_MouseOver);

    QColor fill = checked || partial
        ? QColor(QStringLiteral("#34393e"))
        : QColor(QStringLiteral("#ffffff"));
    QColor border = checked || partial
        ? QColor(QStringLiteral("#292e33"))
        : QColor(QStringLiteral("#7d8790"));
    if (hovered && !checked && !partial) {
        fill = QColor(QStringLiteral("#f0f4f6"));
        border = QColor(QStringLiteral("#59636c"));
    }
    if (!enabled) {
        fill.setAlpha(120);
        border.setAlpha(120);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(border, 1.1));
    painter->setBrush(fill);
    painter->drawRoundedRect(box.adjusted(0.6, 0.6, -0.6, -0.6), 4.2, 4.2);

    if (checked) {
        QPainterPath check;
        check.moveTo(box.left() + 4.2, box.top() + 9.1);
        check.lineTo(box.left() + 7.5, box.top() + 12.0);
        check.lineTo(box.left() + 13.8, box.top() + 5.7);
        QColor checkColor(QStringLiteral("#151a1e"));
        if (!enabled) {
            checkColor.setAlpha(120);
        }
        QPen checkPen(checkColor, 1.7, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin);
        painter->setPen(checkPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(check);
    } else if (partial) {
        QColor markColor(QStringLiteral("#151a1e"));
        if (!enabled) {
            markColor.setAlpha(120);
        }
        painter->setPen(QPen(markColor, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(box.left() + 4.5, box.center().y()),
                          QPointF(box.right() - 4.5, box.center().y()));
    }
    painter->restore();
}

void applyPicoATEApplicationTheme(QApplication& application)
{
    application.setFont(picoATEInterfaceFont());

    auto palette = application.palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#f4f6f7")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#20262b")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f7f8f9")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#222a30")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#273038")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#dcecf6")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#20262b")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#252b30")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#356f98")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#929ca4")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText,
                     QColor(QStringLiteral("#a0a8ae")));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     QColor(QStringLiteral("#a0a8ae")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(QStringLiteral("#a0a8ae")));
    application.setPalette(palette);
    application.setStyleSheet(picoATEStyleSheet());
}

} // namespace PicoATE::Ui
