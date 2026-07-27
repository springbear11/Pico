#include "CxCanAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace PicoATE::Plugins::Can {

namespace {

constexpr std::uint32_t StatusOk = 1;
constexpr std::uint32_t ReceiveError = 0xFFFFFFFFU;
constexpr int UsbCan1 = 3;
constexpr int UsbCan2 = 4;

struct ControlCanBoardInfo {
    std::uint16_t hardwareVersion;
    std::uint16_t firmwareVersion;
    std::uint16_t driverVersion;
    std::uint16_t interfaceVersion;
    std::uint16_t irqNumber;
    std::uint8_t channelCount;
    char serialNumber[20];
    char hardwareType[40];
    std::uint16_t reserved[4];
};

struct ControlCanObject {
    std::uint32_t id;
    std::uint32_t timestamp;
    std::uint8_t timeFlag;
    std::uint8_t sendType;
    std::uint8_t remoteFlag;
    std::uint8_t extendedFlag;
    std::uint8_t dataLength;
    std::uint8_t data[8];
    std::uint8_t reserved[3];
};

struct ControlCanErrorInfo {
    std::uint32_t errorCode;
    std::uint8_t passiveErrorData[3];
    std::uint8_t arbitrationLostData;
};

struct ControlCanInitConfig {
    std::uint32_t acceptanceCode;
    std::uint32_t acceptanceMask;
    std::uint32_t reserved;
    std::uint8_t filter;
    std::uint8_t timing0;
    std::uint8_t timing1;
    std::uint8_t mode;
};

static_assert(sizeof(ControlCanBoardInfo) == 80);
static_assert(sizeof(ControlCanObject) == 24);
static_assert(sizeof(ControlCanErrorInfo) == 8);
static_assert(sizeof(ControlCanInitConfig) == 16);

struct Timing {
    std::uint8_t timing0;
    std::uint8_t timing1;
};

bool timingForBitrate(int bitrate, Timing& timing)
{
    switch (bitrate) {
    case 10000: timing = {0x31, 0x1C}; return true;
    case 20000: timing = {0x18, 0x1C}; return true;
    case 50000: timing = {0x09, 0x1C}; return true;
    case 100000: timing = {0x04, 0x1C}; return true;
    case 125000: timing = {0x03, 0x1C}; return true;
    case 250000: timing = {0x01, 0x1C}; return true;
    case 500000: timing = {0x00, 0x1C}; return true;
    case 800000: timing = {0x00, 0x16}; return true;
    case 1000000: timing = {0x00, 0x14}; return true;
    default: return false;
    }
}

std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    const auto size = MultiByteToWideChar(CP_UTF8,
                                          MB_ERR_INVALID_CHARS,
                                          value.data(),
                                          static_cast<int>(value.size()),
                                          nullptr,
                                          0);
    if (size <= 0) {
        return {};
    }
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        output.data(),
                        size);
    return output;
}

std::string windowsError(DWORD code)
{
    wchar_t* message = nullptr;
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);
    if (!message || length == 0) {
        return std::format("Windows error {}", code);
    }
    const auto utf8Size = WideCharToMultiByte(
        CP_UTF8, 0, message, length, nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(utf8Size), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        message,
                        length,
                        output.data(),
                        utf8Size,
                        nullptr,
                        nullptr);
    LocalFree(message);
    while (!output.empty() &&
           (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

std::filesystem::path currentModuleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&currentModuleDirectory),
            &module)) {
        return std::filesystem::current_path();
    }
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(
        module, path.data(), static_cast<DWORD>(path.size()));
    return length > 0
        ? std::filesystem::path(path.data(), path.data() + length).parent_path()
        : std::filesystem::current_path();
}

std::string fixedString(const char* value, std::size_t size)
{
    const auto end = std::find(value, value + size, '\0');
    return std::string(value, end);
}

std::string errorCodeText(std::uint32_t code)
{
    if (code == 0) {
        return "OK";
    }
    const std::pair<std::uint32_t, const char*> names[] = {
        {0x0001, "ERR_CAN_OVERFLOW"}, {0x0002, "ERR_CAN_ERRALARM"},
        {0x0004, "ERR_CAN_PASSIVE"}, {0x0008, "ERR_CAN_LOSE"},
        {0x0010, "ERR_CAN_BUSERR"}, {0x0020, "ERR_CAN_REG_FULL"},
        {0x0040, "ERR_CAN_REG_OVER"}, {0x0080, "ERR_CAN_ACTIVE"},
        {0x0100, "ERR_DEVICEOPENED"}, {0x0200, "ERR_DEVICEOPEN"},
        {0x0400, "ERR_DEVICENOTOPEN"}, {0x0800, "ERR_BUFFEROVERFLOW"},
        {0x1000, "ERR_DEVICENOTEXIST"}, {0x2000, "ERR_LOADKERNELDLL"},
        {0x4000, "ERR_CMDFAILED"}, {0x8000, "ERR_BUFFERCREATE"},
    };
    std::string text;
    for (const auto& [mask, name] : names) {
        if ((code & mask) == 0) {
            continue;
        }
        if (!text.empty()) {
            text += '|';
        }
        text += name;
    }
    return text.empty() ? "UNKNOWN" : text;
}

} // namespace

Plugin::Json pluginDescription()
{
    using Json = Plugin::Json;
    return {
        {"name", "CX USB-CAN"},
        {"category", "CAN"},
        {"functions", Json::array({
            {
                {"id", "open"},
                {"name", "Open CAN"},
                {"description", "Open and initialize a CX ControlCAN channel"},
                {"timeoutMs", 5000},
                {"inputs", Json::array({
                    {{"key", "deviceType"}, {"name", "Device Type"}, {"type", "enum"},
                     {"required", false}, {"default", 4},
                     {"options", Json::array({
                         {{"label", "Auto"}, {"value", 0}},
                         {{"label", "USBCAN-I"}, {"value", 3}},
                         {{"label", "USBCAN-II / CANalyst-II"}, {"value", 4}}
                     })}},
                    {{"key", "channelIndex"}, {"name", "Channel"}, {"type", "integer"},
                     {"required", false}, {"default", 0}, {"minimum", 0}, {"maximum", 1}},
                    {{"key", "bitrate"}, {"name", "Bitrate"}, {"type", "enum"},
                     {"required", false}, {"default", 500000},
                     {"options", Json::array({
                         {{"label", "125 kbit/s"}, {"value", 125000}},
                         {{"label", "250 kbit/s"}, {"value", 250000}},
                         {{"label", "500 kbit/s"}, {"value", 500000}},
                         {{"label", "1 Mbit/s"}, {"value", 1000000}}
                     })}},
                    {{"key", "listenOnly"}, {"name", "Listen Only"}, {"type", "boolean"},
                     {"required", false}, {"default", false}},
                    {{"key", "selfTest"}, {"name", "Self Test"}, {"type", "boolean"},
                     {"required", false}, {"default", false}}
                })},
                {"outputs", Json::array({
                    {{"key", "connected"}, {"name", "Connected"}, {"type", "boolean"}},
                    {{"key", "device"}, {"name", "Device"}, {"type", "string"}}
                })}
            },
            {
                {"id", "write"},
                {"name", "Send CAN Frame"},
                {"description", "Transmit one classic CAN frame"},
                {"timeoutMs", 2000},
                {"inputs", Json::array({
                    {{"key", "id"}, {"name", "CAN ID (Std 0x000-0x7FF / Ext 0x00000000-0x1FFFFFFF)"}, {"type", "string"},
                     {"required", true}, {"default", "0x123"}},
                    {{"key", "data"}, {"name", "Frame Data"}, {"type", "hex-bytes"},
                     {"required", true}, {"default", "01 02 03 04"}},
                    {{"key", "extended"}, {"name", "Extended Frame"}, {"type", "boolean"},
                     {"required", false}, {"default", false}},
                    {{"key", "remote"}, {"name", "Remote Frame"}, {"type", "boolean"},
                     {"required", false}, {"default", false}}
                })},
                {"outputs", Json::array({
                    {{"key", "transmitted"}, {"name", "Transmitted"}, {"type", "boolean"}}
                })}
            },
            {
                {"id", "read"},
                {"name", "Read CAN Frame"},
                {"description", "Receive one CAN frame with an optional ID filter"},
                {"timeoutMs", 2500},
                {"inputs", Json::array({
                    {{"key", "filterId"}, {"name", "Filter ID (0x00000000-0x1FFFFFFF)"}, {"type", "string"},
                     {"required", false}, {"default", "0x000"}},
                    {{"key", "filterMask"}, {"name", "Filter Mask (0=Any, Std Exact=0x7FF, Ext Exact=0x1FFFFFFF)"}, {"type", "string"},
                     {"required", false}, {"default", "0x000"}},
                    {{"key", "timeoutMs"}, {"name", "Receive Timeout"}, {"type", "integer"},
                     {"required", false}, {"default", 1500}, {"minimum", 0},
                     {"maximum", 60000}, {"unit", "ms"}}
                })},
                {"outputs", Json::array({
                    {{"key", "id"}, {"name", "CAN ID"}, {"type", "string"}},
                    {{"key", "idNumeric"}, {"name", "CAN ID Numeric"}, {"type", "integer"}},
                    {{"key", "dataHex"}, {"name", "Frame Data"}, {"type", "hex-bytes"}},
                    {{"key", "dlc"}, {"name", "Data Length"}, {"type", "integer"}, {"unit", "byte"}}
                })}
            },
            {
                {"id", "close"},
                {"name", "Close CAN"},
                {"description", "Close the active CX CAN channel"},
                {"stepKind", "cleanup"},
                {"timeoutMs", 3000},
                {"inputs", Json::array()},
                {"outputs", Json::array({
                    {{"key", "connected"}, {"name", "Connected"}, {"type", "boolean"}}
                })}
            }
        })}
    };
}

class CxCanAdapter::Impl
{
public:
    using OpenDevice = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using CloseDevice = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t);
    using InitCan = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t,
                                           ControlCanInitConfig*);
    using ReadBoardInfo = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t,
                                                 ControlCanBoardInfo*);
    using ReadErrorInfo = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t,
                                                 std::uint32_t, ControlCanErrorInfo*);
    using ClearBuffer = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using StartCan = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using ResetCan = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using Transmit = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t,
                                            ControlCanObject*, std::uint32_t);
    using Receive = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t,
                                           ControlCanObject*, std::uint32_t, int);

    struct ChannelState {
        bool opened = false;
        OpenOptions options;
    };

    struct DeviceState {
        int deviceType = 0;
        int deviceIndex = 0;
        int channelCount = 0;
        std::array<ChannelState, 2> channels;
        std::string description;
    };

    ~Impl()
    {
        closeAll();
        unload();
    }

    template<typename Function>
    bool resolve(Function& function, const char* name, std::string& errorMessage)
    {
        function = reinterpret_cast<Function>(GetProcAddress(library, name));
        PicoATE_Log("VENDOR CX GetProcAddress symbol={} result={}",
                    name,
                    function ? "FOUND" : "MISSING");
        if (function) {
            return true;
        }
        errorMessage = std::string("ControlCAN.dll does not export ") + name;
        return false;
    }

    OperationResult load(const std::string& requestedPath)
    {
        if (library) {
            return OperationResult::passed();
        }
        const auto path = requestedPath.empty()
            ? currentModuleDirectory() / L"ControlCAN.dll"
            : std::filesystem::path(utf8ToWide(requestedPath));
        if (path.empty()) {
            return OperationResult::failed(
                "InvalidLibraryPath", "libraryPath is not valid UTF-8");
        }
        PicoATE_Log("VENDOR CX LoadLibraryW path={}", path.string());
        library = LoadLibraryW(path.c_str());
        if (!library) {
            PicoATE_Log("VENDOR CX LoadLibraryW result=NULL winError={}", GetLastError());
            return OperationResult::failed(
                "VendorDllLoadFailed",
                std::format("Failed to load {}: {}", path.string(), windowsError(GetLastError())));
        }
        PicoATE_Log("VENDOR CX LoadLibraryW result=OK library=ControlCAN.dll");

        std::string errorMessage;
        if (!(resolve(openDevice, "VCI_OpenDevice", errorMessage) &&
              resolve(closeDevice, "VCI_CloseDevice", errorMessage) &&
              resolve(initCan, "VCI_InitCAN", errorMessage) &&
              resolve(readBoardInfo, "VCI_ReadBoardInfo", errorMessage) &&
              resolve(readErrorInfo, "VCI_ReadErrInfo", errorMessage) &&
              resolve(clearBuffer, "VCI_ClearBuffer", errorMessage) &&
              resolve(startCan, "VCI_StartCAN", errorMessage) &&
              resolve(resetCan, "VCI_ResetCAN", errorMessage) &&
              resolve(transmit, "VCI_Transmit", errorMessage) &&
              resolve(receive, "VCI_Receive", errorMessage))) {
            unload();
            return OperationResult::failed("VendorSymbolMissing", errorMessage);
        }
        return OperationResult::passed();
    }

    void unload() noexcept
    {
        if (library) {
            PicoATE_Log("VENDOR CX FreeLibrary library=ControlCAN.dll");
            FreeLibrary(library);
            library = nullptr;
        }
    }

    DeviceState* findDevice(int deviceIndex)
    {
        const auto it = devices.find(deviceIndex);
        return it == devices.end() ? nullptr : &it->second;
    }

    const DeviceState* findDevice(int deviceIndex) const
    {
        const auto it = devices.find(deviceIndex);
        return it == devices.end() ? nullptr : &it->second;
    }

    static bool anyChannelOpen(const DeviceState& device)
    {
        return std::any_of(device.channels.begin(), device.channels.end(),
                           [](const ChannelState& channel) { return channel.opened; });
    }

    std::string errorDetails(int deviceType, int deviceIndex, int channelIndex)
    {
        if (!readErrorInfo) {
            return {};
        }
        ControlCanErrorInfo info{};
        PicoATE_Log("VENDOR CX VCI_ReadErrInfo(type={}, device={}, channel={})",
                    deviceType, deviceIndex, channelIndex);
        const auto status = readErrorInfo(deviceType, deviceIndex, channelIndex, &info);
        PicoATE_Log("VENDOR CX VCI_ReadErrInfo return={} errorCode=0x{:X} ({}) passive=[0x{:02X},0x{:02X},0x{:02X}] arbitrationLost=0x{:02X}",
                    status,
                    info.errorCode,
                    errorCodeText(info.errorCode),
                    info.passiveErrorData[0],
                    info.passiveErrorData[1],
                    info.passiveErrorData[2],
                    info.arbitrationLostData);
        if (status != StatusOk) {
            return {};
        }
        return std::format(" (ControlCAN error=0x{:X} {})",
                           info.errorCode,
                           errorCodeText(info.errorCode));
    }

    void closeAll() noexcept
    {
        try {
            for (auto& [deviceIndex, device] : devices) {
                for (int channelIndex = 0; channelIndex < 2; ++channelIndex) {
                    if (!device.channels[static_cast<std::size_t>(channelIndex)].opened ||
                        !resetCan) {
                        continue;
                    }
                    PicoATE_Log("VENDOR CX VCI_ResetCAN(type={}, device={}, channel={}) during shutdown",
                                device.deviceType, deviceIndex, channelIndex);
                    const auto status = resetCan(device.deviceType, deviceIndex, channelIndex);
                    PicoATE_Log("VENDOR CX VCI_ResetCAN return={}", status);
                }
                if (closeDevice) {
                    PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={}) during shutdown",
                                device.deviceType, deviceIndex);
                    const auto status = closeDevice(device.deviceType, deviceIndex);
                    PicoATE_Log("VENDOR CX VCI_CloseDevice return={}", status);
                }
            }
            devices.clear();
        } catch (...) {
        }
    }

    HMODULE library = nullptr;
    OpenDevice openDevice = nullptr;
    CloseDevice closeDevice = nullptr;
    InitCan initCan = nullptr;
    ReadBoardInfo readBoardInfo = nullptr;
    ReadErrorInfo readErrorInfo = nullptr;
    ClearBuffer clearBuffer = nullptr;
    StartCan startCan = nullptr;
    ResetCan resetCan = nullptr;
    Transmit transmit = nullptr;
    Receive receive = nullptr;
    std::map<int, DeviceState> devices;
};

CxCanAdapter::CxCanAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

CxCanAdapter::~CxCanAdapter() = default;

DiscoveryResult CxCanAdapter::findDevices(const DiscoveryOptions& options)
{
    DiscoveryResult result;
    result.status = m_impl->load(options.libraryPath);
    if (!result.status.success) {
        return result;
    }
    if (options.deviceType != 0 && options.deviceType != UsbCan1 &&
        options.deviceType != UsbCan2) {
        result.status = OperationResult::failed(
            "InvalidDeviceType", "deviceType must be 0, 3 (USBCAN-I), or 4 (USBCAN-II)");
        return result;
    }

    const auto deviceTypes = options.deviceType == 0
        ? std::vector<int>{UsbCan2, UsbCan1}
        : std::vector<int>{options.deviceType};
    for (int deviceIndex = 0; deviceIndex <= options.maximumDeviceIndex; ++deviceIndex) {
        for (const auto deviceType : deviceTypes) {
            PicoATE_Log("VENDOR CX VCI_OpenDevice(type={}, device={}, reserved=0) for discovery",
                        deviceType, deviceIndex);
            const auto openStatus = m_impl->openDevice(deviceType, deviceIndex, 0);
            PicoATE_Log("VENDOR CX VCI_OpenDevice discovery return={}", openStatus);
            if (openStatus != StatusOk) {
                continue;
            }

            ControlCanBoardInfo board{};
            PicoATE_Log("VENDOR CX VCI_ReadBoardInfo(type={}, device={}) for discovery",
                        deviceType, deviceIndex);
            const auto boardStatus = m_impl->readBoardInfo(deviceType, deviceIndex, &board);
            PicoATE_Log("VENDOR CX VCI_ReadBoardInfo discovery return={}", boardStatus);
            if (boardStatus == StatusOk) {
                const auto serial = fixedString(board.serialNumber, sizeof(board.serialNumber));
                const auto model = fixedString(board.hardwareType, sizeof(board.hardwareType));
                const auto channels = board.channelCount >= '0' && board.channelCount <= '9'
                    ? board.channelCount - '0' : board.channelCount;
                PicoATE_Log("VENDOR CX discovered index={} serial={} model={} channels={}",
                            deviceIndex, serial, model, channels);
                if (!serial.empty()) {
                    result.devices.push_back(
                        {serial, model, deviceType, deviceIndex, channels});
                }
            }
            PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={}) after discovery",
                        deviceType, deviceIndex);
            const auto closeStatus = m_impl->closeDevice(deviceType, deviceIndex);
            PicoATE_Log("VENDOR CX VCI_CloseDevice discovery return={}", closeStatus);
            break;
        }
    }
    result.status = OperationResult::passed();
    return result;
}

OperationResult CxCanAdapter::open(const OpenOptions& options)
{
    if (options.canFd) {
        return OperationResult::failed(
            "CanFdNotSupported", "ControlCAN.dll supports classic CAN only; set canFd=false");
    }
    if (options.listenOnly && options.selfTest) {
        return OperationResult::failed(
            "InvalidCanMode", "listenOnly and selfTest cannot both be true");
    }
    if (options.deviceIndex < 0 || options.channelIndex < 0 || options.channelIndex > 1) {
        return OperationResult::failed(
            "InvalidCanAddress", "deviceIndex must be >= 0 and channelIndex must be 0 or 1");
    }

    Timing timing{};
    if (!timingForBitrate(options.arbitrationBitrate, timing)) {
        return OperationResult::failed(
            "UnsupportedBitrate",
            std::format("Unsupported CX bitrate: {}", options.arbitrationBitrate));
    }
    auto loadResult = m_impl->load(options.libraryPath);
    if (!loadResult.success) {
        return loadResult;
    }

    if (options.deviceType != 0 && options.deviceType != UsbCan1 &&
        options.deviceType != UsbCan2) {
        return OperationResult::failed(
            "InvalidDeviceType", "deviceType must be 0, 3 (USBCAN-I), or 4 (USBCAN-II)");
    }

    auto* device = m_impl->findDevice(options.deviceIndex);
    if (!device) {
        const auto deviceTypes = options.deviceType == 0
            ? std::vector<int>{UsbCan2, UsbCan1}
            : std::vector<int>{options.deviceType};
        std::string lastError;
        for (const auto deviceType : deviceTypes) {
            PicoATE_Log("VENDOR CX VCI_OpenDevice(type={}, device={}, reserved=0)",
                        deviceType, options.deviceIndex);
            const auto status = m_impl->openDevice(deviceType, options.deviceIndex, 0);
            PicoATE_Log("VENDOR CX VCI_OpenDevice return={}", status);
            if (status != StatusOk) {
                lastError = "ControlCAN VCI_OpenDevice failed";
                continue;
            }

            Impl::DeviceState state;
            state.deviceType = deviceType;
            state.deviceIndex = options.deviceIndex;
            state.description = deviceType == UsbCan2
                ? "CX ControlCAN USBCAN-II"
                : "CX ControlCAN USBCAN-I";

            ControlCanBoardInfo board{};
            PicoATE_Log("VENDOR CX VCI_ReadBoardInfo(type={}, device={})",
                        deviceType, options.deviceIndex);
            const auto boardStatus = m_impl->readBoardInfo(deviceType,
                                                            options.deviceIndex,
                                                            &board);
            PicoATE_Log("VENDOR CX VCI_ReadBoardInfo return={}", boardStatus);
            if (boardStatus == StatusOk) {
                state.channelCount = board.channelCount >= '0' && board.channelCount <= '9'
                    ? board.channelCount - '0'
                    : board.channelCount;
                state.description = std::format(
                    "CX ControlCAN type={} {} SN={} channels={}",
                    deviceType,
                    fixedString(board.hardwareType, sizeof(board.hardwareType)),
                    fixedString(board.serialNumber, sizeof(board.serialNumber)),
                    state.channelCount);
                PicoATE_Log("VENDOR CX board hardware={} serial={} channels={}",
                            fixedString(board.hardwareType, sizeof(board.hardwareType)),
                            fixedString(board.serialNumber, sizeof(board.serialNumber)),
                            state.channelCount);
            }
            m_impl->devices.emplace(options.deviceIndex, std::move(state));
            device = m_impl->findDevice(options.deviceIndex);
            break;
        }
        if (!device) {
            return OperationResult::failed(
                "CanOpenFailed",
                lastError.empty()
                    ? std::format("ControlCAN VCI_OpenDevice failed for deviceIndex={}",
                                  options.deviceIndex)
                    : lastError);
        }
    }

    if (options.deviceType != 0 && options.deviceType != device->deviceType) {
        return OperationResult::failed(
            "DeviceTypeConflict",
            std::format("CX device {} is already open as type {}, requested type {}",
                        options.deviceIndex, device->deviceType, options.deviceType));
    }
    if (device->channelCount > 0 && options.channelIndex >= device->channelCount) {
        const auto channelCount = device->channelCount;
        if (!Impl::anyChannelOpen(*device)) {
            const auto deviceType = device->deviceType;
            PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={}) after unavailable channel request",
                        deviceType, options.deviceIndex);
            const auto status = m_impl->closeDevice(deviceType, options.deviceIndex);
            PicoATE_Log("VENDOR CX VCI_CloseDevice return={}", status);
            m_impl->devices.erase(options.deviceIndex);
        }
        return OperationResult::failed(
            "ChannelNotAvailable",
            std::format("CX device reports {} channel(s); channel {} is unavailable",
                        channelCount, options.channelIndex));
    }

    auto& channel = device->channels[static_cast<std::size_t>(options.channelIndex)];
    if (channel.opened) {
        if (channel.options.arbitrationBitrate != options.arbitrationBitrate ||
            channel.options.listenOnly != options.listenOnly ||
            channel.options.selfTest != options.selfTest) {
            return OperationResult::failed(
                "ChannelConfigurationConflict",
                std::format("CX device {} channel {} is already configured at {} bit/s",
                            options.deviceIndex,
                            options.channelIndex,
                            channel.options.arbitrationBitrate));
        }
        PicoATE_Log("VENDOR CX channel already initialized device={} channel={} bitrate={}",
                    options.deviceIndex,
                    options.channelIndex,
                    options.arbitrationBitrate);
        return OperationResult::passed();
    }

    ControlCanInitConfig config{};
    config.acceptanceCode = 0;
    config.acceptanceMask = 0xFFFFFFFFU;
    config.filter = 0;
    config.timing0 = timing.timing0;
    config.timing1 = timing.timing1;
    config.mode = options.selfTest ? 2 : (options.listenOnly ? 1 : 0);

    PicoATE_Log("VENDOR CX VCI_InitCAN(type={}, device={}, channel={}, accCode=0x{:08X}, accMask=0x{:08X}, filter={}, timing0=0x{:02X}, timing1=0x{:02X}, mode={})",
                device->deviceType,
                options.deviceIndex,
                options.channelIndex,
                config.acceptanceCode,
                config.acceptanceMask,
                config.filter,
                config.timing0,
                config.timing1,
                config.mode);
    const auto initStatus = m_impl->initCan(device->deviceType,
                                             options.deviceIndex,
                                             options.channelIndex,
                                             &config);
    PicoATE_Log("VENDOR CX VCI_InitCAN return={}", initStatus);
    if (initStatus != StatusOk) {
        const auto details = m_impl->errorDetails(device->deviceType,
                                                  options.deviceIndex,
                                                  options.channelIndex);
        if (!Impl::anyChannelOpen(*device)) {
            PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={}) after InitCAN failure",
                        device->deviceType, options.deviceIndex);
            const auto closeStatus = m_impl->closeDevice(device->deviceType,
                                                         options.deviceIndex);
            PicoATE_Log("VENDOR CX VCI_CloseDevice return={}", closeStatus);
            m_impl->devices.erase(options.deviceIndex);
        }
        return OperationResult::failed("CanInitFailed",
                                       "ControlCAN VCI_InitCAN failed" + details);
    }

    PicoATE_Log("VENDOR CX VCI_ClearBuffer(type={}, device={}, channel={})",
                device->deviceType, options.deviceIndex, options.channelIndex);
    const auto clearStatus = m_impl->clearBuffer(device->deviceType,
                                                  options.deviceIndex,
                                                  options.channelIndex);
    PicoATE_Log("VENDOR CX VCI_ClearBuffer return={}", clearStatus);
    if (clearStatus != StatusOk) {
        const auto details = m_impl->errorDetails(device->deviceType,
                                                  options.deviceIndex,
                                                  options.channelIndex);
        PicoATE_Log("VENDOR CX VCI_ResetCAN(type={}, device={}, channel={}) after ClearBuffer failure",
                    device->deviceType, options.deviceIndex, options.channelIndex);
        const auto resetStatus = m_impl->resetCan(device->deviceType,
                                                   options.deviceIndex,
                                                   options.channelIndex);
        PicoATE_Log("VENDOR CX VCI_ResetCAN return={}", resetStatus);
        if (!Impl::anyChannelOpen(*device)) {
            const auto deviceType = device->deviceType;
            PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={}) after ClearBuffer failure",
                        deviceType, options.deviceIndex);
            const auto closeStatus = m_impl->closeDevice(deviceType, options.deviceIndex);
            PicoATE_Log("VENDOR CX VCI_CloseDevice return={}", closeStatus);
            m_impl->devices.erase(options.deviceIndex);
        }
        return OperationResult::failed("CanClearBufferFailed",
                                       "ControlCAN VCI_ClearBuffer failed" + details);
    }

    PicoATE_Log("VENDOR CX VCI_StartCAN(type={}, device={}, channel={})",
                device->deviceType, options.deviceIndex, options.channelIndex);
    const auto startStatus = m_impl->startCan(device->deviceType,
                                               options.deviceIndex,
                                               options.channelIndex);
    PicoATE_Log("VENDOR CX VCI_StartCAN return={}", startStatus);
    if (startStatus != StatusOk) {
        const auto details = m_impl->errorDetails(device->deviceType,
                                                  options.deviceIndex,
                                                  options.channelIndex);
        PicoATE_Log("VENDOR CX VCI_ResetCAN(type={}, device={}, channel={}) after StartCAN failure",
                    device->deviceType, options.deviceIndex, options.channelIndex);
        const auto resetStatus = m_impl->resetCan(device->deviceType,
                                                   options.deviceIndex,
                                                   options.channelIndex);
        PicoATE_Log("VENDOR CX VCI_ResetCAN return={}", resetStatus);
        if (!Impl::anyChannelOpen(*device)) {
            const auto deviceType = device->deviceType;
            PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={}) after StartCAN failure",
                        deviceType, options.deviceIndex);
            const auto closeStatus = m_impl->closeDevice(deviceType, options.deviceIndex);
            PicoATE_Log("VENDOR CX VCI_CloseDevice return={}", closeStatus);
            m_impl->devices.erase(options.deviceIndex);
        }
        return OperationResult::failed("CanStartFailed",
                                       "ControlCAN VCI_StartCAN failed" + details);
    }

    channel.opened = true;
    channel.options = options;
    channel.options.deviceType = device->deviceType;
    PicoATE_Log("VENDOR CX channel ready device={} channel={} bitrate={}",
                options.deviceIndex, options.channelIndex, options.arbitrationBitrate);
    return OperationResult::passed();
}

OperationResult CxCanAdapter::close(const OpenOptions& options)
{
    auto* device = m_impl->findDevice(options.deviceIndex);
    if (!device || options.channelIndex < 0 || options.channelIndex > 1) {
        PicoATE_Log("VENDOR CX close ignored device={} channel={} state=not-open",
                    options.deviceIndex, options.channelIndex);
        return OperationResult::passed();
    }
    auto& channel = device->channels[static_cast<std::size_t>(options.channelIndex)];
    OperationResult result = OperationResult::passed();
    if (channel.opened) {
        PicoATE_Log("VENDOR CX VCI_ResetCAN(type={}, device={}, channel={})",
                    device->deviceType, options.deviceIndex, options.channelIndex);
        const auto status = m_impl->resetCan(device->deviceType,
                                              options.deviceIndex,
                                              options.channelIndex);
        PicoATE_Log("VENDOR CX VCI_ResetCAN return={}", status);
        if (status != StatusOk) {
            result = OperationResult::failed(
                "CanResetFailed",
                "ControlCAN VCI_ResetCAN failed" +
                    m_impl->errorDetails(device->deviceType,
                                         options.deviceIndex,
                                         options.channelIndex));
        }
        channel.opened = false;
    }

    if (!Impl::anyChannelOpen(*device)) {
        const auto deviceType = device->deviceType;
        PicoATE_Log("VENDOR CX VCI_CloseDevice(type={}, device={})",
                    deviceType, options.deviceIndex);
        const auto status = m_impl->closeDevice(deviceType, options.deviceIndex);
        PicoATE_Log("VENDOR CX VCI_CloseDevice return={}", status);
        if (status != StatusOk && result.success) {
            result = OperationResult::failed("CanCloseFailed",
                                             "ControlCAN VCI_CloseDevice failed");
        }
        m_impl->devices.erase(options.deviceIndex);
    }
    return result;
}

bool CxCanAdapter::isOpen(const OpenOptions& options) const noexcept
{
    const auto* device = m_impl->findDevice(options.deviceIndex);
    return device && options.channelIndex >= 0 && options.channelIndex < 2 &&
        device->channels[static_cast<std::size_t>(options.channelIndex)].opened;
}

std::string CxCanAdapter::deviceDescription(const OpenOptions& options) const
{
    const auto* device = m_impl->findDevice(options.deviceIndex);
    if (!device || options.channelIndex < 0 || options.channelIndex > 1) {
        return "Disconnected";
    }
    const auto& channel = device->channels[static_cast<std::size_t>(options.channelIndex)];
    if (!channel.opened) {
        return "Disconnected";
    }
    return std::format("{} device={} channel={} bitrate={}",
                       device->description,
                       options.deviceIndex,
                       options.channelIndex,
                       channel.options.arbitrationBitrate);
}

OperationResult CxCanAdapter::transmit(const OpenOptions& options,
                                       const Frame& frame)
{
    auto* device = m_impl->findDevice(options.deviceIndex);
    if (!device || options.channelIndex < 0 || options.channelIndex > 1 ||
        !device->channels[static_cast<std::size_t>(options.channelIndex)].opened) {
        return OperationResult::failed("CanNotOpen", "CAN channel is not open");
    }
    if (frame.data.size() > 8) {
        return OperationResult::failed(
            "InvalidCanFrame", "Classic CAN data exceeds 8 bytes");
    }
    ControlCanObject object{};
    object.id = frame.id;
    object.sendType = 0;
    object.remoteFlag = frame.remote ? 1 : 0;
    object.extendedFlag = frame.extended ? 1 : 0;
    object.dataLength = static_cast<std::uint8_t>(frame.data.size());
    std::copy(frame.data.begin(), frame.data.end(), object.data);
    PicoATE_Log("VENDOR CX VCI_Transmit(type={}, device={}, channel={}, id=0x{:X}, sendType={}, remote={}, extended={}, dlc={})",
                device->deviceType,
                options.deviceIndex,
                options.channelIndex,
                object.id,
                object.sendType,
                object.remoteFlag,
                object.extendedFlag,
                object.dataLength);
    const auto sent = m_impl->transmit(device->deviceType,
                                       options.deviceIndex,
                                       options.channelIndex,
                                       &object,
                                       1);
    PicoATE_Log("VENDOR CX VCI_Transmit return={}", sent);
    if (sent != 1) {
        return OperationResult::failed(
            "CanTransmitFailed",
            "ControlCAN VCI_Transmit failed" +
                m_impl->errorDetails(device->deviceType,
                                     options.deviceIndex,
                                     options.channelIndex));
    }
    return OperationResult::passed();
}

ReceiveResult CxCanAdapter::receive(const OpenOptions& options,
                                    std::uint32_t filterId,
                                    std::uint32_t filterMask,
                                    int timeoutMs)
{
    auto* device = m_impl->findDevice(options.deviceIndex);
    if (!device || options.channelIndex < 0 || options.channelIndex > 1 ||
        !device->channels[static_cast<std::size_t>(options.channelIndex)].opened) {
        return {ReceiveStatus::Error, {}, "CanNotOpen", "CAN channel is not open"};
    }
    const auto started = std::chrono::steady_clock::now();
    do {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto remaining = std::max(0, timeoutMs - static_cast<int>(elapsed));
        ControlCanObject object{};
        PicoATE_Log("VENDOR CX VCI_Receive(type={}, device={}, channel={}, length=1, waitMs={})",
                    device->deviceType,
                    options.deviceIndex,
                    options.channelIndex,
                    remaining);
        const auto count = m_impl->receive(device->deviceType,
                                           options.deviceIndex,
                                           options.channelIndex,
                                           &object,
                                           1,
                                           remaining);
        PicoATE_Log("VENDOR CX VCI_Receive return={}", count);
        if (count == ReceiveError) {
            return {ReceiveStatus::Error,
                    {},
                    "CanReceiveFailed",
                    "ControlCAN VCI_Receive failed" +
                        m_impl->errorDetails(device->deviceType,
                                             options.deviceIndex,
                                             options.channelIndex)};
        }
        if (count > 0) {
            PicoATE_Log("VENDOR CX VCI_Receive frame id=0x{:X} remote={} extended={} dlc={} timestamp={} timeFlag={}",
                        object.id,
                        object.remoteFlag,
                        object.extendedFlag,
                        object.dataLength,
                        object.timestamp,
                        object.timeFlag);
            if ((object.id & filterMask) != (filterId & filterMask)) {
                PicoATE_Log("VENDOR CX VCI_Receive frame filtered id=0x{:X} expected=0x{:X} mask=0x{:X}",
                            object.id, filterId, filterMask);
                continue;
            }
            if (object.dataLength > sizeof(object.data)) {
                return {ReceiveStatus::Error,
                        {},
                        "InvalidReceivedFrame",
                        std::format("ControlCAN returned invalid data length: {}",
                                    object.dataLength)};
            }
            Frame frame;
            frame.id = object.id;
            frame.data.assign(object.data, object.data + object.dataLength);
            frame.extended = object.extendedFlag != 0;
            frame.remote = object.remoteFlag != 0;
            frame.timestampUs = object.timeFlag != 0
                ? static_cast<std::uint64_t>(object.timestamp) * 100ULL
                : 0;
            return {ReceiveStatus::Received, std::move(frame), {}, {}};
        }
        if (remaining > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(std::min(20, remaining)));
        }
    } while (std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - started).count() < timeoutMs);
    return {ReceiveStatus::Timeout,
            {},
            "CanReceiveTimeout",
            "No matching CAN frame received"};
}

std::unique_ptr<ICanAdapter> createCanAdapter()
{
    return std::make_unique<CxCanAdapter>();
}

} // namespace PicoATE::Plugins::Can
