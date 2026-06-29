#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QThread>

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
