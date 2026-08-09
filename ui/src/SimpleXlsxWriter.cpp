#include "SimpleXlsxWriter.h"

#include <QDateTime>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include <algorithm>

namespace PicoATE::Ui {

namespace {

struct ZipEntry {
    QByteArray name;
    QByteArray data;
    quint32 crc = 0;
    quint32 offset = 0;
};

void appendLe16(QByteArray& bytes, quint16 value)
{
    bytes.append(static_cast<char>(value & 0xff));
    bytes.append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray& bytes, quint32 value)
{
    appendLe16(bytes, static_cast<quint16>(value & 0xffff));
    appendLe16(bytes, static_cast<quint16>((value >> 16) & 0xffff));
}

quint32 crc32(const QByteArray& data)
{
    quint32 crc = 0xffffffffu;
    for (const auto character : data) {
        crc ^= static_cast<quint8>(character);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

quint16 dosDate(const QDate& date)
{
    const int year = std::clamp(date.year(), 1980, 2107) - 1980;
    return static_cast<quint16>((year << 9) | (date.month() << 5) | date.day());
}

quint16 dosTime(const QTime& time)
{
    return static_cast<quint16>((time.hour() << 11) |
                                (time.minute() << 5) |
                                (time.second() / 2));
}

QByteArray zipArchive(QVector<ZipEntry> entries)
{
    QByteArray archive;
    const auto now = QDateTime::currentDateTime();
    const auto date = dosDate(now.date());
    const auto time = dosTime(now.time());

    for (auto& entry : entries) {
        entry.crc = crc32(entry.data);
        entry.offset = static_cast<quint32>(archive.size());
        appendLe32(archive, 0x04034b50u);
        appendLe16(archive, 20);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, time);
        appendLe16(archive, date);
        appendLe32(archive, entry.crc);
        appendLe32(archive, static_cast<quint32>(entry.data.size()));
        appendLe32(archive, static_cast<quint32>(entry.data.size()));
        appendLe16(archive, static_cast<quint16>(entry.name.size()));
        appendLe16(archive, 0);
        archive += entry.name;
        archive += entry.data;
    }

    const auto centralOffset = static_cast<quint32>(archive.size());
    for (const auto& entry : entries) {
        appendLe32(archive, 0x02014b50u);
        appendLe16(archive, 20);
        appendLe16(archive, 20);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, time);
        appendLe16(archive, date);
        appendLe32(archive, entry.crc);
        appendLe32(archive, static_cast<quint32>(entry.data.size()));
        appendLe32(archive, static_cast<quint32>(entry.data.size()));
        appendLe16(archive, static_cast<quint16>(entry.name.size()));
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe16(archive, 0);
        appendLe32(archive, 0);
        appendLe32(archive, entry.offset);
        archive += entry.name;
    }
    const auto centralSize = static_cast<quint32>(archive.size()) - centralOffset;
    appendLe32(archive, 0x06054b50u);
    appendLe16(archive, 0);
    appendLe16(archive, 0);
    appendLe16(archive, static_cast<quint16>(entries.size()));
    appendLe16(archive, static_cast<quint16>(entries.size()));
    appendLe32(archive, centralSize);
    appendLe32(archive, centralOffset);
    appendLe16(archive, 0);
    return archive;
}

QString cellReference(int row, int column)
{
    QString letters;
    for (int value = column + 1; value > 0; value = (value - 1) / 26) {
        letters.prepend(QChar(QLatin1Char('A').unicode() + (value - 1) % 26));
    }
    return letters + QString::number(row + 1);
}

int styleId(XlsxRowStyle style)
{
    switch (style) {
    case XlsxRowStyle::Normal: return 0;
    case XlsxRowStyle::Header: return 1;
    case XlsxRowStyle::Passed: return 2;
    case XlsxRowStyle::Failed: return 3;
    case XlsxRowStyle::Skipped: return 4;
    case XlsxRowStyle::SummaryInfo: return 5;
    case XlsxRowStyle::SummaryDuration: return 6;
    case XlsxRowStyle::SummaryPassed: return 7;
    case XlsxRowStyle::SummaryFailed: return 8;
    }
    return 0;
}

bool numericText(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return false;
    }
    bool ok = false;
    text.toDouble(&ok);
    return ok;
}

QByteArray worksheetXml(const QVector<double>& columnWidths,
                        const QVector<XlsxRow>& rows)
{
    int tableHeaderRow = -1;
    for (int row = 0; row < rows.size(); ++row) {
        if (rows[row].style == XlsxRowStyle::Header) {
            tableHeaderRow = row;
        }
    }

    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.setAutoFormatting(false);
    xml.writeStartDocument(QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("worksheet"));
    xml.writeDefaultNamespace(
        QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));

    xml.writeStartElement(QStringLiteral("sheetViews"));
    xml.writeStartElement(QStringLiteral("sheetView"));
    xml.writeAttribute(QStringLiteral("workbookViewId"), QStringLiteral("0"));
    xml.writeEndElement();
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("cols"));
    for (int column = 0; column < columnWidths.size(); ++column) {
        xml.writeEmptyElement(QStringLiteral("col"));
        xml.writeAttribute(QStringLiteral("min"), QString::number(column + 1));
        xml.writeAttribute(QStringLiteral("max"), QString::number(column + 1));
        xml.writeAttribute(QStringLiteral("width"), QString::number(columnWidths[column]));
        xml.writeAttribute(QStringLiteral("customWidth"), QStringLiteral("1"));
    }
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("sheetData"));
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        xml.writeStartElement(QStringLiteral("row"));
        xml.writeAttribute(QStringLiteral("r"), QString::number(rowIndex + 1));
        if (row.height > 0.0 || row.style == XlsxRowStyle::Header) {
            xml.writeAttribute(
                QStringLiteral("ht"),
                QString::number(row.height > 0.0 ? row.height : 24.0));
            xml.writeAttribute(QStringLiteral("customHeight"), QStringLiteral("1"));
        }
        for (int column = 0; column < row.cells.size(); ++column) {
            const bool mergedChild = std::any_of(
                row.mergedColumnRanges.cbegin(),
                row.mergedColumnRanges.cend(),
                [column](const auto& range) {
                    return column > range.first && column <= range.second;
                });
            if (mergedChild) {
                continue;
            }
            const auto value = row.cells[column];
            const auto cellStyle = row.cellStyles.value(column, row.style);
            xml.writeStartElement(QStringLiteral("c"));
            xml.writeAttribute(QStringLiteral("r"), cellReference(rowIndex, column));
            xml.writeAttribute(QStringLiteral("s"), QString::number(styleId(cellStyle)));
            if (row.numericColumns.contains(column) && numericText(value) &&
                cellStyle != XlsxRowStyle::Header) {
                xml.writeAttribute(QStringLiteral("t"), QStringLiteral("n"));
                xml.writeTextElement(QStringLiteral("v"), value);
            } else {
                xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                xml.writeStartElement(QStringLiteral("is"));
                xml.writeTextElement(QStringLiteral("t"), value);
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();

    int tableLastRow = tableHeaderRow;
    for (int row = tableHeaderRow + 1; row < rows.size(); ++row) {
        if (rows[row].excludeFromAutoFilter) {
            break;
        }
        tableLastRow = row;
    }
    if (tableHeaderRow >= 0 && !rows[tableHeaderRow].cells.isEmpty()) {
        xml.writeEmptyElement(QStringLiteral("autoFilter"));
        xml.writeAttribute(
            QStringLiteral("ref"),
            QStringLiteral("A%1:%2")
                .arg(tableHeaderRow + 1)
                .arg(cellReference(
                    tableLastRow,
                    rows[tableHeaderRow].cells.size() - 1)));
    }
    int mergedRangeCount = 0;
    for (const auto& row : rows) {
        mergedRangeCount += row.mergedColumnRanges.size();
    }
    if (mergedRangeCount > 0) {
        xml.writeStartElement(QStringLiteral("mergeCells"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(mergedRangeCount));
        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            for (const auto& range : rows[rowIndex].mergedColumnRanges) {
                xml.writeEmptyElement(QStringLiteral("mergeCell"));
                xml.writeAttribute(
                    QStringLiteral("ref"),
                    QStringLiteral("%1:%2")
                        .arg(cellReference(rowIndex, range.first),
                             cellReference(rowIndex, range.second)));
            }
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return bytes;
}

QByteArray workbookXml(QString sheetName)
{
    sheetName = sheetName.trimmed().left(31);
    if (sheetName.isEmpty()) {
        sheetName = QStringLiteral("Report");
    }
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.writeStartDocument(QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("workbook"));
    xml.writeDefaultNamespace(
        QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    xml.writeNamespace(
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
        QStringLiteral("r"));
    xml.writeStartElement(QStringLiteral("sheets"));
    xml.writeEmptyElement(QStringLiteral("sheet"));
    xml.writeAttribute(QStringLiteral("name"), sheetName);
    xml.writeAttribute(QStringLiteral("sheetId"), QStringLiteral("1"));
    xml.writeAttribute(
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
        QStringLiteral("id"),
        QStringLiteral("rId1"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return bytes;
}

QByteArray stylesXml()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"5\"><font><sz val=\"10.5\"/><color rgb=\"FF344054\"/><name val=\"Microsoft YaHei UI\"/></font>"
        "<font><b/><sz val=\"10.5\"/><color rgb=\"FF1D2939\"/><name val=\"Microsoft YaHei UI\"/></font>"
        "<font><b/><sz val=\"10\"/><color rgb=\"FF475467\"/><name val=\"Microsoft YaHei UI\"/></font>"
        "<font><b/><sz val=\"11\"/><color rgb=\"FF1D2939\"/><name val=\"Microsoft YaHei UI\"/></font>"
        "<font><b/><sz val=\"20\"/><color rgb=\"FFFFFFFF\"/><name val=\"Microsoft YaHei UI\"/></font></fonts>"
        "<fills count=\"9\"><fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFDDF4E4\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFFDE2E1\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFECEFF3\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF2F4F7\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFD0D5DD\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF16794A\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFB42318\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
        "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color rgb=\"FFD0D5DD\"/></left>"
        "<right style=\"thin\"><color rgb=\"FFD0D5DD\"/></right>"
        "<top style=\"thin\"><color rgb=\"FFD0D5DD\"/></top>"
        "<bottom style=\"thin\"><color rgb=\"FFD0D5DD\"/></bottom><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"9\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"5\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"6\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"7\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"8\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>");
}

} // namespace

SimpleXlsxWriteResult writeSimpleXlsx(const QString& filePath,
                                      const QString& sheetName,
                                      const QVector<double>& columnWidths,
                                      const QVector<XlsxRow>& rows)
{
    const QVector<ZipEntry> entries = {
        {QByteArrayLiteral("[Content_Types].xml"), QByteArrayLiteral(
             "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
             "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
             "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
             "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
             "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
             "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
             "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
             "</Types>")},
        {QByteArrayLiteral("_rels/.rels"), QByteArrayLiteral(
             "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
             "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
             "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
             "</Relationships>")},
        {QByteArrayLiteral("xl/workbook.xml"), workbookXml(sheetName)},
        {QByteArrayLiteral("xl/_rels/workbook.xml.rels"), QByteArrayLiteral(
             "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
             "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
             "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
             "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
             "</Relationships>")},
        {QByteArrayLiteral("xl/styles.xml"), stylesXml()},
        {QByteArrayLiteral("xl/worksheets/sheet1.xml"), worksheetXml(columnWidths, rows)}
    };

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return {false, file.errorString()};
    }
    const auto bytes = zipArchive(entries);
    if (file.write(bytes) != bytes.size()) {
        return {false, file.errorString()};
    }
    if (!file.commit()) {
        return {false, file.errorString()};
    }
    return {true, {}};
}

} // namespace PicoATE::Ui
