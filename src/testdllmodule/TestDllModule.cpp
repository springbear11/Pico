#include "PicoATE/Core/PluginLog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

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

} // namespace

PICOATE_DEFINE_LOG_SINK()

extern "C" __declspec(dllexport)
int PicoATE_Describe(char* descriptionJsonUtf8, int descriptionBufferSize)
{
    const QJsonObject description{
        {"name", "PicoATE Test DLL"},
        {"category", "Test"},
        {"functions", QJsonArray{
            QJsonObject{
                {"id", "echo"},
                {"name", "Echo"},
                {"inputs", QJsonArray{
                    QJsonObject{{"key", "value"}, {"name", "Value"},
                                {"type", "string"}, {"required", true}}
                }},
                {"outputs", QJsonArray{
                    QJsonObject{{"key", "value"}, {"name", "Value"},
                                {"type", "string"}}
                }}
            }
        }}
    };
    return writeJsonResponse(description, descriptionJsonUtf8, descriptionBufferSize);
}

extern "C" __declspec(dllexport)
int PicoATE_GetAbiVersion()
{
    return 1;
}

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

    for (const auto& value : inputs.value("logMessages").toArray()) {
        PicoATE_Log(value.toString().toStdString());
    }
    const int logCount = qMax(0, inputs.value("logCount").toInt(0));
    for (int index = 0; index < logCount; ++index) {
        PicoATE_Log("burst log {}", index);
    }

    for (const auto& value : inputs.value("stdoutMessages").toArray()) {
        const auto bytes = value.toString().toUtf8();
        std::fprintf(stdout, "%s\n", bytes.constData());
    }
    for (const auto& value : inputs.value("stderrMessages").toArray()) {
        const auto bytes = value.toString().toUtf8();
        std::fprintf(stderr, "%s\n", bytes.constData());
    }
    const int rawStdoutCount = qMax(0, inputs.value("rawStdoutCount").toInt(0));
    for (int index = 0; index < rawStdoutCount; ++index) {
        std::fprintf(stdout, "vendor stdout burst %d\n", index);
    }
    const int rawNoNewlineCharacters = qMax(
        0, inputs.value("rawNoNewlineCharacters").toInt(0));
    if (rawNoNewlineCharacters > 0) {
        const std::string raw(static_cast<std::size_t>(rawNoNewlineCharacters), 'X');
        std::fwrite(raw.data(), 1, raw.size(), stdout);
    }
    std::fflush(stdout);
    std::fflush(stderr);

    const auto sleepMs = inputs.value("dllSleepMs").toInt(0);
    if (sleepMs > 0) {
        QThread::msleep(static_cast<unsigned long>(sleepMs));
    }

    if (inputs.contains("dllReturnCode")) {
        return inputs.value("dllReturnCode").toInt(1);
    }

    QJsonObject response;
    response.insert("outcome", inputs.value("outcome").toString("Passed"));
    response.insert("outputs", inputs);
    response.insert("measurements", inputs.value("measurements").toObject());
    response.insert("errorCode", inputs.value("errorCode").toString());
    response.insert("errorMessage", inputs.value("errorMessage").toString());
    return writeJsonResponse(response, responseJsonUtf8, responseBufferSize);
}
