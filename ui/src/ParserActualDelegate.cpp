#include "ParserActualDelegate.h"

#include "RunnerModels.h"

#include <QApplication>
#include <QPainter>
#include <QSet>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextLayout>

namespace PicoATE::Ui {

namespace {

struct ActualDisplayText {
    QString text;
    int rawLabelLength = 0;
    int parsedLabelStart = -1;
    int parsedLabelLength = 0;
    QVector<QPair<int, int>> selectedRanges;
};

QStringList stringList(const QVariant& value)
{
    QStringList result;
    const auto values = value.toList();
    result.reserve(values.size());
    for (const auto& item : values) {
        result.push_back(item.toString());
    }
    return result;
}

QSet<int> integerSet(const QVariant& value)
{
    QSet<int> result;
    for (const auto& item : value.toList()) {
        result.insert(item.toInt());
    }
    return result;
}

ActualDisplayText buildDisplayText(const QVariantMap& display)
{
    ActualDisplayText result;
    const auto tokens = stringList(display.value(QStringLiteral("tokens")));
    const auto selected = integerSet(
        display.value(QStringLiteral("selectedIndices")));
    const auto format = display.value(QStringLiteral("format")).toString();
    const int groupSize = display.value(QStringLiteral("groupSize")).toInt();
    const int sourceOffset = display
                                 .value(QStringLiteral("sourceTokenOffset"))
                                 .toInt();
    const int sourceCount = display
                                .value(QStringLiteral("sourceTokenCount"))
                                .toInt();

    result.text = QStringLiteral("Raw:");
    result.rawLabelLength = result.text.size();
    result.text += QLatin1Char(' ');
    if (sourceOffset > 0) {
        result.text += QStringLiteral("... ");
    }
    for (int index = 0; index < tokens.size(); ++index) {
        if (index > 0) {
            const int absoluteIndex = sourceOffset + index;
            if (groupSize > 0 && absoluteIndex % groupSize == 0) {
                result.text += format == QStringLiteral("bits")
                    ? QStringLiteral(" ")
                    : QStringLiteral(" | ");
            } else if (format != QStringLiteral("bits")) {
                result.text += QLatin1Char(' ');
            }
        }
        const int start = result.text.size();
        result.text += tokens[index];
        if (selected.contains(index)) {
            result.selectedRanges.push_back({start, tokens[index].size()});
        }
    }
    if (sourceOffset + tokens.size() < sourceCount) {
        result.text += QStringLiteral(" ...");
    }

    result.text += QStringLiteral("  \u2192  ");
    result.parsedLabelStart = result.text.size();
    result.text += QStringLiteral("Parsed:");
    result.parsedLabelLength = result.text.size() - result.parsedLabelStart;
    result.text += QLatin1Char(' ');
    result.text += display.value(QStringLiteral("parsedDisplay")).toString();
    return result;
}

QTextLayout::FormatRange formatRange(int start,
                                     int length,
                                     const QTextCharFormat& format)
{
    QTextLayout::FormatRange range;
    range.start = start;
    range.length = length;
    range.format = format;
    return range;
}

} // namespace

ParserActualDelegate::ParserActualDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ParserActualDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    const auto display = index
                             .data(UutStepModel::ParserSelectionDisplayRole)
                             .toMap();
    if (display.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const auto content = buildDisplayText(display);
    if (content.text.isEmpty() || content.selectedRanges.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem backgroundOption(option);
    initStyleOption(&backgroundOption, index);
    backgroundOption.text.clear();
    backgroundOption.icon = {};
    auto* style = backgroundOption.widget
        ? backgroundOption.widget->style()
        : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem,
                       &backgroundOption,
                       painter,
                       backgroundOption.widget);

    auto textRect = style->subElementRect(QStyle::SE_ItemViewItemText,
                                          &backgroundOption,
                                          backgroundOption.widget)
                        .adjusted(3, 0, -3, 0);
    if (textRect.isEmpty()) {
        return;
    }

    const bool rowSelected = option.state.testFlag(QStyle::State_Selected);
    const auto textColor = option.palette.color(
        rowSelected ? QPalette::HighlightedText : QPalette::Text);
    auto mutedColor = textColor;
    mutedColor.setAlpha(rowSelected ? 210 : 155);

    QTextCharFormat normalFormat;
    normalFormat.setForeground(textColor);
    QTextCharFormat labelFormat;
    labelFormat.setForeground(mutedColor);
    QTextCharFormat selectedFormat;
    selectedFormat.setForeground(QColor(QStringLiteral("#163a59")));
    selectedFormat.setBackground(QColor(QStringLiteral("#bfe5ff")));
    selectedFormat.setFontWeight(QFont::DemiBold);

    QVector<QTextLayout::FormatRange> formats;
    formats.reserve(content.selectedRanges.size() + 3);
    formats.push_back(formatRange(0, content.text.size(), normalFormat));
    formats.push_back(formatRange(0, content.rawLabelLength, labelFormat));
    formats.push_back(formatRange(content.parsedLabelStart,
                                  content.parsedLabelLength,
                                  labelFormat));
    for (const auto& [start, length] : content.selectedRanges) {
        formats.push_back(formatRange(start, length, selectedFormat));
    }

    QTextLayout layout(content.text, backgroundOption.font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(textOption);
    layout.beginLayout();
    auto line = layout.createLine();
    if (line.isValid()) {
        line.setLineWidth(textRect.width());
    }
    layout.endLayout();
    if (!line.isValid()) {
        return;
    }

    const QPointF origin(textRect.left(),
                         textRect.top() + (textRect.height() - line.height()) / 2.0);
    painter->save();
    painter->setClipRect(textRect);
    layout.draw(painter, origin, formats, textRect);
    painter->restore();
}

} // namespace PicoATE::Ui
