#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QVector>

namespace PicoATE::Ui {

enum class XlsxRowStyle {
    Normal,
    Header,
    Passed,
    Failed,
    Skipped
};

struct XlsxRow {
    QStringList cells;
    XlsxRowStyle style = XlsxRowStyle::Normal;
    QSet<int> numericColumns;
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
