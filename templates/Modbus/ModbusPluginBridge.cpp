#include "ModbusAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace PicoATE::Plugins::Modbus {

namespace {

std::mutex g_mutex;
std::unique_ptr<IModbusAdapter> g_adapter;
std::uint8_t g_defaultUnitId = 0;

IModbusAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createModbusAdapter();
    }
    if (!g_adapter) {
        throw std::runtime_error("Modbus adapter factory returned null");
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

Plugin::Json resultResponse(const Result& result, const char* outputName = "value")
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    if (result.value.is_object()) {
        return Plugin::response("Passed", result.value);
    }
    auto outputs = Plugin::Json::object();
    if (!result.value.is_null()) {
        outputs[outputName] = result.value;
    }
    return Plugin::response("Passed", std::move(outputs));
}

bool parseUnsignedValue(const Plugin::Json& value, std::uint64_t& result)
{
    try {
        if (value.is_number_unsigned()) {
            result = value.get<std::uint64_t>();
            return true;
        }
        if (value.is_number_integer()) {
            const auto number = value.get<std::int64_t>();
            if (number < 0) {
                return false;
            }
            result = static_cast<std::uint64_t>(number);
            return true;
        }
        if (!value.is_string()) {
            return false;
        }
        const auto text = value.get<std::string>();
        std::size_t consumed = 0;
        const auto number = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        result = number;
        return true;
    } catch (...) {
        return false;
    }
}

bool unitIdValue(const Plugin::Json& input,
                 std::uint8_t fallback,
                 std::uint8_t& unitId)
{
    auto value = input.find("unitId");
    if (value == input.end()) {
        value = input.find("slaveId");
    }
    if (value == input.end()) {
        unitId = fallback;
        return true;
    }
    std::uint64_t number = 0;
    if (!parseUnsignedValue(*value, number) || number > 0xFF) return false;
    unitId = static_cast<std::uint8_t>(number);
    return true;
}

bool unsignedValue(const Plugin::Json& input,
                   const char* key,
                   std::uint16_t minimum,
                   std::uint16_t maximum,
                   std::uint16_t& result)
{
    const auto value = input.find(key);
    if (value == input.end()) {
        return false;
    }
    std::uint64_t number = 0;
    if (!parseUnsignedValue(*value, number) || number < minimum || number > maximum) {
        return false;
    }
    result = static_cast<std::uint16_t>(number);
    return true;
}

bool registerValues(const Plugin::Json& input, std::vector<std::uint16_t>& values)
{
    values.clear();
    const auto source = input.find("values");
    if (source == input.end()) {
        return false;
    }

    Plugin::Json parsedValues;
    if (source->is_array()) {
        parsedValues = *source;
    } else if (source->is_string()) {
        const auto text = source->get<std::string>();
        try {
            parsedValues = Plugin::Json::parse(text);
        } catch (...) {
            parsedValues = Plugin::Json::array();
            auto valuesText = text;
            if (valuesText.size() >= 2 && valuesText.front() == '[' && valuesText.back() == ']') {
                valuesText = valuesText.substr(1, valuesText.size() - 2);
            }
            std::size_t begin = 0;
            while (begin < valuesText.size()) {
                const auto end = valuesText.find(',', begin);
                auto token = valuesText.substr(begin, end == std::string::npos ? end : end - begin);
                const auto first = token.find_first_not_of(" \t\r\n");
                const auto last = token.find_last_not_of(" \t\r\n");
                if (first == std::string::npos) {
                    return false;
                }
                token = token.substr(first, last - first + 1);
                std::uint64_t number = 0;
                if (!parseUnsignedValue(Plugin::Json(token), number) || number > 0xFFFF) {
                    return false;
                }
                parsedValues.push_back(number);
                if (end == std::string::npos) break;
                begin = end + 1;
            }
        }
    } else {
        return false;
    }
    if (!parsedValues.is_array() || parsedValues.empty() || parsedValues.size() > 123) {
        return false;
    }

    values.reserve(parsedValues.size());
    for (const auto& value : parsedValues) {
        std::uint64_t number = 0;
        if (!parseUnsignedValue(value, number) || number > 0xFFFF) {
            return false;
        }
        values.push_back(static_cast<std::uint16_t>(number));
    }
    return true;
}

bool coilValues(const Plugin::Json& input, std::vector<bool>& values)
{
    values.clear();
    const auto source = input.find("values");
    if (source == input.end()) return false;

    Plugin::Json parsedValues;
    if (source->is_array()) {
        parsedValues = *source;
    } else if (source->is_string()) {
        try {
            parsedValues = Plugin::Json::parse(source->get<std::string>());
        } catch (...) {
            return false;
        }
    } else {
        return false;
    }
    if (!parsedValues.is_array() || parsedValues.empty() || parsedValues.size() > 1968) {
        return false;
    }
    values.reserve(parsedValues.size());
    for (const auto& value : parsedValues) {
        if (!value.is_boolean()) return false;
        values.push_back(value.get<bool>());
    }
    return true;
}

bool textRegisterValues(const Plugin::Json& input,
                        const std::string& encoding,
                        std::vector<std::uint16_t>& values,
                        std::string& text,
                        std::size_t& byteCount,
                        std::size_t& registerCount)
{
    values.clear();
    text.clear();
    byteCount = 0;
    registerCount = 0;
    const auto textValue = input.find("text");
    if (textValue == input.end() || !textValue->is_string()) return false;
    text = textValue->get<std::string>();
    if (encoding != "ascii" && encoding != "utf8") return false;
    if (encoding == "ascii" && std::any_of(text.begin(), text.end(), [](unsigned char value) {
            return value > 0x7F;
        })) return false;
    std::uint16_t configuredRegisterCount = 0;
    if (!unsignedValue(input, "registerCount", 1, 123, configuredRegisterCount)) return false;
    registerCount = configuredRegisterCount;
    std::uint16_t padByte = 0;
    if (input.contains("padByte") && !unsignedValue(input, "padByte", 0, 0xFF, padByte)) return false;
    const auto byteOrderValue = input.find("byteOrder");
    const auto byteOrder = byteOrderValue == input.end() || !byteOrderValue->is_string()
        ? std::string("highbytefirst")
        : normalized(byteOrderValue->get<std::string>());
    const bool highByteFirst = byteOrder == "highbytefirst" || byteOrder == "big" || byteOrder == "msbfirst";
    const bool lowByteFirst = byteOrder == "lowbytefirst" || byteOrder == "little" || byteOrder == "lsbfirst";
    if ((!highByteFirst && !lowByteFirst) || text.size() > registerCount * 2) return false;
    byteCount = text.size();
    values.reserve(registerCount);
    for (std::size_t index = 0; index < registerCount; ++index) {
        const auto first = index * 2 < text.size() ? static_cast<std::uint8_t>(text[index * 2]) : static_cast<std::uint8_t>(padByte);
        const auto second = index * 2 + 1 < text.size() ? static_cast<std::uint8_t>(text[index * 2 + 1]) : static_cast<std::uint8_t>(padByte);
        values.push_back(highByteFirst ? static_cast<std::uint16_t>((first << 8) | second) : static_cast<std::uint16_t>((second << 8) | first));
    }
    return true;
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& modbus = adapter();

    if (function == "open" || function == "connect") {
        const auto endpoint = Plugin::stringValue(
            input,
            "address",
            Plugin::stringValue(input, "resource"));
        if (endpoint.empty()) {
            return Plugin::errorResponse("ModbusAddressRequired", "Configure the Station resource/address");
        }
        std::uint8_t unitId = 0;
        if (!unitIdValue(input, 0, unitId)) {
            return Plugin::errorResponse("InvalidUnitId", "unitId must be an integer or 0x hexadecimal value from 0 to 255");
        }
        PicoATE_Log("MODBUS_OPEN endpoint={} defaultUnitId=0x{:02X} ({})", endpoint, static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId));
        const auto result = modbus.connect(endpoint, unitId, input);
        if (result.success) {
            g_defaultUnitId = unitId;
        }
        return resultResponse(result, "connected");
    }
    if (function == "close" || function == "disconnect") {
        modbus.disconnect();
        PicoATE_Log("MODBUS_CLOSE");
        return Plugin::response("Passed", {{"connected", false}});
    }
    if (function == "status" || function == "health" || function == "connectionstatus") {
        return Plugin::response("Passed", {
            {"connected", modbus.isConnected()},
            {"healthy", modbus.isConnected()},
        });
    }
    if (!modbus.isConnected()) {
        return Plugin::errorResponse("ModbusNotConnected", "Call open before Modbus I/O");
    }

    std::uint8_t unitId = g_defaultUnitId;
    if (!unitIdValue(input, g_defaultUnitId, unitId)) {
        return Plugin::errorResponse("InvalidUnitId", "unitId must be an integer or 0x hexadecimal value from 0 to 255");
    }
    std::uint16_t address = 0;
    if (!unsignedValue(input, "address", 0, 0xFFFF, address)) {
        return Plugin::errorResponse("InvalidModbusAddress", "address must be an integer or 0x hexadecimal value from 0 to 65535");
    }

    if (function == "readcoils" || function == "readdiscreteinputs") {
        std::uint16_t count = 0;
        if (!unsignedValue(input, "count", 1, 2000, count)) {
            return Plugin::errorResponse("InvalidModbusCount", "count must be an integer from 1 to 2000");
        }
        if (function == "readcoils") {
            PicoATE_Log("MODBUS_READ_COILS unitId=0x{:02X} ({}) address=0x{:04X} ({}) count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), count);
            return resultResponse(modbus.readCoils(unitId, address, count));
        }
        PicoATE_Log("MODBUS_READ_DISCRETE unitId=0x{:02X} ({}) address=0x{:04X} ({}) count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), count);
        return resultResponse(modbus.readDiscreteInputs(unitId, address, count));
    }
    if (function == "readholdingregisters" || function == "read" ||
        function == "readinputregisters") {
        std::uint16_t count = 0;
        if (!unsignedValue(input, "count", 1, 125, count)) {
            return Plugin::errorResponse("InvalidModbusCount", "count must be an integer from 1 to 125");
        }
        if (function == "readinputregisters") {
            PicoATE_Log("MODBUS_READ_INPUT unitId=0x{:02X} ({}) address=0x{:04X} ({}) count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), count);
            return resultResponse(modbus.readInputRegisters(unitId, address, count));
        }
        PicoATE_Log("MODBUS_READ_HOLDING unitId=0x{:02X} ({}) address=0x{:04X} ({}) count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), count);
        return resultResponse(modbus.readHoldingRegisters(unitId, address, count));
    }
    if (function == "writesinglecoil") {
        const auto value = input.find("value");
        if (value == input.end() || !value->is_boolean()) {
            return Plugin::errorResponse("InvalidModbusValue", "value must be a boolean");
        }
        return resultResponse(modbus.writeSingleCoil(unitId, address, value->get<bool>()));
    }
    if (function == "writesingleregister") {
        std::uint16_t value = 0;
        if (!unsignedValue(input, "value", 0, 0xFFFF, value)) {
            return Plugin::errorResponse("InvalidModbusValue", "value must be an integer or 0x hexadecimal value from 0 to 65535");
        }
        return resultResponse(modbus.writeSingleRegister(unitId, address, value));
    }
    if (function == "writemultiplecoils") {
        std::vector<bool> values;
        if (!coilValues(input, values)) {
            return Plugin::errorResponse(
                "InvalidModbusValues",
                "values must be a JSON boolean array containing 1 to 1968 values");
        }
        PicoATE_Log("MODBUS_WRITE_COILS unitId=0x{:02X} ({}) address=0x{:04X} ({}) count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), values.size());
        return resultResponse(modbus.writeMultipleCoils(unitId, address, values));
    }
    if (function == "writemultipleregisters" || function == "write") {
        const auto formatValue = input.find("dataFormat");
        const auto dataFormat = formatValue == input.end() || !formatValue->is_string()
            ? std::string("registers")
            : normalized(formatValue->get<std::string>());
        if (dataFormat == "asciitext" || dataFormat == "utf8text") {
            std::vector<std::uint16_t> textValues;
            std::string textValue;
            std::size_t byteCount = 0;
            std::size_t registerCount = 0;
            const auto encoding = dataFormat == "asciitext" ? "ascii" : "utf8";
            if (!textRegisterValues(input, encoding, textValues, textValue, byteCount, registerCount)) {
                return Plugin::errorResponse("InvalidModbusText", "text must be ASCII/UTF-8 and fit within registerCount registers");
            }
            PicoATE_Log("MODBUS_WRITE_TEXT_REGISTERS unitId=0x{:02X} ({}) address=0x{:04X} ({}) bytes={} count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), byteCount, registerCount);
            const auto result = modbus.writeMultipleRegisters(unitId, address, textValues);
            if (!result.success) return resultResponse(result);
            auto outputs = result.value.is_object() ? result.value : Plugin::Json::object();
            outputs["dataFormat"] = dataFormat;
            outputs["text"] = textValue;
            outputs["byteCount"] = byteCount;
            outputs["registerCount"] = registerCount;
            outputs["registers"] = textValues;
            return Plugin::response("Passed", std::move(outputs));
        }
        if (dataFormat != "registers") {
            return Plugin::errorResponse("InvalidModbusDataFormat", "dataFormat must be registers, asciiText, or utf8Text");
        }
        std::vector<std::uint16_t> values;
        if (!registerValues(input, values)) {
            return Plugin::errorResponse(
                "InvalidModbusValues",
                "values must contain 1 to 123 integer or 0x hexadecimal registers in the range 0..65535");
        }
        PicoATE_Log("MODBUS_WRITE_MULTIPLE unitId=0x{:02X} ({}) address=0x{:04X} ({}) count={}", static_cast<unsigned int>(unitId), static_cast<unsigned int>(unitId), static_cast<unsigned int>(address), static_cast<unsigned int>(address), values.size());
        return resultResponse(modbus.writeMultipleRegisters(unitId, address, values));
    }
    return Plugin::errorResponse(
        "UnknownFunction",
        "Use open, close, status, readCoils, readDiscreteInputs, readHoldingRegisters, "
        "readInputRegisters, writeSingleCoil, writeSingleRegister, writeMultipleCoils, or writeMultipleRegisters");
}

} // namespace

} // namespace PicoATE::Plugins::Modbus

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::Modbus::g_mutex);
    return PicoATE::Plugin::executeJson(requestJsonUtf8,
                                        responseJsonUtf8,
                                        responseBufferSize,
                                        PicoATE::Plugins::Modbus::execute);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Describe(
    char* descriptionJsonUtf8,
    int descriptionBufferSize)
{
    return PicoATE::Plugin::writeDescription(
        PicoATE::Plugins::Modbus::pluginDescription(),
        descriptionJsonUtf8,
        descriptionBufferSize);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_GetAbiVersion()
{
    return PicoATE::Plugin::AbiVersion;
}
