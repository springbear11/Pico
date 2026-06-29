#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextStream>
#include <QThread>

namespace {

struct DeviceState {
    bool connected = false;
    bool healthy = true;
    QString address;
    QString deviceType;
    QString lastMode;
    int openCount = 0;
    int readCount = 0;
    int reconnectCount = 0;
    int configureCount = 0;
    double range = 0.0;
    double nplc = 10.0;
};

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

QJsonObject passedResponse(const QJsonObject& outputs, const QJsonObject& measurements = {})
{
    QJsonObject response;
    response.insert("outcome", "Passed");
    response.insert("outputs", outputs);
    response.insert("measurements", measurements);
    response.insert("errorCode", "");
    response.insert("errorMessage", "");
    return response;
}

void writeResponse(const QJsonObject& response)
{
    QTextStream out(stdout);
    out << QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)) << Qt::endl;
}

QString deviceIdFromInputs(const QJsonObject& inputs)
{
    return inputs.value("deviceId").toString("DMM1").trimmed();
}

QJsonObject stateOutputs(const QString& deviceId, const DeviceState& state)
{
    QJsonObject outputs;
    outputs.insert("deviceId", deviceId);
    outputs.insert("deviceType", state.deviceType);
    outputs.insert("address", state.address);
    outputs.insert("connected", state.connected);
    outputs.insert("healthy", state.healthy);
    outputs.insert("openCount", state.openCount);
    outputs.insert("readCount", state.readCount);
    outputs.insert("reconnectCount", state.reconnectCount);
    outputs.insert("configureCount", state.configureCount);
    outputs.insert("lastMode", state.lastMode);
    outputs.insert("range", state.range);
    outputs.insert("nplc", state.nplc);
    outputs.insert("sessionId", QString("%1:%2").arg(deviceId).arg(state.openCount));
    return outputs;
}

QJsonObject openDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    if (deviceId.isEmpty()) {
        return errorResponse("DeviceIdMissing", "deviceId is required");
    }

    auto& state = devices[deviceId];
    if (!state.connected) {
        state.connected = true;
        state.openCount += 1;
    }
    state.address = inputs.value("address").toString(state.address);
    state.deviceType = inputs.value("deviceType").toString("DMM");
    state.healthy = true;
    return passedResponse(stateOutputs(deviceId, state));
}

QJsonObject requireOpen(const QString& deviceId, QHash<QString, DeviceState>& devices, DeviceState** state)
{
    auto it = devices.find(deviceId);
    if (it == devices.end() || !it->connected) {
        *state = nullptr;
        return errorResponse("DeviceNotOpen", QString("Device is not open: %1").arg(deviceId));
    }

    *state = &it.value();
    return {};
}

QJsonObject readDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    DeviceState* state = nullptr;
    const auto error = requireOpen(deviceId, devices, &state);
    if (!state) {
        return error;
    }

    state->readCount += 1;
    const auto value = inputs.value("value").toDouble(10.0 + state->readCount);

    auto outputs = stateOutputs(deviceId, *state);
    outputs.insert("value", value);

    QJsonObject measurement;
    measurement.insert("name", inputs.value("measurementName").toString("FAKE_INSTRUMENT_READ"));
    measurement.insert("value", value);
    measurement.insert("unit", inputs.value("unit").toString("V"));
    measurement.insert("status", "Passed");
    return passedResponse(outputs, measurement);
}

QJsonObject configureDcvDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    DeviceState* state = nullptr;
    const auto error = requireOpen(deviceId, devices, &state);
    if (!state) {
        return error;
    }

    state->lastMode = "DCV";
    state->range = inputs.value("range").toDouble(state->range);
    state->nplc = inputs.value("nplc").toDouble(state->nplc);
    state->configureCount += 1;

    auto outputs = stateOutputs(deviceId, *state);
    outputs.insert("mode", state->lastMode);
    return passedResponse(outputs);
}

QJsonObject identityDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    DeviceState* state = nullptr;
    const auto error = requireOpen(deviceId, devices, &state);
    if (!state) {
        return error;
    }

    auto outputs = stateOutputs(deviceId, *state);
    outputs.insert("identity", QString("PicoATE Fake,%1,FAKE-%2,1.0")
                                   .arg(state->deviceType.isEmpty() ? QString("DMM") : state->deviceType,
                                        deviceId));
    return passedResponse(outputs);
}

QJsonObject readFrameDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    DeviceState* state = nullptr;
    const auto error = requireOpen(deviceId, devices, &state);
    if (!state) {
        return error;
    }

    state->readCount += 1;
    const auto frameId = inputs.value("frameId").toString("0x123");
    const auto data = inputs.value("data").toString("01 02 03 04 05 06 07 08");

    auto outputs = stateOutputs(deviceId, *state);
    outputs.insert("frameId", frameId);
    outputs.insert("data", data);

    QJsonObject measurement;
    measurement.insert("name", inputs.value("measurementName").toString("CAN_FRAME_READ"));
    measurement.insert("value", state->readCount);
    measurement.insert("unit", "frame");
    measurement.insert("status", "Passed");
    return passedResponse(outputs, measurement);
}

QJsonObject closeDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    auto it = devices.find(deviceId);
    if (it == devices.end()) {
        return passedResponse(QJsonObject{
            {"deviceId", deviceId},
            {"connected", false},
            {"openCount", 0},
            {"readCount", 0},
        });
    }

    it->connected = false;
    return passedResponse(stateOutputs(deviceId, *it));
}

QJsonObject statusDevice(const QJsonObject& inputs, const QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    const auto it = devices.constFind(deviceId);
    if (it == devices.constEnd()) {
        return passedResponse(QJsonObject{
            {"deviceId", deviceId},
            {"connected", false},
            {"openCount", 0},
            {"readCount", 0},
        });
    }
    return passedResponse(stateOutputs(deviceId, it.value()));
}

QJsonObject reconnectDevice(const QJsonObject& inputs, QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    if (deviceId.isEmpty()) {
        return errorResponse("DeviceIdMissing", "deviceId is required");
    }

    auto& state = devices[deviceId];
    const bool wasConnected = state.connected;
    state.connected = true;
    state.healthy = true;
    state.reconnectCount += 1;
    if (!wasConnected) {
        state.openCount += 1;
    }
    state.address = inputs.value("address").toString(state.address);
    state.deviceType = inputs.value("deviceType").toString(state.deviceType.isEmpty() ? QString("DMM") : state.deviceType);
    return passedResponse(stateOutputs(deviceId, state));
}

QJsonObject healthDevice(const QJsonObject& inputs, const QHash<QString, DeviceState>& devices)
{
    const auto deviceId = deviceIdFromInputs(inputs);
    const auto it = devices.constFind(deviceId);
    if (it == devices.constEnd()) {
        return passedResponse(QJsonObject{
            {"deviceId", deviceId},
            {"connected", false},
            {"healthy", false},
            {"openCount", 0},
            {"readCount", 0},
            {"reconnectCount", 0},
            {"configureCount", 0},
        });
    }
    return passedResponse(stateOutputs(deviceId, it.value()));
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QHash<QString, DeviceState> devices;
    QTextStream in(stdin);
    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(errorResponse("InvalidRequest", parseError.errorString()));
            continue;
        }

        const auto request = document.object();
        const auto context = request.value("context").toObject();
        const auto inputs = context.value("inputs").toObject();
        const auto delayMs = inputs.value("mockDelayMs").toInt(0);
        if (delayMs > 0) {
            QThread::msleep(static_cast<unsigned long>(delayMs));
        }

        const auto function = request.value("function").toString().trimmed().toLower();
        if (function == "shutdown") {
            writeResponse(passedResponse(QJsonObject{{"shutdown", true}}));
            return 0;
        }
        if (function == "open") {
            writeResponse(openDevice(inputs, devices));
        } else if (function == "read") {
            writeResponse(readDevice(inputs, devices));
        } else if (function == "configuredcv") {
            writeResponse(configureDcvDevice(inputs, devices));
        } else if (function == "identity" || function == "getidentity") {
            writeResponse(identityDevice(inputs, devices));
        } else if (function == "readframe" || function == "readcan") {
            writeResponse(readFrameDevice(inputs, devices));
        } else if (function == "close") {
            writeResponse(closeDevice(inputs, devices));
        } else if (function == "status") {
            writeResponse(statusDevice(inputs, devices));
        } else if (function == "health") {
            writeResponse(healthDevice(inputs, devices));
        } else if (function == "reconnect") {
            writeResponse(reconnectDevice(inputs, devices));
        } else {
            writeResponse(errorResponse("UnsupportedFunction",
                                        QString("Unsupported fake instrument function: %1").arg(function)));
        }
    }

    return 0;
}
