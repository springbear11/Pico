#include "RegisterConfigImporter.h"

#include <QtCore/private/qzipreader_p.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace PicoATE::RegisterImport {

namespace {

constexpr auto kOuterId = "register_config";

struct SheetReference {
    QString name;
    QString path;
    bool hidden = false;
};

struct SheetData {
    QString name;
    QMap<int, QMap<int, QString>> rows;
};

struct RegisterRow {
    int excelRow = 0;
    quint16 address = 0;
    QString addressText;
    QString name;
    QString dataType;
    int registerCount = 0;
    bool writable = false;
    QJsonValue expected;
    long double physicalValue = 0;
    long double scale = 1;
    long double valueOffset = 0;
    quint64 encodedRaw = 0;
};

QString normalizedHeader(QString value)
{
    value = value.trimmed();
    value.remove(QRegularExpression(QStringLiteral("[\\s_\\-]+")));
    return value.toLower();
}

QString attributeByLocalName(const QXmlStreamAttributes& attributes,
                             const QString& localName)
{
    for (const auto& attribute : attributes) {
        if (attribute.name() == localName) {
            return attribute.value().toString();
        }
    }
    return {};
}

QString xmlError(const QXmlStreamReader& xml)
{
    return QStringLiteral("XML line %1, column %2: %3")
        .arg(xml.lineNumber())
        .arg(xml.columnNumber())
        .arg(xml.errorString());
}

void addError(RegisterImportResult& result,
              QString path,
              QString message,
              QString suggestion = {})
{
    result.errors.push_back(
        {std::move(path), std::move(message), std::move(suggestion)});
}

void addWarning(RegisterImportResult& result,
                QString path,
                QString message,
                QString suggestion = {})
{
    result.warnings.push_back(
        {std::move(path), std::move(message), std::move(suggestion)});
}

int columnIndexFromReference(const QString& reference)
{
    int column = 0;
    bool found = false;
    for (const QChar character : reference) {
        if (!character.isLetter()) {
            break;
        }
        found = true;
        column = column * 26 + character.toUpper().unicode() - 'A' + 1;
    }
    return found ? column - 1 : -1;
}

QString collectTextElement(QXmlStreamReader& xml)
{
    QString text;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isCharacters()) {
            text += xml.text();
        } else if (xml.isEndElement() && xml.name() == QStringLiteral("t")) {
            break;
        }
    }
    return text;
}

bool parseSharedStrings(const QByteArray& data,
                        QVector<QString>& strings,
                        QString& error)
{
    if (data.isEmpty()) {
        return true;
    }
    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("si")) {
            continue;
        }
        QString value;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("t")) {
                value += collectTextElement(xml);
            } else if (xml.isEndElement() && xml.name() == QStringLiteral("si")) {
                break;
            }
        }
        strings.push_back(value);
    }
    if (xml.hasError()) {
        error = xmlError(xml);
        return false;
    }
    return true;
}

bool parseWorkbookRelationships(const QByteArray& data,
                                QHash<QString, QString>& targets,
                                QString& error)
{
    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() ||
            xml.name() != QStringLiteral("Relationship")) {
            continue;
        }
        const auto attributes = xml.attributes();
        const auto id = attributeByLocalName(attributes, QStringLiteral("Id"));
        auto target = attributeByLocalName(attributes, QStringLiteral("Target"));
        if (id.isEmpty() || target.isEmpty()) {
            continue;
        }
        target.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (target.startsWith(QLatin1Char('/'))) {
            target.remove(0, 1);
        } else if (!target.startsWith(QStringLiteral("xl/"))) {
            target = QDir::cleanPath(QStringLiteral("xl/") + target);
        }
        targets.insert(id, target);
    }
    if (xml.hasError()) {
        error = xmlError(xml);
        return false;
    }
    return true;
}

bool parseWorkbook(const QByteArray& data,
                   const QHash<QString, QString>& relationshipTargets,
                   QVector<SheetReference>& sheets,
                   QString& error)
{
    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("sheet")) {
            continue;
        }
        const auto attributes = xml.attributes();
        SheetReference sheet;
        sheet.name = attributeByLocalName(attributes, QStringLiteral("name"));
        const auto relationshipId =
            attributeByLocalName(attributes, QStringLiteral("id"));
        sheet.path = relationshipTargets.value(relationshipId);
        sheet.hidden = attributeByLocalName(attributes, QStringLiteral("state"))
                           .compare(QStringLiteral("visible"), Qt::CaseInsensitive) != 0 &&
                       !attributeByLocalName(attributes, QStringLiteral("state"))
                            .isEmpty();
        if (!sheet.name.isEmpty() && !sheet.path.isEmpty()) {
            sheets.push_back(std::move(sheet));
        }
    }
    if (xml.hasError()) {
        error = xmlError(xml);
        return false;
    }
    return true;
}

bool parseWorksheet(const QByteArray& data,
                    const QVector<QString>& sharedStrings,
                    SheetData& sheet,
                    QString& error)
{
    QXmlStreamReader xml(data);
    int currentRow = -1;
    int nextColumn = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("row")) {
            bool ok = false;
            currentRow = attributeByLocalName(xml.attributes(), QStringLiteral("r"))
                             .toInt(&ok);
            if (!ok) {
                currentRow = sheet.rows.isEmpty() ? 1 : sheet.rows.lastKey() + 1;
            }
            nextColumn = 0;
            continue;
        }
        if (!xml.isStartElement() || xml.name() != QStringLiteral("c") ||
            currentRow < 0) {
            continue;
        }

        const auto attributes = xml.attributes();
        const auto reference =
            attributeByLocalName(attributes, QStringLiteral("r"));
        const auto type = attributeByLocalName(attributes, QStringLiteral("t"));
        int column = columnIndexFromReference(reference);
        if (column < 0) {
            column = nextColumn;
        }
        nextColumn = column + 1;

        QString rawValue;
        QString inlineText;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("v")) {
                rawValue = xml.readElementText();
            } else if (xml.isStartElement() &&
                       xml.name() == QStringLiteral("t")) {
                inlineText += collectTextElement(xml);
            } else if (xml.isEndElement() && xml.name() == QStringLiteral("c")) {
                break;
            }
        }

        QString value = inlineText;
        if (value.isEmpty()) {
            if (type == QStringLiteral("s")) {
                bool ok = false;
                const int index = rawValue.toInt(&ok);
                if (ok && index >= 0 && index < sharedStrings.size()) {
                    value = sharedStrings[index];
                }
            } else if (type == QStringLiteral("b")) {
                value = rawValue == QStringLiteral("1")
                    ? QStringLiteral("TRUE")
                    : QStringLiteral("FALSE");
            } else {
                value = rawValue;
            }
        }
        if (!value.isEmpty()) {
            sheet.rows[currentRow].insert(column, value.trimmed());
        }
    }
    if (xml.hasError()) {
        error = xmlError(xml);
        return false;
    }
    return true;
}

bool loadWorkbook(const QString& workbookPath,
                  QVector<SheetData>& sheets,
                  RegisterImportResult& result)
{
    QZipReader archive(workbookPath);
    if (!archive.exists() || !archive.isReadable() ||
        archive.status() != QZipReader::NoError) {
        addError(result,
                 QStringLiteral("workbook"),
                 QStringLiteral("Unable to open workbook as an XLSX package: %1")
                     .arg(workbookPath),
                 QStringLiteral("Close Excel and verify that the file is a valid .xlsx/.xlsm workbook"));
        return false;
    }

    const auto workbookXml = archive.fileData(QStringLiteral("xl/workbook.xml"));
    const auto relationshipsXml =
        archive.fileData(QStringLiteral("xl/_rels/workbook.xml.rels"));
    if (workbookXml.isEmpty() || relationshipsXml.isEmpty()) {
        addError(result,
                 QStringLiteral("workbook"),
                 QStringLiteral("Workbook metadata is incomplete"),
                 QStringLiteral("Open and save the file again as an Excel .xlsx workbook"));
        return false;
    }

    QString error;
    QVector<QString> sharedStrings;
    if (!parseSharedStrings(
            archive.fileData(QStringLiteral("xl/sharedStrings.xml")),
            sharedStrings,
            error)) {
        addError(result, QStringLiteral("workbook.sharedStrings"), error);
        return false;
    }

    QHash<QString, QString> relationshipTargets;
    if (!parseWorkbookRelationships(
            relationshipsXml, relationshipTargets, error)) {
        addError(result, QStringLiteral("workbook.relationships"), error);
        return false;
    }

    QVector<SheetReference> references;
    if (!parseWorkbook(workbookXml, relationshipTargets, references, error)) {
        addError(result, QStringLiteral("workbook.sheets"), error);
        return false;
    }

    for (const auto& reference : references) {
        if (reference.hidden) {
            continue;
        }
        const auto sheetXml = archive.fileData(reference.path);
        if (sheetXml.isEmpty()) {
            addError(result,
                     QStringLiteral("sheet[%1]").arg(reference.name),
                     QStringLiteral("Worksheet data is missing: %1")
                         .arg(reference.path));
            continue;
        }
        SheetData sheet;
        sheet.name = reference.name;
        if (!parseWorksheet(sheetXml, sharedStrings, sheet, error)) {
            addError(result,
                     QStringLiteral("sheet[%1]").arg(reference.name),
                     error);
            continue;
        }
        sheets.push_back(std::move(sheet));
    }
    return result.errors.isEmpty();
}

QString cellValue(const QMap<int, QString>& row,
                  const QHash<QString, int>& columns,
                  const QString& header)
{
    const int column = columns.value(normalizedHeader(header), -1);
    return column >= 0 ? row.value(column).trimmed() : QString{};
}

std::optional<quint16> parseAddress(QString value)
{
    value = value.trimmed();
    static const QRegularExpression addressPattern(
        QStringLiteral("^0[xX]([0-9A-Fa-f]{1,4})$"));
    const auto match = addressPattern.match(value);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const auto parsed = match.captured(1).toULongLong(&ok, 16);
    if (!ok || parsed > std::numeric_limits<quint16>::max()) {
        return std::nullopt;
    }
    return static_cast<quint16>(parsed);
}

std::optional<long double> parseNumber(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    long double number = 0;
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        number = static_cast<long double>(value.mid(2).toULongLong(&ok, 16));
    } else {
        number = static_cast<long double>(value.toDouble(&ok));
    }
    return ok && std::isfinite(number) ? std::optional<long double>(number)
                                       : std::nullopt;
}

QString formatDecimal(long double value)
{
    if (std::fabs(value - std::round(value)) < 1e-12L) {
        if (value >= 0 &&
            value > static_cast<long double>(std::numeric_limits<qint64>::max())) {
            return QString::number(static_cast<qulonglong>(value));
        }
        return QString::number(static_cast<qlonglong>(value));
    }
    return QString::number(static_cast<double>(value), 'g', 15);
}

QString canonicalDataType(QString value, int registerCount)
{
    value = value.trimmed().toLower();
    value.remove(QRegularExpression(QStringLiteral("[\\s_\\-]+")));
    if (value == QStringLiteral("u16") || value == QStringLiteral("uint16")) {
        return registerCount == 1 ? QStringLiteral("uint16") : QString{};
    }
    if (value == QStringLiteral("u32") || value == QStringLiteral("uint32")) {
        return registerCount == 2 ? QStringLiteral("uint32") : QString{};
    }
    if (value == QStringLiteral("u64") || value == QStringLiteral("uint64")) {
        return registerCount == 4 ? QStringLiteral("uint64") : QString{};
    }
    if (value == QStringLiteral("int") || value == QStringLiteral("signed") ||
        value == QStringLiteral("i16") || value == QStringLiteral("int16") ||
        value == QStringLiteral("i32") || value == QStringLiteral("int32") ||
        value == QStringLiteral("i64") || value == QStringLiteral("int64")) {
        if (registerCount == 1) return QStringLiteral("int16");
        if (registerCount == 2) return QStringLiteral("int32");
        if (registerCount == 4) return QStringLiteral("int64");
    }
    return {};
}

int inferredRegisterCount(QString dataType)
{
    dataType = dataType.trimmed().toLower();
    dataType.remove(QRegularExpression(QStringLiteral("[\\s_\\-]+")));
    if (dataType == QStringLiteral("u16") ||
        dataType == QStringLiteral("uint16") ||
        dataType == QStringLiteral("i16") ||
        dataType == QStringLiteral("int16")) return 1;
    if (dataType == QStringLiteral("u32") ||
        dataType == QStringLiteral("uint32") ||
        dataType == QStringLiteral("i32") ||
        dataType == QStringLiteral("int32")) return 2;
    if (dataType == QStringLiteral("u64") ||
        dataType == QStringLiteral("uint64") ||
        dataType == QStringLiteral("i64") ||
        dataType == QStringLiteral("int64")) return 4;
    return 0;
}

bool isSignedType(const QString& dataType)
{
    return dataType.startsWith(QStringLiteral("int"));
}

bool encodeRawValue(RegisterRow& row,
                    RegisterImportResult& result,
                    const QString& path)
{
    if (std::fabs(row.scale) < 1e-18L) {
        addError(result,
                 path + QStringLiteral(".缩放比例"),
                 QStringLiteral("Scale must not be zero"));
        return false;
    }
    const long double raw =
        (row.physicalValue - row.valueOffset) / row.scale;
    const long double rounded = std::round(raw);
    if (std::fabs(raw - rounded) > 1e-9L) {
        addError(result,
                 path + QStringLiteral(".寄存器值"),
                 QStringLiteral("Physical value %1 cannot be represented exactly: "
                                "(value - offset) / scale = %2")
                     .arg(formatDecimal(row.physicalValue), formatDecimal(raw)),
                 QStringLiteral("Adjust the value, offset, or scale so the raw register value is an integer"));
        return false;
    }

    const int bits = row.registerCount * 16;
    if (isSignedType(row.dataType)) {
        const long double minimum = bits == 64
            ? static_cast<long double>(std::numeric_limits<qint64>::min())
            : -std::ldexp(1.0L, bits - 1);
        const long double maximum = bits == 64
            ? static_cast<long double>(std::numeric_limits<qint64>::max())
            : std::ldexp(1.0L, bits - 1) - 1;
        if (rounded < minimum || rounded > maximum) {
            addError(result,
                     path + QStringLiteral(".寄存器值"),
                     QStringLiteral("Raw value %1 is outside the signed %2-bit range")
                         .arg(formatDecimal(rounded))
                         .arg(bits));
            return false;
        }
        const qint64 signedRaw = static_cast<qint64>(rounded);
        row.encodedRaw = static_cast<quint64>(signedRaw);
        if (bits < 64) {
            row.encodedRaw &= (quint64{1} << bits) - 1;
        }
    } else {
        const long double maximum = bits == 64
            ? static_cast<long double>(std::numeric_limits<quint64>::max())
            : std::ldexp(1.0L, bits) - 1;
        if (rounded < 0 || rounded > maximum) {
            addError(result,
                     path + QStringLiteral(".寄存器值"),
                     QStringLiteral("Raw value %1 is outside the unsigned %2-bit range")
                         .arg(formatDecimal(rounded))
                         .arg(bits));
            return false;
        }
        row.encodedRaw = static_cast<quint64>(rounded);
    }
    return true;
}

QJsonValue jsonNumberValue(long double value)
{
    constexpr long double exactIntegerLimit = 9007199254740991.0L;
    if (std::fabs(value - std::round(value)) < 1e-12L &&
        std::fabs(value) > exactIntegerLimit) {
        return formatDecimal(value);
    }
    return static_cast<double>(value);
}

QString registerWords(quint64 raw, int count)
{
    QStringList words;
    words.reserve(count);
    for (int word = count - 1; word >= 0; --word) {
        const quint16 value = static_cast<quint16>((raw >> (word * 16)) & 0xFFFFu);
        words.push_back(QStringLiteral("0x") +
                        QString::number(value, 16)
                            .rightJustified(4, QLatin1Char('0'))
                            .toUpper());
    }
    return QStringLiteral("[%1]").arg(words.join(QLatin1Char(',')));
}

QString unitIdForSheet(const QString& sheetName,
                       RegisterImportResult& result,
                       int sheetIndex)
{
    if (sheetName.contains(QStringLiteral("M2系统监控"), Qt::CaseInsensitive)) {
        return QStringLiteral("0x0F");
    }
    if (sheetName.contains(QStringLiteral("CCU终端共用"), Qt::CaseInsensitive)) {
        return QStringLiteral("0x01");
    }
    if (sheetName.contains(QStringLiteral("MCU主控板"), Qt::CaseInsensitive)) {
        return QStringLiteral("0x00");
    }
    addWarning(result,
               QStringLiteral("sheets[%1].name").arg(sheetIndex),
               QStringLiteral("Sheet '%1' has no explicit unitId mapping; 0x00 was used")
                   .arg(sheetName),
               QStringLiteral("Rename the sheet to include M2系统监控, CCU终端共用, or MCU主控板 if another unitId is required"));
    return QStringLiteral("0x00");
}

QString fixedSheetId(const QString& sheetName)
{
    if (sheetName.contains(QStringLiteral("M2系统监控"), Qt::CaseInsensitive)) {
        return QStringLiteral("001");
    }
    if (sheetName.contains(QStringLiteral("CCU终端共用"), Qt::CaseInsensitive)) {
        return QStringLiteral("002");
    }
    if (sheetName.contains(QStringLiteral("MCU主控板"), Qt::CaseInsensitive)) {
        return QStringLiteral("003");
    }
    return {};
}

bool parseSheetRows(const SheetData& sheet,
                    QVector<RegisterRow>& parsedRows,
                    RegisterImportResult& result,
                    int sheetIndex)
{
    const QSet<QString> requiredHeaders = {
        normalizedHeader(QStringLiteral("起始地址")),
        normalizedHeader(QStringLiteral("参数名称")),
        normalizedHeader(QStringLiteral("数据类型")),
        normalizedHeader(QStringLiteral("功能")),
        normalizedHeader(QStringLiteral("寄存器值")),
    };
    QHash<QString, int> columns;
    int headerRow = -1;
    for (auto iterator = sheet.rows.cbegin(); iterator != sheet.rows.cend(); ++iterator) {
        QHash<QString, int> candidate;
        for (auto cell = iterator.value().cbegin(); cell != iterator.value().cend(); ++cell) {
            const auto key = normalizedHeader(cell.value());
            if (!key.isEmpty()) {
                candidate.insert(key, cell.key());
            }
        }
        bool complete = true;
        for (const auto& required : requiredHeaders) {
            complete = complete && candidate.contains(required);
        }
        if (complete) {
            columns = std::move(candidate);
            headerRow = iterator.key();
            break;
        }
    }

    const auto sheetPath = QStringLiteral("sheets[%1]").arg(sheetIndex);
    if (headerRow < 0) {
        addError(result,
                 sheetPath,
                 QStringLiteral("Sheet '%1' does not contain the required register-table headers")
                     .arg(sheet.name),
                 QStringLiteral("Required: 起始地址, 参数名称, 数据类型, 功能, 寄存器值; 寄存器数 may be inferred"));
        return false;
    }

    int dataIndex = 0;
    for (auto iterator = sheet.rows.upperBound(headerRow);
         iterator != sheet.rows.cend(); ++iterator) {
        const auto& rowCells = iterator.value();
        const auto addressValue =
            cellValue(rowCells, columns, QStringLiteral("起始地址"));
        const auto nameValue =
            cellValue(rowCells, columns, QStringLiteral("参数名称"));
        const auto typeValue =
            cellValue(rowCells, columns, QStringLiteral("数据类型"));
        const auto functionValue =
            cellValue(rowCells, columns, QStringLiteral("功能"));
        const auto physicalValueText =
            cellValue(rowCells, columns, QStringLiteral("寄存器值"));
        if (addressValue.isEmpty() && nameValue.isEmpty() && typeValue.isEmpty() &&
            functionValue.isEmpty() && physicalValueText.isEmpty()) {
            continue;
        }

        ++dataIndex;
        const auto rowPath = QStringLiteral("%1.rows[%2]")
                                 .arg(sheetPath)
                                 .arg(iterator.key());
        RegisterRow row;
        row.excelRow = iterator.key();
        const auto address = parseAddress(addressValue);
        if (!address) {
            addError(result,
                     rowPath + QStringLiteral(".起始地址"),
                     QStringLiteral("Invalid Modbus register address '%1'").arg(addressValue),
                     QStringLiteral("Use hexadecimal text with a 0x prefix, from 0x0000 through 0xFFFF"));
            continue;
        }
        row.address = *address;
        row.addressText = QStringLiteral("0x") +
            QString::number(row.address, 16)
                .rightJustified(4, QLatin1Char('0'))
                .toUpper();
        row.name = nameValue.trimmed();
        if (row.name.isEmpty()) {
            addError(result,
                     rowPath + QStringLiteral(".参数名称"),
                     QStringLiteral("Parameter name is required"));
            continue;
        }

        bool countOk = false;
        row.registerCount =
            cellValue(rowCells, columns, QStringLiteral("寄存器数"))
                .toInt(&countOk);
        if (!countOk || row.registerCount <= 0) {
            row.registerCount = inferredRegisterCount(typeValue);
        }
        row.dataType = canonicalDataType(typeValue, row.registerCount);
        if (row.dataType.isEmpty()) {
            addError(result,
                     rowPath + QStringLiteral(".数据类型"),
                     QStringLiteral("Unsupported data type/register count: '%1' / %2")
                         .arg(typeValue)
                         .arg(row.registerCount),
                     QStringLiteral("Use U16/1, U32/2, U64/4, or int with 1/2/4 registers"));
            continue;
        }

        auto normalizedFunction = functionValue.trimmed().toUpper();
        normalizedFunction.remove(QRegularExpression(QStringLiteral("[\\s_\\-]+")));
        if (normalizedFunction == QStringLiteral("R")) {
            row.writable = false;
        } else if (normalizedFunction == QStringLiteral("R/W") ||
                   normalizedFunction == QStringLiteral("W/R") ||
                   normalizedFunction == QStringLiteral("RW")) {
            row.writable = true;
        } else {
            addError(result,
                     rowPath + QStringLiteral(".功能"),
                     QStringLiteral("Unsupported register function '%1'").arg(functionValue),
                     QStringLiteral("Use R or R/W"));
            continue;
        }

        const auto physicalValue = parseNumber(physicalValueText);
        if (!physicalValue) {
            addError(result,
                     rowPath + QStringLiteral(".寄存器值"),
                     QStringLiteral("Register physical value '%1' is not numeric")
                         .arg(physicalValueText));
            continue;
        }
        row.physicalValue = *physicalValue;
        row.expected = jsonNumberValue(row.physicalValue);

        const auto offsetText =
            cellValue(rowCells, columns, QStringLiteral("偏移"));
        if (!offsetText.isEmpty()) {
            const auto offset = parseNumber(offsetText);
            if (!offset) {
                addError(result,
                         rowPath + QStringLiteral(".偏移"),
                         QStringLiteral("Offset '%1' is not numeric").arg(offsetText));
                continue;
            }
            row.valueOffset = *offset;
        }
        const auto scaleText =
            cellValue(rowCells, columns, QStringLiteral("缩放比例"));
        if (!scaleText.isEmpty()) {
            const auto scale = parseNumber(scaleText);
            if (!scale) {
                addError(result,
                         rowPath + QStringLiteral(".缩放比例"),
                         QStringLiteral("Scale '%1' is not numeric").arg(scaleText));
                continue;
            }
            row.scale = *scale;
        }
        if (row.writable && !encodeRawValue(row, result, rowPath)) {
            continue;
        }
        parsedRows.push_back(std::move(row));
    }

    if (dataIndex == 0) {
        addWarning(result,
                   sheetPath,
                   QStringLiteral("Sheet '%1' contains no register rows and was skipped")
                       .arg(sheet.name));
    }
    return result.errors.isEmpty();
}

QJsonObject actionStep(const QString& id,
                       const QString& name,
                       const QString& moduleId,
                       const QString& function,
                       QJsonObject inputs,
                       int timeoutMs)
{
    QJsonObject step{
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), QStringLiteral("action")},
        {QStringLiteral("name"), name},
        {QStringLiteral("moduleId"), moduleId},
        {QStringLiteral("function"), function},
        {QStringLiteral("inputs"), std::move(inputs)},
    };
    if (timeoutMs > 0) {
        step.insert(QStringLiteral("timeout"),
                    QJsonObject{{QStringLiteral("timeoutMs"), timeoutMs}});
    }
    return step;
}

QJsonObject buildParameterStep(const RegisterRow& row,
                               const QString& sheetId,
                               const QString& parameterId,
                               const QString& unitId,
                               const RegisterImportOptions& options)
{
    QJsonArray steps;
    QString readId;
    QString decodeId;
    if (row.writable) {
        steps.push_back(actionStep(
            QStringLiteral("01"),
            QStringLiteral("10_设置：%1（%2,%3,%4）")
                .arg(row.name, unitId, row.addressText,
                     formatDecimal(row.physicalValue)),
            QStringLiteral("device"),
            QStringLiteral("writeMultipleRegisters"),
            QJsonObject{
                {QStringLiteral("address"), row.addressText},
                {QStringLiteral("dataFormat"), QStringLiteral("registers")},
                {QStringLiteral("deviceId"), options.deviceId},
                {QStringLiteral("unitId"), unitId},
                {QStringLiteral("values"),
                 registerWords(row.encodedRaw, row.registerCount)},
            },
            options.timeoutMs));
        steps.push_back(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("02")},
            {QStringLiteral("kind"), QStringLiteral("wait")},
            {QStringLiteral("name"),
             QStringLiteral("Wait %1ms").arg(options.waitMs)},
            {QStringLiteral("ms"), options.waitMs},
        });
        readId = QStringLiteral("03");
        decodeId = QStringLiteral("04");
    } else {
        readId = QStringLiteral("01");
        decodeId = QStringLiteral("02");
    }

    steps.push_back(actionStep(
        readId,
        QStringLiteral("03_查询：%1（%2,%3,%4）")
            .arg(row.name, unitId, row.addressText)
            .arg(row.registerCount),
        QStringLiteral("device"),
        QStringLiteral("readHoldingRegisters"),
        QJsonObject{
            {QStringLiteral("address"), row.addressText},
            {QStringLiteral("count"), row.registerCount},
            {QStringLiteral("deviceId"), options.deviceId},
            {QStringLiteral("unitId"), unitId},
        },
        options.timeoutMs));

    const auto parameterPath =
        QStringLiteral("%1.%2.%3").arg(QString::fromLatin1(kOuterId),
                                       sheetId,
                                       parameterId);
    steps.push_back(actionStep(
        decodeId,
        QStringLiteral("解析%1").arg(row.name),
        QStringLiteral("builtin.data-parser"),
        QStringLiteral("decodeRegisters"),
        QJsonObject{
            {QStringLiteral("dataType"), row.dataType},
            {QStringLiteral("layout"), QStringLiteral("normal")},
            {QStringLiteral("registerOffset"), 0},
            {QStringLiteral("scale"), static_cast<double>(row.scale)},
            {QStringLiteral("source"),
             QStringLiteral("${step:%1.%2.outputs.registers}")
                 .arg(parameterPath, readId)},
            {QStringLiteral("valueOffset"),
             static_cast<double>(row.valueOffset)},
        },
        0));

    const auto limitId = row.writable ? QStringLiteral("05")
                                      : QStringLiteral("03");
    steps.push_back(QJsonObject{
        {QStringLiteral("id"), limitId},
        {QStringLiteral("kind"), QStringLiteral("limit")},
        {QStringLiteral("name"), QStringLiteral("校验%1").arg(row.name)},
        {QStringLiteral("inputs"),
         QJsonObject{{QStringLiteral("actual"),
                      QStringLiteral("${step:%1.%2.outputs.value}")
                          .arg(parameterPath, decodeId)}}},
        {QStringLiteral("parameters"),
         QJsonObject{
             {QStringLiteral("comparison"), QStringLiteral("equal")},
             {QStringLiteral("expected"), row.expected},
             {QStringLiteral("measurementName"), row.name},
             {QStringLiteral("tolerance"), 0},
         }},
    });

    return QJsonObject{
        {QStringLiteral("id"), parameterId},
        {QStringLiteral("kind"), QStringLiteral("testItem")},
        {QStringLiteral("name"), row.name},
        {QStringLiteral("retry"),
         QJsonObject{{QStringLiteral("maxAttempts"), 3}}},
        {QStringLiteral("steps"), steps},
    };
}

QJsonArray diagnosticsToJson(const QVector<RegisterImportDiagnostic>& diagnostics)
{
    QJsonArray values;
    for (const auto& diagnostic : diagnostics) {
        QJsonObject object{
            {QStringLiteral("path"), diagnostic.path},
            {QStringLiteral("message"), diagnostic.message},
        };
        if (!diagnostic.suggestion.isEmpty()) {
            object.insert(QStringLiteral("suggestion"), diagnostic.suggestion);
        }
        values.push_back(object);
    }
    return values;
}

} // namespace

RegisterImportResult importRegisterDirectory(
    const QString& directoryPath,
    const RegisterImportOptions& options)
{
    RegisterImportResult result;
    const QDir directory(QFileInfo(directoryPath).absoluteFilePath());
    if (!directory.exists()) {
        addError(result,
                 QStringLiteral("registerDirectory"),
                 QStringLiteral("Register directory does not exist: %1")
                     .arg(directory.absolutePath()),
                 QStringLiteral("Create <project>/Register and place one .xlsx workbook in it"));
        return result;
    }

    auto workbooks = directory.entryInfoList(
        QStringList{QStringLiteral("*.xlsx"), QStringLiteral("*.xlsm")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    workbooks.erase(
        std::remove_if(workbooks.begin(), workbooks.end(),
                       [](const QFileInfo& file) {
                           return file.fileName().startsWith(QStringLiteral("~$"));
                       }),
        workbooks.end());
    if (workbooks.isEmpty()) {
        const bool hasLegacyXls = !directory.entryList(
            QStringList{QStringLiteral("*.xls")}, QDir::Files).isEmpty();
        addError(result,
                 QStringLiteral("registerDirectory"),
                 hasLegacyXls
                     ? QStringLiteral("Legacy .xls workbooks are not supported")
                     : QStringLiteral("No .xlsx/.xlsm workbook was found in %1")
                           .arg(directory.absolutePath()),
                 hasLegacyXls
                     ? QStringLiteral("Save the register table as .xlsx")
                     : QStringLiteral("Place exactly one register workbook in the Register directory"));
        return result;
    }
    if (workbooks.size() != 1) {
        QStringList names;
        for (const auto& workbook : workbooks) {
            names.push_back(workbook.fileName());
        }
        addError(result,
                 QStringLiteral("registerDirectory"),
                 QStringLiteral("Multiple register workbooks were found: %1")
                     .arg(names.join(QStringLiteral(", "))),
                 QStringLiteral("Keep exactly one active .xlsx/.xlsm workbook in the Register directory"));
        return result;
    }
    return importRegisterWorkbook(workbooks.first().absoluteFilePath(), options);
}

RegisterImportResult importRegisterWorkbook(
    const QString& workbookPath,
    const RegisterImportOptions& options)
{
    RegisterImportResult result;
    const QFileInfo workbook(workbookPath);
    result.workbookPath = workbook.absoluteFilePath();
    if (!workbook.exists() || !workbook.isFile()) {
        addError(result,
                 QStringLiteral("workbook"),
                 QStringLiteral("Workbook does not exist: %1")
                     .arg(result.workbookPath));
        return result;
    }
    if (options.deviceId.trimmed().isEmpty()) {
        addError(result,
                 QStringLiteral("options.deviceId"),
                 QStringLiteral("Modbus device ID must not be empty"));
        return result;
    }

    QVector<SheetData> sheets;
    if (!loadWorkbook(result.workbookPath, sheets, result)) {
        return result;
    }

    QJsonArray sheetItems;
    QSet<QString> usedSheetIds;
    int nextCustomSheetId = 4;
    for (int index = 0; index < sheets.size(); ++index) {
        QVector<RegisterRow> rows;
        if (!parseSheetRows(sheets[index], rows, result, index)) {
            continue;
        }
        if (rows.isEmpty()) {
            continue;
        }
        auto sheetId = fixedSheetId(sheets[index].name);
        if (!sheetId.isEmpty() && usedSheetIds.contains(sheetId)) {
            addError(
                result,
                QStringLiteral("sheets[%1].name").arg(index),
                QStringLiteral("Multiple sheets map to the fixed ID %1")
                    .arg(sheetId),
                QStringLiteral("Keep only one M2 system, CCU common, or MCU controller sheet per workbook"));
            continue;
        }
        if (sheetId.isEmpty()) {
            do {
                sheetId = QStringLiteral("%1")
                              .arg(nextCustomSheetId++, 3, 10,
                                   QLatin1Char('0'));
            } while (usedSheetIds.contains(sheetId));
            addWarning(
                result,
                QStringLiteral("sheets[%1].name").arg(index),
                QStringLiteral("Sheet '%1' was assigned custom ID %2")
                    .arg(sheets[index].name, sheetId),
                QStringLiteral("Use M2系统监控, CCU终端共用, or MCU主控板 in the sheet name for a fixed ID"));
        }
        usedSheetIds.insert(sheetId);
        const auto unitId = unitIdForSheet(sheets[index].name, result, index);
        QJsonArray parameterItems;
        for (int parameterIndex = 0; parameterIndex < rows.size(); ++parameterIndex) {
            const auto parameterId = QStringLiteral("%1")
                                         .arg(parameterIndex + 1, 2, 10,
                                              QLatin1Char('0'));
            parameterItems.push_back(buildParameterStep(
                rows[parameterIndex], sheetId, parameterId, unitId, options));
            ++result.parameterCount;
        }
        sheetItems.push_back(QJsonObject{
            {QStringLiteral("id"), sheetId},
            {QStringLiteral("kind"), QStringLiteral("testItem")},
            {QStringLiteral("name"), sheets[index].name},
            {QStringLiteral("steps"), parameterItems},
        });
        ++result.sheetCount;
    }

    if (!result.errors.isEmpty()) {
        result.testItem = {};
        return result;
    }
    if (sheetItems.isEmpty()) {
        addError(result,
                 QStringLiteral("workbook"),
                 QStringLiteral("No register parameters were generated"),
                 QStringLiteral("Verify that visible sheets contain the required headers and at least one data row"));
        return result;
    }
    result.testItem = QJsonObject{
        {QStringLiteral("id"), QString::fromLatin1(kOuterId)},
        {QStringLiteral("kind"), QStringLiteral("testItem")},
        {QStringLiteral("name"), QStringLiteral("寄存器表配置写入")},
        {QStringLiteral("steps"), sheetItems},
    };
    return result;
}

QJsonObject resultToJson(const RegisterImportResult& result)
{
    QJsonObject object{
        {QStringLiteral("ok"), result.ok()},
        {QStringLiteral("workbook"), result.workbookPath},
        {QStringLiteral("sheetCount"), result.sheetCount},
        {QStringLiteral("parameterCount"), result.parameterCount},
        {QStringLiteral("errors"), diagnosticsToJson(result.errors)},
        {QStringLiteral("warnings"), diagnosticsToJson(result.warnings)},
    };
    if (result.ok()) {
        object.insert(QStringLiteral("testItem"), result.testItem);
    }
    return object;
}

} // namespace PicoATE::RegisterImport
