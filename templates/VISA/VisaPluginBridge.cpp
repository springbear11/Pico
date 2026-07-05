#include "VisaAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>

namespace PicoATE::Plugins::Visa {

namespace {

std::mutex g_mutex;
std::unique_ptr<IVisaAdapter> g_adapter;

IVisaAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createVisaAdapter();
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
                            const std::string& outputName = "value",
                            bool measurement = false)
{
    if (!result.success) {
        return Plugin::errorResponse(result.errorCode, result.errorMessage);
    }
    Plugin::Json outputs = Plugin::Json::object();
    if (!result.value.is_null()) {
        outputs[outputName] = result.value;
    }
    if (!measurement || result.value.is_null()) {
        return Plugin::response("Passed", std::move(outputs));
    }
    return Plugin::response("Passed", std::move(outputs), {
        {"name", "VISA_READING"},
        {"value", result.value},
        {"status", "Passed"},
    });
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = normalized(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    auto& visa = adapter();

    if (function == "connect" || function == "connectdmm") {
        const auto address = Plugin::stringValue(input, "visaAddress",
                                                  Plugin::stringValue(input, "address"));
        PicoATE_Log("VISA_CONNECT address={}", address);
        return resultResponse(visa.connect(address, input), "connected");
    }
    if (function == "disconnect") {
        visa.disconnect();
        PicoATE_Log("VISA_DISCONNECT");
        return Plugin::response("Passed", {{"connected", false}});
    }
    if (function == "status") {
        return Plugin::response("Passed", {{"connected", visa.isConnected()}});
    }
    if (!visa.isConnected()) {
        return Plugin::errorResponse("VisaNotConnected", "Call connect before VISA I/O");
    }
    if (function == "getidentity" || function == "identity") {
        return resultResponse(visa.identity(), "identity");
    }
    if (function == "reset") {
        return resultResponse(visa.reset());
    }
    if (function == "clear") {
        return resultResponse(visa.clear());
    }
    if (function == "readdmm" || function == "read") {
        const auto result = visa.read();
        if (result.success) {
            PicoATE_Log("VISA_READ value={}", result.value.dump());
        }
        return resultResponse(result, "value", true);
    }
    if (function == "sendandrecdmm" || function == "sendandreceive" || function == "query") {
        const auto command = Plugin::stringValue(input, "command",
                                                  Plugin::stringValue(input, "scpiCmd"));
        PicoATE_Log("VISA_QUERY {}", command);
        return resultResponse(visa.sendAndReceive(command), "response");
    }

    MeasurementMode mode;
    if (function == "configuredcv") mode = MeasurementMode::Dcv;
    else if (function == "configureacv") mode = MeasurementMode::Acv;
    else if (function == "configuredci") mode = MeasurementMode::Dci;
    else if (function == "configureaci") mode = MeasurementMode::Aci;
    else if (function == "configureresistance2w") mode = MeasurementMode::Resistance2W;
    else if (function == "configureresistance4w") mode = MeasurementMode::Resistance4W;
    else if (function == "configurefrequency") mode = MeasurementMode::Frequency;
    else if (function == "configurediode") mode = MeasurementMode::Diode;
    else return Plugin::errorResponse("UnknownFunction", "Unsupported VISA function");

    const auto range = Plugin::numberValue(input, "range", 0.0);
    const auto integration = Plugin::numberValue(
        input, mode == MeasurementMode::Frequency ? "aperture" : "nplc", 10.0);
    return resultResponse(visa.configure(mode, range, integration));
}

} // namespace

} // namespace PicoATE::Plugins::Visa

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::Visa::g_mutex);
    return PicoATE::Plugin::executeJson(requestJsonUtf8,
                                        responseJsonUtf8,
                                        responseBufferSize,
                                        PicoATE::Plugins::Visa::execute);
}
