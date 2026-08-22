#include "McuAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace PicoATE::Plugins::Mcu {

namespace {

std::mutex g_mutex;
std::unique_ptr<IMcuAdapter> g_adapter;
std::uint8_t g_defaultUnitId = 0;
constexpr std::uint8_t InitializationUnitId = 0x10;

IMcuAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createMcuAdapter();
    }
    if (!g_adapter) {
        throw std::runtime_error("MCU adapter factory returned null");
    }
    return *g_adapter;
}

std::string normalized(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
        return character == '-' || character == '_' || std::isspace(character);
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parseUnsigned(const Plugin::Json& value,
                   std::uint64_t minimum,
                   std::uint64_t maximum,
                   std::uint64_t& result)
{
    try {
        std::uint64_t parsed = 0;
        if (value.is_number_unsigned()) {
            parsed = value.get<std::uint64_t>();
        } else if (value.is_number_integer()) {
            const auto signedValue = value.get<std::int64_t>();
            if (signedValue < 0) return false;
            parsed = static_cast<std::uint64_t>(signedValue);
        } else if (value.is_string()) {
            const auto text = value.get<std::string>();
            std::size_t consumed = 0;
            parsed = std::stoull(text, &consumed, 0);
            if (consumed != text.size()) return false;
        } else {
            return false;
        }
        if (parsed < minimum || parsed > maximum) return false;
        result = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool unsignedInput(const Plugin::Json& input,
                   const char* key,
                   std::uint64_t fallback,
                   std::uint64_t minimum,
                   std::uint64_t maximum,
                   std::uint64_t& result)
{
    const auto value = input.find(key);
    if (value == input.end()) {
        result = fallback;
        return fallback >= minimum && fallback <= maximum;
    }
    return parseUnsigned(*value, minimum, maximum, result);
}

bool booleanInput(const Plugin::Json& input, const char* key, bool& result)
{
    const auto value = input.find(key);
    if (value == input.end()) return false;
    if (value->is_boolean()) {
        result = value->get<bool>();
        return true;
    }
    if (value->is_number_integer()) {
        const auto number = value->get<int>();
        if (number == 0 || number == 1) {
            result = number != 0;
            return true;
        }
    }
    if (value->is_string()) {
        const auto text = normalized(value->get<std::string>());
        if (text == "true" || text == "on" || text == "1") {
            result = true;
            return true;
        }
        if (text == "false" || text == "off" || text == "0") {
            result = false;
            return true;
        }
    }
    return false;
}

Plugin::Json resultResponse(const Result& result)
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    return Plugin::response(
        "Passed",
        result.value.is_object() ? result.value : Plugin::Json::object());
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& mcu = adapter();

    if (function == "open" || function == "connect") {
        const auto endpoint = Plugin::stringValue(
            input,
            "address",
            Plugin::stringValue(input, "resource"));
        if (endpoint.empty()) {
            return Plugin::errorResponse(
                "McuAddressRequired",
                "Configure the Station serial resource, for example COM3");
        }
        std::uint64_t unitId = 0;
        std::uint64_t timeout = 5000;
        if (!unsignedInput(input, "unitId", 0, 0, 255, unitId)) {
            return Plugin::errorResponse(
                "InvalidMcuUnitId",
                "unitId must be an integer or 0x hexadecimal value from 0 to 255");
        }
        if (!unsignedInput(input, "ioTimeoutMs", 5000, 100, 60000, timeout)) {
            return Plugin::errorResponse(
                "InvalidMcuTimeout",
                "ioTimeoutMs must be from 100 to 60000");
        }
        PicoATE_Log("MCU_SWITCHBOARD_OPEN endpoint={} unitId=0x{:02X} timeoutMs={}",
                    endpoint,
                    static_cast<unsigned int>(unitId),
                    timeout);
        auto result = mcu.open(endpoint,
                               static_cast<std::uint8_t>(unitId),
                               static_cast<int>(timeout));
        if (!result.success) {
            return resultResponse(result);
        }
        PicoATE_Log("MCU_SWITCHBOARD_INITIALIZE unitId=0x{:02X} source=open",
                    static_cast<unsigned int>(InitializationUnitId));
        const auto initialization = mcu.initializeAllIo(InitializationUnitId);
        if (!initialization.success) {
            mcu.close();
            return resultResponse(initialization);
        }
        g_defaultUnitId = static_cast<std::uint8_t>(unitId);
        if (!result.value.is_object()) result.value = Plugin::Json::object();
        result.value["initialized"] = true;
        result.value["initializationUnitId"] = InitializationUnitId;
        result.value["responseVerified"] = initialization.value.value(
            "responseVerified", false);
        return resultResponse(result);
    }

    if (function == "close" || function == "disconnect") {
        mcu.close();
        PicoATE_Log("MCU_SWITCHBOARD_CLOSE");
        return Plugin::response("Passed", {{"connected", false}});
    }

    if (function == "health" || function == "status" ||
        function == "connectionstatus") {
        return Plugin::response("Passed", {
            {"connected", mcu.isOpen()},
            {"healthy", mcu.isOpen()},
        });
    }

    if (!mcu.isOpen()) {
        return Plugin::errorResponse(
            "McuNotConnected",
            "Call open before switch-board I/O");
    }

    std::uint64_t unitId = g_defaultUnitId;
    if (!unsignedInput(input, "unitId", g_defaultUnitId, 0, 255, unitId)) {
        return Plugin::errorResponse(
            "InvalidMcuUnitId",
            "unitId must be an integer or 0x hexadecimal value from 0 to 255");
    }

    if (function == "readallio") {
        PicoATE_Log("MCU_SWITCHBOARD_READ_ALL unitId=0x{:02X}",
                    static_cast<unsigned int>(unitId));
        return resultResponse(mcu.readAllIo(static_cast<std::uint8_t>(unitId)));
    }

    if (function == "readiobank") {
        std::uint64_t bank = 0;
        if (!unsignedInput(input, "bank", 0, 1, 7, bank)) {
            return Plugin::errorResponse("InvalidMcuBank", "bank must be from 1 to 7");
        }
        PicoATE_Log("MCU_SWITCHBOARD_READ_BANK unitId=0x{:02X} bank={}",
                    static_cast<unsigned int>(unitId),
                    bank);
        return resultResponse(mcu.readIoBank(static_cast<std::uint8_t>(unitId),
                                             static_cast<std::uint8_t>(bank)));
    }

    if (function == "writeiobank") {
        std::uint64_t bank = 0;
        std::uint64_t value = 0;
        if (!unsignedInput(input, "bank", 0, 1, 7, bank)) {
            return Plugin::errorResponse("InvalidMcuBank", "bank must be from 1 to 7");
        }
        if (!unsignedInput(input, "value", 0, 0, 0xFFFF, value)) {
            return Plugin::errorResponse(
                "InvalidMcuBankValue",
                "value must be an integer or 0x hexadecimal value from 0 to 65535");
        }
        PicoATE_Log("MCU_SWITCHBOARD_WRITE_BANK unitId=0x{:02X} bank={} value=0x{:04X}",
                    static_cast<unsigned int>(unitId),
                    bank,
                    static_cast<unsigned int>(value));
        return resultResponse(mcu.writeIoBank(static_cast<std::uint8_t>(unitId),
                                              static_cast<std::uint8_t>(bank),
                                              static_cast<std::uint16_t>(value)));
    }

    if (function == "setio") {
        std::uint64_t channel = 0;
        bool state = false;
        if (!unsignedInput(input, "channel", 0, 1, 112, channel)) {
            return Plugin::errorResponse(
                "InvalidMcuChannel",
                "channel must be from 1 to 112");
        }
        if (!booleanInput(input, "state", state)) {
            return Plugin::errorResponse(
                "InvalidMcuState",
                "state must be On/Off or true/false");
        }
        PicoATE_Log("MCU_SWITCHBOARD_SET_IO unitId=0x{:02X} channel={} state={}",
                    static_cast<unsigned int>(unitId),
                    channel,
                    state ? "ON" : "OFF");
        return resultResponse(mcu.setIo(static_cast<std::uint8_t>(unitId),
                                        static_cast<std::uint16_t>(channel),
                                        state));
    }

    if (function == "initializeallio" || function == "initialize") {
        std::uint64_t initializationUnitId = InitializationUnitId;
        if (!unsignedInput(input,
                           "initializationUnitId",
                           InitializationUnitId,
                           0,
                           255,
                           initializationUnitId)) {
            return Plugin::errorResponse(
                "InvalidMcuInitializationUnitId",
                "initializationUnitId must be from 0 to 255");
        }
        PicoATE_Log("MCU_SWITCHBOARD_INITIALIZE unitId=0x{:02X}",
                    static_cast<unsigned int>(initializationUnitId));
        return resultResponse(mcu.initializeAllIo(
            static_cast<std::uint8_t>(initializationUnitId)));
    }

    return Plugin::errorResponse(
        "UnknownFunction",
        "Use open, health, readAllIo, readIoBank, writeIoBank, setIo, initializeAllIo, or close");
}

} // namespace

} // namespace PicoATE::Plugins::Mcu

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::Mcu::g_mutex);
    return PicoATE::Plugin::executeJson(requestJsonUtf8,
                                        responseJsonUtf8,
                                        responseBufferSize,
                                        PicoATE::Plugins::Mcu::execute);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Describe(
    char* descriptionJsonUtf8,
    int descriptionBufferSize)
{
    return PicoATE::Plugin::writeDescription(
        PicoATE::Plugins::Mcu::pluginDescription(),
        descriptionJsonUtf8,
        descriptionBufferSize);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_GetAbiVersion()
{
    return PicoATE::Plugin::AbiVersion;
}
