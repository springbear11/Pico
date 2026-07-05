#include "ModbusAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace PicoATE::Plugins::Modbus {

namespace {

std::mutex g_mutex;
std::unique_ptr<IModbusAdapter> g_adapter;

IModbusAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createModbusAdapter();
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

Plugin::Json resultResponse(const Result& result)
{
    return result.success
        ? Plugin::response("Passed", result.value.is_null()
                                         ? Plugin::Json::object()
                                         : Plugin::Json{{"value", result.value}})
        : Plugin::errorResponse(result.errorCode, result.errorMessage);
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& modbus = adapter();

    if (function == "connect") {
        const auto endpoint = Plugin::stringValue(input, "endpoint");
        const auto slaveId = Plugin::numberValue(input, "slaveId", 1);
        PicoATE_Log("MODBUS_CONNECT endpoint={} slave={}", endpoint, slaveId);
        return resultResponse(modbus.connect(endpoint, slaveId, input));
    }
    if (function == "disconnect") {
        modbus.disconnect();
        PicoATE_Log("MODBUS_DISCONNECT");
        return Plugin::response("Passed", {{"connected", false}});
    }
    if (function == "status") {
        return Plugin::response("Passed", {{"connected", modbus.isConnected()}});
    }
    if (!modbus.isConnected()) {
        return Plugin::errorResponse("ModbusNotConnected", "Call connect before Modbus I/O");
    }

    const auto address = Plugin::numberValue<std::uint16_t>(input, "address", 0);
    const auto count = Plugin::numberValue<std::uint16_t>(input, "count", 1);
    if (function == "readcoils") return resultResponse(modbus.readCoils(address, count));
    if (function == "readdiscreteinputs") return resultResponse(modbus.readDiscreteInputs(address, count));
    if (function == "readholdingregisters") return resultResponse(modbus.readHoldingRegisters(address, count));
    if (function == "readinputregisters") return resultResponse(modbus.readInputRegisters(address, count));
    if (function == "writesinglecoil") {
        return resultResponse(modbus.writeSingleCoil(
            address, Plugin::boolValue(input, "value", false)));
    }
    if (function == "writesingleregister") {
        return resultResponse(modbus.writeSingleRegister(
            address, Plugin::numberValue<std::uint16_t>(input, "value", 0)));
    }
    if (function == "writemultipleregisters") {
        std::vector<std::uint16_t> values;
        const auto source = input.find("values");
        if (source == input.end() || !source->is_array()) {
            return Plugin::errorResponse("InvalidModbusValues", "values must be an array");
        }
        for (const auto& value : *source) {
            if (!value.is_number_unsigned() && !value.is_number_integer()) {
                return Plugin::errorResponse("InvalidModbusValues", "register values must be integers");
            }
            const auto number = value.get<int>();
            if (number < 0 || number > 0xFFFF) {
                return Plugin::errorResponse("InvalidModbusValues", "register values must be 0..65535");
            }
            values.push_back(static_cast<std::uint16_t>(number));
        }
        return resultResponse(modbus.writeMultipleRegisters(address, values));
    }
    return Plugin::errorResponse("UnknownFunction", "Unsupported Modbus function");
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
