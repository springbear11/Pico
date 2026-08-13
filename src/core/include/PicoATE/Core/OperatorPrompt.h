#pragma once

#include <QHash>
#include <QString>
#include <QVariantMap>

#include <condition_variable>
#include <chrono>
#include <mutex>

namespace PicoATE::Core {

class StopToken;

enum class OperatorPromptMode {
    Confirm,
    Notice,
    Judgment,
    Input
};

enum class OperatorPromptResponse {
    None,
    Shown,
    Confirmed,
    Passed,
    Failed,
    Submitted,
    Cancelled
};

enum class OperatorPromptWaitStatus {
    Pending,
    Accepted,
    Rejected,
    Timeout,
    Cancelled,
    Unavailable
};

class OperatorPromptController final
{
public:
    void setResponderAvailable(bool available);
    bool responderAvailable() const;

    bool registerPrompt(const QString& instanceId);
    bool respond(const QString& instanceId,
                 OperatorPromptResponse response,
                 QVariantMap values = {});
    OperatorPromptWaitStatus takeResponse(
        const QString& instanceId,
        OperatorPromptResponse acceptedResponse,
        OperatorPromptResponse rejectedResponse = OperatorPromptResponse::None,
        QVariantMap* responseValues = nullptr);
    bool cancelPrompt(const QString& instanceId);
    void waitForChange(std::chrono::milliseconds maximumWait);
    OperatorPromptWaitStatus waitForResponse(
        const QString& instanceId,
        OperatorPromptResponse acceptedResponse,
        int timeoutMs,
        const StopToken& stopToken,
        OperatorPromptResponse rejectedResponse = OperatorPromptResponse::None,
        QVariantMap* responseValues = nullptr);
    void cancelAll();

private:
    struct PromptState {
        OperatorPromptResponse response = OperatorPromptResponse::None;
        QVariantMap values;
        bool cancelled = false;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    QHash<QString, PromptState> m_prompts;
    bool m_responderAvailable = false;
};

QString operatorPromptModeName(OperatorPromptMode mode);
OperatorPromptMode operatorPromptModeFromName(const QString& name);
bool normalizeOperatorPromptInput(const QVariantMap& payload,
                                  const QVariantMap& responseValues,
                                  QVariantMap& outputs,
                                  QString& errorMessage);

} // namespace PicoATE::Core
