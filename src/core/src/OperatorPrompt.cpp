#include "PicoATE/Core/OperatorPrompt.h"

#include "PicoATE/Core/StopToken.h"

#include <cmath>
#include <chrono>
#include <utility>

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
                                       OperatorPromptResponse response,
                                       QVariantMap values)
{
    {
        std::lock_guard lock(m_mutex);
        auto it = m_prompts.find(instanceId);
        if (it == m_prompts.end() || it->cancelled ||
            it->response != OperatorPromptResponse::None ||
            response == OperatorPromptResponse::None) {
            return false;
        }
        it->response = response;
        it->values = std::move(values);
    }
    m_changed.notify_all();
    return true;
}

OperatorPromptWaitStatus OperatorPromptController::takeResponse(
    const QString& instanceId,
    OperatorPromptResponse acceptedResponse,
    OperatorPromptResponse rejectedResponse,
    QVariantMap* responseValues)
{
    std::lock_guard lock(m_mutex);
    auto it = m_prompts.find(instanceId);
    if (it == m_prompts.end()) {
        return OperatorPromptWaitStatus::Cancelled;
    }
    if (!m_responderAvailable) {
        m_prompts.erase(it);
        return OperatorPromptWaitStatus::Unavailable;
    }
    if (it->cancelled || it->response == OperatorPromptResponse::Cancelled) {
        m_prompts.erase(it);
        return OperatorPromptWaitStatus::Cancelled;
    }
    if (it->response == acceptedResponse) {
        if (responseValues) {
            *responseValues = it->values;
        }
        m_prompts.erase(it);
        return OperatorPromptWaitStatus::Accepted;
    }
    if (rejectedResponse != OperatorPromptResponse::None &&
        it->response == rejectedResponse) {
        if (responseValues) {
            *responseValues = it->values;
        }
        m_prompts.erase(it);
        return OperatorPromptWaitStatus::Rejected;
    }
    return OperatorPromptWaitStatus::Pending;
}

bool OperatorPromptController::cancelPrompt(const QString& instanceId)
{
    bool removed = false;
    {
        std::lock_guard lock(m_mutex);
        removed = m_prompts.remove(instanceId);
    }
    if (removed) {
        m_changed.notify_all();
    }
    return removed;
}

void OperatorPromptController::waitForChange(std::chrono::milliseconds maximumWait)
{
    if (maximumWait <= std::chrono::milliseconds::zero()) {
        return;
    }
    std::unique_lock lock(m_mutex);
    m_changed.wait_for(lock, maximumWait);
}

OperatorPromptWaitStatus OperatorPromptController::waitForResponse(
    const QString& instanceId,
    OperatorPromptResponse acceptedResponse,
    int timeoutMs,
    const StopToken& stopToken,
    OperatorPromptResponse rejectedResponse,
    QVariantMap* responseValues)
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
            if (responseValues) {
                *responseValues = it->values;
            }
            m_prompts.erase(it);
            return OperatorPromptWaitStatus::Accepted;
        }
        if (rejectedResponse != OperatorPromptResponse::None &&
            it->response == rejectedResponse) {
            if (responseValues) {
                *responseValues = it->values;
            }
            m_prompts.erase(it);
            return OperatorPromptWaitStatus::Rejected;
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
    switch (mode) {
    case OperatorPromptMode::Confirm:
        return QStringLiteral("confirm");
    case OperatorPromptMode::Notice:
        return QStringLiteral("notice");
    case OperatorPromptMode::Judgment:
        return QStringLiteral("judgment");
    case OperatorPromptMode::Input:
        return QStringLiteral("input");
    }
    return QStringLiteral("confirm");
}

OperatorPromptMode operatorPromptModeFromName(const QString& name)
{
    const auto normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("notice") ||
        normalized == QStringLiteral("continue")) {
        return OperatorPromptMode::Notice;
    }
    if (normalized == QStringLiteral("judgment") ||
        normalized == QStringLiteral("passfail") ||
        normalized == QStringLiteral("operatorcheck")) {
        return OperatorPromptMode::Judgment;
    }
    if (normalized == QStringLiteral("input") ||
        normalized == QStringLiteral("entry") ||
        normalized == QStringLiteral("valueinput")) {
        return OperatorPromptMode::Input;
    }
    return OperatorPromptMode::Confirm;
}

bool normalizeOperatorPromptInput(const QVariantMap& payload,
                                  const QVariantMap& responseValues,
                                  QVariantMap& outputs,
                                  QString& errorMessage)
{
    const auto inputType = payload.value("inputType", "text")
                               .toString()
                               .trimmed()
                               .toLower();
    QString text = responseValues.value("text").toString();
    if (!responseValues.contains("text") && responseValues.contains("value")) {
        text = responseValues.value("value").toString();
    }

    if (text.trimmed().isEmpty()) {
        errorMessage = QStringLiteral("Operator input must not be empty");
        return false;
    }

    QVariant value = text;
    if (inputType == QStringLiteral("integer")) {
        bool ok = false;
        const auto parsed = text.trimmed().toLongLong(&ok, 10);
        if (!ok) {
            errorMessage = QStringLiteral("Operator input is not a valid integer");
            return false;
        }
        value = parsed;
    } else if (inputType == QStringLiteral("number")) {
        bool ok = false;
        const auto parsed = text.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(parsed)) {
            errorMessage = QStringLiteral("Operator input is not a valid number");
            return false;
        }
        value = parsed;
    }

    outputs.insert("value", value);
    outputs.insert("text", text);
    outputs.insert("inputType", inputType);
    outputs.insert("response", QStringLiteral("submitted"));
    return true;
}

} // namespace PicoATE::Core
