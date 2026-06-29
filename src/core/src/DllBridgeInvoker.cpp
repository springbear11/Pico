#include "PicoATE/Core/DllBridgeInvoker.h"

#include "PicoATE/Core/ModuleTransportJson.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <memory>
#include <utility>

namespace PicoATE::Core {

namespace {

using PicoATEExecuteFn = int (*)(const char* requestJsonUtf8,
                                 char* responseJsonUtf8,
                                 int responseBufferSize);

struct BridgeCallState {
    QMutex mutex;
    QWaitCondition finishedCondition;
    bool finished = false;
    ModuleTransportStatus status = ModuleTransportStatus::TransportError;
    ModuleTransportResponse response;
};

void setError(ModuleTransportResponse& response,
              QString code,
              QString message)
{
    response.outcome = ModuleOutcome::Error;
    response.errorCode = std::move(code);
    response.errorMessage = std::move(message);
}

ModuleTransportStatus invokeDllFunction(const QString& libraryPath,
                                        const QString& symbolName,
                                        int responseBufferSize,
                                        const ModuleTransportRequest& request,
                                        ModuleTransportResponse& response)
{
    QLibrary library(libraryPath);
    // NativeHost may serve multiple steps through PersistentQProcessTransport.
    // Keep module static state (for example vendor device handles) alive until
    // the host process exits instead of unloading the DLL after every call.
    library.setLoadHints(QLibrary::PreventUnloadHint);
    if (!library.load()) {
        setError(response, "DllLoadFailed", library.errorString());
        return ModuleTransportStatus::TransportError;
    }

    const auto function = reinterpret_cast<PicoATEExecuteFn>(library.resolve(symbolName.toUtf8().constData()));
    if (!function) {
        setError(response,
                 "DllSymbolNotFound",
                 QString("Failed to resolve symbol: %1").arg(symbolName));
        return ModuleTransportStatus::TransportError;
    }

    const auto requestBytes = QJsonDocument(moduleTransportRequestToJson(request))
                                  .toJson(QJsonDocument::Compact);
    QByteArray responseBuffer(responseBufferSize > 0 ? responseBufferSize : 65536, '\0');

    const int statusCode = function(requestBytes.constData(),
                                    responseBuffer.data(),
                                    responseBuffer.size());
    if (statusCode != 0) {
        setError(response,
                 "DllExecuteFailed",
                 QString("DLL returned status code %1").arg(statusCode));
        return ModuleTransportStatus::TransportError;
    }

    const QByteArray responseBytes(responseBuffer.constData());
    if (responseBytes.trimmed().isEmpty()) {
        setError(response,
                 "DllEmptyResponse",
                 "DLL returned an empty JSON response");
        return ModuleTransportStatus::TransportError;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(response,
                 "DllInvalidJsonResponse",
                 parseError.errorString());
        return ModuleTransportStatus::TransportError;
    }

    response = moduleTransportResponseFromJson(document.object());
    return ModuleTransportStatus::Ok;
}

} // namespace

DllBridgeInvoker::DllBridgeInvoker(QString libraryPath,
                                   QString symbolName,
                                   int responseBufferSize)
    : m_libraryPath(std::move(libraryPath))
    , m_symbolName(std::move(symbolName))
    , m_responseBufferSize(responseBufferSize)
{
}

ModuleTransportStatus DllBridgeInvoker::call(const ModuleTransportRequest& request,
                                             ModuleTransportResponse& response,
                                             int timeoutMs)
{
    const int effectiveTimeoutMs = timeoutMs > 0 ? timeoutMs : 30000;
    auto state = std::make_shared<BridgeCallState>();

    auto* thread = QThread::create([state,
                                    libraryPath = m_libraryPath,
                                    symbolName = m_symbolName,
                                    responseBufferSize = m_responseBufferSize,
                                    request]() {
        ModuleTransportResponse localResponse;
        const auto status = invokeDllFunction(libraryPath,
                                              symbolName,
                                              responseBufferSize,
                                              request,
                                              localResponse);

        QMutexLocker locker(&state->mutex);
        state->status = status;
        state->response = localResponse;
        state->finished = true;
        state->finishedCondition.wakeAll();
    });

    thread->start();

    ModuleTransportStatus status = ModuleTransportStatus::TransportError;
    {
        QMutexLocker locker(&state->mutex);
        if (!state->finished) {
            state->finishedCondition.wait(&state->mutex, effectiveTimeoutMs);
        }

        if (state->finished) {
            status = state->status;
            response = state->response;
        } else {
            response.outcome = ModuleOutcome::Timeout;
            response.errorCode = "DllExecuteTimeout";
            response.errorMessage =
                "DLL call timed out. The in-process worker thread cannot be safely terminated.";
            status = ModuleTransportStatus::Timeout;
        }
    }

    if (status == ModuleTransportStatus::Timeout) {
        thread->requestInterruption();
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        return status;
    }

    thread->wait();
    delete thread;
    return status;
}

QString DllBridgeInvoker::libraryPath() const
{
    return m_libraryPath;
}

QString DllBridgeInvoker::symbolName() const
{
    return m_symbolName;
}

int DllBridgeInvoker::responseBufferSize() const
{
    return m_responseBufferSize;
}

} // namespace PicoATE::Core
