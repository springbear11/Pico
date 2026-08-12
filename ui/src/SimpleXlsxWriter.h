#pragma once

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
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

struct XlsxImage {
    QByteArray pngData;
    QString name;
    int column = 0;
    int row = 0;
    int columnOffsetPixels = 0;
    int rowOffsetPixels = 0;
    int widthPixels = 0;
    int heightPixels = 0;
};

struct SimpleXlsxWriteResult {
    bool success = false;
    QString errorMessage;
};

SimpleXlsxWriteResult writeSimpleXlsx(const QString& filePath,
                                      const QString& sheetName,
                                      const QVector<double>& columnWidths,
                                      const QVector<XlsxRow>& rows,
                                      const QVector<XlsxImage>& images = {});

} // namespace PicoATE::Ui
