#include "PicoATE/Core/OperatorPrompt.h"

#include "PicoATE/Core/StopToken.h"

#include <chrono>

namespace PicoATE::Core {

void OperatorPromptController::setResponderAvailable(bool available)
{
    {
        std::lock_guard lock(m_mutex);
        m_responderAvailable = available;
        if (!available) {
            for (auto it = m_prompts.begin(); it != m_prompts.end(); ++it) {
                it->cancelled = true;
            }
        }
    }
    m_changed.notify_all();
}

bool OperatorPromptController::responderAvailable() const
{
    std::lock_guard lock(m_mutex);
    return m_responderAvailable;
}

bool OperatorPromptController::registerPrompt(const QString& instanceId)
{
    std::lock_guard lock(m_mutex);
    if (!m_responderAvailable || instanceId.trimmed().isEmpty() ||
        m_prompts.contains(instanceId)) {
        return false;
    }
    m_prompts.insert(instanceId, {});
    return true;
}

bool OperatorPromptController::respond(const QString& instanceId,
                                       OperatorPromptResponse response)
{
    {
        std::lock_guard lock(m_mutex);
        auto it = m_prompts.find(instanceId);
        if (it == m_prompts.end() || it->cancelled ||
            response == OperatorPromptResponse::None) {
            return false;
        }
        it->response = response;
    }
    m_changed.notify_all();
    return true;
}

OperatorPromptWaitStatus OperatorPromptController::waitForResponse(
    const QString& instanceId,
    OperatorPromptResponse acceptedResponse,
    int timeoutMs,
    const StopToken& stopToken)
{
    std::unique_lock lock(m_mutex);
    if (!m_responderAvailable) {
        m_prompts.remove(instanceId);
        return OperatorPromptWaitStatus::Unavailable;
    }

    const auto started = std::chrono::steady_clock::now();
    while (true) {
        auto it = m_prompts.find(instanceId);
        if (it == m_prompts.end()) {
            return OperatorPromptWaitStatus::Cancelled;
        }
        if (it->cancelled || stopToken.isStopRequested() || !m_responderAvailable) {
            m_prompts.erase(it);
            return OperatorPromptWaitStatus::Cancelled;
        }
        if (it->response == acceptedResponse) {
            m_prompts.erase(it);
            return OperatorPromptWaitStatus::Accepted;
        }
        if (timeoutMs > 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count() >= timeoutMs) {
            m_prompts.erase(it);
            return OperatorPromptWaitStatus::Timeout;
        }
        m_changed.wait_for(lock, std::chrono::milliseconds(20));
    }
}

void OperatorPromptController::cancelAll()
{
    {
        std::lock_guard lock(m_mutex);
        for (auto it = m_prompts.begin(); it != m_prompts.end(); ++it) {
            it->cancelled = true;
        }
    }
    m_changed.notify_all();
}

QString operatorPromptModeName(OperatorPromptMode mode)
{
    return mode == OperatorPromptMode::Notice ? QStringLiteral("notice")
                                               : QStringLiteral("confirm");
}

OperatorPromptMode operatorPromptModeFromName(const QString& name)
{
    return name.trimmed().compare(QStringLiteral("notice"), Qt::CaseInsensitive) == 0
        ? OperatorPromptMode::Notice
        : OperatorPromptMode::Confirm;
}

} // namespace PicoATE::Core
