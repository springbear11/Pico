#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define PICOATE_PLUGIN_CALL __cdecl
#define PICOATE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define PICOATE_PLUGIN_CALL
#define PICOATE_PLUGIN_EXPORT
#endif

namespace PicoATE::Plugin {

using Json = nlohmann::json;

inline Json response(std::string_view outcome,
                     Json outputs = Json::object(),
                     Json measurements = Json::object(),
                     std::string_view errorCode = {},
                     std::string_view errorMessage = {})
{
    return {
        {"outcome", outcome},
        {"outputs", std::move(outputs)},
        {"measurements", std::move(measurements)},
        {"errorCode", errorCode},
        {"errorMessage", errorMessage},
    };
}

inline Json errorResponse(std::string_view code, std::string_view message)
{
    return response("Error", Json::object(), Json::object(), code, message);
}

inline const Json& inputs(const Json& request)
{
    static const Json empty = Json::object();
    const auto context = request.find("context");
    if (context == request.end() || !context->is_object()) {
        return empty;
    }
    const auto value = context->find("inputs");
    return value != context->end() && value->is_object() ? *value : empty;
}

inline int writeResponse(const Json& value, char* buffer, int bufferSize) noexcept
{
    if (!buffer || bufferSize <= 1) {
        return 2;
    }
    try {
        const auto text = value.dump();
        if (text.size() >= static_cast<std::size_t>(bufferSize)) {
            buffer[0] = '\0';
            return 3;
        }
        std::memcpy(buffer, text.data(), text.size());
        buffer[text.size()] = '\0';
        return 0;
    } catch (...) {
        buffer[0] = '\0';
        return 4;
    }
}

template<typename Execute>
int executeJson(const char* requestJsonUtf8,
                char* responseJsonUtf8,
                int responseBufferSize,
                Execute&& execute) noexcept
{
    if (!requestJsonUtf8) {
        return writeResponse(errorResponse("NullRequest", "Request pointer is null"),
                             responseJsonUtf8,
                             responseBufferSize);
    }
    try {
        const auto request = Json::parse(requestJsonUtf8);
        if (!request.is_object()) {
            return writeResponse(errorResponse("InvalidJson", "Request root must be an object"),
                                 responseJsonUtf8,
                                 responseBufferSize);
        }
        return writeResponse(execute(request), responseJsonUtf8, responseBufferSize);
    } catch (const std::exception& exception) {
        return writeResponse(errorResponse("InvalidJson", exception.what()),
                             responseJsonUtf8,
                             responseBufferSize);
    } catch (...) {
        return writeResponse(errorResponse("PluginException", "Unhandled plugin exception"),
                             responseJsonUtf8,
                             responseBufferSize);
    }
}

inline std::string stringValue(const Json& object,
                               std::string_view key,
                               std::string fallback = {})
{
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>()
                                                       : std::move(fallback);
}

template<typename Number>
Number numberValue(const Json& object, std::string_view key, Number fallback)
{
    const auto value = object.find(key);
    return value != object.end() && value->is_number() ? value->get<Number>() : fallback;
}

inline bool boolValue(const Json& object, std::string_view key, bool fallback)
{
    const auto value = object.find(key);
    return value != object.end() && value->is_boolean() ? value->get<bool>() : fallback;
}

} // namespace PicoATE::Plugin
