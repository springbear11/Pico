#include "VendorCanAdapter.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <algorithm>
#include <cstring>

namespace {

QMutex g_mutex;
VendorCanAdapter g_adapter;

QJsonObject response(const QString& outcome,
                     const QJsonObject& outputs = {},
                     const QJsonValue& measurements = QJsonObject{},
                     const QString& errorCode = {},
                     const QString& errorMessage = {})
{
    return {
        {"outcome", outcome},
        {"outputs", outputs},
        {"measurements", measurements},
        {"errorCode", errorCode},
        {"errorMessage", errorMessage},
    };
}

int writeResponse(const QJsonObject& object, char* buffer, int bufferSize)
{
    if (!buffer || bufferSize <= 1) {
        return 2;
    }
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() >= bufferSize) {
        buffer[0] = '\0';
        return 3;
    }
    std::memcpy(buffer, bytes.constData(), static_cast<size_t>(bytes.size()));
    buffer[bytes.size()] = '\0';
    return 0;
}

bool readUnsigned(const QJsonValue& value, quint32& result)
{
    if (value.isDouble()) {
        const auto number = value.toDouble(-1);
        if (number < 0 || number > 0x1FFFFFFF) return false;
        result = static_cast<quint32>(number);
        return true;
    }
    if (!value.isString()) return false;
    auto text = value.toString().trimmed();
    bool ok = false;
    result = text.toUInt(&ok, text.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
    return ok && result <= 0x1FFFFFFF;
}

bool readData(const QJsonValue& value, QByteArray& data, QString& errorMessage)
{
    data.clear();
    if (value.isArray()) {
        for (const auto& item : value.toArray()) {
            if (!item.isDouble() || item.toInt(-1) < 0 || item.toInt(-1) > 255) {
                errorMessage = "data array items must be bytes (0..255)";
                return false;
            }
            data.push_back(static_cast<char>(item.toInt()));
        }
        return true;
    }
    if (!value.isString()) {
        errorMessage = "data must be a byte array or hexadecimal string";
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
            const int byte = part.toInt(&ok, 16);
            if (!ok || byte < 0 || byte > 255) {
                errorMessage = QString("invalid CAN byte: %1").arg(part);
                return false;
            }
            data.push_back(static_cast<char>(byte));
        }
        return true;
    }
    text.remove(' ');
    if ((text.size() % 2) != 0) {
        errorMessage = "hexadecimal data must contain an even number of digits";
        return false;
    }
    data = QByteArray::fromHex(text.toLatin1());
    return !data.isEmpty() || text.isEmpty();
}

QString dataText(const QByteArray& data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

bool frameFromInputs(const QJsonObject& object, CanFrame& frame, QString& errorMessage)
{
    if (!readUnsigned(object.value("id"), frame.id)) {
        errorMessage = "id must be a standard/extended CAN identifier";
        return false;
    }
    if (!readData(object.value("data"), frame.data, errorMessage)) {
        return false;
    }
    frame.extended = object.value("extended").toBool(frame.id > 0x7FF);
    frame.remote = object.value("remote").toBool(false);
    frame.canFd = object.value("canFd").toBool(false);
    frame.bitrateSwitch = object.value("bitrateSwitch").toBool(false);
    const int maximumBytes = frame.canFd ? 64 : 8;
    if (frame.data.size() > maximumBytes) {
        errorMessage = QString("frame data exceeds %1 bytes").arg(maximumBytes);
        return false;
    }
    return true;
}

QJsonObject frameToJson(const CanFrame& frame)
{
    QJsonArray bytes;
    for (const auto byte : frame.data) {
        bytes.push_back(static_cast<int>(static_cast<quint8>(byte)));
    }
    return {
        {"id", QString("0x%1").arg(frame.id, frame.extended ? 8 : 3, 16, QLatin1Char('0')).toUpper()},
        {"idNumeric", static_cast<double>(frame.id)},
        {"data", bytes},
        {"dataHex", dataText(frame.data)},
        {"dlc", frame.data.size()},
        {"extended", frame.extended},
        {"remote", frame.remote},
        {"canFd", frame.canFd},
        {"bitrateSwitch", frame.bitrateSwitch},
        {"timestampUs", static_cast<double>(frame.timestampUs)},
    };
}

QJsonObject receiveResponse(const CanFrame& frame)
{
    const auto outputs = frameToJson(frame);
    const QJsonObject measurement{
        {"name", "CAN_RX_DLC"},
        {"value", frame.data.size()},
        {"unit", "byte"},
        {"rawValue", dataText(frame.data)},
        {"status", "Passed"},
        {"frameId", outputs.value("id")},
    };
    return response("Passed", outputs, measurement);
}

QJsonObject execute(const QJsonObject& request)
{
    const auto function = request.value("function").toString().trimmed();
    const auto inputs = request.value("context").toObject().value("inputs").toObject();

    if (function.compare("open", Qt::CaseInsensitive) == 0 ||
        function.compare("ConnectCAN", Qt::CaseInsensitive) == 0) {
        CanOpenOptions options;
        options.deviceIndex = inputs.value("deviceIndex").toInt(0);
        options.channelIndex = inputs.value("channelIndex").toInt(0);
        options.arbitrationBitrate = inputs.value("bitrate").toInt(500000);
        options.dataBitrate = inputs.value("dataBitrate").toInt(2000000);
        options.canFd = inputs.value("canFd").toBool(false);
        options.listenOnly = inputs.value("listenOnly").toBool(false);
        QString errorMessage;
        if (!g_adapter.open(options, errorMessage)) {
            return response("Error", {}, {}, "CanOpenFailed", errorMessage);
        }
        return response("Passed", {
            {"connected", true},
            {"device", g_adapter.deviceDescription()},
            {"deviceIndex", options.deviceIndex},
            {"channelIndex", options.channelIndex},
            {"bitrate", options.arbitrationBitrate},
            {"canFd", options.canFd},
        });
    }

    if (function.compare("close", Qt::CaseInsensitive) == 0 ||
        function.compare("Disconnect", Qt::CaseInsensitive) == 0) {
        g_adapter.close();
        return response("Passed", {{"connected", false}});
    }

    if (function.compare("status", Qt::CaseInsensitive) == 0) {
        return response("Passed", {
            {"connected", g_adapter.isOpen()},
            {"device", g_adapter.deviceDescription()},
        });
    }

    if (!g_adapter.isOpen()) {
        return response("Error", {}, {}, "CanNotOpen", "Call open before CAN I/O");
    }

    if (function.compare("write", Qt::CaseInsensitive) == 0) {
        CanFrame frame;
        QString errorMessage;
        if (!frameFromInputs(inputs, frame, errorMessage)) {
            return response("Error", {}, {}, "InvalidCanFrame", errorMessage);
        }
        if (!g_adapter.transmit(frame, errorMessage)) {
            return response("Error", {}, {}, "CanTransmitFailed", errorMessage);
        }
        return response("Passed", {{"transmitted", true}, {"frame", frameToJson(frame)}});
    }

    if (function.compare("read", Qt::CaseInsensitive) == 0) {
        quint32 filterId = 0;
        quint32 filterMask = 0;
        if (inputs.contains("filterId") && !readUnsigned(inputs.value("filterId"), filterId)) {
            return response("Error", {}, {}, "InvalidCanFilter", "filterId is invalid");
        }
        if (inputs.contains("filterMask") && !readUnsigned(inputs.value("filterMask"), filterMask)) {
            return response("Error", {}, {}, "InvalidCanFilter", "filterMask is invalid");
        }
        CanFrame frame;
        QString errorMessage;
        const auto status = g_adapter.receive(
            filterId, filterMask, inputs.value("timeoutMs").toInt(1000), frame, errorMessage);
        if (status == CanReceiveStatus::Timeout) {
            return response("Timeout", {}, {}, "CanReceiveTimeout", "No matching CAN frame received");
        }
        if (status == CanReceiveStatus::Error) {
            return response("Error", {}, {}, "CanReceiveFailed", errorMessage);
        }
        return receiveResponse(frame);
    }

    if (function.compare("requestResponse", Qt::CaseInsensitive) == 0) {
        CanFrame transmitFrame;
        QString errorMessage;
        const auto transmitObject = inputs.value("tx").isObject()
            ? inputs.value("tx").toObject()
            : inputs;
        if (!frameFromInputs(transmitObject, transmitFrame, errorMessage)) {
            return response("Error", {}, {}, "InvalidCanFrame", errorMessage);
        }
        if (!g_adapter.transmit(transmitFrame, errorMessage)) {
            return response("Error", {}, {}, "CanTransmitFailed", errorMessage);
        }
        quint32 filterId = transmitFrame.id;
        quint32 filterMask = transmitFrame.extended ? 0x1FFFFFFF : 0x7FF;
        readUnsigned(inputs.value("rxId"), filterId);
        readUnsigned(inputs.value("rxMask"), filterMask);
        CanFrame receivedFrame;
        const auto status = g_adapter.receive(
            filterId, filterMask, inputs.value("timeoutMs").toInt(1000), receivedFrame, errorMessage);
        if (status == CanReceiveStatus::Timeout) {
            return response("Timeout", {}, {}, "CanReceiveTimeout", "No response CAN frame received");
        }
        if (status == CanReceiveStatus::Error) {
            return response("Error", {}, {}, "CanReceiveFailed", errorMessage);
        }
        return receiveResponse(receivedFrame);
    }

    return response("Error", {}, {}, "UnknownFunction", "Use open, close, status, write, read, or requestResponse");
}

} // namespace

extern "C" __declspec(dllexport)
int PicoATE_Execute(const char* requestJsonUtf8,
                    char* responseJsonUtf8,
                    int responseBufferSize)
{
    if (!requestJsonUtf8) {
        return writeResponse(response("Error", {}, {}, "NullRequest", "Request pointer is null"),
                             responseJsonUtf8,
                             responseBufferSize);
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(requestJsonUtf8), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return writeResponse(response("Error", {}, {}, "InvalidJson", parseError.errorString()),
                             responseJsonUtf8,
                             responseBufferSize);
    }
    QMutexLocker lock(&g_mutex);
    return writeResponse(execute(document.object()), responseJsonUtf8, responseBufferSize);
}
