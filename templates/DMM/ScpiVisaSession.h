#pragma once

#include "DmmAdapter.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <format>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace PicoATE::Plugins::Dmm::Detail {

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

inline constexpr ViStatus VisaSuccess = 0;
inline constexpr ViStatus VisaSuccessMaxCount = 0x3FFF0006L;
inline constexpr ViStatus VisaErrorTimeout = static_cast<ViStatus>(0xBFFF0015L);
inline constexpr ViAttr VisaAttrTimeoutValue = 0x3FFF001AL;
inline constexpr ViAttr VisaAttrSendEndEnabled = 0x3FFF0016L;
inline constexpr ViAttr VisaAttrTermChar = 0x3FFF0018L;
inline constexpr ViAttr VisaAttrTermCharEnabled = 0x3FFF0038L;
inline constexpr ViAttrState VisaTrue = 1;
inline constexpr ViAttrState VisaFalse = 0;
inline constexpr ViAccessMode VisaNoLock = 0;

using ViOpenDefaultRm = ViStatus(__stdcall*)(ViSession* session);
using ViOpen = ViStatus(__stdcall*)(ViSession, const ViChar*, ViAccessMode, ViUInt32, ViSession*);
using ViClose = ViStatus(__stdcall*)(ViObject object);
using ViSetAttribute = ViStatus(__stdcall*)(ViObject, ViAttr, ViAttrState);
using ViWrite = ViStatus(__stdcall*)(ViSession, const ViByte*, ViUInt32, ViUInt32*);
using ViRead = ViStatus(__stdcall*)(ViSession, ViByte*, ViUInt32, ViUInt32*);
using ViClear = ViStatus(__stdcall*)(ViSession);
using ViStatusDesc = ViStatus(__stdcall*)(ViObject, ViStatus, ViChar[]);

inline bool succeeded(ViStatus status)
{
    return status >= VisaSuccess;
}

inline std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), length);
    return result;
}

inline std::string trim(std::string value)
{
    const auto first = std::find_if(value.begin(), value.end(), [](unsigned char character) {
        return character != '\0' && !std::isspace(character);
    });
    value.erase(value.begin(), first);
    while (!value.empty() && (value.back() == '\0' || std::isspace(static_cast<unsigned char>(value.back())))) {
        value.pop_back();
    }
    return value;
}

inline std::string sanitize(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 && character != '\r' && character != '\n' && character != '\t';
    }), value.end());
    return value;
}inline std::string decodeTermination(std::string value)
{
    if (value == "\\n") return "\n";
    if (value == "\\r") return "\r";
    if (value == "\\r\\n") return "\r\n";
    return value;
}

class VisaScpiSession
{
public:
    ~VisaScpiSession()
    {
        disconnect();
        if (m_library) FreeLibrary(m_library);
    }

    Result connect(const std::string& address, const Plugin::Json& options)
    {
        disconnect();
        if (address.empty()) return Result::failed("VisaAddressRequired", "Set the DMM VISA resource in Station address");
        const auto loaded = load(options);
        if (!loaded.success) return loaded;

        m_ioTimeoutMs = std::clamp(Plugin::numberValue(options, "ioTimeoutMs", 5000), 1, 600000);
        m_commandDelayMs = std::clamp(Plugin::numberValue(options, "commandDelayMs", 20), 0, 10000);
        m_readBufferSize = std::clamp(Plugin::numberValue(options, "readBufferSize", 4096), 64, 65536);
        m_writeTermination = decodeTermination(Plugin::stringValue(options, "writeTermination", "\n"));
        m_readTermination = decodeTermination(Plugin::stringValue(options, "readTermination", "\n"));

        auto status = m_openDefaultRm(&m_resourceManager);
        if (!succeeded(status)) return Result::failed("VisaResourceManagerOpenFailed", error(status));
        status = m_open(m_resourceManager, address.c_str(), VisaNoLock, static_cast<ViUInt32>(m_ioTimeoutMs), &m_instrument);
        if (!succeeded(status)) {
            const auto message = error(status);
            disconnect();
            return Result::failed("VisaInstrumentOpenFailed", message);
        }
        status = m_setAttribute(m_instrument, VisaAttrTimeoutValue, static_cast<ViAttrState>(m_ioTimeoutMs));
        if (!succeeded(status)) {
            const auto message = error(status);
            disconnect();
            return Result::failed("VisaConfigureFailed", message);
        }
        m_setAttribute(m_instrument, VisaAttrSendEndEnabled, VisaTrue);
        if (m_readTermination.empty()) {
            m_setAttribute(m_instrument, VisaAttrTermCharEnabled, VisaFalse);
        } else {
            m_setAttribute(m_instrument, VisaAttrTermChar, static_cast<unsigned char>(m_readTermination.front()));
            m_setAttribute(m_instrument, VisaAttrTermCharEnabled, VisaTrue);
        }
        if (m_clear && Plugin::boolValue(options, "clearOnConnect", false)) m_clear(m_instrument);
        return Result::passed({{"connected", true}, {"address", address}});
    }

    void disconnect() noexcept
    {
        if (m_instrument && m_close) m_close(m_instrument);
        m_instrument = 0;
        if (m_resourceManager && m_close) m_close(m_resourceManager);
        m_resourceManager = 0;
    }

    bool connected() const noexcept { return m_instrument != 0; }

    Result write(const std::string& command)
    {
        if (!connected()) return Result::failed("DmmNotConnected", "VISA session is not open");
        if (command.empty()) return Result::failed("ScpiCommandRequired", "SCPI command is empty");
        const auto payload = command + m_writeTermination;
        ViUInt32 written = 0;
        const auto status = m_write(m_instrument, reinterpret_cast<const ViByte*>(payload.data()),
                                    static_cast<ViUInt32>(payload.size()), &written);
        if (!succeeded(status)) return Result::failed("VisaWriteFailed", error(status));
        if (written != payload.size()) return Result::failed("VisaShortWrite", std::format("VISA wrote {} of {} bytes", written, payload.size()));
        if (m_commandDelayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(m_commandDelayMs));
        return Result::passed();
    }

    Result query(const std::string& command)
    {
        const auto sent = write(command);
        if (!sent.success) return sent;
        std::string response;
        std::vector<ViByte> buffer(static_cast<std::size_t>(m_readBufferSize));
        while (response.size() < 65536) {
            ViUInt32 received = 0;
            const auto status = m_read(m_instrument, buffer.data(), static_cast<ViUInt32>(buffer.size()), &received);
            if (!succeeded(status) && received == 0) return Result::failed("VisaReadFailed", error(status));
            response.append(reinterpret_cast<const char*>(buffer.data()), received);
            if (status == VisaErrorTimeout || status != VisaSuccessMaxCount || received < buffer.size()) break;
        }
        response = trim(sanitize(std::move(response)));
        return response.empty() ? Result::failed("EmptyInstrumentResponse", command + " returned no data") : Result::passed(response);
    }

    Result numericQuery(const std::string& command)
    {
        const auto response = query(command);
        if (!response.success) return response;
        const auto text = response.value.get<std::string>();
        char* end = nullptr;
        const auto value = std::strtod(text.c_str(), &end);
        while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (!end || end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
            return Result::failed("InvalidNumericResponse", command + " returned invalid numeric data: " + text);
        }
        return Result::passed(value);
    }

    Result clear()
    {
        if (m_clear) {
            const auto status = m_clear(m_instrument);
            if (!succeeded(status)) return Result::failed("VisaClearFailed", error(status));
            return Result::passed();
        }
        return write("*CLS");
    }

private:
    template<typename Function>
    bool resolve(Function& destination, const char* name, std::string& message)
    {
        destination = reinterpret_cast<Function>(GetProcAddress(m_library, name));
        if (destination) return true;
        message = std::string("VISA Runtime does not export ") + name;
        return false;
    }

    Result load(const Plugin::Json& options)
    {
        if (m_library) return Result::passed();
        const auto libraryName = Plugin::stringValue(options, "visaLibrary", "visa64.dll");
        const auto path = utf8ToWide(libraryName);
        if (path.empty()) return Result::failed("InvalidVisaLibrary", "visaLibrary is not valid UTF-8");
        m_library = LoadLibraryW(path.c_str());
        if (!m_library) return Result::failed("VisaRuntimeLoadFailed", "Failed to load " + libraryName);
        std::string message;
        if (!(resolve(m_openDefaultRm, "viOpenDefaultRM", message) && resolve(m_open, "viOpen", message) &&
              resolve(m_close, "viClose", message) && resolve(m_setAttribute, "viSetAttribute", message) &&
              resolve(m_write, "viWrite", message) && resolve(m_read, "viRead", message))) {
            FreeLibrary(m_library);
            m_library = nullptr;
            return Result::failed("VisaSymbolMissing", message);
        }
        m_clear = reinterpret_cast<ViClear>(GetProcAddress(m_library, "viClear"));
        m_statusDesc = reinterpret_cast<ViStatusDesc>(GetProcAddress(m_library, "viStatusDesc"));
        return Result::passed();
    }

    std::string error(ViStatus status) const
    {
        if (m_statusDesc) {
            char text[512]{};
            const auto object = m_instrument ? m_instrument : m_resourceManager;
            if (succeeded(m_statusDesc(object, status, text))) return trim(sanitize(text));
        }
        return std::format("VISA status 0x{:08X}", static_cast<unsigned long>(status));
    }

    HMODULE m_library = nullptr;
    ViSession m_resourceManager = 0;
    ViSession m_instrument = 0;
    ViOpenDefaultRm m_openDefaultRm = nullptr;
    ViOpen m_open = nullptr;
    ViClose m_close = nullptr;
    ViSetAttribute m_setAttribute = nullptr;
    ViWrite m_write = nullptr;
    ViRead m_read = nullptr;
    ViClear m_clear = nullptr;
    ViStatusDesc m_statusDesc = nullptr;
    int m_ioTimeoutMs = 5000;
    int m_commandDelayMs = 20;
    int m_readBufferSize = 4096;
    std::string m_writeTermination = "\n";
    std::string m_readTermination = "\n";
};

inline std::string commandFor(MeasurementMode mode, double range)
{
    const auto rangeValue = range > 0.0 ? std::format(" {}", range) : " AUTO";
    switch (mode) {
    case MeasurementMode::Dcv: return "CONF:VOLT:DC" + rangeValue;
    case MeasurementMode::Acv: return "CONF:VOLT:AC" + rangeValue;
    case MeasurementMode::Dci: return "CONF:CURR:DC" + rangeValue;
    case MeasurementMode::Aci: return "CONF:CURR:AC" + rangeValue;
    case MeasurementMode::Resistance2W: return "CONF:RES" + rangeValue;
    case MeasurementMode::Resistance4W: return "CONF:FRES" + rangeValue;
    case MeasurementMode::Frequency: return "CONF:FREQ" + (range > 0.0 ? std::format(" {}", range) : "");
    case MeasurementMode::Period: return "CONF:PER" + (range > 0.0 ? std::format(" {}", range) : "");
    case MeasurementMode::Diode: return "CONF:DIOD";
    case MeasurementMode::Continuity: return "CONF:CONT";
    case MeasurementMode::Capacitance: return "CONF:CAP" + rangeValue;
    }
    return {};
}

inline std::string integrationCommand(MeasurementMode mode, double integration)
{
    switch (mode) {
    case MeasurementMode::Dcv: return std::format("SENS:VOLT:DC:NPLC {}", integration);
    case MeasurementMode::Acv: return std::format("SENS:VOLT:AC:NPLC {}", integration);
    case MeasurementMode::Dci: return std::format("SENS:CURR:DC:NPLC {}", integration);
    case MeasurementMode::Aci: return std::format("SENS:CURR:AC:NPLC {}", integration);
    case MeasurementMode::Resistance2W: return std::format("SENS:RES:NPLC {}", integration);
    case MeasurementMode::Resistance4W: return std::format("SENS:FRES:NPLC {}", integration);
    case MeasurementMode::Frequency: return std::format("SENS:FREQ:APER {}", integration);
    case MeasurementMode::Period: return std::format("SENS:PER:APER {}", integration);
    default: return {};
    }
}

} // namespace PicoATE::Plugins::Dmm::Detail