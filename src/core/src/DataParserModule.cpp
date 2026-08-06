#include "PicoATE/Core/DataParserModule.h"

#include <QDateTime>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cstring>
#include <limits>

namespace PicoATE::Core {

namespace {

constexpr int MaximumParserTextCharacters = 1024 * 1024;
constexpr int MaximumNamedTextFields = 128;

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char(' '));
    return value;
}

void publishLog(const ModuleExecutionContext& context, const QString& message)
{
    if (!context.logSink) {
        return;
    }
    ModuleLogRecord record;
    record.timestampUtc = QDateTime::currentDateTimeUtc();
    record.message = message;
    context.logSink->publishModuleLog(record);
}

ModuleResult parserError(const ModuleExecutionContext& context,
                         const QString& code,
                         const QString& message)
{
    publishLog(context, QStringLiteral("PARSER_ERROR code=%1 message=%2")
                            .arg(code, message));
    ModuleResult result;
    result.outcome = ModuleOutcome::Error;
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

QString visibleText(const QString& value, int maximumCharacters = 160)
{
    QString visible;
    visible.reserve(std::min(value.size(), qsizetype(maximumCharacters)));
    for (const auto character : value.left(maximumCharacters)) {
        if (character == QLatin1Char('\r')) {
            visible += QStringLiteral("\\r");
        } else if (character == QLatin1Char('\n')) {
            visible += QStringLiteral("\\n");
        } else if (character == QLatin1Char('\t')) {
            visible += QStringLiteral("\\t");
        } else if (character.unicode() < 0x20) {
            visible += QStringLiteral("\\x%1")
                           .arg(static_cast<uint>(character.unicode()),
                                2, 16, QLatin1Char('0'));
        } else {
            visible += character;
        }
    }
    if (value.size() > maximumCharacters) {
        visible += QStringLiteral("...");
    }
    return visible;
}

QString bytesToHex(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

bool parseHexBytes(const QString& source, QByteArray& bytes, QString& error)
{
    QByteArray digits;
    const auto text = source.trimmed();
    for (int index = 0; index < text.size(); ++index) {
        const auto character = text[index];
        if (character == QLatin1Char('0') && index + 1 < text.size() &&
            (text[index + 1] == QLatin1Char('x') ||
             text[index + 1] == QLatin1Char('X'))) {
            ++index;
            continue;
        }
        if (character.isSpace() || character == QLatin1Char(',') ||
            character == QLatin1Char(':') || character == QLatin1Char(';') ||
            character == QLatin1Char('-') || character == QLatin1Char('_')) {
            continue;
        }
        const auto latin = character.toLatin1();
        const bool hexadecimal = (latin >= '0' && latin <= '9') ||
                                 (latin >= 'a' && latin <= 'f') ||
                                 (latin >= 'A' && latin <= 'F');
        if (!hexadecimal) {
            error = QStringLiteral("hex input contains invalid character '%1'")
                        .arg(character);
            return false;
        }
        digits.push_back(latin);
    }
    if (digits.isEmpty()) {
        bytes.clear();
        return true;
    }
    if ((digits.size() % 2) != 0) {
        error = QStringLiteral("hex input must contain complete byte pairs");
        return false;
    }
    bytes = QByteArray::fromHex(digits);
    return true;
}

QByteArray encodedText(const QString& text, const QString& encoding)
{
    const auto mode = normalized(encoding);
    if (mode == QStringLiteral("latin1") || mode == QStringLiteral("ascii")) {
        return text.toLatin1();
    }
    return text.toUtf8();
}

QString decodedText(const QByteArray& bytes, const QString& encoding)
{
    const auto mode = normalized(encoding);
    if (mode == QStringLiteral("latin1") || mode == QStringLiteral("ascii")) {
        return QString::fromLatin1(bytes);
    }
    return QString::fromUtf8(bytes);
}

bool variantToBytes(const QVariant& source,
                    const QString& sourceFormat,
                    const QString& encoding,
                    QByteArray& bytes,
                    QString& error)
{
    const auto typeId = source.metaType().id();
    if (typeId == QMetaType::QByteArray) {
        bytes = source.toByteArray();
        return true;
    }
    if (typeId == QMetaType::QVariantList || typeId == QMetaType::QStringList) {
        const auto values = source.toList();
        bytes.clear();
        bytes.reserve(values.size());
        for (int index = 0; index < values.size(); ++index) {
            bool ok = false;
            const auto value = values[index].toLongLong(&ok);
            if (!ok || value < 0 || value > 0xFF) {
                error = QStringLiteral("source[%1] must be an integer in range 0..255")
                            .arg(index);
                return false;
            }
            bytes.push_back(static_cast<char>(value));
        }
        return true;
    }
    if (typeId != QMetaType::QString) {
        error = QStringLiteral("source must be a byte array, byte list, or string");
        return false;
    }

    const auto text = source.toString();
    const auto format = normalized(sourceFormat);
    if (format == QStringLiteral("text")) {
        bytes = encodedText(text, encoding);
        return true;
    }
    if (format == QStringLiteral("hex")) {
        return parseHexBytes(text, bytes, error);
    }
    if (!format.isEmpty() && format != QStringLiteral("auto")) {
        error = QStringLiteral("unsupported sourceFormat: %1").arg(sourceFormat);
        return false;
    }

    QString hexError;
    if (parseHexBytes(text, bytes, hexError)) {
        return true;
    }
    bytes = encodedText(text, encoding);
    return true;
}

bool variantToText(const QVariant& source,
                   const QString& encoding,
                   QString& text,
                   QString& error)
{
    if (!source.isValid() || source.isNull()) {
        error = QStringLiteral("source is missing");
        return false;
    }
    const auto typeId = source.metaType().id();
    if (typeId == QMetaType::QString) {
        text = source.toString();
    } else if (typeId == QMetaType::QByteArray ||
               typeId == QMetaType::QVariantList ||
               typeId == QMetaType::QStringList) {
        QByteArray bytes;
        if (!variantToBytes(source, QStringLiteral("auto"), encoding, bytes, error)) {
            return false;
        }
        text = decodedText(bytes, encoding);
    } else if (source.canConvert<QString>()) {
        text = source.toString();
    } else {
        error = QStringLiteral("source cannot be converted to text");
        return false;
    }
    if (text.size() > MaximumParserTextCharacters) {
        error = QStringLiteral("source text exceeds the %1 character safety limit")
                    .arg(MaximumParserTextCharacters);
        return false;
    }
    return true;
}

QString decodeEscapes(const QString& source)
{
    QString decoded;
    decoded.reserve(source.size());
    for (int index = 0; index < source.size(); ++index) {
        if (source[index] != QLatin1Char('\\') || index + 1 >= source.size()) {
            decoded += source[index];
            continue;
        }
        const auto escaped = source[++index];
        if (escaped == QLatin1Char('r')) {
            decoded += QLatin1Char('\r');
        } else if (escaped == QLatin1Char('n')) {
            decoded += QLatin1Char('\n');
        } else if (escaped == QLatin1Char('t')) {
            decoded += QLatin1Char('\t');
        } else if (escaped == QLatin1Char('0')) {
            decoded += QChar(0);
        } else if (escaped == QLatin1Char('\\')) {
            decoded += QLatin1Char('\\');
        } else if (escaped == QLatin1Char('x') && index + 2 < source.size()) {
            bool ok = false;
            const auto code = source.mid(index + 1, 2).toUShort(&ok, 16);
            if (ok) {
                decoded += QChar(code);
                index += 2;
            } else {
                decoded += QStringLiteral("\\x");
            }
        } else if (escaped == QLatin1Char('u') && index + 4 < source.size()) {
            bool ok = false;
            const auto code = source.mid(index + 1, 4).toUShort(&ok, 16);
            if (ok) {
                decoded += QChar(code);
                index += 4;
            } else {
                decoded += QStringLiteral("\\u");
            }
        } else {
            decoded += QLatin1Char('\\');
            decoded += escaped;
        }
    }
    return decoded;
}

int numberBase(const QVariant& value)
{
    const auto text = normalized(value.toString());
    if (text.isEmpty() || text == QStringLiteral("auto") || text == QStringLiteral("0")) {
        return 0;
    }
    if (text == QStringLiteral("binary") || text == QStringLiteral("bin")) {
        return 2;
    }
    if (text == QStringLiteral("hex") || text == QStringLiteral("hexadecimal")) {
        return 16;
    }
    bool ok = false;
    const int parsed = text.toInt(&ok);
    return ok && (parsed == 2 || parsed == 10 || parsed == 16) ? parsed : -1;
}

int inferredBase(const QString& text, int configuredBase)
{
    if (configuredBase != 0) {
        return configuredBase;
    }
    const auto trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        return 16;
    }
    if (trimmed.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
        return 2;
    }
    return 10;
}

QString withoutNumericPrefix(QString text, int base)
{
    text = text.trimmed();
    if (base == 16 && text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text.remove(0, 2);
    } else if (base == 2 && text.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
        text.remove(0, 2);
    }
    return text;
}

bool convertTextValue(const QString& text,
                      const QString& requestedType,
                      const QVariant& requestedBase,
                      QVariant& value,
                      QString& error)
{
    const auto type = normalized(requestedType);
    if (type.isEmpty() || type == QStringLiteral("string") ||
        type == QStringLiteral("text")) {
        value = text;
        return true;
    }

    const int configuredBase = numberBase(requestedBase);
    if (configuredBase < 0) {
        error = QStringLiteral("numberBase must be auto, 2, 10, or 16");
        return false;
    }
    const int base = inferredBase(text, configuredBase);
    const auto numericText = withoutNumericPrefix(text, base);
    bool ok = false;
    if (type == QStringLiteral("integer") || type == QStringLiteral("signed")) {
        const auto parsed = numericText.toLongLong(&ok, base);
        if (ok) {
            value = QVariant::fromValue<qlonglong>(parsed);
        }
    } else if (type == QStringLiteral("unsigned")) {
        const auto parsed = numericText.toULongLong(&ok, base);
        if (ok) {
            value = QVariant::fromValue<qulonglong>(parsed);
        }
    } else if (type == QStringLiteral("number") ||
               type == QStringLiteral("double") ||
               type == QStringLiteral("float")) {
        const auto parsed = text.trimmed().toDouble(&ok);
        if (ok) {
            value = parsed;
        }
    } else if (type == QStringLiteral("boolean") || type == QStringLiteral("bool")) {
        const auto boolean = normalized(text);
        if (boolean == QStringLiteral("true") || boolean == QStringLiteral("1") ||
            boolean == QStringLiteral("on") || boolean == QStringLiteral("yes") ||
            boolean == QStringLiteral("pass")) {
            value = true;
            ok = true;
        } else if (boolean == QStringLiteral("false") || boolean == QStringLiteral("0") ||
                   boolean == QStringLiteral("off") || boolean == QStringLiteral("no") ||
                   boolean == QStringLiteral("fail")) {
            value = false;
            ok = true;
        }
    } else if (type == QStringLiteral("hex")) {
        const auto parsed = numericText.toULongLong(&ok, base == 10 ? 16 : base);
        if (ok) {
            value = QStringLiteral("0x%1").arg(QString::number(parsed, 16).toUpper());
        }
    } else {
        error = QStringLiteral("unsupported outputType: %1").arg(requestedType);
        return false;
    }

    if (!ok) {
        error = QStringLiteral("'%1' cannot be converted to %2")
                    .arg(visibleText(text), requestedType);
        return false;
    }
    return true;
}

bool readInteger(const QVariantMap& inputs,
                 const QString& key,
                 qint64 defaultValue,
                 qint64 minimum,
                 qint64 maximum,
                 qint64& result,
                 QString& error);

enum class TextResultMode {
    Single,
    Multiple
};

struct NamedTextField {
    int sourceIndex = 0;
    QString name;
    QString outputType;
};

bool readTextResultMode(const QVariantMap& inputs,
                        TextResultMode& mode,
                        QString& error)
{
    const auto configured = normalized(
        inputs.value(QStringLiteral("resultMode"),
                     QStringLiteral("single")).toString());
    if (configured.isEmpty() || configured == QStringLiteral("single") ||
        configured == QStringLiteral("singlefield")) {
        mode = TextResultMode::Single;
        return true;
    }
    if (configured == QStringLiteral("multiple") ||
        configured == QStringLiteral("multi") ||
        configured == QStringLiteral("namedfields")) {
        mode = TextResultMode::Multiple;
        return true;
    }
    error = QStringLiteral("resultMode must be single or multiple");
    return false;
}

bool isSupportedTextOutputType(const QString& value)
{
    const auto type = normalized(value);
    return type.isEmpty() || type == QStringLiteral("string") ||
           type == QStringLiteral("text") ||
           type == QStringLiteral("integer") ||
           type == QStringLiteral("signed") ||
           type == QStringLiteral("unsigned") ||
           type == QStringLiteral("number") ||
           type == QStringLiteral("double") ||
           type == QStringLiteral("float") ||
           type == QStringLiteral("boolean") ||
           type == QStringLiteral("bool") ||
           type == QStringLiteral("hex");
}

bool readNamedTextFields(const QVariantMap& inputs,
                         const QString& sourceKey,
                         int minimumSourceIndex,
                         int maximumSourceIndex,
                         QVector<NamedTextField>& fields,
                         QString& error)
{
    const auto configured = inputs.value(QStringLiteral("fields"));
    if (configured.metaType().id() != QMetaType::QVariantList) {
        error = QStringLiteral("fields must be a non-empty array");
        return false;
    }
    const auto values = configured.toList();
    if (values.isEmpty() || values.size() > MaximumNamedTextFields) {
        error = QStringLiteral("fields must contain 1..%1 definitions")
                    .arg(MaximumNamedTextFields);
        return false;
    }

    static const QRegularExpression namePattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QSet<QString> names;
    fields.clear();
    fields.reserve(values.size());
    for (int row = 0; row < values.size(); ++row) {
        if (values[row].metaType().id() != QMetaType::QVariantMap) {
            error = QStringLiteral("fields[%1] must be an object").arg(row);
            return false;
        }
        const auto definition = values[row].toMap();
        const auto nameValue = definition.value(QStringLiteral("name"));
        if (nameValue.metaType().id() != QMetaType::QString) {
            error = QStringLiteral("fields[%1].name must be a string").arg(row);
            return false;
        }
        const auto name = nameValue.toString().trimmed();
        if (!namePattern.match(name).hasMatch()) {
            error = QStringLiteral(
                        "fields[%1].name must start with a letter or underscore "
                        "and contain only letters, digits, or underscores")
                        .arg(row);
            return false;
        }
        const auto nameKey = name.toCaseFolded();
        if (names.contains(nameKey)) {
            error = QStringLiteral("fields[%1].name duplicates '%2'")
                        .arg(row).arg(name);
            return false;
        }
        names.insert(nameKey);

        if (!definition.contains(sourceKey)) {
            error = QStringLiteral("fields[%1].%2 is required")
                        .arg(row).arg(sourceKey);
            return false;
        }
        qint64 sourceIndex = 0;
        if (!readInteger(definition, sourceKey, 0,
                         minimumSourceIndex, maximumSourceIndex,
                         sourceIndex, error)) {
            error = QStringLiteral("fields[%1].%2: %3")
                        .arg(row).arg(sourceKey, error);
            return false;
        }
        const auto outputType = definition
                                    .value(QStringLiteral("type"),
                                           QStringLiteral("string"))
                                    .toString().trimmed();
        if (!isSupportedTextOutputType(outputType)) {
            error = QStringLiteral("fields[%1].type is unsupported: %2")
                        .arg(row).arg(outputType);
            return false;
        }
        fields.push_back({static_cast<int>(sourceIndex), name, outputType});
    }
    return true;
}

bool convertNamedTextField(const ModuleExecutionContext& context,
                           const NamedTextField& field,
                           QString extracted,
                           QVariant& value,
                           QString& error)
{
    if (context.inputs.value(QStringLiteral("trim"), true).toBool()) {
        extracted = extracted.trimmed();
    }
    return convertTextValue(
        extracted,
        field.outputType,
        context.inputs.value(QStringLiteral("numberBase"),
                             QStringLiteral("auto")),
        value,
        error);
}

bool readInteger(const QVariantMap& inputs,
                 const QString& key,
                 qint64 defaultValue,
                 qint64 minimum,
                 qint64 maximum,
                 qint64& result,
                 QString& error)
{
    if (!inputs.contains(key)) {
        result = defaultValue;
        return true;
    }
    bool ok = false;
    result = inputs.value(key).toLongLong(&ok);
    if (!ok || result < minimum || result > maximum) {
        error = QStringLiteral("%1 must be an integer in range %2..%3")
                    .arg(key)
                    .arg(minimum)
                    .arg(maximum);
        return false;
    }
    return true;
}

quint64 unsignedFromBytes(const QByteArray& bytes, bool littleEndian)
{
    quint64 value = 0;
    if (littleEndian) {
        for (int index = 0; index < bytes.size(); ++index) {
            value |= static_cast<quint64>(static_cast<uchar>(bytes[index]))
                     << (index * 8);
        }
    } else {
        for (const auto byte : bytes) {
            value = (value << 8) | static_cast<uchar>(byte);
        }
    }
    return value;
}

qint64 signedFromUnsigned(quint64 value, int bitCount)
{
    if (bitCount >= 64) {
        return static_cast<qint64>(value);
    }
    const quint64 sign = quint64{1} << (bitCount - 1);
    if ((value & sign) != 0) {
        value |= std::numeric_limits<quint64>::max() << bitCount;
    }
    return static_cast<qint64>(value);
}

QVariant scaledValue(const QVariant& raw, double scale, double valueOffset)
{
    if (scale == 1.0 && valueOffset == 0.0) {
        return raw;
    }
    return raw.toDouble() * scale + valueOffset;
}

bool decodeNumericBytes(const QByteArray& bytes,
                        const QString& requestedType,
                        bool littleEndian,
                        QVariant& rawValue,
                        QString& error)
{
    const auto type = normalized(requestedType);
    if (bytes.isEmpty() || bytes.size() > 8) {
        error = QStringLiteral("numeric fields must contain 1..8 bytes");
        return false;
    }
    const auto raw = unsignedFromBytes(bytes, littleEndian);
    if (type == QStringLiteral("unsigned") || type == QStringLiteral("uint")) {
        rawValue = QVariant::fromValue<qulonglong>(raw);
        return true;
    }
    if (type == QStringLiteral("signed") || type == QStringLiteral("integer") ||
        type == QStringLiteral("int")) {
        rawValue = QVariant::fromValue<qlonglong>(
            signedFromUnsigned(raw, bytes.size() * 8));
        return true;
    }
    if (type == QStringLiteral("boolean") || type == QStringLiteral("bool")) {
        rawValue = raw != 0;
        return true;
    }
    if (type == QStringLiteral("float32")) {
        if (bytes.size() != 4) {
            error = QStringLiteral("float32 requires exactly 4 bytes");
            return false;
        }
        const auto bits = static_cast<quint32>(raw);
        float decoded = 0.0F;
        std::memcpy(&decoded, &bits, sizeof(decoded));
        rawValue = static_cast<double>(decoded);
        return true;
    }
    if (type == QStringLiteral("float64") || type == QStringLiteral("double")) {
        if (bytes.size() != 8) {
            error = QStringLiteral("float64 requires exactly 8 bytes");
            return false;
        }
        double decoded = 0.0;
        std::memcpy(&decoded, &raw, sizeof(decoded));
        rawValue = decoded;
        return true;
    }
    error = QStringLiteral("unsupported numeric dataType: %1").arg(requestedType);
    return false;
}

int inferredByteLength(const QString& requestedType, int remaining)
{
    const auto type = normalized(requestedType);
    if (type == QStringLiteral("float32")) {
        return 4;
    }
    if (type == QStringLiteral("float64") || type == QStringLiteral("double")) {
        return 8;
    }
    if (type == QStringLiteral("hex") || type == QStringLiteral("text") ||
        type == QStringLiteral("string") || type == QStringLiteral("utf8") ||
        type == QStringLiteral("ascii") || type == QStringLiteral("bcd")) {
        return remaining;
    }
    return remaining;
}

ModuleResult decodeBinary(const ModuleExecutionContext& context)
{
    const auto& inputs = context.inputs;
    if (!inputs.contains(QStringLiteral("source"))) {
        return parserError(context, QStringLiteral("ParserSourceMissing"),
                           QStringLiteral("decodeBinary requires source"));
    }

    QString error;
    QByteArray source;
    const auto encoding = inputs.value(QStringLiteral("encoding"),
                                       QStringLiteral("utf8")).toString();
    if (!variantToBytes(inputs.value(QStringLiteral("source")),
                        inputs.value(QStringLiteral("sourceFormat"),
                                     QStringLiteral("auto")).toString(),
                        encoding,
                        source,
                        error)) {
        return parserError(context, QStringLiteral("ParserSourceTypeError"), error);
    }

    qint64 offset = 0;
    qint64 length = 0;
    if (!readInteger(inputs, QStringLiteral("offset"), 0, 0,
                     std::numeric_limits<int>::max(), offset, error) ||
        !readInteger(inputs, QStringLiteral("length"), 0, 0, 64, length, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }

    const auto unit = normalized(inputs.value(QStringLiteral("unit"),
                                              QStringLiteral("byte")).toString());
    const auto dataType = inputs.value(QStringLiteral("dataType"),
                                       QStringLiteral("unsigned")).toString();
    const auto byteOrder = normalized(inputs.value(QStringLiteral("byteOrder"),
                                                    QStringLiteral("big")).toString());
    if (byteOrder != QStringLiteral("big") && byteOrder != QStringLiteral("little")) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("byteOrder must be big or little"));
    }

    QVariant rawValue;
    QVariant value;
    QByteArray selectedBytes;
    int reportedBitLength = 0;
    if (unit == QStringLiteral("bit") || unit == QStringLiteral("bits")) {
        if (length <= 0 || length > 64 || offset + length > source.size() * 8LL) {
            return parserError(
                context, QStringLiteral("ParserRangeError"),
                QStringLiteral("bit field offset=%1 length=%2 exceeds %3 source bits")
                    .arg(offset).arg(length).arg(source.size() * 8));
        }
        const auto bitOrder = normalized(inputs.value(QStringLiteral("bitOrder"),
                                                       QStringLiteral("lsb0")).toString());
        if (bitOrder != QStringLiteral("lsb0") && bitOrder != QStringLiteral("msb0")) {
            return parserError(context, QStringLiteral("ParserConfigurationError"),
                               QStringLiteral("bitOrder must be lsb0 or msb0"));
        }
        quint64 raw = 0;
        for (int index = 0; index < length; ++index) {
            const auto sourceBit = static_cast<int>(offset) + index;
            const auto sourceByte = sourceBit / 8;
            const auto bitInByte = bitOrder == QStringLiteral("lsb0")
                ? sourceBit % 8 : 7 - (sourceBit % 8);
            const auto bit = (static_cast<uchar>(source[sourceByte]) >> bitInByte) & 1U;
            if (bitOrder == QStringLiteral("lsb0")) {
                raw |= static_cast<quint64>(bit) << index;
            } else {
                raw = (raw << 1) | bit;
            }
        }
        const auto normalizedType = normalized(dataType);
        if (normalizedType == QStringLiteral("signed") ||
            normalizedType == QStringLiteral("integer") ||
            normalizedType == QStringLiteral("int")) {
            rawValue = QVariant::fromValue<qlonglong>(
                signedFromUnsigned(raw, static_cast<int>(length)));
        } else if (normalizedType == QStringLiteral("boolean") ||
                   normalizedType == QStringLiteral("bool")) {
            rawValue = raw != 0;
        } else if (normalizedType == QStringLiteral("hex")) {
            rawValue = QStringLiteral("0x%1").arg(QString::number(raw, 16).toUpper());
        } else if (normalizedType == QStringLiteral("unsigned") ||
                   normalizedType == QStringLiteral("uint")) {
            rawValue = QVariant::fromValue<qulonglong>(raw);
        } else {
            return parserError(
                context, QStringLiteral("ParserConfigurationError"),
                QStringLiteral("bit fields support unsigned, signed, boolean, or hex dataType"));
        }
        const int firstByte = static_cast<int>(offset / 8);
        const int lastByte = static_cast<int>((offset + length - 1) / 8);
        selectedBytes = source.mid(firstByte, lastByte - firstByte + 1);
        reportedBitLength = static_cast<int>(length);
        value = rawValue;
    } else if (unit == QStringLiteral("byte") || unit == QStringLiteral("bytes")) {
        if (offset > source.size()) {
            return parserError(context, QStringLiteral("ParserRangeError"),
                               QStringLiteral("byte offset %1 exceeds source length %2")
                                   .arg(offset).arg(source.size()));
        }
        if (length == 0) {
            length = inferredByteLength(dataType, source.size() - static_cast<int>(offset));
        }
        if (length <= 0 || offset + length > source.size()) {
            return parserError(
                context, QStringLiteral("ParserRangeError"),
                QStringLiteral("byte field offset=%1 length=%2 exceeds source length %3")
                    .arg(offset).arg(length).arg(source.size()));
        }
        selectedBytes = source.mid(static_cast<int>(offset), static_cast<int>(length));
        const auto normalizedType = normalized(dataType);
        if (normalizedType == QStringLiteral("hex")) {
            rawValue = bytesToHex(selectedBytes);
        } else if (normalizedType == QStringLiteral("text") ||
                   normalizedType == QStringLiteral("string") ||
                   normalizedType == QStringLiteral("utf8") ||
                   normalizedType == QStringLiteral("ascii")) {
            rawValue = decodedText(selectedBytes,
                                   normalizedType == QStringLiteral("ascii")
                                       ? QStringLiteral("ascii") : encoding);
        } else if (normalizedType == QStringLiteral("bcd")) {
            auto logicalBytes = selectedBytes;
            if (byteOrder == QStringLiteral("little")) {
                std::reverse(logicalBytes.begin(), logicalBytes.end());
            }
            QString digits;
            for (const auto byte : logicalBytes) {
                const auto high = (static_cast<uchar>(byte) >> 4) & 0x0F;
                const auto low = static_cast<uchar>(byte) & 0x0F;
                if (high > 9 || low > 9) {
                    return parserError(context, QStringLiteral("ParserConversionError"),
                                       QStringLiteral("BCD field contains a nibble greater than 9"));
                }
                digits += QChar(static_cast<ushort>(QLatin1Char('0').unicode() + high));
                digits += QChar(static_cast<ushort>(QLatin1Char('0').unicode() + low));
            }
            bool ok = false;
            const auto decoded = digits.toULongLong(&ok, 10);
            rawValue = ok ? QVariant::fromValue<qulonglong>(decoded)
                          : QVariant(digits);
        } else if (!decodeNumericBytes(selectedBytes,
                                       dataType,
                                       byteOrder == QStringLiteral("little"),
                                       rawValue,
                                       error)) {
            return parserError(context, QStringLiteral("ParserConversionError"), error);
        }
        value = rawValue;
    } else {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("unit must be byte or bit"));
    }

    const auto normalizedType = normalized(dataType);
    const bool scalable = normalizedType != QStringLiteral("hex") &&
                          normalizedType != QStringLiteral("text") &&
                          normalizedType != QStringLiteral("string") &&
                          normalizedType != QStringLiteral("utf8") &&
                          normalizedType != QStringLiteral("ascii") &&
                          normalizedType != QStringLiteral("boolean") &&
                          normalizedType != QStringLiteral("bool");
    if (scalable) {
        value = scaledValue(rawValue,
                            inputs.value(QStringLiteral("scale"), 1.0).toDouble(),
                            inputs.value(QStringLiteral("valueOffset"), 0.0).toDouble());
    }

    ModuleResult result;
    result.outputs.insert(QStringLiteral("value"), value);
    result.outputs.insert(QStringLiteral("rawValue"), rawValue);
    result.outputs.insert(QStringLiteral("rawHex"), bytesToHex(selectedBytes));
    result.outputs.insert(QStringLiteral("sourceLength"), source.size());
    result.outputs.insert(QStringLiteral("offset"), offset);
    result.outputs.insert(QStringLiteral("length"), length);
    result.outputs.insert(QStringLiteral("unit"), unit);
    if (reportedBitLength > 0) {
        result.outputs.insert(QStringLiteral("bitLength"), reportedBitLength);
    }
    publishLog(context,
               QStringLiteral("PARSE_BINARY offset=%1 length=%2 unit=%3 type=%4 raw=%5 value=%6")
                   .arg(offset)
                   .arg(length)
                   .arg(unit)
                   .arg(dataType)
                   .arg(bytesToHex(selectedBytes))
                   .arg(value.toString()));
    return result;
}

bool variantToRegisters(const QVariant& source,
                        QVector<quint16>& registers,
                        QString& error)
{
    const auto typeId = source.metaType().id();
    if (typeId == QMetaType::QVariantList || typeId == QMetaType::QStringList) {
        const auto values = source.toList();
        registers.clear();
        registers.reserve(values.size());
        for (int index = 0; index < values.size(); ++index) {
            bool ok = false;
            const auto value = values[index].toULongLong(&ok);
            if (!ok || value > 0xFFFF) {
                error = QStringLiteral("source[%1] must be a register in range 0..65535")
                            .arg(index);
                return false;
            }
            registers.push_back(static_cast<quint16>(value));
        }
        return true;
    }
    if (typeId != QMetaType::QString) {
        error = QStringLiteral("register source must be an integer list or text list");
        return false;
    }

    const auto tokens = source.toString().split(
        QRegularExpression(QStringLiteral("[\\s,;:]+")), Qt::SkipEmptyParts);
    registers.clear();
    registers.reserve(tokens.size());
    for (int index = 0; index < tokens.size(); ++index) {
        auto token = tokens[index].trimmed();
        const bool hexadecimal = token.startsWith(QStringLiteral("0x"),
                                                   Qt::CaseInsensitive) ||
            token.contains(QRegularExpression(QStringLiteral("[A-Fa-f]")));
        if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            token.remove(0, 2);
        }
        bool ok = false;
        const auto value = token.toULongLong(&ok, hexadecimal ? 16 : 10);
        if (!ok || value > 0xFFFF) {
            error = QStringLiteral("register token %1 is outside 0x0000..0xFFFF")
                        .arg(index);
            return false;
        }
        registers.push_back(static_cast<quint16>(value));
    }
    return true;
}

int registerCountForType(const QString& requestedType)
{
    const auto type = normalized(requestedType);
    if (type == QStringLiteral("uint16") || type == QStringLiteral("int16")) {
        return 1;
    }
    if (type == QStringLiteral("uint32") || type == QStringLiteral("int32") ||
        type == QStringLiteral("float32")) {
        return 2;
    }
    if (type == QStringLiteral("uint64") || type == QStringLiteral("int64") ||
        type == QStringLiteral("float64") || type == QStringLiteral("double")) {
        return 4;
    }
    return 0;
}

QByteArray registerBytes(const QVector<quint16>& registers, const QString& layout)
{
    QByteArray bytes;
    bytes.reserve(registers.size() * 2);
    for (const auto value : registers) {
        bytes.push_back(static_cast<char>((value >> 8) & 0xFF));
        bytes.push_back(static_cast<char>(value & 0xFF));
    }

    const auto mode = normalized(layout);
    if (mode == QStringLiteral("swapbytes") || mode == QStringLiteral("badc")) {
        for (int index = 0; index + 1 < bytes.size(); index += 2) {
            std::swap(bytes[index], bytes[index + 1]);
        }
    } else if (mode == QStringLiteral("reversewords") || mode == QStringLiteral("cdab")) {
        QByteArray reversed;
        reversed.reserve(bytes.size());
        for (int index = bytes.size() - 2; index >= 0; index -= 2) {
            reversed += bytes.mid(index, 2);
        }
        bytes = reversed;
    } else if (mode == QStringLiteral("reverseall") || mode == QStringLiteral("dcba")) {
        std::reverse(bytes.begin(), bytes.end());
    }
    return bytes;
}

ModuleResult decodeRegisters(const ModuleExecutionContext& context)
{
    const auto& inputs = context.inputs;
    if (!inputs.contains(QStringLiteral("source"))) {
        return parserError(context, QStringLiteral("ParserSourceMissing"),
                           QStringLiteral("decodeRegisters requires source"));
    }
    QString error;
    QVector<quint16> registers;
    if (!variantToRegisters(inputs.value(QStringLiteral("source")), registers, error)) {
        return parserError(context, QStringLiteral("ParserSourceTypeError"), error);
    }

    qint64 registerOffset = 0;
    if (!readInteger(inputs, QStringLiteral("registerOffset"), 0, 0,
                     std::numeric_limits<int>::max(), registerOffset, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    const auto dataType = inputs.value(QStringLiteral("dataType"),
                                       QStringLiteral("uint16")).toString();
    const int count = registerCountForType(dataType);
    if (count == 0) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("unsupported register dataType: %1").arg(dataType));
    }
    if (registerOffset + count > registers.size()) {
        return parserError(
            context, QStringLiteral("ParserRangeError"),
            QStringLiteral("registerOffset=%1 requires %2 registers but source contains %3")
                .arg(registerOffset).arg(count).arg(registers.size()));
    }

    QVector<quint16> selected;
    selected.reserve(count);
    QVariantList rawRegisters;
    for (int index = 0; index < count; ++index) {
        const auto value = registers[static_cast<int>(registerOffset) + index];
        selected.push_back(value);
        rawRegisters.push_back(value);
    }
    const auto layout = inputs.value(QStringLiteral("layout"),
                                     QStringLiteral("normal")).toString();
    const auto normalizedLayout = normalized(layout);
    static const QStringList supportedLayouts = {
        QStringLiteral("normal"), QStringLiteral("abcd"),
        QStringLiteral("swapbytes"), QStringLiteral("badc"),
        QStringLiteral("reversewords"), QStringLiteral("cdab"),
        QStringLiteral("reverseall"), QStringLiteral("dcba")};
    if (!supportedLayouts.contains(normalizedLayout)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("unsupported register layout: %1").arg(layout));
    }
    const auto bytes = registerBytes(selected, layout);
    auto numericType = normalized(dataType);
    if (numericType.startsWith(QStringLiteral("uint"))) {
        numericType = QStringLiteral("unsigned");
    } else if (numericType.startsWith(QStringLiteral("int"))) {
        numericType = QStringLiteral("signed");
    }
    QVariant rawValue;
    if (!decodeNumericBytes(bytes, numericType, false, rawValue, error)) {
        return parserError(context, QStringLiteral("ParserConversionError"), error);
    }
    const auto value = scaledValue(
        rawValue,
        inputs.value(QStringLiteral("scale"), 1.0).toDouble(),
        inputs.value(QStringLiteral("valueOffset"), 0.0).toDouble());

    ModuleResult result;
    result.outputs.insert(QStringLiteral("value"), value);
    result.outputs.insert(QStringLiteral("rawValue"), rawValue);
    result.outputs.insert(QStringLiteral("rawHex"), bytesToHex(bytes));
    result.outputs.insert(QStringLiteral("rawRegisters"), rawRegisters);
    result.outputs.insert(QStringLiteral("registerOffset"), registerOffset);
    result.outputs.insert(QStringLiteral("registerCount"), count);
    result.outputs.insert(QStringLiteral("layout"), layout);
    publishLog(context,
               QStringLiteral("PARSE_REGISTERS offset=%1 count=%2 type=%3 layout=%4 raw=%5 value=%6")
                   .arg(registerOffset)
                   .arg(count)
                   .arg(dataType)
                   .arg(layout)
                   .arg(bytesToHex(bytes))
                   .arg(value.toString()));
    return result;
}

ModuleResult decodeRegisterText(const ModuleExecutionContext& context)
{
    const auto& inputs = context.inputs;
    if (!inputs.contains(QStringLiteral("source"))) {
        return parserError(context, QStringLiteral("ParserSourceMissing"),
                           QStringLiteral("decodeRegisterText requires source"));
    }

    QString error;
    QVector<quint16> registers;
    if (!variantToRegisters(inputs.value(QStringLiteral("source")), registers, error)) {
        return parserError(context, QStringLiteral("ParserSourceTypeError"), error);
    }
    if (registers.isEmpty()) {
        return parserError(context, QStringLiteral("ParserRangeError"),
                           QStringLiteral("register source must not be empty"));
    }

    qint64 registerOffset = 0;
    if (!readInteger(inputs, QStringLiteral("registerOffset"), 0, 0,
                     std::numeric_limits<int>::max(), registerOffset, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    qint64 requestedCount = 0;
    if (!readInteger(inputs, QStringLiteral("registerCount"), 0, 0,
                     std::numeric_limits<int>::max(), requestedCount, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    if (registerOffset >= registers.size()) {
        return parserError(
            context, QStringLiteral("ParserRangeError"),
            QStringLiteral("registerOffset=%1 is outside the %2-register source")
                .arg(registerOffset)
                .arg(registers.size()));
    }

    const qint64 registerCount = requestedCount == 0
        ? registers.size() - registerOffset
        : requestedCount;
    if (registerCount <= 0 || registerOffset + registerCount > registers.size()) {
        return parserError(
            context, QStringLiteral("ParserRangeError"),
            QStringLiteral("registerOffset=%1 and registerCount=%2 exceed the %3-register source")
                .arg(registerOffset)
                .arg(registerCount)
                .arg(registers.size()));
    }
    if (registerCount * 2 > MaximumParserTextCharacters) {
        return parserError(
            context, QStringLiteral("ParserRangeError"),
            QStringLiteral("selected register text exceeds the %1-byte safety limit")
                .arg(MaximumParserTextCharacters));
    }

    const auto byteOrder = inputs.value(
        QStringLiteral("byteOrder"), QStringLiteral("highByteFirst")).toString();
    const auto normalizedByteOrder = normalized(byteOrder);
    const bool highByteFirst = normalizedByteOrder == QStringLiteral("highbytefirst");
    if (!highByteFirst && normalizedByteOrder != QStringLiteral("lowbytefirst")) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("unsupported register text byteOrder: %1")
                               .arg(byteOrder));
    }

    QByteArray rawBytes;
    rawBytes.reserve(static_cast<int>(registerCount * 2));
    for (qint64 index = 0; index < registerCount; ++index) {
        const auto value = registers[static_cast<int>(registerOffset + index)];
        const auto high = static_cast<char>((value >> 8) & 0xFF);
        const auto low = static_cast<char>(value & 0xFF);
        rawBytes.push_back(highByteFirst ? high : low);
        rawBytes.push_back(highByteFirst ? low : high);
    }

    const auto padding = inputs.value(
        QStringLiteral("padding"), QStringLiteral("trimTrailingNulls")).toString();
    const auto normalizedPadding = normalized(padding);
    QByteArray parsedBytes = rawBytes;
    QString canonicalPadding;
    if (normalizedPadding == QStringLiteral("trimtrailingnulls") ||
        normalizedPadding == QStringLiteral("striptrailingnulls") ||
        normalizedPadding == QStringLiteral("striptrailingzeros")) {
        while (!parsedBytes.isEmpty() && parsedBytes.endsWith('\0')) {
            parsedBytes.chop(1);
        }
        canonicalPadding = QStringLiteral("trimTrailingNulls");
    } else if (normalizedPadding == QStringLiteral("keep") ||
               normalizedPadding == QStringLiteral("keepall")) {
        canonicalPadding = QStringLiteral("keep");
    } else {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("unsupported register text padding: %1")
                               .arg(padding));
    }

    const auto encoding = inputs.value(
        QStringLiteral("encoding"), QStringLiteral("utf8")).toString();
    const auto normalizedEncoding = normalized(encoding);
    QString text;
    QString canonicalEncoding;
    if (normalizedEncoding == QStringLiteral("ascii")) {
        for (int index = 0; index < parsedBytes.size(); ++index) {
            if (static_cast<uchar>(parsedBytes[index]) > 0x7F) {
                return parserError(
                    context, QStringLiteral("ParserConversionError"),
                    QStringLiteral("ASCII register text contains byte 0x%1 at byte offset %2")
                        .arg(static_cast<uchar>(parsedBytes[index]), 2, 16,
                             QLatin1Char('0'))
                        .arg(index));
            }
        }
        text = QString::fromLatin1(parsedBytes);
        canonicalEncoding = QStringLiteral("ascii");
    } else if (normalizedEncoding == QStringLiteral("utf8")) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        text = decoder(parsedBytes);
        if (decoder.hasError()) {
            return parserError(context, QStringLiteral("ParserConversionError"),
                               QStringLiteral("register text contains invalid UTF-8"));
        }
        canonicalEncoding = QStringLiteral("utf8");
    } else {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("unsupported register text encoding: %1")
                               .arg(encoding));
    }

    QVariantList rawByteValues;
    rawByteValues.reserve(rawBytes.size());
    for (const auto byte : rawBytes) {
        rawByteValues.push_back(static_cast<uchar>(byte));
    }

    ModuleResult result;
    result.outputs.insert(QStringLiteral("value"), text);
    result.outputs.insert(QStringLiteral("text"), text);
    result.outputs.insert(QStringLiteral("rawBytes"), rawByteValues);
    result.outputs.insert(QStringLiteral("rawHex"), bytesToHex(rawBytes));
    result.outputs.insert(QStringLiteral("parsedLength"), parsedBytes.size());
    result.outputs.insert(QStringLiteral("characterCount"), text.toUcs4().size());
    result.outputs.insert(QStringLiteral("registerOffset"), registerOffset);
    result.outputs.insert(QStringLiteral("registerCount"), registerCount);
    result.outputs.insert(QStringLiteral("byteOrder"),
                          highByteFirst ? QStringLiteral("highByteFirst")
                                        : QStringLiteral("lowByteFirst"));
    result.outputs.insert(QStringLiteral("encoding"), canonicalEncoding);
    result.outputs.insert(QStringLiteral("padding"), canonicalPadding);
    publishLog(
        context,
        QStringLiteral("PARSE_REGISTER_TEXT offset=%1 count=%2 byteOrder=%3 encoding=%4 padding=%5 raw=%6 parsedBytes=%7 text=%8")
            .arg(registerOffset)
            .arg(registerCount)
            .arg(result.outputs.value(QStringLiteral("byteOrder")).toString())
            .arg(canonicalEncoding)
            .arg(canonicalPadding)
            .arg(bytesToHex(rawBytes))
            .arg(parsedBytes.size())
            .arg(visibleText(text)));
    return result;
}

bool prepareText(const ModuleExecutionContext& context,
                 QString& source,
                 QString& error)
{
    if (!context.inputs.contains(QStringLiteral("source"))) {
        error = QStringLiteral("source is required");
        return false;
    }
    return variantToText(
        context.inputs.value(QStringLiteral("source")),
        context.inputs.value(QStringLiteral("encoding"),
                             QStringLiteral("utf8")).toString(),
        source,
        error);
}

bool finishTextResult(const ModuleExecutionContext& context,
                      const QString& operation,
                      const QString& extracted,
                      QVariantMap metadata,
                      ModuleResult& result)
{
    QString error;
    QVariant value;
    if (!convertTextValue(
            extracted,
            context.inputs.value(QStringLiteral("outputType"),
                                 QStringLiteral("string")).toString(),
            context.inputs.value(QStringLiteral("numberBase"),
                                 QStringLiteral("auto")),
            value,
            error)) {
        result = parserError(context, QStringLiteral("ParserConversionError"), error);
        return false;
    }
    result.outputs = std::move(metadata);
    result.outputs.insert(QStringLiteral("text"), extracted);
    result.outputs.insert(QStringLiteral("value"), value);
    publishLog(context,
               QStringLiteral("%1 text='%2' value='%3' type=%4")
                   .arg(operation)
                   .arg(visibleText(extracted))
                   .arg(visibleText(value.toString()))
                   .arg(context.inputs.value(QStringLiteral("outputType"),
                                             QStringLiteral("string")).toString()));
    return true;
}

ModuleResult extractBetween(const ModuleExecutionContext& context)
{
    QString source;
    QString error;
    if (!prepareText(context, source, error)) {
        return parserError(context, QStringLiteral("ParserSourceTypeError"), error);
    }
    const auto startMarker = decodeEscapes(
        context.inputs.value(QStringLiteral("startMarker")).toString());
    const auto endMarker = decodeEscapes(
        context.inputs.value(QStringLiteral("endMarker")).toString());
    qint64 occurrence = 1;
    if (!readInteger(context.inputs, QStringLiteral("occurrence"), 1, 1,
                     std::numeric_limits<int>::max(), occurrence, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    if (startMarker.isEmpty() && occurrence > 1) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("occurrence greater than 1 requires startMarker"));
    }
    const auto sensitivity = context.inputs.value(QStringLiteral("caseSensitive"), true)
        .toBool() ? Qt::CaseSensitive : Qt::CaseInsensitive;

    int markerIndex = startMarker.isEmpty() ? 0 : -1;
    int searchFrom = 0;
    for (int match = 0; match < occurrence && !startMarker.isEmpty(); ++match) {
        markerIndex = source.indexOf(startMarker, searchFrom, sensitivity);
        if (markerIndex < 0) {
            return parserError(
                context, QStringLiteral("ParserMarkerNotFound"),
                QStringLiteral("startMarker '%1' occurrence %2 was not found")
                    .arg(visibleText(startMarker)).arg(occurrence));
        }
        searchFrom = markerIndex + startMarker.size();
    }
    const int valueStart = markerIndex + startMarker.size();
    const int valueEnd = endMarker.isEmpty()
        ? source.size()
        : source.indexOf(endMarker, valueStart, sensitivity);
    if (valueEnd < 0) {
        return parserError(
            context, QStringLiteral("ParserMarkerNotFound"),
            QStringLiteral("endMarker '%1' was not found after startMarker")
                .arg(visibleText(endMarker)));
    }
    auto extracted = source.mid(valueStart, valueEnd - valueStart);
    if (context.inputs.value(QStringLiteral("trim"), true).toBool()) {
        extracted = extracted.trimmed();
    }

    ModuleResult result;
    QVariantMap metadata;
    metadata.insert(QStringLiteral("startIndex"), valueStart);
    metadata.insert(QStringLiteral("endIndex"), valueEnd);
    metadata.insert(QStringLiteral("occurrence"), occurrence);
    finishTextResult(context, QStringLiteral("PARSE_TEXT_BETWEEN"),
                     extracted, std::move(metadata), result);
    return result;
}

ModuleResult splitText(const ModuleExecutionContext& context)
{
    QString source;
    QString error;
    if (!prepareText(context, source, error)) {
        return parserError(context, QStringLiteral("ParserSourceTypeError"), error);
    }
    const auto delimiter = decodeEscapes(
        context.inputs.value(QStringLiteral("delimiter")).toString());
    if (delimiter.isEmpty()) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("delimiter cannot be empty"));
    }
    const auto sensitivity = context.inputs.value(QStringLiteral("caseSensitive"), true)
        .toBool() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const auto fields = source.split(delimiter, Qt::KeepEmptyParts, sensitivity);

    TextResultMode resultMode = TextResultMode::Single;
    if (!readTextResultMode(context.inputs, resultMode, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    if (resultMode == TextResultMode::Multiple) {
        QVector<NamedTextField> mappings;
        if (!readNamedTextFields(
                context.inputs, QStringLiteral("index"),
                std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max(), mappings, error)) {
            return parserError(context, QStringLiteral("ParserConfigurationError"), error);
        }

        QVariantMap namedFields;
        QVariantList values;
        values.reserve(mappings.size());
        for (const auto& mapping : mappings) {
            const int index = mapping.sourceIndex < 0
                ? fields.size() + mapping.sourceIndex
                : mapping.sourceIndex;
            if (index < 0 || index >= fields.size()) {
                return parserError(
                    context, QStringLiteral("ParserRangeError"),
                    QStringLiteral("named field '%1' uses index %2 outside %3 split fields")
                        .arg(mapping.name)
                        .arg(mapping.sourceIndex)
                        .arg(fields.size()));
            }
            QVariant converted;
            if (!convertNamedTextField(context, mapping, fields[index],
                                       converted, error)) {
                return parserError(
                    context, QStringLiteral("ParserConversionError"),
                    QStringLiteral("named field '%1': %2")
                        .arg(mapping.name, error));
            }
            namedFields.insert(mapping.name, converted);
            values.push_back(converted);
            publishLog(
                context,
                QStringLiteral("PARSE_TEXT_SPLIT field=%1 index=%2 value='%3' type=%4")
                    .arg(mapping.name)
                    .arg(index)
                    .arg(visibleText(converted.toString()))
                    .arg(mapping.outputType));
        }

        ModuleResult result;
        result.outputs.insert(QStringLiteral("fields"), namedFields);
        result.outputs.insert(QStringLiteral("values"), values);
        result.outputs.insert(QStringLiteral("fieldCount"), fields.size());
        result.outputs.insert(QStringLiteral("namedFieldCount"), mappings.size());
        publishLog(context,
                   QStringLiteral("PARSE_TEXT_SPLIT completed namedFields=%1 sourceFields=%2")
                       .arg(mappings.size()).arg(fields.size()));
        return result;
    }

    qint64 requestedIndex = 0;
    if (!readInteger(context.inputs, QStringLiteral("fieldIndex"), 0,
                     std::numeric_limits<int>::min(),
                     std::numeric_limits<int>::max(), requestedIndex, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    const int index = requestedIndex < 0
        ? fields.size() + static_cast<int>(requestedIndex)
        : static_cast<int>(requestedIndex);
    if (index < 0 || index >= fields.size()) {
        return parserError(
            context, QStringLiteral("ParserRangeError"),
            QStringLiteral("fieldIndex %1 is outside %2 split fields")
                .arg(requestedIndex).arg(fields.size()));
    }
    auto extracted = fields[index];
    if (context.inputs.value(QStringLiteral("trim"), true).toBool()) {
        extracted = extracted.trimmed();
    }

    ModuleResult result;
    QVariantMap metadata;
    metadata.insert(QStringLiteral("fieldIndex"), index);
    metadata.insert(QStringLiteral("fieldCount"), fields.size());
    finishTextResult(context, QStringLiteral("PARSE_TEXT_SPLIT"),
                     extracted, std::move(metadata), result);
    return result;
}

ModuleResult regexCapture(const ModuleExecutionContext& context)
{
    QString source;
    QString error;
    if (!prepareText(context, source, error)) {
        return parserError(context, QStringLiteral("ParserSourceTypeError"), error);
    }
    const auto pattern = context.inputs.value(QStringLiteral("pattern")).toString();
    if (pattern.isEmpty() || pattern.size() > 4096) {
        return parserError(context, QStringLiteral("ParserConfigurationError"),
                           QStringLiteral("pattern must contain 1..4096 characters"));
    }
    QRegularExpression expression(pattern);
    if (!context.inputs.value(QStringLiteral("caseSensitive"), true).toBool()) {
        expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }
    if (!expression.isValid()) {
        return parserError(context, QStringLiteral("ParserRegexInvalid"),
                           expression.errorString());
    }
    qint64 occurrence = 1;
    if (!readInteger(context.inputs, QStringLiteral("occurrence"), 1, 1,
                     std::numeric_limits<int>::max(), occurrence, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }

    auto iterator = expression.globalMatch(source);
    QRegularExpressionMatch selected;
    qint64 matchedCount = 0;
    while (matchedCount < occurrence && iterator.hasNext()) {
        selected = iterator.next();
        ++matchedCount;
    }
    if (matchedCount != occurrence || !selected.hasMatch()) {
        return parserError(
            context, QStringLiteral("ParserPatternNotFound"),
            QStringLiteral("regular expression occurrence %1 was not found")
                .arg(occurrence));
    }

    TextResultMode resultMode = TextResultMode::Single;
    if (!readTextResultMode(context.inputs, resultMode, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    if (resultMode == TextResultMode::Multiple) {
        QVector<NamedTextField> mappings;
        if (!readNamedTextFields(
                context.inputs, QStringLiteral("group"), 0,
                expression.captureCount(), mappings, error)) {
            return parserError(context, QStringLiteral("ParserConfigurationError"), error);
        }

        QVariantMap namedFields;
        QVariantList values;
        values.reserve(mappings.size());
        for (const auto& mapping : mappings) {
            if (selected.capturedStart(mapping.sourceIndex) < 0) {
                return parserError(
                    context, QStringLiteral("ParserPatternNotFound"),
                    QStringLiteral("named field '%1' capture group %2 did not participate in the match")
                        .arg(mapping.name).arg(mapping.sourceIndex));
            }
            QVariant converted;
            if (!convertNamedTextField(
                    context, mapping, selected.captured(mapping.sourceIndex),
                    converted, error)) {
                return parserError(
                    context, QStringLiteral("ParserConversionError"),
                    QStringLiteral("named field '%1': %2")
                        .arg(mapping.name, error));
            }
            namedFields.insert(mapping.name, converted);
            values.push_back(converted);
            publishLog(
                context,
                QStringLiteral("PARSE_TEXT_REGEX field=%1 group=%2 value='%3' type=%4")
                    .arg(mapping.name)
                    .arg(mapping.sourceIndex)
                    .arg(visibleText(converted.toString()))
                    .arg(mapping.outputType));
        }

        ModuleResult result;
        result.outputs.insert(QStringLiteral("fields"), namedFields);
        result.outputs.insert(QStringLiteral("values"), values);
        result.outputs.insert(QStringLiteral("match"), selected.captured(0));
        result.outputs.insert(QStringLiteral("occurrence"), occurrence);
        result.outputs.insert(QStringLiteral("namedFieldCount"), mappings.size());
        publishLog(context,
                   QStringLiteral("PARSE_TEXT_REGEX completed namedFields=%1 occurrence=%2")
                       .arg(mappings.size()).arg(occurrence));
        return result;
    }

    qint64 captureGroup = 1;
    if (!readInteger(context.inputs, QStringLiteral("captureGroup"), 1, 0,
                     1000, captureGroup, error)) {
        return parserError(context, QStringLiteral("ParserConfigurationError"), error);
    }
    if (captureGroup > expression.captureCount()) {
        return parserError(
            context, QStringLiteral("ParserConfigurationError"),
            QStringLiteral("captureGroup %1 exceeds pattern capture count %2")
                .arg(captureGroup).arg(expression.captureCount()));
    }
    auto extracted = selected.captured(static_cast<int>(captureGroup));
    if (context.inputs.value(QStringLiteral("trim"), true).toBool()) {
        extracted = extracted.trimmed();
    }

    ModuleResult result;
    QVariantMap metadata;
    metadata.insert(QStringLiteral("match"), selected.captured(0));
    metadata.insert(QStringLiteral("captureGroup"), captureGroup);
    metadata.insert(QStringLiteral("occurrence"), occurrence);
    metadata.insert(QStringLiteral("startIndex"),
                    selected.capturedStart(static_cast<int>(captureGroup)));
    metadata.insert(QStringLiteral("endIndex"),
                    selected.capturedEnd(static_cast<int>(captureGroup)));
    finishTextResult(context, QStringLiteral("PARSE_TEXT_REGEX"),
                     extracted, std::move(metadata), result);
    return result;
}

} // namespace

ModuleId DataParserModule::moduleId() const
{
    return QString::fromLatin1(BuiltInDataParserModuleId);
}

ModuleResult DataParserModule::execute(const ModuleFunction& functionName,
                                       const ModuleExecutionContext& context)
{
    const auto function = normalized(functionName);
    if (function == QStringLiteral("decodebinary")) {
        return decodeBinary(context);
    }
    if (function == QStringLiteral("decoderegisters")) {
        return decodeRegisters(context);
    }
    if (function == QStringLiteral("decoderegistertext")) {
        return decodeRegisterText(context);
    }
    if (function == QStringLiteral("extractbetween")) {
        return extractBetween(context);
    }
    if (function == QStringLiteral("splittext")) {
        return splitText(context);
    }
    if (function == QStringLiteral("regexcapture")) {
        return regexCapture(context);
    }
    return parserError(context,
                       QStringLiteral("ParserFunctionNotSupported"),
                       QStringLiteral("unsupported parser function: %1")
                           .arg(functionName));
}

} // namespace PicoATE::Core
