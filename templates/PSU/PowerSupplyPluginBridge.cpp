#include "PowerSupplyAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>

namespace PicoATE::Plugins::PowerSupply {

namespace {

std::mutex g_mutex;
std::unique_ptr<IPowerSupplyAdapter> g_adapter;

IPowerSupplyAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createPowerSupplyAdapter();
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

Plugin::Json resultResponse(const Result& result,
                            const std::string& outputName = "value")
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    if (result.value.is_object()) {
        return Plugin::response("Passed", result.value);
    }
    Plugin::Json outputs = Plugin::Json::object();
    if (!result.value.is_null()) {
        outputs[outputName] = result.value;
    }
    return Plugin::response("Passed", std::move(outputs));
}

Plugin::Json measurementResponse(const Result& result,
                                 const std::string& outputName,
                                 const std::string& measurementName,
                                 const std::string& unit)
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    return Plugin::response(
        "Passed",
        {{outputName, result.value}},
        {{"name", measurementName},
         {"value", result.value},
         {"unit", unit},
         {"status", "Passed"}});
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& powerSupply = adapter();

    if (function == "open" || function == "connect") {
        const auto address = Plugin::stringValue(
            input, "visaAddress", Plugin::stringValue(input, "address"));
        PicoATE_Log("PSU_OPEN VISA address={}", address);
        const auto result = powerSupply.connect(address, input);
        if (!result.success) {
            return Plugin::errorResponse(result.errorCode, result.errorMessage);
        }
        Plugin::Json outputs = result.value.is_object()
            ? result.value
            : Plugin::Json::object();
        outputs["connected"] = true;
        outputs["address"] = address;
        return Plugin::response("Passed", std::move(outputs));
    }
    if (function == "close" || function == "disconnect") {
        powerSupply.disconnect();
        PicoATE_Log("PSU_CLOSE passed");
        return Plugin::response("Passed", {{"connected", false}});
    }
    if (function == "health") {
        return Plugin::response("Passed", {
            {"healthy", powerSupply.isConnected()},
            {"connected", powerSupply.isConnected()},
        });
    }
    if (function == "connectionstatus") {
        return Plugin::response("Passed", {{"connected", powerSupply.isConnected()}});
    }
    if (!powerSupply.isConnected()) {
        return Plugin::errorResponse(
            "PowerSupplyNotConnected", "Call open before power-supply I/O");
    }

    const int channel = Plugin::numberValue(input, "channel", 1);
    if (function == "identity" || function == "getidentity") {
        return resultResponse(powerSupply.identity(), "identity");
    }
    if (function == "setcurrent") {
        const auto value = Plugin::numberValue(input, "current", 0.0);
        PicoATE_Log("PSU_ISET channel={} current={} A", channel, value);
        return resultResponse(powerSupply.setCurrent(channel, value), "current");
    }
    if (function == "getcurrentsetpoint") {
        return measurementResponse(powerSupply.currentSetpoint(channel),
                                   "current", "PSU_CURRENT_SETPOINT", "A");
    }
    if (function == "setvoltage") {
        const auto value = Plugin::numberValue(input, "voltage", 0.0);
        PicoATE_Log("PSU_VSET channel={} voltage={} V", channel, value);
        return resultResponse(powerSupply.setVoltage(channel, value), "voltage");
    }
    if (function == "getvoltagesetpoint") {
        return measurementResponse(powerSupply.voltageSetpoint(channel),
                                   "voltage", "PSU_VOLTAGE_SETPOINT", "V");
    }
    if (function == "measurecurrent" || function == "getoutputcurrent") {
        return measurementResponse(powerSupply.outputCurrent(channel),
                                   "current", "PSU_OUTPUT_CURRENT", "A");
    }
    if (function == "measurevoltage" || function == "getoutputvoltage") {
        return measurementResponse(powerSupply.outputVoltage(channel),
                                   "voltage", "PSU_OUTPUT_VOLTAGE", "V");
    }
    if (function == "setbeep") {
        return resultResponse(powerSupply.setBeep(
            Plugin::boolValue(input, "enabled", false)), "enabled");
    }
    if (function == "setoutput") {
        const bool enabled = Plugin::boolValue(input, "enabled", false);
        PicoATE_Log("PSU_OUTPUT enabled={}", enabled);
        return resultResponse(powerSupply.setOutput(enabled), "enabled");
    }
    if (function == "readstatus" || function == "status") {
        return resultResponse(powerSupply.status());
    }
    if (function == "recall") {
        return resultResponse(powerSupply.recall(
            Plugin::numberValue(input, "slot", 1)), "slot");
    }
    if (function == "save") {
        return resultResponse(powerSupply.save(
            Plugin::numberValue(input, "slot", 1)), "slot");
    }
    if (function == "setocp") {
        return resultResponse(powerSupply.setOcp(
            Plugin::boolValue(input, "enabled", false)), "enabled");
    }
    if (function == "setovp") {
        return resultResponse(powerSupply.setOvp(
            Plugin::boolValue(input, "enabled", false)), "enabled");
    }
    if (function == "setlock") {
        return resultResponse(powerSupply.setLock(
            Plugin::boolValue(input, "enabled", false)), "enabled");
    }

    return Plugin::errorResponse("UnknownFunction", "Unsupported power-supply function");
}

} // namespace

} // namespace PicoATE::Plugins::PowerSupply

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::PowerSupply::g_mutex);
    return PicoATE::Plugin::executeJson(
        requestJsonUtf8,
        responseJsonUtf8,
        responseBufferSize,
        PicoATE::Plugins::PowerSupply::execute);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Describe(
    char* descriptionJsonUtf8,
    int descriptionBufferSize)
{
    return PicoATE::Plugin::writeDescription(
        PicoATE::Plugins::PowerSupply::pluginDescription(),
        descriptionJsonUtf8,
        descriptionBufferSize);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_GetAbiVersion()
{
    return PicoATE::Plugin::AbiVersion;
}
