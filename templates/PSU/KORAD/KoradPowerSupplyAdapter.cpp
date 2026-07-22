#include "KoradPowerSupplyAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <format>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace PicoATE::Plugins::PowerSupply {

namespace {

using ViUInt32 = unsigned long;
using ViInt32 = long;
using ViStatus = ViInt32;
using ViSession = ViUInt32;
using ViObject = ViUInt32;
using ViAttr = ViUInt32;
using ViAttrState = ViUInt32;
using ViAccessMode = ViUInt32;
using ViByte = unsigned char;
using ViChar = char;

constexpr ViStatus VisaSuccess = 0;
constexpr ViStatus VisaSuccessMaxCount = 0x3FFF0006L;
constexpr ViStatus VisaErrorTimeout = static_cast<ViStatus>(0xBFFF0015L);
constexpr ViAttr VisaAttrTimeoutValue = 0x3FFF001AL;
constexpr ViAttr VisaAttrSendEndEnabled = 0x3FFF0016L;
constexpr ViAttr VisaAttrTermChar = 0x3FFF0018L;
constexpr ViAttr VisaAttrTermCharEnabled = 0x3FFF0038L;
constexpr ViAttr VisaAttrAsrlAvailable = 0x3FFF00ACL;
constexpr ViAttrState VisaTrue = 1;
constexpr ViAttrState VisaFalse = 0;
constexpr ViAccessMode VisaNoLock = 0;

using ViOpenDefaultRm = ViStatus(__stdcall*)(ViSession* session);
using ViOpen = ViStatus(__stdcall*)(ViSession, const ViChar*, ViAccessMode,
                                    ViUInt32, ViSession*);
using ViClose = ViStatus(__stdcall*)(ViObject object);
using ViSetAttribute = ViStatus(__stdcall*)(ViObject, ViAttr, ViAttrState);
using ViGetAttribute = ViStatus(__stdcall*)(ViObject, ViAttr, void*);
using ViWrite = ViStatus(__stdcall*)(ViSession, const ViByte*, ViUInt32, ViUInt32*);
using ViRead = ViStatus(__stdcall*)(ViSession, ViByte*, ViUInt32, ViUInt32*);
using ViClear = ViStatus(__stdcall*)(ViSession instrument);
using ViStatusDesc = ViStatus(__stdcall*)(ViObject, ViStatus, ViChar[]);

bool visaSucceeded(ViStatus status)
{
    return status >= VisaSuccess;
}

std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.data(), static_cast<int>(value.size()),
                                          nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::string windowsError(DWORD code)
{
    wchar_t* message = nullptr;
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    if (!message || length == 0) return std::format("Windows error {}", code);
    const auto size = WideCharToMultiByte(CP_UTF8, 0, message, length,
                                          nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, message, length,
                        result.data(), size, nullptr, nullptr);
    LocalFree(message);
    while (!result.empty() &&
           (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

std::string localTextToUtf8(const char* value)
{
    if (!value || *value == '\0') return {};
    const auto wideSize = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (wideSize <= 1) return value;
    std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value, -1, wide.data(), wideSize);
    const auto utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                              nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 1) return value;
    std::string utf8(static_cast<std::size_t>(utf8Size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        utf8.data(), utf8Size, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}

std::string trimResponse(std::string value)
{
    while (!value.empty() &&
           (value.back() == '\0' || value.back() == '\r' || value.back() == '\n' ||
            value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    const auto first = std::find_if(value.begin(), value.end(), [](unsigned char c) {
        return c != '\0' && !std::isspace(c);
    });
    value.erase(value.begin(), first);
    return value;
}

std::string sanitizeAsciiResponse(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
        return character != '\r' && character != '\n' && character != '\t' &&
               (character < 0x20 || character > 0x7E);
    }), value.end());
    return value;
}

std::string decimalText(double value)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(6);
    stream << value;
    auto text = stream.str();
    while (text.size() > 1 && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

Result validateChannel(int channel)
{
    return channel >= 1 && channel <= 3
        ? Result::passed()
        : Result::failed("InvalidChannel", "channel must be in the range 1..3");
}

Result validateSlot(int slot)
{
    return slot >= 1 && slot <= 5
        ? Result::passed()
        : Result::failed("InvalidMemorySlot", "slot must be in the range 1..5");
}

Result validateSetpoint(double value, const char* field)
{
    return std::isfinite(value) && value >= 0.0
        ? Result::passed()
        : Result::failed("InvalidSetpoint",
                         std::format("{} must be a finite non-negative number", field));
}

} // namespace

class KoradPowerSupplyAdapter::Impl
{
public:
    ~Impl()
    {
        disconnect();
        if (library) FreeLibrary(library);
    }

    template<typename Function>
    bool resolve(Function& function, const char* name, std::string& errorMessage)
    {
        function = reinterpret_cast<Function>(GetProcAddress(library, name));
        if (function) return true;
        errorMessage = std::string("VISA Runtime does not export ") + name;
        return false;
    }

    Result loadVisa(const Plugin::Json& options)
    {
        if (library) return Result::passed();
        const auto requested = Plugin::stringValue(options, "visaLibrary", "visa64.dll");
        const auto path = utf8ToWide(requested);
        if (path.empty()) {
            return Result::failed("InvalidVisaLibrary", "visaLibrary is not valid UTF-8");
        }
        library = LoadLibraryW(path.c_str());
        if (!library) {
            return Result::failed("VisaRuntimeLoadFailed",
                std::format("Failed to load {}: {}", requested, windowsError(GetLastError())));
        }
        std::string errorMessage;
        if (!(resolve(openDefaultRm, "viOpenDefaultRM", errorMessage) &&
              resolve(open, "viOpen", errorMessage) &&
              resolve(close, "viClose", errorMessage) &&
              resolve(setAttribute, "viSetAttribute", errorMessage) &&
              resolve(write, "viWrite", errorMessage) &&
              resolve(read, "viRead", errorMessage))) {
            FreeLibrary(library);
            library = nullptr;
            return Result::failed("VisaSymbolMissing", errorMessage);
        }
        clear = reinterpret_cast<ViClear>(GetProcAddress(library, "viClear"));
        statusDesc = reinterpret_cast<ViStatusDesc>(GetProcAddress(library, "viStatusDesc"));
        return Result::passed();
    }

    std::string visaError(ViStatus status) const
    {
        if (statusDesc) {
            char description[512]{};
            const auto object = instrument != 0 ? instrument : resourceManager;
            if (visaSucceeded(statusDesc(object, status, description))) {
                return trimResponse(localTextToUtf8(description));
            }
        }
        return std::format("VISA status 0x{:08X}", static_cast<unsigned long>(status));
    }

    Result connect(const std::string& address, const Plugin::Json& options)
    {
        disconnect();
        if (address.empty()) {
            return Result::failed("VisaAddressRequired",
                                  "Set the USB VISA resource in Station address");
        }
        const auto loaded = loadVisa(options);
        if (!loaded.success) return loaded;

        ioTimeoutMs = std::clamp(Plugin::numberValue(options, "ioTimeoutMs", 2000),
                                 1, 600000);
        commandDelayMs = std::clamp(Plugin::numberValue(options, "commandDelayMs", 20),
                                    0, 10000);
        readBufferSize = std::clamp(Plugin::numberValue(options, "readBufferSize", 4096),
                                    64, 65536);
        writeTermination = Plugin::stringValue(options, "writeTermination");
        readTermination = Plugin::stringValue(options, "readTermination");

        auto status = openDefaultRm(&resourceManager);
        if (!visaSucceeded(status)) {
            resourceManager = 0;
            return Result::failed("VisaResourceManagerOpenFailed", visaError(status));
        }
        status = open(resourceManager, address.c_str(), VisaNoLock,
                      static_cast<ViUInt32>(ioTimeoutMs), &instrument);
        if (!visaSucceeded(status)) {
            const auto message = visaError(status);
            disconnect();
            return Result::failed("VisaInstrumentOpenFailed", message);
        }
        status = setAttribute(instrument, VisaAttrTimeoutValue,
                              static_cast<ViAttrState>(ioTimeoutMs));
        if (!visaSucceeded(status)) {
            const auto message = visaError(status);
            disconnect();
            return Result::failed("VisaConfigureFailed", message);
        }
        setAttribute(instrument, VisaAttrSendEndEnabled, VisaTrue);
        if (!readTermination.empty()) {
            setAttribute(instrument, VisaAttrTermChar,
                         static_cast<unsigned char>(readTermination.front()));
            setAttribute(instrument, VisaAttrTermCharEnabled, VisaTrue);
        } else {
            setAttribute(instrument, VisaAttrTermCharEnabled, VisaFalse);
        }
        if (clear && Plugin::boolValue(options, "clearOnConnect", false)) {
            clear(instrument);
        }
        visaAddress = address;
        return Result::passed({{"visaAddress", address}});
    }

    void disconnect() noexcept
    {
        if (instrument != 0 && close) close(instrument);
        instrument = 0;
        if (resourceManager != 0 && close) close(resourceManager);
        resourceManager = 0;
        visaAddress.clear();
    }

    Result writeCommand(const std::string& command)
    {
        if (instrument == 0) {
            return Result::failed("PowerSupplyNotConnected", "VISA session is not open");
        }
        const auto payload = command + writeTermination;
        ViUInt32 written = 0;
        const auto status = write(instrument,
            reinterpret_cast<const ViByte*>(payload.data()),
            static_cast<ViUInt32>(payload.size()), &written);
        if (!visaSucceeded(status)) {
            return Result::failed("VisaWriteFailed", visaError(status));
        }
        if (written != payload.size()) {
            return Result::failed("VisaShortWrite",
                std::format("VISA wrote {} of {} bytes", written, payload.size()));
        }
        if (commandDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(commandDelayMs));
        }
        return Result::passed();
    }

    Result query(const std::string& command, bool trim = true)
    {
        const auto sent = writeCommand(command);
        if (!sent.success) return sent;
        std::string response;
        std::vector<ViByte> buffer(static_cast<std::size_t>(readBufferSize));
        while (response.size() < 65536) {
            ViUInt32 received = 0;
            const auto status = read(instrument, buffer.data(),
                                     static_cast<ViUInt32>(buffer.size()), &received);
            if (!visaSucceeded(status) && received == 0) {
                return Result::failed("VisaReadFailed", visaError(status));
            }
            response.append(reinterpret_cast<const char*>(buffer.data()), received);
            if (status == VisaErrorTimeout) break;
            if (status != VisaSuccessMaxCount || received < buffer.size()) break;
        }
        if (trim) {
            response = trimResponse(sanitizeAsciiResponse(std::move(response)));
        }
        return response.empty()
            ? Result::failed("EmptyInstrumentResponse", command + " returned no data")
            : Result::passed(response);
    }

    Result numericQuery(const std::string& command)
    {
        const auto result = query(command);
        if (!result.success) return result;
        const auto text = result.value.get<std::string>();
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
        bool validSuffix = end && *end == '\0';
        if (end && *end != '\0') {
            const auto suffix = text.substr(static_cast<std::size_t>(end - text.c_str()));
            validSuffix = std::all_of(suffix.cbegin(), suffix.cend(), [](unsigned char character) {
                return std::isalpha(character);
            });
        }
        if (!end || end == text.c_str() || !validSuffix || !std::isfinite(value)) {
            return Result::failed("InvalidNumericResponse",
                std::format("{} returned invalid numeric data: {}", command, text));
        }
        return Result::passed(value);
    }

    HMODULE library = nullptr;
    ViOpenDefaultRm openDefaultRm = nullptr;
    ViOpen open = nullptr;
    ViClose close = nullptr;
    ViSetAttribute setAttribute = nullptr;
    ViWrite write = nullptr;
    ViRead read = nullptr;
    ViClear clear = nullptr;
    ViStatusDesc statusDesc = nullptr;
    ViSession resourceManager = 0;
    ViSession instrument = 0;
    int ioTimeoutMs = 2000;
    int commandDelayMs = 20;
    int readBufferSize = 4096;
    std::string writeTermination;
    std::string readTermination;
    std::string visaAddress;
};

KoradPowerSupplyAdapter::KoradPowerSupplyAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

KoradPowerSupplyAdapter::~KoradPowerSupplyAdapter() = default;

Result KoradPowerSupplyAdapter::connect(const std::string& visaAddress,
                                        const Plugin::Json& options)
{
    return m_impl->connect(visaAddress, options);
}

void KoradPowerSupplyAdapter::disconnect() noexcept { m_impl->disconnect(); }
bool KoradPowerSupplyAdapter::isConnected() const noexcept { return m_impl->instrument != 0; }
Result KoradPowerSupplyAdapter::identity() { return m_impl->query("*IDN?"); }

Result KoradPowerSupplyAdapter::setCurrent(int channel, double amperes)
{
    if (const auto valid = validateChannel(channel); !valid.success) return valid;
    if (const auto valid = validateSetpoint(amperes, "current"); !valid.success) return valid;
    const auto result = m_impl->writeCommand(
        std::format("ISET{}:{}", channel, decimalText(amperes)));
    return result.success ? Result::passed(amperes) : result;
}

Result KoradPowerSupplyAdapter::currentSetpoint(int channel)
{
    if (const auto valid = validateChannel(channel); !valid.success) return valid;
    return m_impl->numericQuery(std::format("ISET{}?", channel));
}

Result KoradPowerSupplyAdapter::setVoltage(int channel, double volts)
{
    if (const auto valid = validateChannel(channel); !valid.success) return valid;
    if (const auto valid = validateSetpoint(volts, "voltage"); !valid.success) return valid;
    const auto result = m_impl->writeCommand(
        std::format("VSET{}:{}", channel, decimalText(volts)));
    return result.success ? Result::passed(volts) : result;
}

Result KoradPowerSupplyAdapter::voltageSetpoint(int channel)
{
    if (const auto valid = validateChannel(channel); !valid.success) return valid;
    return m_impl->numericQuery(std::format("VSET{}?", channel));
}

Result KoradPowerSupplyAdapter::outputCurrent(int channel)
{
    if (const auto valid = validateChannel(channel); !valid.success) return valid;
    return m_impl->numericQuery(std::format("IOUT{}?", channel));
}

Result KoradPowerSupplyAdapter::outputVoltage(int channel)
{
    if (const auto valid = validateChannel(channel); !valid.success) return valid;
    return m_impl->numericQuery(std::format("VOUT{}?", channel));
}

Result KoradPowerSupplyAdapter::setBeep(bool enabled)
{
    const auto result = m_impl->writeCommand(std::format("BEEP{}", enabled ? 1 : 0));
    return result.success ? Result::passed(enabled) : result;
}

Result KoradPowerSupplyAdapter::setOutput(bool enabled)
{
    constexpr int maximumAttempts = 3;
    for (int attempt = 1; attempt <= maximumAttempts; ++attempt) {
        const auto writeResult = m_impl->writeCommand(
            std::format("OUT{}", enabled ? 1 : 0));
        if (!writeResult.success) return writeResult;

        const auto statusResult = status();
        if (!statusResult.success) {
            if (attempt == maximumAttempts) return statusResult;
            continue;
        }
        const bool actual = statusResult.value.value("outputEnabled", false);
        if (actual == enabled) return Result::passed(enabled);
        PicoATE_Log("PSU_OUTPUT verify mismatch requested={} actual={} attempt={}",
                    enabled, actual, attempt);
    }
    return Result::failed(
        "OutputStateMismatch",
        std::format("OUT{} was not confirmed after {} attempts",
                    enabled ? 1 : 0, maximumAttempts));
}

Result KoradPowerSupplyAdapter::status()
{
    const auto result = m_impl->query("STATUS?", false);
    if (!result.success) return result;
    const auto response = result.value.get<std::string>();
    unsigned int raw = 0;
    bool parsed = false;
    if (response.size() == 1) {
        raw = static_cast<unsigned char>(response.front());
        parsed = true;
    } else if (response.size() == 8 &&
               std::all_of(response.begin(), response.end(), [](char character) {
                   return character == '0' || character == '1';
               })) {
        raw = static_cast<unsigned int>(std::stoul(response, nullptr, 2));
        parsed = true;
    } else {
        try {
            std::size_t consumed = 0;
            raw = static_cast<unsigned int>(std::stoul(response, &consumed, 0));
            parsed = consumed == response.size();
        } catch (...) {
            parsed = false;
        }
    }
    if (!parsed || raw > std::numeric_limits<unsigned char>::max()) {
        return Result::failed("InvalidStatusResponse",
            std::format("STATUS? returned unsupported data: {}", response));
    }
    return Result::passed({
        {"rawStatus", raw},
        {"binary", std::format("{:08b}", raw)},
        {"ch1CvMode", (raw & (1U << 0)) != 0},
        {"beepEnabled", (raw & (1U << 4)) != 0},
        {"ocpEnabled", (raw & (1U << 5)) != 0},
        {"outputEnabled", (raw & (1U << 6)) != 0},
        {"ovpEnabled", (raw & (1U << 7)) != 0},
    });
}

Result KoradPowerSupplyAdapter::recall(int slot)
{
    if (const auto valid = validateSlot(slot); !valid.success) return valid;
    const auto result = m_impl->writeCommand(std::format("RCL{}", slot));
    return result.success ? Result::passed(slot) : result;
}

Result KoradPowerSupplyAdapter::save(int slot)
{
    if (const auto valid = validateSlot(slot); !valid.success) return valid;
    const auto result = m_impl->writeCommand(std::format("SAV{}", slot));
    return result.success ? Result::passed(slot) : result;
}

Result KoradPowerSupplyAdapter::setOcp(bool enabled)
{
    const auto result = m_impl->writeCommand(std::format("OCP{}", enabled ? 1 : 0));
    return result.success ? Result::passed(enabled) : result;
}

Result KoradPowerSupplyAdapter::setOvp(bool enabled)
{
    const auto result = m_impl->writeCommand(std::format("OVP{}", enabled ? 1 : 0));
    return result.success ? Result::passed(enabled) : result;
}

Result KoradPowerSupplyAdapter::setLock(bool enabled)
{
    const auto result = m_impl->writeCommand(std::format("LOCK{}", enabled ? 1 : 0));
    return result.success ? Result::passed(enabled) : result;
}

std::unique_ptr<IPowerSupplyAdapter> createPowerSupplyAdapter()
{
    return std::make_unique<KoradPowerSupplyAdapter>();
}

Plugin::Json pluginDescription()
{
    return Plugin::Json::parse(R"json(
{
  "name": "KORAD USB VISA Power Supply",
  "category": "PSU",
  "functions": [
    {
      "id": "open", "name": "Open Power Supply",
      "description": "Open the USB VISA resource configured by Station",
      "timeoutMs": 5000,
      "inputs": [
        {"key":"address","name":"VISA Resource","type":"string","required":true,"description":"Example: USB0::...::INSTR"},
        {"key":"ioTimeoutMs","name":"VISA I/O Timeout","type":"integer","required":false,"default":2000,"minimum":1,"maximum":600000,"unit":"ms"},
        {"key":"commandDelayMs","name":"Command Delay","type":"integer","required":false,"default":20,"minimum":0,"maximum":10000,"unit":"ms"},
        {"key":"writeTermination","name":"Write Termination","type":"string","required":false,"default":""},
        {"key":"readTermination","name":"Read Termination","type":"string","required":false,"default":""}
      ],
      "outputs": [
        {"key":"connected","name":"Connected","type":"boolean"},
        {"key":"address","name":"VISA Resource","type":"string"}
      ]
    },
    {"id":"identity","name":"Read Identity","description":"Send *IDN?","timeoutMs":2000,"inputs":[],"outputs":[{"key":"identity","name":"Identity","type":"string"}]},
    {"id":"setCurrent","name":"Set Current","description":"Send ISET<X>:<NR2>","timeoutMs":2000,"inputs":[{"key":"channel","name":"Channel","type":"integer","required":false,"default":1,"minimum":1,"maximum":3},{"key":"current","name":"Current","type":"number","required":true,"default":1.0,"minimum":0.0,"unit":"A"}],"outputs":[{"key":"current","name":"Current","type":"number","unit":"A"}]},
    {"id":"getCurrentSetpoint","name":"Read Current Setpoint","description":"Send ISET<X>?","timeoutMs":2000,"inputs":[{"key":"channel","name":"Channel","type":"integer","required":false,"default":1,"minimum":1,"maximum":3}],"outputs":[{"key":"current","name":"Current Setpoint","type":"number","unit":"A"}]},
    {"id":"setVoltage","name":"Set Voltage","description":"Send VSET<X>:<NR2>","timeoutMs":2000,"inputs":[{"key":"channel","name":"Channel","type":"integer","required":false,"default":1,"minimum":1,"maximum":3},{"key":"voltage","name":"Voltage","type":"number","required":true,"default":5.0,"minimum":0.0,"unit":"V"}],"outputs":[{"key":"voltage","name":"Voltage","type":"number","unit":"V"}]},
    {"id":"getVoltageSetpoint","name":"Read Voltage Setpoint","description":"Send VSET<X>?","timeoutMs":2000,"inputs":[{"key":"channel","name":"Channel","type":"integer","required":false,"default":1,"minimum":1,"maximum":3}],"outputs":[{"key":"voltage","name":"Voltage Setpoint","type":"number","unit":"V"}]},
    {"id":"measureCurrent","name":"Measure Output Current","description":"Send IOUT<X>?","timeoutMs":2000,"inputs":[{"key":"channel","name":"Channel","type":"integer","required":false,"default":1,"minimum":1,"maximum":3}],"outputs":[{"key":"current","name":"Output Current","type":"number","unit":"A"}]},
    {"id":"measureVoltage","name":"Measure Output Voltage","description":"Send VOUT<X>?","timeoutMs":2000,"inputs":[{"key":"channel","name":"Channel","type":"integer","required":false,"default":1,"minimum":1,"maximum":3}],"outputs":[{"key":"voltage","name":"Output Voltage","type":"number","unit":"V"}]},
    {"id":"setBeep","name":"Set Beeper","description":"Send BEEP<Boolean>","timeoutMs":2000,"inputs":[{"key":"enabled","name":"Enabled","type":"boolean","required":true,"default":true}],"outputs":[{"key":"enabled","name":"Enabled","type":"boolean"}]},
    {"id":"setOutput","name":"Set Output","description":"Send OUT<Boolean>","timeoutMs":2000,"inputs":[{"key":"enabled","name":"Enabled","type":"boolean","required":true,"default":true}],"outputs":[{"key":"enabled","name":"Enabled","type":"boolean"}]},
    {"id":"readStatus","name":"Read Working Status","description":"Send STATUS? and decode bits 0, 4, 5, 6, and 7","timeoutMs":2000,"inputs":[],"outputs":[{"key":"rawStatus","name":"Raw Status","type":"integer"},{"key":"binary","name":"Binary Status","type":"string"},{"key":"ch1CvMode","name":"CH1 CV Mode","type":"boolean"},{"key":"beepEnabled","name":"Beeper Enabled","type":"boolean"},{"key":"ocpEnabled","name":"OCP Enabled","type":"boolean"},{"key":"outputEnabled","name":"Output Enabled","type":"boolean"},{"key":"ovpEnabled","name":"OVP Enabled","type":"boolean"}]},
    {"id":"recall","name":"Recall Preset","description":"Send RCL<NR1>","timeoutMs":2000,"inputs":[{"key":"slot","name":"Memory Slot","type":"integer","required":true,"default":1,"minimum":1,"maximum":5}],"outputs":[{"key":"slot","name":"Memory Slot","type":"integer"}]},
    {"id":"save","name":"Save Preset","description":"Send SAV<NR1>","timeoutMs":2000,"inputs":[{"key":"slot","name":"Memory Slot","type":"integer","required":true,"default":1,"minimum":1,"maximum":5}],"outputs":[{"key":"slot","name":"Memory Slot","type":"integer"}]},
    {"id":"setOcp","name":"Set OCP","description":"Send OCP<Boolean>","timeoutMs":2000,"inputs":[{"key":"enabled","name":"Enabled","type":"boolean","required":true,"default":true}],"outputs":[{"key":"enabled","name":"Enabled","type":"boolean"}]},
    {"id":"setOvp","name":"Set OVP","description":"Send OVP<Boolean>","timeoutMs":2000,"inputs":[{"key":"enabled","name":"Enabled","type":"boolean","required":true,"default":true}],"outputs":[{"key":"enabled","name":"Enabled","type":"boolean"}]},
    {"id":"setLock","name":"Set Keyboard Lock","description":"Send LOCK<Boolean>","timeoutMs":2000,"inputs":[{"key":"enabled","name":"Enabled","type":"boolean","required":true,"default":true}],"outputs":[{"key":"enabled","name":"Enabled","type":"boolean"}]},
    {"id":"close","name":"Close Power Supply","description":"Close the VISA session","stepKind":"cleanup","timeoutMs":3000,"inputs":[],"outputs":[{"key":"connected","name":"Connected","type":"boolean"}]}
  ]
}
)json");
}

} // namespace PicoATE::Plugins::PowerSupply
