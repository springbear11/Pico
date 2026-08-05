#pragma once

#include <QHash>
#include <QString>

#include <condition_variable>
#include <mutex>

namespace PicoATE::Core {

class StopToken;

enum class OperatorPromptMode {
    Confirm,
    Notice,
    Judgment
};

enum class OperatorPromptResponse {
    None,
    Shown,
    Confirmed,
    Passed,
    Failed,
    Cancelled
};

enum class OperatorPromptWaitStatus {
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
    bool respond(const QString& instanceId, OperatorPromptResponse response);
    OperatorPromptWaitStatus waitForResponse(
        const QString& instanceId,
        OperatorPromptResponse acceptedResponse,
        int timeoutMs,
        const StopToken& stopToken,
        OperatorPromptResponse rejectedResponse = OperatorPromptResponse::None);
    void cancelAll();

private:
    struct PromptState {
        OperatorPromptResponse response = OperatorPromptResponse::None;
        bool cancelled = false;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    QHash<QString, PromptState> m_prompts;
    bool m_responderAvailable = false;
};

QString operatorPromptModeName(OperatorPromptMode mode);
OperatorPromptMode operatorPromptModeFromName(const QString& name);

} // namespace PicoATE::Core
