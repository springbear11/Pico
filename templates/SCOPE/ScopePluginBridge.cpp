#include "ScopeAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace PicoATE::Plugins::Scope {

namespace {

std::mutex g_mutex;
std::unique_ptr<IScopeAdapter> g_adapter;

IScopeAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createScopeAdapter();
    }
    if (!g_adapter) {
        throw std::runtime_error("Oscilloscope adapter factory returned null");
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

Plugin::Json measurementResponse(const Result& result)
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    return Plugin::response(
        "Passed",
        {{"value", result.value}},
        {{"name", "SCOPE_MEASUREMENT"},
         {"value", result.value},
         {"status", "Passed"}});
}

Plugin::Json executeImpl(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& osc = adapter();

    if (function == "open" || function == "connect" || function == "connectscope") {
        const auto address = Plugin::stringValue(
            input, "visaAddress", Plugin::stringValue(input, "address"));
        PicoATE_Log("SCOPE_OPEN address={}", address);
        const auto result = osc.connect(address, input);
        if (!result.success) {
            PicoATE_Log("SCOPE_OPEN failed: {}", result.errorMessage);
        }
        return resultResponse(result, "connected");
    }

    if (function == "close" || function == "disconnect" || function == "disconnectscope") {
        osc.disconnect();
        PicoATE_Log("SCOPE_CLOSE passed");
        return Plugin::response("Passed", {{"connected", false}});
    }

    if (function == "health" || function == "status" || function == "connectionstatus") {
        return Plugin::response("Passed", {
            {"healthy", osc.isConnected()},
            {"connected", osc.isConnected()},
        });
    }

    if (!osc.isConnected()) {
        return Plugin::errorResponse("ScopeNotConnected", "Call open before oscilloscope I/O");
    }

    if (function == "identity" || function == "getidentity") {
        return resultResponse(osc.identity(), "identity");
    }

    if (function == "reset") {
        return resultResponse(osc.reset());
    }

    if (function == "clear") {
        return resultResponse(osc.clear());
    }

    if (function == "lockfrontpanel") {
        const auto lock = Plugin::numberValue(input, "lock",
            Plugin::boolValue(input, "locked", false) ? 1 : 0);
        PicoATE_Log("SCOPE_LOCK_FRONT_PANEL lock={}", lock);
        return resultResponse(osc.lockFrontPanel(lock));
    }

    if (function == "run") {
        PicoATE_Log("SCOPE_RUN");
        return resultResponse(osc.run());
    }

    if (function == "stop") {
        PicoATE_Log("SCOPE_STOP");
        return resultResponse(osc.stop());
    }

    if (function == "single") {
        PicoATE_Log("SCOPE_SINGLE");
        return resultResponse(osc.single());
    }

    if (function == "forcetrigger") {
        PicoATE_Log("SCOPE_FORCE_TRIGGER");
        return resultResponse(osc.forceTrigger());
    }

    if (function == "enablechannel") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto enable = Plugin::numberValue(input, "enable",
            Plugin::boolValue(input, "enabled", false) ? 1 : 0);
        PicoATE_Log("SCOPE_CHANNEL_DISPLAY channel={} enable={}", channel, enable);
        return resultResponse(osc.enableChannel(channel, enable));
    }

    if (function == "setchannelcoupling") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto coupling = Plugin::numberValue(input, "coupling", CouplingDc);
        PicoATE_Log("SCOPE_CHANNEL_COUPLING channel={} coupling={}", channel, coupling);
        return resultResponse(osc.setChannelCoupling(channel, coupling));
    }

    if (function == "setprobe") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto attenuation = Plugin::numberValue(input, "attenuation", 10.0);
        PicoATE_Log("SCOPE_PROBE channel={} attenuation={}", channel, attenuation);
        return resultResponse(osc.setProbe(channel, attenuation));
    }

    if (function == "setvoltagediv") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto voltsPerDiv = Plugin::numberValue(input, "voltsPerDiv", 1.0);
        PicoATE_Log("SCOPE_VOLTAGE_DIV channel={} voltsPerDiv={}", channel, voltsPerDiv);
        return resultResponse(osc.setVoltageDiv(channel, voltsPerDiv));
    }

    if (function == "setchanneloffset") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto offset = Plugin::numberValue(input, "offset", 0.0);
        PicoATE_Log("SCOPE_CHANNEL_OFFSET channel={} offset={}", channel, offset);
        return resultResponse(osc.setChannelOffset(channel, offset));
    }

    if (function == "setinvert") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto invert = Plugin::numberValue(input, "invert",
            Plugin::boolValue(input, "enabled", false) ? 1 : 0);
        PicoATE_Log("SCOPE_INVERT channel={} invert={}", channel, invert);
        return resultResponse(osc.setInvert(channel, invert));
    }

    if (function == "setbandwidthlimit") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        const auto limit = Plugin::numberValue(input, "limit", BandwidthOff);
        PicoATE_Log("SCOPE_BANDWIDTH_LIMIT channel={} limit={}", channel, limit);
        return resultResponse(osc.setBandwidthLimit(channel, limit));
    }

    if (function == "settimebasescale") {
        const auto secondsPerDiv = Plugin::numberValue(input, "secondsPerDiv", 0.001);
        PicoATE_Log("SCOPE_TIMEBASE_SCALE secondsPerDiv={}", secondsPerDiv);
        return resultResponse(osc.setTimebaseScale(secondsPerDiv));
    }

    if (function == "settriggermode") {
        const auto mode = Plugin::numberValue(input, "mode", TriggerEdge);
        PicoATE_Log("SCOPE_TRIGGER_MODE mode={}", mode);
        return resultResponse(osc.setTriggerMode(mode));
    }

    if (function == "settriggersweep") {
        const auto sweep = Plugin::numberValue(input, "sweep", SweepAuto);
        PicoATE_Log("SCOPE_TRIGGER_SWEEP sweep={}", sweep);
        return resultResponse(osc.setTriggerSweep(sweep));
    }

    if (function == "settriggercoupling") {
        const auto coupling = Plugin::numberValue(input, "coupling", TriggerCouplingDc);
        PicoATE_Log("SCOPE_TRIGGER_COUPLING coupling={}", coupling);
        return resultResponse(osc.setTriggerCoupling(coupling));
    }

    if (function == "setedgetriggersource") {
        const auto channel = Plugin::numberValue(input, "channel", 1);
        PicoATE_Log("SCOPE_EDGE_TRIGGER_SOURCE channel={}", channel);
        return resultResponse(osc.setEdgeTriggerSource(channel));
    }

    if (function == "setedgetriggerlevel") {
        const auto level = Plugin::numberValue(input, "level", 0.0);
        PicoATE_Log("SCOPE_EDGE_TRIGGER_LEVEL level={}", level);
        return resultResponse(osc.setEdgeTriggerLevel(level));
    }

    if (function == "setedgetriggerslope") {
        const auto slope = Plugin::numberValue(input, "slope", EdgeRising);
        PicoATE_Log("SCOPE_EDGE_TRIGGER_SLOPE slope={}", slope);
        return resultResponse(osc.setEdgeTriggerSlope(slope));
    }

    if (function == "measure") {
        const auto item = Plugin::stringValue(input, "item");
        if (item.empty()) {
            return Plugin::errorResponse("MeasureItemRequired", "Set item (VPP/VMAX/VRMS/FREQuency/...)");
        }
        const auto source = Plugin::numberValue(input, "source", 0);
        const auto source2 = Plugin::numberValue(input, "source2", 0);
        PicoATE_Log("SCOPE_MEASURE item={} source={} source2={}", item, source, source2);
        const auto result = osc.measure(item, source, source2);
        if (result.success) {
            PicoATE_Log("SCOPE_MEASURE value={}", result.value.dump());
        }
        return measurementResponse(result);
    }

    if (function == "autoscale") {
        PicoATE_Log("SCOPE_AUTOSCALE");
        return resultResponse(osc.autoScale());
    }

    if (function == "query" || function == "sendandreceive" || function == "sendandrecscope") {
        const auto command = Plugin::stringValue(
            input, "command", Plugin::stringValue(input, "scpiCmd"));
        if (command.empty()) {
            return Plugin::errorResponse("ScpiCommandRequired", "Set command or scpiCmd");
        }
        PicoATE_Log("SCOPE_QUERY {}", command);
        const auto result = osc.query(command);
        if (result.success) {
            PicoATE_Log("SCOPE_QUERY {} => {}", command, result.value.get<std::string>());
        } else {
            PicoATE_Log("SCOPE_QUERY {} failed: {}", command, result.errorMessage);
        }
        return resultResponse(result, "response");
    }

    if (function == "write" || function == "send") {
        const auto command = Plugin::stringValue(
            input, "command", Plugin::stringValue(input, "scpiCmd"));
        if (command.empty()) {
            return Plugin::errorResponse("ScpiCommandRequired", "Set command or scpiCmd");
        }
        PicoATE_Log("SCOPE_WRITE {}", command);
        return resultResponse(osc.write(command));
    }

    return Plugin::errorResponse("UnknownFunction", "Unsupported oscilloscope function");
}

// Unified result logging: every function call logs function name + outcome/code/message for R&D debugging
Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto response = executeImpl(request);
    const auto outcome = Plugin::stringValue(response, "outcome");
    const auto code = Plugin::stringValue(response, "errorCode");
    const auto message = Plugin::stringValue(response, "errorMessage");
    PicoATE_Log("SCOPE_FUNCTION {} outcome={} code={} message={}",
                function, outcome, code, message);
    return response;
}

} // namespace

} // namespace PicoATE::Plugins::Scope

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::Scope::g_mutex);
    return PicoATE::Plugin::executeJson(
        requestJsonUtf8,
        responseJsonUtf8,
        responseBufferSize,
        PicoATE::Plugins::Scope::execute);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Describe(
    char* descriptionJsonUtf8,
    int descriptionBufferSize)
{
    return PicoATE::Plugin::writeDescription(
        PicoATE::Plugins::Scope::pluginDescription(),
        descriptionJsonUtf8,
        descriptionBufferSize);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_GetAbiVersion()
{
    return PicoATE::Plugin::AbiVersion;
}
