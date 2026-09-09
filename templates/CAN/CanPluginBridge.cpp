#include "CanAdapter.h"

#include "PicoATE/Plugin/PluginAbi.h"
#include "PicoATE/Plugin/PluginLog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace PicoATE::Plugins::Can {

namespace {

std::mutex g_mutex;
std::unique_ptr<ICanAdapter> g_adapter;

ICanAdapter& adapter()
{
    if (!g_adapter) {
        g_adapter = createCanAdapter();
    }
    if (!g_adapter) {
        throw std::runtime_error("CAN adapter factory returned null");
    }
    return *g_adapter;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool readUnsigned(const Plugin::Json& value, std::uint32_t& result)
{
    try {
        std::uint64_t parsed = 0;
        if (value.is_number_unsigned()) {
            parsed = value.get<std::uint64_t>();
        } else if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
            parsed = static_cast<std::uint64_t>(value.get<std::int64_t>());
        } else if (value.is_string()) {
            const auto text = value.get<std::string>();
            std::size_t consumed = 0;
            parsed = std::stoull(text, &consumed, 0);
            if (consumed != text.size()) {
                return false;
            }
        } else {
            return false;
        }
        if (parsed > 0x1FFFFFFFULL) {
            return false;
        }
        result = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::string jsonValueText(const Plugin::Json& value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

bool readData(const Plugin::Json& value,
              std::vector<std::uint8_t>& data,
              std::string& errorMessage)
{
    data.clear();
    if (value.is_array()) {
        for (const auto& item : value) {
            if (!item.is_number_integer()) {
                errorMessage = "data array items must be bytes (0..255)";
                return false;
            }
            if (item < 0 || item > 255) {
                errorMessage = "data array items must be bytes (0..255)";
                return false;
            }
            data.push_back(item.get<std::uint8_t>());
        }
        return true;
    }
    if (!value.is_string()) {
        errorMessage = "data must be a byte array or hexadecimal string";
        return false;
    }

    auto text = value.get<std::string>();
    std::replace(text.begin(), text.end(), ',', ' ');
    std::replace(text.begin(), text.end(), '-', ' ');
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        if (token.starts_with("0x") || token.starts_with("0X")) {
            token.erase(0, 2);
        }
        try {
            std::size_t consumed = 0;
            const auto byte = std::stoul(token, &consumed, 16);
            if (consumed != token.size() || byte > 255) {
                errorMessage = "invalid CAN byte: " + token;
                return false;
            }
            data.push_back(static_cast<std::uint8_t>(byte));
        } catch (...) {
            errorMessage = "invalid CAN byte: " + token;
            return false;
        }
    }
    return true;
}

std::string dataText(const std::vector<std::uint8_t>& data)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < data.size(); ++index) {
        if (index > 0) {
            stream << ' ';
        }
        stream << std::setw(2) << static_cast<unsigned>(data[index]);
    }
    return stream.str();
}

std::string identifierText(std::uint32_t value, bool extended)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(extended ? 8 : 3) << value;
    return stream.str();
}

OpenOptions optionsFromInputs(const Plugin::Json& input)
{
    OpenOptions options;
    options.libraryPath = Plugin::stringValue(input, "libraryPath");
    options.deviceType = Plugin::numberValue(input, "deviceType", 0);
    options.deviceIndex = Plugin::numberValue(input, "deviceIndex", 0);
    options.channelIndex = Plugin::numberValue(input, "channelIndex", 0);
    options.arbitrationBitrate = Plugin::numberValue(input, "bitrate", 500000);
    options.dataBitrate = Plugin::numberValue(input, "dataBitrate", 2000000);
    options.canFd = Plugin::boolValue(input, "canFd", false);
    options.listenOnly = Plugin::boolValue(input, "listenOnly", false);
    options.selfTest = Plugin::boolValue(input, "selfTest", false);
    options.vendorOptions = input;
    return options;
}

bool frameFromInputs(const Plugin::Json& object, Frame& frame, std::string& errorMessage)
{
    const auto id = object.find("id");
    if (id == object.end()) {
        errorMessage = "CAN id is missing; allowed range is 0x000 to 0x7FF "
                       "or 0x00000000 to 0x1FFFFFFF for extended frames";
        return false;
    }
    if (!readUnsigned(*id, frame.id) || frame.id > 0x1FFFFFFFU) {
        errorMessage = "CAN id value " + jsonValueText(*id) +
            " is invalid; allowed range is 0x00000000 to 0x1FFFFFFF";
        return false;
    }
    const auto inputData = object.find("data");
    if (inputData == object.end()) {
        errorMessage = "CAN data is missing; provide a byte array or hexadecimal string";
        return false;
    }
    if (!readData(*inputData, frame.data, errorMessage)) {
        return false;
    }
    frame.extended = Plugin::boolValue(object, "extended", frame.id > 0x7FF);
    if (!frame.extended && frame.id > 0x7FF) {
        errorMessage = "CAN id value " + identifierText(frame.id, true) +
            " is invalid for a standard frame; allowed range is 0x000 to 0x7FF "
            "or enable Extended Frame";
        return false;
    }
    frame.remote = Plugin::boolValue(object, "remote", false);
    frame.canFd = Plugin::boolValue(object, "canFd", false);
    frame.bitrateSwitch = Plugin::boolValue(object, "bitrateSwitch", false);
    const auto maximumBytes = frame.canFd ? 64U : 8U;
    if (frame.data.size() > maximumBytes) {
        errorMessage = "frame data exceeds " + std::to_string(maximumBytes) + " bytes";
        return false;
    }
    return true;
}

Plugin::Json frameToJson(const Frame& frame)
{
    return {
        {"id", identifierText(frame.id, frame.extended)},
        {"idNumeric", frame.id},
        {"data", frame.data},
        {"dataHex", dataText(frame.data)},
        {"dlc", frame.data.size()},
        {"extended", frame.extended},
        {"remote", frame.remote},
        {"canFd", frame.canFd},
        {"bitrateSwitch", frame.bitrateSwitch},
        {"timestampUs", frame.timestampUs},
    };
}

Plugin::Json receiveResponse(const Frame& frame,
                             std::uint32_t filterId,
                             std::uint32_t filterMask)
{
    auto outputs = frameToJson(frame);
    const auto expectedId = identifierText(filterId, filterId > 0x7FF);
    const auto expectedMask = identifierText(filterMask, filterMask > 0x7FF);
    const auto expectedFilter = "ID=" + expectedId + " | MASK=" + expectedMask;
    const auto actual = "ID=" + outputs["id"].get<std::string>() +
        " | MASK=" + expectedMask +
        " | DATA=" + dataText(frame.data) +
        " | DLC=" + std::to_string(frame.data.size());
    const Plugin::Json measurement{
        {"name", "CAN_RX_FRAME"},
        {"value", actual},
        {"rawValue", dataText(frame.data)},
        {"status", "Passed"},
        {"frameId", outputs["id"]},
        {"filterId", expectedId},
        {"filterMask", expectedMask},
        {"displayLower", expectedFilter},
        {"displayUpper", expectedFilter},
        {"dataHex", outputs["dataHex"]},
        {"dlc", outputs["dlc"]},
    };
    return Plugin::response("Passed", std::move(outputs), measurement);
}

Plugin::Json execute(const Plugin::Json& request)
{
    const auto function = lower(Plugin::stringValue(request, "function"));
    const auto& input = Plugin::inputs(request);
    const auto options = optionsFromInputs(input);
    auto& can = adapter();

    if (function == "finddevices") {
        DiscoveryOptions discovery;
        discovery.libraryPath = options.libraryPath;
        discovery.deviceType = options.deviceType;
        discovery.maximumDeviceIndex = std::clamp(
            Plugin::numberValue(input, "maximumDeviceIndex", 15), 0, 63);
        PicoATE_Log("CAN_FIND_DEVICES type={} maxIndex={}",
                    discovery.deviceType,
                    discovery.maximumDeviceIndex);
        const auto found = can.findDevices(discovery);
        if (!found.status.success) {
            PicoATE_Log("CAN_FIND_DEVICES failed: {}", found.status.errorMessage);
            return Plugin::errorResponse(found.status.errorCode,
                                         found.status.errorMessage);
        }
        Plugin::Json devices = Plugin::Json::array();
        for (const auto& device : found.devices) {
            devices.push_back({
                {"serialNumber", device.serialNumber},
                {"model", device.model},
                {"deviceType", device.deviceType},
                {"deviceIndex", device.deviceIndex},
                {"channelCount", device.channelCount},
            });
        }
        PicoATE_Log("CAN_FIND_DEVICES passed count={}", devices.size());
        return Plugin::response("Passed", {{"devices", std::move(devices)}});
    }

    if (function == "open" || function == "connectcan") {
        PicoATE_Log("CAN_OPEN type={} device={} channel={} bitrate={} mode={}",
                    options.deviceType,
                    options.deviceIndex,
                    options.channelIndex,
                    options.arbitrationBitrate,
                    options.selfTest ? "self-test" : (options.listenOnly ? "listen-only" : "normal"));
        const auto result = can.open(options);
        if (!result.success) {
            PicoATE_Log("CAN_OPEN failed: {}", result.errorMessage);
            return Plugin::errorResponse(result.errorCode, result.errorMessage);
        }
        PicoATE_Log("CAN_OPEN passed: {}", can.deviceDescription(options));
        return Plugin::response("Passed", {
            {"connected", true},
            {"device", can.deviceDescription(options)},
        });
    }

    if (function == "close" || function == "disconnect") {
        PicoATE_Log("CAN_CLOSE device={} channel={} begin",
                    options.deviceIndex,
                    options.channelIndex);
        const auto result = can.close(options);
        if (!result.success) {
            PicoATE_Log("CAN_CLOSE failed: {}", result.errorMessage);
            return Plugin::errorResponse(result.errorCode, result.errorMessage);
        }
        PicoATE_Log("CAN_CLOSE device={} channel={} passed",
                    options.deviceIndex,
                    options.channelIndex);
        return Plugin::response("Passed", {{"connected", false}});
    }

    if (function == "status") {
        return Plugin::response("Passed", {
            {"connected", can.isOpen(options)},
            {"device", can.deviceDescription(options)},
        });
    }

    if (!can.isOpen(options)) {
        return Plugin::errorResponse("CanNotOpen", "Call open before CAN I/O");
    }

    if (function == "write") {
        Frame frame;
        std::string message;
        if (!frameFromInputs(input, frame, message)) {
            return Plugin::errorResponse("InvalidCanFrame", message);
        }
        PicoATE_Log("CAN_SEND id=0x{:X} data={}", frame.id, dataText(frame.data));
        const auto result = can.transmit(options, frame);
        if (!result.success) {
            PicoATE_Log("CAN_SEND failed: {}", result.errorMessage);
            return Plugin::errorResponse(result.errorCode, result.errorMessage);
        }
        PicoATE_Log("CAN_SEND passed");
        return Plugin::response("Passed", {{"transmitted", true}, {"frame", frameToJson(frame)}});
    }

    if (function == "read") {
        std::uint32_t filterId = 0;
        std::uint32_t filterMask = 0;
        if (const auto value = input.find("filterId"); value != input.end()) {
            if (!readUnsigned(*value, filterId) || filterId > 0x1FFFFFFFU) {
                return Plugin::errorResponse(
                    "InvalidCanFilter",
                    "filterId value " + jsonValueText(*value) +
                        " is invalid; allowed range is 0x00000000 to 0x1FFFFFFF");
            }
        }
        if (const auto value = input.find("filterMask"); value != input.end()) {
            if (!readUnsigned(*value, filterMask) || filterMask > 0x1FFFFFFFU) {
                return Plugin::errorResponse(
                    "InvalidCanFilter",
                    "filterMask value " + jsonValueText(*value) +
                        " is invalid; allowed range is 0x00000000 to 0x1FFFFFFF");
            }
        }
        const auto timeoutMs = Plugin::numberValue(input, "timeoutMs", 1000);
        PicoATE_Log("CAN_RECV wait filterId=0x{:X} mask=0x{:X} timeoutMs={}",
                    filterId,
                    filterMask,
                    timeoutMs);
        const auto result = can.receive(options, filterId, filterMask, timeoutMs);
        if (result.status == ReceiveStatus::Timeout) {
            PicoATE_Log("CAN_RECV timeout");
            return Plugin::response("Timeout", {}, {}, "CanReceiveTimeout", "No matching CAN frame received");
        }
        if (result.status == ReceiveStatus::Error) {
            PicoATE_Log("CAN_RECV failed: {}", result.errorMessage);
            return Plugin::errorResponse(result.errorCode, result.errorMessage);
        }
        PicoATE_Log("CAN_RECV id=0x{:X} data={}", result.frame.id, dataText(result.frame.data));
        return receiveResponse(result.frame, filterId, filterMask);
    }

    if (function == "requestresponse" || function == "sendandread") {
        const bool sendAndRead = function == "sendandread";
        Frame transmitFrame;
        std::string message;
        const auto tx = input.find("tx");
        const auto& transmitInput = tx != input.end() && tx->is_object() ? *tx : input;
        if (!frameFromInputs(transmitInput, transmitFrame, message)) {
            return Plugin::errorResponse("InvalidCanFrame", message);
        }
        std::uint32_t filterId = transmitFrame.id;
        std::uint32_t filterMask = sendAndRead || transmitFrame.extended ? 0x1FFFFFFF : 0x7FF;
        if (sendAndRead && !input.contains("rxId")) {
            return Plugin::errorResponse("InvalidCanFilter", "rxId is required for sendAndRead");
        }
        if (const auto value = input.find("rxId"); value != input.end()) {
            if (!readUnsigned(*value, filterId) || filterId > 0x1FFFFFFFU) {
                return Plugin::errorResponse(
                    "InvalidCanFilter",
                    "rxId value " + jsonValueText(*value) +
                        " is invalid; allowed range is 0x00000000 to 0x1FFFFFFF");
            }
        }
        if (const auto value = input.find("rxMask"); value != input.end()) {
            if (!readUnsigned(*value, filterMask) || filterMask > 0x1FFFFFFFU) {
                return Plugin::errorResponse(
                    "InvalidCanFilter",
                    "rxMask value " + jsonValueText(*value) +
                        " is invalid; allowed range is 0x00000000 to 0x1FFFFFFF");
            }
        }
        int timeoutMs = sendAndRead ? 1500 : Plugin::numberValue(input, "timeoutMs", 1000);
        if (sendAndRead) {
            if (const auto value = input.find("timeoutMs"); value != input.end()) {
                if (!value->is_number_integer() || *value < 1 || *value > 60000) {
                    return Plugin::errorResponse("InvalidCanTimeout",
                        "timeoutMs value " + jsonValueText(*value) + " must be an integer in 1..60000 ms");
                }
                timeoutMs = value->get<int>();
            }
        }
        const auto details = "TX=" + identifierText(transmitFrame.id, transmitFrame.extended) +
            " RX=" + identifierText(filterId, filterId > 0x7FF) +
            " MASK=" + identifierText(filterMask, filterMask > 0x7FF) +
            " channel=" + std::to_string(options.channelIndex) +
            " timeoutMs=" + std::to_string(timeoutMs);
        bool transmitted = false;
        const auto response = [&](Plugin::Json result) {
            if (sendAndRead) {
                if (!result["outputs"].is_object()) result["outputs"] = Plugin::Json::object();
                result["outputs"]["transmitted"] = transmitted;
                result["outputs"]["txId"] = identifierText(transmitFrame.id, transmitFrame.extended);
                result["outputs"]["txDataHex"] = dataText(transmitFrame.data);
            }
            return result;
        };
        // Validate both directions before sending any command to the product.
        PicoATE_Log("CAN_REQUEST send {} data={}", details, dataText(transmitFrame.data));
        const auto transmitResult = can.transmit(options, transmitFrame);
        if (!transmitResult.success) {
            PicoATE_Log("CAN_REQUEST send failed: {} | {}", transmitResult.errorMessage, details);
            return response(Plugin::errorResponse(transmitResult.errorCode,
                transmitResult.errorMessage + " | " + details));
        }
        transmitted = true;
        PicoATE_Log("CAN_REQUEST wait {}", details);
        const auto result = can.receive(options, filterId, filterMask, timeoutMs);
        if (result.status == ReceiveStatus::Timeout) {
            PicoATE_Log("CAN_REQUEST timeout {}", details);
            return response(Plugin::response("Timeout", {}, {}, "CanReceiveTimeout",
                "No response CAN frame received | " + details));
        }
        if (result.status == ReceiveStatus::Error) {
            PicoATE_Log("CAN_REQUEST receive failed: {} | {}", result.errorMessage, details);
            return response(Plugin::errorResponse(result.errorCode, result.errorMessage + " | " + details));
        }
        PicoATE_Log("CAN_REQUEST receive id=0x{:X} data={}",
                    result.frame.id,
                    dataText(result.frame.data));
        return response(receiveResponse(result.frame, filterId, filterMask));
    }

    return Plugin::errorResponse(
        "UnknownFunction", "Use open, close, status, write, read, sendAndRead, requestResponse, or findDevices");
}

} // namespace

Plugin::Json sendAndReadDescription()
{
    using Json = Plugin::Json;
    return {
        {"id", "sendAndRead"},
        {"name", "Send And Read CAN Frame"},
        {"description", "Send one frame and wait for a response matching rxId/rxMask within one serialized call. Open the channel first; use distinct response IDs for different UUTs."},
        {"timeoutMs", 65000},
        {"inputs", Json::array({
            {{"key", "id"}, {"name", "CAN ID"}, {"type", "string"},
             {"required", true}, {"default", "0x123"}},
            {{"key", "data"}, {"name", "Frame Data"}, {"type", "hex-bytes"},
             {"required", true}, {"default", "01 02 03 04"}},
            {{"key", "extended"}, {"name", "Extended Frame"}, {"type", "boolean"},
             {"required", false}, {"default", false}},
            {{"key", "remote"}, {"name", "Remote Frame"}, {"type", "boolean"},
             {"required", false}, {"default", false}},
            {{"key", "rxId"}, {"name", "Response CAN ID (0x00000000-0x1FFFFFFF)"}, {"type", "string"},
             {"required", true}, {"default", ""}},
            {{"key", "rxMask"}, {"name", "Response Mask (0x1FFFFFFF=Exact)"}, {"type", "string"},
             {"required", false}, {"default", "0x1FFFFFFF"}},
            {{"key", "timeoutMs"}, {"name", "Receive Timeout"}, {"type", "integer"},
             {"required", false}, {"default", 1500}, {"minimum", 1}, {"maximum", 60000}, {"unit", "ms"}}
        })},
        {"outputs", Json::array({
            {{"key", "transmitted"}, {"name", "Transmitted"}, {"type", "boolean"}},
            {{"key", "id"}, {"name", "Response CAN ID"}, {"type", "string"}},
            {{"key", "idNumeric"}, {"name", "Response CAN ID Numeric"}, {"type", "integer"}},
            {{"key", "dataHex"}, {"name", "Response Frame Data"}, {"type", "hex-bytes"}},
            {{"key", "dlc"}, {"name", "Response Data Length"}, {"type", "integer"}, {"unit", "byte"}},
            {{"key", "extended"}, {"name", "Response Extended Frame"}, {"type", "boolean"}},
            {{"key", "timestampUs"}, {"name", "Response Timestamp"}, {"type", "integer"}, {"unit", "us"}},
            {{"key", "txId"}, {"name", "Sent CAN ID"}, {"type", "string"}},
            {{"key", "txDataHex"}, {"name", "Sent Frame Data"}, {"type", "hex-bytes"}}
        })}
    };
}

} // namespace PicoATE::Plugins::Can

PICOATE_DEFINE_LOG_SINK()

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize)
{
    std::scoped_lock lock(PicoATE::Plugins::Can::g_mutex);
    return PicoATE::Plugin::executeJson(requestJsonUtf8,
                                        responseJsonUtf8,
                                        responseBufferSize,
                                        PicoATE::Plugins::Can::execute);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_Describe(
    char* descriptionJsonUtf8,
    int descriptionBufferSize)
{
    return PicoATE::Plugin::writeDescription(
        PicoATE::Plugins::Can::pluginDescription(),
        descriptionJsonUtf8,
        descriptionBufferSize);
}

extern "C" PICOATE_PLUGIN_EXPORT int PICOATE_PLUGIN_CALL PicoATE_GetAbiVersion()
{
    return PicoATE::Plugin::AbiVersion;
}
