#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cstring>

namespace {

QJsonObject errorResponse(const QString& code, const QString& message)
{
    QJsonObject response;
    response.insert("outcome", "Error");
    response.insert("outputs", QJsonObject{});
    response.insert("measurements", QJsonObject{});
    response.insert("errorCode", code);
    response.insert("errorMessage", message);
    return response;
}

int writeJsonResponse(const QJsonObject& response,
                      char* responseJsonUtf8,
                      int responseBufferSize)
{
    if (!responseJsonUtf8 || responseBufferSize <= 1) {
        return 2;
    }

    const auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const int bytesSize = static_cast<int>(bytes.size());
    const int bytesToCopy = std::min(bytesSize, responseBufferSize - 1);
    std::memcpy(responseJsonUtf8, bytes.constData(), static_cast<size_t>(bytesToCopy));
    responseJsonUtf8[bytesToCopy] = '\0';
    return bytesSize < responseBufferSize ? 0 : 3;
}

bool parseFrameBytes(const QJsonValue& value, QVector<quint8>& bytes, QString& error)
{
    bytes.clear();

    if (value.isArray()) {
        const auto array = value.toArray();
        for (const auto& item : array) {
            if (!item.isDouble()) {
                error = "rawBytes array items must be numbers";
                return false;
            }
            const int parsed = item.toInt(-1);
            if (parsed < 0 || parsed > 255) {
                error = "rawBytes array items must be between 0 and 255";
                return false;
            }
            bytes.push_back(static_cast<quint8>(parsed));
        }
        return !bytes.isEmpty();
    }

    if (!value.isString()) {
        error = "rawBytes must be a hex string or an array of bytes";
        return false;
    }

    auto text = value.toString().trimmed();
    text.remove("0x", Qt::CaseInsensitive);
    text.replace(',', ' ');
    text.replace('-', ' ');

    const auto parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() > 1) {
        for (const auto& part : parts) {
            bool ok = false;
            const int parsed = part.toInt(&ok, 16);
            if (!ok || parsed < 0 || parsed > 255) {
                error = QString("Invalid byte: %1").arg(part);
                return false;
            }
            bytes.push_back(static_cast<quint8>(parsed));
        }
        return !bytes.isEmpty();
    }

    text.remove(' ');
    if (text.isEmpty() || (text.size() % 2) != 0) {
        error = "rawBytes hex string must contain an even number of digits";
        return false;
    }

    for (int i = 0; i < text.size(); i += 2) {
        bool ok = false;
        const int parsed = text.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            error = QString("Invalid hex byte at offset %1").arg(i);
            return false;
        }
        bytes.push_back(static_cast<quint8>(parsed));
    }
    return !bytes.isEmpty();
}

quint64 decodeUnsigned(const QVector<quint8>& bytes, int startByte, int byteLength, bool littleEndian)
{
    quint64 raw = 0;
    for (int i = 0; i < byteLength; ++i) {
        const auto byte = static_cast<quint64>(bytes[startByte + i]);
        if (littleEndian) {
            raw |= byte << (8 * i);
        } else {
            raw = (raw << 8) | byte;
        }
    }
    return raw;
}

qint64 applySigned(quint64 raw, int bitLength)
{
    if (bitLength <= 0 || bitLength >= 64) {
        return static_cast<qint64>(raw);
    }

    const quint64 signBit = 1ULL << (bitLength - 1);
    if ((raw & signBit) == 0) {
        return static_cast<qint64>(raw);
    }

    const quint64 mask = (1ULL << bitLength) - 1;
    return -static_cast<qint64>((~raw + 1) & mask);
}

bool readLimit(const QJsonObject& signal, const QString& key, double& value)
{
    if (!signal.contains(key) || signal.value(key).isNull()) {
        return false;
    }
    value = signal.value(key).toDouble();
    return true;
}

QString byteString(const QVector<quint8>& bytes)
{
    QStringList parts;
    for (const auto byte : bytes) {
        parts.push_back(QString("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper());
    }
    return parts.join(' ');
}

} // namespace

extern "C" __declspec(dllexport)
int PicoATE_Execute(const char* requestJsonUtf8,
                    char* responseJsonUtf8,
                    int responseBufferSize)
{
    if (!requestJsonUtf8) {
        return writeJsonResponse(errorResponse("NullRequest", "Request JSON pointer is null"),
                                 responseJsonUtf8,
                                 responseBufferSize);
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(requestJsonUtf8), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return writeJsonResponse(errorResponse("InvalidRequest", parseError.errorString()),
                                 responseJsonUtf8,
                                 responseBufferSize);
    }

    const auto request = document.object();
    const auto context = request.value("context").toObject();
    const auto inputs = context.value("inputs").toObject();

    QVector<quint8> bytes;
    QString parseErrorText;
    if (!parseFrameBytes(inputs.value("rawBytes"), bytes, parseErrorText)) {
        return writeJsonResponse(errorResponse("InvalidCanFrame", parseErrorText),
                                 responseJsonUtf8,
                                 responseBufferSize);
    }

    const auto signal = inputs.value("signal").toObject();
    const auto signalName = signal.value("name").toString("CAN_SIGNAL");
    const int startByte = signal.value("startByte").toInt(0);
    const int byteLength = signal.value("byteLength").toInt(2);
    if (startByte < 0 || byteLength <= 0 || startByte + byteLength > bytes.size() || byteLength > 8) {
        return writeJsonResponse(errorResponse("SignalOutOfRange", "Signal byte range is outside rawBytes"),
                                 responseJsonUtf8,
                                 responseBufferSize);
    }

    const auto byteOrder = signal.value("byteOrder").toString("littleEndian");
    const bool littleEndian = byteOrder.compare("bigEndian", Qt::CaseInsensitive) != 0;
    const bool signedValue = signal.value("signed").toBool(false);
    const auto rawUnsigned = decodeUnsigned(bytes, startByte, byteLength, littleEndian);
    const auto rawSigned = signedValue ? applySigned(rawUnsigned, byteLength * 8) : static_cast<qint64>(rawUnsigned);
    const double scale = signal.value("scale").toDouble(1.0);
    const double offset = signal.value("offset").toDouble(0.0);
    const double physicalValue = static_cast<double>(rawSigned) * scale + offset;
    const auto unit = signal.value("unit").toString();

    double minValue = 0.0;
    double maxValue = 0.0;
    const bool hasMin = readLimit(signal, "min", minValue);
    const bool hasMax = readLimit(signal, "max", maxValue);
    const bool belowMin = hasMin && physicalValue < minValue;
    const bool aboveMax = hasMax && physicalValue > maxValue;
    const bool passed = !belowMin && !aboveMax;

    QJsonObject measurements;
    measurements.insert("name", signalName);
    measurements.insert("value", physicalValue);
    measurements.insert("unit", unit);
    measurements.insert("rawValue", static_cast<double>(rawSigned));
    if (hasMin) {
        measurements.insert("min", minValue);
    }
    if (hasMax) {
        measurements.insert("max", maxValue);
    }

    QJsonObject outputs;
    outputs.insert("frameId", inputs.value("frameId").toString());
    outputs.insert("rawBytes", byteString(bytes));
    outputs.insert("signalName", signalName);
    outputs.insert("rawValue", static_cast<double>(rawSigned));
    outputs.insert("physicalValue", physicalValue);
    outputs.insert("unit", unit);
    outputs.insert("passed", passed);

    QJsonObject response;
    response.insert("outcome", passed ? "Passed" : "Failed");
    response.insert("outputs", outputs);
    response.insert("measurements", measurements);
    response.insert("errorCode", passed ? QString() : QString("LimitFail"));
    response.insert("errorMessage",
                    passed ? QString()
                           : QString("%1=%2 %3 outside [%4, %5]")
                                 .arg(signalName)
                                 .arg(physicalValue)
                                 .arg(unit)
                                 .arg(hasMin ? QString::number(minValue) : QString("-inf"))
                                 .arg(hasMax ? QString::number(maxValue) : QString("+inf")));
    return writeJsonResponse(response, responseJsonUtf8, responseBufferSize);
}
