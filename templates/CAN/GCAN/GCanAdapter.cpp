#include "GCanAdapter.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PicoATE::Plugins::Can {

namespace {

constexpr std::uint32_t StatusOk = 1;
constexpr int UsbCan1 = 3;
constexpr int UsbCan2 = 4;

struct GCanBoardInfo {
    std::uint16_t hardwareVersion;
    std::uint16_t firmwareVersion;
    std::uint16_t driverVersion;
    std::uint16_t interfaceVersion;
    std::uint16_t irqNumber;
    std::uint8_t canChannelCount;
    char serialNumber[20];
    char hardwareType[40];
    std::uint16_t reserved[4];
};

struct GCanObject {
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

struct GCanErrorInfo {
    std::uint32_t errorCode;
    std::uint8_t passiveErrorData[3];
    std::uint8_t arbitrationLostData;
};

struct GCanInitConfig {
    std::uint32_t acceptanceCode;
    std::uint32_t acceptanceMask;
    std::uint32_t reserved;
    std::uint8_t filter;
    std::uint8_t timing0;
    std::uint8_t timing1;
    std::uint8_t mode;
};

static_assert(sizeof(GCanBoardInfo) == 80);
static_assert(sizeof(GCanObject) == 24);
static_assert(sizeof(GCanErrorInfo) == 8);
static_assert(sizeof(GCanInitConfig) == 16);

struct Timing {
    std::uint8_t timing0;
    std::uint8_t timing1;
};

bool timingForBitrate(int bitrate, Timing& timing)
{
    switch (bitrate) {
    case 5000: timing = {0xBF, 0xFF}; return true;
    case 10000: timing = {0x31, 0x1C}; return true;
    case 20000: timing = {0x18, 0x1C}; return true;
    case 40000: timing = {0x87, 0xFF}; return true;
    case 50000: timing = {0x09, 0x1C}; return true;
    case 80000: timing = {0x83, 0xFF}; return true;
    case 100000: timing = {0x04, 0x1C}; return true;
    case 125000: timing = {0x03, 0x1C}; return true;
    case 200000: timing = {0x81, 0xFA}; return true;
    case 250000: timing = {0x01, 0x1C}; return true;
    case 400000: timing = {0x80, 0xFA}; return true;
    case 500000: timing = {0x00, 0x1C}; return true;
    case 666000: timing = {0x80, 0xB6}; return true;
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
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.data(), static_cast<int>(value.size()),
                                          nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        output.data(), size);
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
    const auto utf8Size = WideCharToMultiByte(CP_UTF8, 0, message, length, nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(utf8Size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, message, length, output.data(), utf8Size, nullptr, nullptr);
    LocalFree(message);
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
        output.pop_back();
    }
    return output;
}

std::filesystem::path currentModuleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&currentModuleDirectory),
            &module)) {
        return std::filesystem::current_path();
    }
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    return length > 0 ? std::filesystem::path(path.data(), path.data() + length).parent_path()
                      : std::filesystem::current_path();
}

std::string fixedString(const char* value, std::size_t size)
{
    const auto end = std::find(value, value + size, '\0');
    return std::string(value, end);
}

} // namespace

class GCanAdapter::Impl
{
public:
    using OpenDevice = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using CloseDevice = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t);
    using InitCan = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t, GCanInitConfig*);
    using ReadBoardInfo = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, GCanBoardInfo*);
    using ReadErrorInfo = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t, GCanErrorInfo*);
    using ClearBuffer = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using StartCan = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using ResetCan = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t);
    using Transmit = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t, GCanObject*, std::uint32_t);
    using Receive = std::uint32_t(WINAPI*)(std::uint32_t, std::uint32_t, std::uint32_t, GCanObject*, std::uint32_t, int);

    ~Impl()
    {
        close();
        unload();
    }

    template<typename Function>
    bool resolve(Function& function, const char* name, std::string& errorMessage)
    {
        function = reinterpret_cast<Function>(GetProcAddress(library, name));
        if (function) {
            return true;
        }
        errorMessage = std::string("ECanVci64.dll does not export ") + name;
        return false;
    }

    OperationResult load(const std::string& requestedPath)
    {
        if (library) {
            return OperationResult::passed();
        }
        auto path = requestedPath.empty()
            ? currentModuleDirectory() / L"ECanVci64.dll"
            : std::filesystem::path(utf8ToWide(requestedPath));
        if (path.empty()) {
            return OperationResult::failed("InvalidLibraryPath", "libraryPath is not valid UTF-8");
        }
        const auto companionPath = path.parent_path() / L"CHUSBDLL64.dll";
        companion = LoadLibraryW(companionPath.c_str());
        if (!companion) {
            return OperationResult::failed(
                "CompanionDllLoadFailed",
                std::format("Failed to load CHUSBDLL64.dll: {}", windowsError(GetLastError())));
        }
        library = LoadLibraryW(path.c_str());
        if (!library) {
            const auto message = windowsError(GetLastError());
            unload();
            return OperationResult::failed(
                "VendorDllLoadFailed",
                std::format("Failed to load ECanVci64.dll: {}", message));
        }

        std::string errorMessage;
        if (!(resolve(openDevice, "OpenDevice", errorMessage) &&
              resolve(closeDevice, "CloseDevice", errorMessage) &&
              resolve(initCan, "InitCAN", errorMessage) &&
              resolve(readBoardInfo, "ReadBoardInfo", errorMessage) &&
              resolve(readErrorInfo, "ReadErrInfo", errorMessage) &&
              resolve(clearBuffer, "ClearBuffer", errorMessage) &&
              resolve(startCan, "StartCAN", errorMessage) &&
              resolve(resetCan, "ResetCAN", errorMessage) &&
              resolve(transmit, "Transmit", errorMessage) &&
              resolve(receive, "Receive", errorMessage))) {
            unload();
            return OperationResult::failed("VendorSymbolMissing", errorMessage);
        }
        return OperationResult::passed();
    }

    void unload() noexcept
    {
        if (library) {
            FreeLibrary(library);
            library = nullptr;
        }
        if (companion) {
            FreeLibrary(companion);
            companion = nullptr;
        }
    }

    void close() noexcept
    {
        if (opened && resetCan && closeDevice) {
            resetCan(options.deviceType, options.deviceIndex, options.channelIndex);
            closeDevice(options.deviceType, options.deviceIndex);
        }
        opened = false;
        description.clear();
    }

    std::string errorDetails() const
    {
        if (!opened || !readErrorInfo) {
            return {};
        }
        GCanErrorInfo info{};
        if (readErrorInfo(options.deviceType,
                          options.deviceIndex,
                          options.channelIndex,
                          &info) != StatusOk) {
            return {};
        }
        return std::format(" (GCAN error=0x{:X})", info.errorCode);
    }

    HMODULE companion = nullptr;
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
    bool opened = false;
    OpenOptions options;
    std::string description;
};

GCanAdapter::GCanAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

GCanAdapter::~GCanAdapter() = default;

OperationResult GCanAdapter::open(const OpenOptions& options)
{
    if (m_impl->opened) {
        return OperationResult::passed();
    }
    if (options.canFd) {
        return OperationResult::failed(
            "CanFdNotSupported", "ECanVci64.dll supports classic CAN only; set canFd=false");
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
            std::format("Unsupported GCAN bitrate: {}", options.arbitrationBitrate));
    }

    auto loadResult = m_impl->load(options.libraryPath);
    if (!loadResult.success) {
        return loadResult;
    }

    std::vector<int> deviceTypes;
    if (options.deviceType == 0) {
        deviceTypes = {UsbCan2, UsbCan1};
    } else if (options.deviceType == UsbCan1 || options.deviceType == UsbCan2) {
        deviceTypes = {options.deviceType};
    } else {
        m_impl->unload();
        return OperationResult::failed(
            "InvalidDeviceType", "deviceType must be 0 (auto), 3 (USBCAN-I), or 4 (USBCAN-II)");
    }

    std::string lastError;
    for (const auto deviceType : deviceTypes) {
        auto selected = options;
        selected.deviceType = deviceType;
        if (m_impl->openDevice(deviceType, selected.deviceIndex, 0) != StatusOk) {
            continue;
        }

        GCanInitConfig config{};
        config.acceptanceCode = 0;
        config.acceptanceMask = 0xFFFFFFFFU;
        config.filter = 0;
        config.timing0 = timing.timing0;
        config.timing1 = timing.timing1;
        config.mode = selected.selfTest ? 2 : (selected.listenOnly ? 1 : 0);

        if (m_impl->initCan(deviceType,
                            selected.deviceIndex,
                            selected.channelIndex,
                            &config) != StatusOk) {
            lastError = "GCAN InitCAN failed";
            m_impl->closeDevice(deviceType, selected.deviceIndex);
            continue;
        }
        m_impl->clearBuffer(deviceType, selected.deviceIndex, selected.channelIndex);
        if (m_impl->startCan(deviceType,
                             selected.deviceIndex,
                             selected.channelIndex) != StatusOk) {
            lastError = "GCAN StartCAN failed";
            m_impl->closeDevice(deviceType, selected.deviceIndex);
            continue;
        }

        m_impl->options = selected;
        m_impl->opened = true;
        GCanBoardInfo board{};
        if (m_impl->readBoardInfo(deviceType, selected.deviceIndex, &board) == StatusOk) {
            const auto channels = board.canChannelCount >= '0' && board.canChannelCount <= '9'
                ? board.canChannelCount - '0'
                : board.canChannelCount;
            m_impl->description = std::format("GCAN type={} {} SN={} channels={}",
                                              deviceType,
                                              fixedString(board.hardwareType, sizeof(board.hardwareType)),
                                              fixedString(board.serialNumber, sizeof(board.serialNumber)),
                                              channels);
        } else {
            m_impl->description = deviceType == UsbCan2 ? "GCAN USBCAN-II" : "GCAN USBCAN-I";
        }
        return OperationResult::passed();
    }

    m_impl->unload();
    return OperationResult::failed(
        "CanOpenFailed",
        lastError.empty()
            ? std::format("GCAN OpenDevice failed for deviceIndex={}", options.deviceIndex)
            : lastError);
}

void GCanAdapter::close() noexcept
{
    m_impl->close();
}

bool GCanAdapter::isOpen() const noexcept
{
    return m_impl->opened;
}

std::string GCanAdapter::deviceDescription() const
{
    return m_impl->opened ? m_impl->description : "Disconnected";
}

OperationResult GCanAdapter::transmit(const Frame& frame)
{
    if (!m_impl->opened) {
        return OperationResult::failed("CanNotOpen", "CAN device is not open");
    }
    if (frame.data.size() > 8) {
        return OperationResult::failed("InvalidCanFrame", "Classic CAN data exceeds 8 bytes");
    }
    GCanObject object{};
    object.id = frame.id;
    object.sendType = 0;
    object.remoteFlag = frame.remote ? 1 : 0;
    object.extendedFlag = frame.extended ? 1 : 0;
    object.dataLength = static_cast<std::uint8_t>(frame.data.size());
    std::copy(frame.data.begin(), frame.data.end(), object.data);
    const auto sent = m_impl->transmit(m_impl->options.deviceType,
                                       m_impl->options.deviceIndex,
                                       m_impl->options.channelIndex,
                                       &object,
                                       1);
    if (sent != 1) {
        return OperationResult::failed(
            "CanTransmitFailed", "GCAN Transmit failed" + m_impl->errorDetails());
    }
    return OperationResult::passed();
}

ReceiveResult GCanAdapter::receive(std::uint32_t filterId,
                                   std::uint32_t filterMask,
                                   int timeoutMs)
{
    if (!m_impl->opened) {
        return {ReceiveStatus::Error, {}, "CanNotOpen", "CAN device is not open"};
    }
    const auto started = std::chrono::steady_clock::now();
    do {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto remaining = std::max(0, timeoutMs - static_cast<int>(elapsed));
        GCanObject object{};
        const auto count = m_impl->receive(m_impl->options.deviceType,
                                           m_impl->options.deviceIndex,
                                           m_impl->options.channelIndex,
                                           &object,
                                           1,
                                           remaining);
        if (count > 0) {
            if ((object.id & filterMask) != (filterId & filterMask)) {
                continue;
            }
            if (object.dataLength > sizeof(object.data)) {
                return {ReceiveStatus::Error,
                        {},
                        "InvalidReceivedFrame",
                        std::format("GCAN returned invalid data length: {}", object.dataLength)};
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
    } while (std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - started).count() < timeoutMs);
    return {ReceiveStatus::Timeout, {}, "CanReceiveTimeout", "No matching CAN frame received"};
}

std::unique_ptr<ICanAdapter> createCanAdapter()
{
    return std::make_unique<GCanAdapter>();
}

} // namespace PicoATE::Plugins::Can
