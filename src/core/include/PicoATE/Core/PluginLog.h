#pragma once

#include <atomic>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define PICOATE_PLUGIN_CALL __cdecl
#define PICOATE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define PICOATE_PLUGIN_CALL
#define PICOATE_PLUGIN_EXPORT
#endif

using PicoATELogSink = void(PICOATE_PLUGIN_CALL*)(void* userData, const char* messageUtf8);
using PicoATESetLogSink = void(PICOATE_PLUGIN_CALL*)(PicoATELogSink sink, void* userData);

namespace PicoATE::Plugin {

inline std::atomic<PicoATELogSink> g_logSink{nullptr};
inline std::atomic<void*> g_logUserData{nullptr};

inline void setLogSink(PicoATELogSink sink, void* userData) noexcept
{
    if (!sink) {
        g_logSink.store(nullptr, std::memory_order_release);
        g_logUserData.store(nullptr, std::memory_order_release);
        return;
    }
    g_logUserData.store(userData, std::memory_order_release);
    g_logSink.store(sink, std::memory_order_release);
}

inline bool logEnabled() noexcept
{
    return g_logSink.load(std::memory_order_acquire) != nullptr;
}

inline void emitLog(std::string_view message) noexcept
{
    const auto sink = g_logSink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }

    try {
        const std::string text(message);
        sink(g_logUserData.load(std::memory_order_acquire), text.c_str());
    } catch (...) {
        // Logging must never change business execution or crash the module.
    }
}

} // namespace PicoATE::Plugin

inline void PicoATE_Log(const char* message) noexcept
{
    if (message) {
        PicoATE::Plugin::emitLog(message);
    }
}

inline void PicoATE_Log(std::string_view message) noexcept
{
    PicoATE::Plugin::emitLog(message);
}

template<typename... Args>
    requires(sizeof...(Args) > 0)
inline void PicoATE_Log(std::format_string<Args...> format, Args&&... args) noexcept
{
    if (!PicoATE::Plugin::logEnabled()) {
        return;
    }
    try {
        PicoATE::Plugin::emitLog(
            std::format(format, std::forward<Args>(args)...));
    } catch (...) {
        PicoATE::Plugin::emitLog("PicoATE_Log format error");
    }
}

#define PICOATE_DEFINE_LOG_SINK()                                                   \
    extern "C" PICOATE_PLUGIN_EXPORT void PICOATE_PLUGIN_CALL PicoATE_SetLogSink( \
        PicoATELogSink sink, void* userData)                                       \
    {                                                                               \
        PicoATE::Plugin::setLogSink(sink, userData);                               \
    }

