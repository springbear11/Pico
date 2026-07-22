#include "DmmAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace PicoATE::Plugins::Dmm {

namespace {

std::mutex g_mutex;
std::unique_ptr<IDmmAdapter> g_adapter;

IDmmAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createDmmAdapter();
    }
    if (!g_adapter) {
        throw std::runtime_error("DMM adapter factory returned null");
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

Plugin::Json readingResponse(const Result& result)
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    return Plugin::response(
        "Passed",
        {{"value", result.value}},
        {{"name", "DMM_READING"},
         {"value", result.value},
         {"status", "Passed"}});
}

bool measurementModeFromFunction(const std::string& function,
                                 MeasurementMode& mode)
{
    if (function == "configuredcv") {
        mode = MeasurementMode::Dcv;
    } else if (function == "configureacv") {
        mode = MeasurementMode::Acv;
    } else if (function == "configuredci") {
        mode = MeasurementMode::Dci;
    } else if (function == "configureaci") {
        mode = MeasurementMode::Aci;
    } else if (function == "configureresistance2w") {
        mode = MeasurementMode::Resistance2W;
    } else if (function == "configureresistance4w") {
        mode = MeasurementMode::Resistance4W;
    } else if (function == "configurefrequency") {
        mode = MeasurementMode::Frequency;
    } else if (function == "configureperiod") {
        mode = MeasurementMode::Period;
    } else if (function == "configurediode") {
        mode = MeasurementMode::Diode;
    } else if (function == "configurecontinuity") {
        mode = MeasurementMode::Continuity;
    } else if (function == "configurecapacitance") {
        mode = MeasurementMode::Capacitance;
    } else {
        return false;
    }
    return true;
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& dmm = adapter();

    if (function == "open" || function == "connect" || function == "connectdmm") {
        const auto address = Plugin::stringValue(
            input, "visaAddress", Plugin::stringValue(input, "address"));
        PicoATE_Log("DMM_OPEN address={}", address);
        const auto result = dmm.connect(address, input);
        if (!result.success) {
            PicoATE_Log("DMM_OPEN failed: {}", result.errorMessage);
        }
        return resultResponse(result, "connected");
    }

    if (function == "close" || function == "disconnect" || function == "disconnectdmm") {
        dmm.disconnect();
        PicoATE_Log("DMM_CLOSE passed");
        return Plugin::response("Passed", {{"connected", false}});
    }

    if (function == "health" || function == "status" || function == "connectionstatus") {
        return Plugin::response("Passed", {
            {"healthy", dmm.isConnected()},
            {"connected", dmm.isConnected()},
        });
    }

    if (!dmm.isConnected()) {
        return Plugin::errorResponse("DmmNotConnected", "Call open before DMM I/O");
    }

    if (function == "identity" || function == "getidentity") {
        return resultResponse(dmm.identity(), "identity");
    }

    if (function == "reset") {
        return resultResponse(dmm.reset());
    }

    if (function == "clear") {
        return resultResponse(dmm.clear());
    }

    if (function == "read" || function == "readdmm") {
        const auto result = dmm.read();
        if (result.success) {
            PicoATE_Log("DMM_READ value={}", result.value.dump());
        }
        return readingResponse(result);
    }

    if (function == "query" || function == "sendandreceive" || function == "sendandrecdmm") {
        const auto command = Plugin::stringValue(
            input, "command", Plugin::stringValue(input, "scpiCmd"));
        if (command.empty()) {
            return Plugin::errorResponse("ScpiCommandRequired", "Set command or scpiCmd");
        }
        PicoATE_Log("DMM_QUERY {}", command);
        return resultResponse(dmm.query(command), "response");
    }

    if (function == "write" || function == "send") {
        const auto command = Plugin::stringValue(
            input, "command", Plugin::stringValue(input, "scpiCmd"));
        if (command.empty()) {
            return Plugin::errorResponse("ScpiCommandRequired", "Set command or scpiCmd");
        }
        PicoATE_Log("DMM_WRITE {}", command);
        return resultResponse(dmm.write(command));
    }

    MeasurementMode mode;
    if (!measurementModeFromFunction(function, mode)) {
        return Plugin::errorResponse("UnknownFunction", "Unsupported DMM function");
    }

    const auto range = Plugin::numberValue(input, "range", 0.0);
    const bool apertureMode = mode == MeasurementMode::Frequency ||
                              mode == MeasurementMode::Period;
    const auto integration = Plugin::numberValue(
        input,
        apertureMode ? "aperture" : "nplc",
        apertureMode ? 0.1 : 10.0);
    PicoATE_Log("DMM_CONFIG function={} range={} integration={}",
                function,
                range,
                integration);
    const auto configured = dmm.configure(mode, range, integration);
    if (!configured.success) {
        return Plugin::errorResponse(configured.errorCode, configured.errorMessage);
    }
    return Plugin::response("Passed", {{"configured", true}});
}

} // namespace

} // namespace PicoATE::Plugins::Dmm

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::Dmm::g_mutex);
    return PicoATE::Plugin::executeJson(
        requestJsonUtf8,
        responseJsonUtf8,
        responseBufferSize,
        PicoATE::Plugins::Dmm::execute);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Describe(
    char* descriptionJsonUtf8,
    int descriptionBufferSize)
{
    return PicoATE::Plugin::writeDescription(
        PicoATE::Plugins::Dmm::pluginDescription(),
        descriptionJsonUtf8,
        descriptionBufferSize);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_GetAbiVersion()
{
    return PicoATE::Plugin::AbiVersion;
}