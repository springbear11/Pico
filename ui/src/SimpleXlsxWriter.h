#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QVector>

namespace PicoATE::Ui {

enum class XlsxRowStyle {
    Normal,
    Header,
    Passed,
    Failed,
    Skipped,
    SummaryInfo,
    SummaryDuration,
    SummaryPassed,
    SummaryFailed
};

struct XlsxRow {
    QStringList cells;
    XlsxRowStyle style = XlsxRowStyle::Normal;
    QSet<int> numericColumns;
    double height = 0.0;
    QVector<QPair<int, int>> mergedColumnRanges;
    QHash<int, XlsxRowStyle> cellStyles;
    bool excludeFromAutoFilter = false;
};

struct SimpleXlsxWriteResult {
    bool success = false;
    QString errorMessage;
};

SimpleXlsxWriteResult writeSimpleXlsx(const QString& filePath,
                                      const QString& sheetName,
                                      const QVector<double>& columnWidths,
                                      const QVector<XlsxRow>& rows);

} // namespace PicoATE::Ui
