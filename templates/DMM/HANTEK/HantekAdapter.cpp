#include "HantekAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace PicoATE::Plugins::Dmm {

Plugin::Json pluginDescription()
{
    return Plugin::Json::parse(R"json(
{
  "schema": "picoate.plugin",
  "schemaVersion": 1,
  "pluginId": "picoate.dmm.hantek",
  "moduleId": "plugin.dmm.hantek",
  "name": "Hantek VISA DMM",
  "category": "DMM",
  "connectionKinds": ["visa"],
  "vendor": "Hantek",
  "version": "1.0.0",
  "functions": [
    {
      "id": "open",
      "name": "Open DMM",
      "description": "Open the VISA resource configured by Station",
      "stepKind": "action",
      "timeoutMs": 10000,
      "inputs": [
        {
          "key": "address",
          "name": "VISA Resource",
          "type": "string",
          "required": true
        },
        {
          "key": "visaLibrary",
          "name": "VISA Runtime",
          "type": "string",
          "default": "visa64.dll"
        },
        {
          "key": "ioTimeoutMs",
          "name": "VISA I/O Timeout",
          "type": "integer",
          "default": 5000,
          "minimum": 1,
          "maximum": 600000,
          "unit": "ms"
        },
        {
          "key": "commandDelayMs",
          "name": "Command Delay",
          "type": "integer",
          "default": 20,
          "minimum": 0,
          "maximum": 10000,
          "unit": "ms"
        },
        {
          "key": "writeTermination",
          "name": "Write Termination",
          "type": "string",
          "default": "\\n"
        },
        {
          "key": "readTermination",
          "name": "Read Termination",
          "type": "string",
          "default": "\\n"
        }
      ],
      "outputs": [
        {
          "key": "connected",
          "name": "Connected",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "identity",
      "name": "Read Identity",
      "description": "Send *IDN?",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": [
        {
          "key": "identity",
          "name": "Identity",
          "type": "string"
        }
      ]
    },
    {
      "id": "reset",
      "name": "Reset DMM",
      "description": "Send *RST",
      "timeoutMs": 10000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "clear",
      "name": "Clear DMM Status",
      "description": "Clear VISA/SCPI status",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "configureDcv",
      "name": "Configure DC Voltage",
      "description": "CONF:VOLT:DC and SENS:VOLT:DC:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "0.1 V", "value": 0.1},
            {"label": "1 V", "value": 1},
            {"label": "10 V", "value": 10},
            {"label": "100 V", "value": 100},
            {"label": "1000 V", "value": 1000}
          ]
        },
        {
          "key": "resolution",
          "name": "Resolution",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (highest)", "value": 0},
            {"label": "0.1 (1 digit)", "value": 0.1},
            {"label": "0.01 (2 digits)", "value": 0.01},
            {"label": "0.001 (3 digits)", "value": 0.001},
            {"label": "0.0001 (4 digits)", "value": 0.0001}
          ]
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "enum",
          "default": 10,
          "options": [
            {"label": "0.02 (fast)", "value": 0.02},
            {"label": "0.2", "value": 0.2},
            {"label": "1", "value": 1},
            {"label": "10 (default)", "value": 10},
            {"label": "100 (slow, precise)", "value": 100}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureAcv",
      "name": "Configure AC Voltage",
      "description": "CONF:VOLT:AC and SENS:VOLT:AC:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "0.1 V", "value": 0.1},
            {"label": "1 V", "value": 1},
            {"label": "10 V", "value": 10},
            {"label": "100 V", "value": 100},
            {"label": "750 V", "value": 750}
          ]
        },
        {
          "key": "resolution",
          "name": "Resolution",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (highest)", "value": 0},
            {"label": "0.1 (1 digit)", "value": 0.1},
            {"label": "0.01 (2 digits)", "value": 0.01},
            {"label": "0.001 (3 digits)", "value": 0.001},
            {"label": "0.0001 (4 digits)", "value": 0.0001}
          ]
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "enum",
          "default": 10,
          "options": [
            {"label": "0.02 (fast)", "value": 0.02},
            {"label": "0.2", "value": 0.2},
            {"label": "1", "value": 1},
            {"label": "10 (default)", "value": 10},
            {"label": "100 (slow, precise)", "value": 100}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureDci",
      "name": "Configure DC Current",
      "description": "CONF:CURR:DC and SENS:CURR:DC:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "0.1 mA", "value": 0.0001},
            {"label": "1 mA", "value": 0.001},
            {"label": "10 mA", "value": 0.01},
            {"label": "100 mA", "value": 0.1},
            {"label": "1 A", "value": 1},
            {"label": "10 A", "value": 10}
          ]
        },
        {
          "key": "resolution",
          "name": "Resolution",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (highest)", "value": 0},
            {"label": "0.1 (1 digit)", "value": 0.1},
            {"label": "0.01 (2 digits)", "value": 0.01},
            {"label": "0.001 (3 digits)", "value": 0.001},
            {"label": "0.0001 (4 digits)", "value": 0.0001}
          ]
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "enum",
          "default": 10,
          "options": [
            {"label": "0.02 (fast)", "value": 0.02},
            {"label": "0.2", "value": 0.2},
            {"label": "1", "value": 1},
            {"label": "10 (default)", "value": 10},
            {"label": "100 (slow, precise)", "value": 100}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureAci",
      "name": "Configure AC Current",
      "description": "CONF:CURR:AC and SENS:CURR:AC:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "0.1 mA", "value": 0.0001},
            {"label": "1 mA", "value": 0.001},
            {"label": "10 mA", "value": 0.01},
            {"label": "100 mA", "value": 0.1},
            {"label": "1 A", "value": 1},
            {"label": "10 A", "value": 10}
          ]
        },
        {
          "key": "resolution",
          "name": "Resolution",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (highest)", "value": 0},
            {"label": "0.1 (1 digit)", "value": 0.1},
            {"label": "0.01 (2 digits)", "value": 0.01},
            {"label": "0.001 (3 digits)", "value": 0.001},
            {"label": "0.0001 (4 digits)", "value": 0.0001}
          ]
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "enum",
          "default": 10,
          "options": [
            {"label": "0.02 (fast)", "value": 0.02},
            {"label": "0.2", "value": 0.2},
            {"label": "1", "value": 1},
            {"label": "10 (default)", "value": 10},
            {"label": "100 (slow, precise)", "value": 100}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
)json"
    R"json(
    {
      "id": "configureResistance2w",
      "name": "Configure 2-Wire Resistance",      "description": "CONF:RES and SENS:RES:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "100 Ohm", "value": 100},
            {"label": "1 kOhm", "value": 1000},
            {"label": "10 kOhm", "value": 10000},
            {"label": "100 kOhm", "value": 100000},
            {"label": "1 MOhm", "value": 1000000},
            {"label": "10 MOhm", "value": 10000000},
            {"label": "100 MOhm", "value": 100000000}
          ]
        },
        {
          "key": "resolution",
          "name": "Resolution",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (highest)", "value": 0},
            {"label": "0.1 (1 digit)", "value": 0.1},
            {"label": "0.01 (2 digits)", "value": 0.01},
            {"label": "0.001 (3 digits)", "value": 0.001},
            {"label": "0.0001 (4 digits)", "value": 0.0001}
          ]
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "enum",
          "default": 10,
          "options": [
            {"label": "0.02 (fast)", "value": 0.02},
            {"label": "0.2", "value": 0.2},
            {"label": "1", "value": 1},
            {"label": "10 (default)", "value": 10},
            {"label": "100 (slow, precise)", "value": 100}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureResistance4w",
      "name": "Configure 4-Wire Resistance",
      "description": "CONF:FRES and SENS:FRES:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "100 Ohm", "value": 100},
            {"label": "1 kOhm", "value": 1000},
            {"label": "10 kOhm", "value": 10000},
            {"label": "100 kOhm", "value": 100000},
            {"label": "1 MOhm", "value": 1000000},
            {"label": "10 MOhm", "value": 10000000},
            {"label": "100 MOhm", "value": 100000000}
          ]
        },
        {
          "key": "resolution",
          "name": "Resolution",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (highest)", "value": 0},
            {"label": "0.1 (1 digit)", "value": 0.1},
            {"label": "0.01 (2 digits)", "value": 0.01},
            {"label": "0.001 (3 digits)", "value": 0.001},
            {"label": "0.0001 (4 digits)", "value": 0.0001}
          ]
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "enum",
          "default": 10,
          "options": [
            {"label": "0.02 (fast)", "value": 0.02},
            {"label": "0.2", "value": 0.2},
            {"label": "1", "value": 1},
            {"label": "10 (default)", "value": 10},
            {"label": "100 (slow, precise)", "value": 100}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureFrequency",
      "name": "Configure Frequency",
      "description": "CONF:FREQ and SENS:FREQ:APER",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "aperture",
          "name": "Aperture",
          "type": "enum",
          "default": 0.1,
          "options": [
            {"label": "0.01 s (fast)", "value": 0.01},
            {"label": "0.1 s", "value": 0.1},
            {"label": "1 s (slow, precise)", "value": 1}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configurePeriod",
      "name": "Configure Period",
      "description": "CONF:PER and SENS:PER:APER",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "aperture",
          "name": "Aperture",
          "type": "enum",
          "default": 0.1,
          "options": [
            {"label": "0.01 s (fast)", "value": 0.01},
            {"label": "0.1 s", "value": 0.1},
            {"label": "1 s (slow, precise)", "value": 1}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureDiode",
      "name": "Configure Diode",
      "description": "CONF:DIOD",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureContinuity",
      "name": "Configure Continuity",
      "description": "CONF:CONT",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "configureCapacitance",
      "name": "Configure Capacitance",
      "description": "CONF:CAP",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Auto (auto-ranging)", "value": 0},
            {"label": "1 nF", "value": 0.000000001},
            {"label": "10 nF", "value": 0.00000001},
            {"label": "100 nF", "value": 0.0000001},
            {"label": "1 uF", "value": 0.000001},
            {"label": "10 uF", "value": 0.00001},
            {"label": "100 uF", "value": 0.0001}
          ]
        }
      ],
      "outputs": [
        {
          "key": "configured",
          "name": "Configured",
          "type": "boolean"
        }
      ]
    },
    {
      "id": "read",
      "name": "Read Measurement",
      "description": "Trigger and read using READ?",
      "timeoutMs": 10000,
      "inputs": [],
      "outputs": [
        {
          "key": "value",
          "name": "Measurement",
          "type": "number"
        }
      ]
    },
    {
      "id": "query",
      "name": "Send SCPI Query",
      "description": "Write command and return response",
      "timeoutMs": 10000,
      "inputs": [
        {
          "key": "command",
          "name": "SCPI Command",
          "type": "string",
          "required": true
        }
      ],
      "outputs": [
        {
          "key": "response",
          "name": "Response",
          "type": "string"
        }
      ]
    },
    {
      "id": "write",
      "name": "Send SCPI Command",
      "description": "Write command without readback",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "command",
          "name": "SCPI Command",
          "type": "string",
          "required": true
        }
      ],
      "outputs": []
    },
    {
      "id": "close",
      "name": "Close DMM",
      "description": "Close VISA session",
      "stepKind": "cleanup",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": [
        {
          "key": "connected",
          "name": "Connected",
          "type": "boolean"
        }
      ]
    }
  ]
}
)json");
}

namespace {

// ============================================================================
// VISA session (embedded in this file, SCOPE/PSU style: each plugin carries
// its own copy, no shared header between plugins)
// ============================================================================

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

// Convert ANSI-codepage text (e.g. viStatusDesc output from a Chinese NI-VISA
// build returns GBK-encoded Chinese) into valid UTF-8. Without this the bytes
// break nlohmann::json::dump (PicoATE_Execute returns 4 with an empty response).
inline std::string localTextToUtf8(const char* value)
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
}

// Raw bytes read back from VISA are not guaranteed to be valid UTF-8 (e.g. an
// interrupted response can carry binary garbage). nlohmann::json::dump throws
// on invalid UTF-8 (which makes PicoATE_Execute return 4 with an empty response),
// so responses must be sanitized before they enter the JSON: invalid bytes are
// replaced with '?'.
inline std::string toValidUtf8(std::string value)
{
    std::string result;
    result.reserve(value.size());
    const auto size = value.size();
    std::size_t index = 0;
    while (index < size) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first < 0x80) {
            result.push_back(static_cast<char>(first));
            ++index;
            continue;
        }
        std::size_t length = 0;
        unsigned char minimum = 0;
        unsigned char maximum = 0;
        if ((first & 0xE0) == 0xC0) {
            length = 2;
            minimum = 0x80;
            maximum = 0xBF;
        } else if ((first & 0xF0) == 0xE0) {
            length = 3;
            minimum = 0x80;
            maximum = 0xBF;
        } else if ((first & 0xF8) == 0xF0) {
            length = 4;
            minimum = 0x80;
            maximum = 0xBF;
        } else {
            result.push_back('?');
            ++index;
            continue;
        }
        if (index + length > size) {
            result.push_back('?');
            ++index;
            continue;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            if (byte < minimum || byte > maximum) {
                valid = false;
                break;
            }
        }
        // Reject overlong encodings: the first byte of a multi-byte sequence must
        // be at least 0xC2 (2-byte), 0xE0 (3-byte), 0xF0 (4-byte).
        if (valid && length == 2 && first < 0xC2) valid = false;
        if (valid && length == 3 && first < 0xE0) valid = false;
        if (valid && length == 4 && first < 0xF0) valid = false;
        if (valid) {
            result.append(value, index, length);
            index += length;
        } else {
            result.push_back('?');
            ++index;
        }
    }
    return result;
}

inline std::string decodeTermination(std::string value)
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
        PicoATE_Log("DMM_VISA connect address={}", address);
        PicoATE_Log("DMM_VISA options visaLibrary={} ioTimeoutMs={} commandDelayMs={} readBufferSize={} writeTermination={} readTermination={}",
                    Plugin::stringValue(options, "visaLibrary", "visa64.dll"),
                    Plugin::numberValue(options, "ioTimeoutMs", 5000),
                    Plugin::numberValue(options, "commandDelayMs", 20),
                    Plugin::numberValue(options, "readBufferSize", 4096),
                    Plugin::stringValue(options, "writeTermination", "\n"),
                    Plugin::stringValue(options, "readTermination", "\n"));
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
        PicoATE_Log("DMM_VISA viOpenDefaultRM status=0x{:08X} rm={}", static_cast<unsigned long>(status), m_resourceManager);
        if (!succeeded(status)) return Result::failed("VisaResourceManagerOpenFailed", error(status));
        status = m_open(m_resourceManager, address.c_str(), VisaNoLock, static_cast<ViUInt32>(m_ioTimeoutMs), &m_instrument);
        PicoATE_Log("DMM_VISA viOpen status=0x{:08X} session={}", static_cast<unsigned long>(status), m_instrument);
        if (!succeeded(status)) {
            const auto message = error(status);
            disconnect();
            return Result::failed("VisaInstrumentOpenFailed", message);
        }
        status = m_setAttribute(m_instrument, VisaAttrTimeoutValue, static_cast<ViAttrState>(m_ioTimeoutMs));
        PicoATE_Log("DMM_VISA setAttribute(Timeout={}ms) status=0x{:08X}", m_ioTimeoutMs, static_cast<unsigned long>(status));
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
        PicoATE_Log("DMM_VISA connected");
        return Result::passed({{"connected", true}, {"address", address}});
    }

    void disconnect() noexcept
    {
        if (m_instrument && m_close) m_close(m_instrument);
        if (m_instrument || m_resourceManager) {
            PicoATE_Log("DMM_VISA disconnect instrument={} rm={}", m_instrument, m_resourceManager);
        }
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
        if (!succeeded(status)) {
            const auto message = error(status);
            PicoATE_Log("DMM_VISA write '{}' status=0x{:08X} failed: {}", command, static_cast<unsigned long>(status), message);
            return Result::failed("VisaWriteFailed", message);
        }
        PicoATE_Log("DMM_VISA write '{}' bytes={} written={} status=0x{:08X}",
                    command, payload.size(), written, static_cast<unsigned long>(status));
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
        int readCount = 0;
        while (response.size() < 65536) {
            ViUInt32 received = 0;
            const auto status = m_read(m_instrument, buffer.data(), static_cast<ViUInt32>(buffer.size()), &received);
            ++readCount;
            if (!succeeded(status) && received == 0) {
                // The response may still be in flight (instrument busy, e.g.
                // right after *RST or :AUToscale). Flush the pending bytes so
                // the next query does not consume a stale response.
                PicoATE_Log("DMM_VISA read#{0} status=0x{1:08X} received=0 -> timeout, draining pending",
                            readCount, static_cast<unsigned long>(status));
                drainPending(buffer);
                return Result::failed("VisaReadFailed", error(status));
            }
            response.append(reinterpret_cast<const char*>(buffer.data()), received);
            PicoATE_Log("DMM_VISA read#{0} status=0x{1:08X} received={2} total={3}",
                        readCount, static_cast<unsigned long>(status), received, response.size());
            if (status == VisaErrorTimeout || status != VisaSuccessMaxCount || received < buffer.size()) break;
        }
        response = toValidUtf8(trim(sanitize(std::move(response))));
        if (response.empty()) {
            PicoATE_Log("DMM_VISA query '{}' -> EMPTY (no data)", command);
            return Result::failed("EmptyInstrumentResponse", command + " returned no data");
        }
        PicoATE_Log("DMM_VISA query '{}' -> '{}'", command, response);
        return Result::passed(response);
    }

    Result numericQuery(const std::string& command)
    {
        const auto response = query(command);
        if (!response.success) return response;
        const auto text = response.value.get<std::string>();
        char* end = nullptr;
        const auto value = std::strtod(text.c_str(), &end);
        while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
        // Allow a unit suffix after the number (e.g. "1.000000V", "999.9 OHM");
        // some instruments return values with a unit attached.
        bool validSuffix = end && *end == '\0';
        if (end && *end != '\0') {
            const auto suffix = text.substr(static_cast<std::size_t>(end - text.c_str()));
            validSuffix = std::all_of(suffix.cbegin(), suffix.cend(), [](unsigned char character) {
                return std::isalpha(character) || character == ' ';
            });
        }
        if (!end || end == text.c_str() || !validSuffix || !std::isfinite(value)) {
            PicoATE_Log("DMM_VISA numericQuery '{}' -> INVALID '{}'", command, text);
            return Result::failed("InvalidNumericResponse", command + " returned invalid numeric data: " + text);
        }
        // Overload / open (OL) sentinel: the DMM reports 9.9E37 when it cannot
        // produce a valid reading (open resistance/diode, over-range). Return
        // the string "OL" so sequence limits can compare outputs.value = "OL".
        const bool overload = std::abs(value) >= 1.0E36;
        if (overload) {
            PicoATE_Log("DMM_VISA numericQuery '{}' -> raw {} -> parsed OL", command, value);
            return Result::passed("OL");
        }
        PicoATE_Log("DMM_VISA numericQuery '{}' -> raw '{}' -> parsed {}",
                    command, text, value);
        return Result::passed(value);
    }

    Result clear()
    {
        if (m_clear) {
            const auto status = m_clear(m_instrument);
            PicoATE_Log("DMM_VISA viClear status=0x{:08X}", static_cast<unsigned long>(status));
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
        if (!m_library) {
            PicoATE_Log("DMM_VISA load '{}' FAILED (error={})", libraryName, static_cast<unsigned long>(GetLastError()));
            return Result::failed("VisaRuntimeLoadFailed", "Failed to load " + libraryName);
        }
        PicoATE_Log("DMM_VISA load '{}' OK", libraryName);
        std::string message;
        if (!(resolve(m_openDefaultRm, "viOpenDefaultRM", message) && resolve(m_open, "viOpen", message) &&
              resolve(m_close, "viClose", message) && resolve(m_setAttribute, "viSetAttribute", message) &&
              resolve(m_write, "viWrite", message) && resolve(m_read, "viRead", message))) {
            PicoATE_Log("DMM_VISA symbol resolve FAILED: {}", message);
            FreeLibrary(m_library);
            m_library = nullptr;
            return Result::failed("VisaSymbolMissing", message);
        }
        m_clear = reinterpret_cast<ViClear>(GetProcAddress(m_library, "viClear"));
        m_statusDesc = reinterpret_cast<ViStatusDesc>(GetProcAddress(m_library, "viStatusDesc"));
        PicoATE_Log("DMM_VISA symbols resolved (viClear={} viStatusDesc={})",
                    m_clear ? "yes" : "no", m_statusDesc ? "yes" : "no");
        return Result::passed();
    }

    std::string error(ViStatus status) const
    {
        // viStatusDesc returns locale-dependent text (a Chinese NI-VISA build
        // returns GBK-encoded Chinese). Convert it to valid UTF-8 first, so the
        // description is preserved in the log and nlohmann::json::dump does not
        // throw (which would make PicoATE_Execute return 4 with an empty response).
        if (m_statusDesc) {
            char description[512]{};
            const auto object = m_instrument ? m_instrument : m_resourceManager;
            if (succeeded(m_statusDesc(object, status, description))) {
                const auto text = trim(sanitize(localTextToUtf8(description)));
                if (!text.empty()) return text;
            }
        }
        if (status == VisaErrorTimeout) {
            return "VISA timeout (the instrument did not respond in time)";
        }
        return std::format("VISA status 0x{:08X}", static_cast<unsigned long>(status));
    }

    // Discard any bytes the instrument sends after a read timeout. Without
    // this the late response stays in the VISA buffer and the next query
    // consumes a stale value (response misalignment).
    void drainPending(std::vector<ViByte>& buffer)
    {
        if (!m_read) return;
        int flushedTotal = 0;
        for (int attempt = 0; attempt < 8; ++attempt) {
            ViUInt32 received = 0;
            const auto status = m_read(m_instrument, buffer.data(),
                                       static_cast<ViUInt32>(buffer.size()), &received);
            if (!succeeded(status) || received == 0) {
                PicoATE_Log("DMM_VISA drain finished after {} bytes (attempt {})", flushedTotal, attempt);
                return;
            }
            flushedTotal += static_cast<int>(received);
        }
        PicoATE_Log("DMM_VISA drain stopped after {} bytes (attempt limit)", flushedTotal);
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

// SCPI configuration helpers (DMM measurement modes -> CONF/SENS commands)
std::string commandFor(MeasurementMode mode, double range, double resolution)
{
    const auto rangeValue = range > 0.0 ? std::format(" {}", range) : " AUTO";
    // HDM3000 series: CONF <mode>[,<range>[,<resolution>]] - resolution is the
    // display step (0.1/0.01/0.001/0.0001, discrete). 0 means "do not set".
    const auto resolutionValue = resolution > 0.0
        ? std::format(", {}", resolution)
        : std::string{};
    switch (mode) {
    case MeasurementMode::Dcv: return "CONF:VOLT:DC" + rangeValue + resolutionValue;
    case MeasurementMode::Acv: return "CONF:VOLT:AC" + rangeValue + resolutionValue;
    case MeasurementMode::Dci: return "CONF:CURR:DC" + rangeValue + resolutionValue;
    case MeasurementMode::Aci: return "CONF:CURR:AC" + rangeValue + resolutionValue;
    case MeasurementMode::Resistance2W: return "CONF:RES" + rangeValue + resolutionValue;
    case MeasurementMode::Resistance4W: return "CONF:FRES" + rangeValue + resolutionValue;
    case MeasurementMode::Frequency: return "CONF:FREQ" + (range > 0.0 ? std::format(" {}", range) : "");
    case MeasurementMode::Period: return "CONF:PER" + (range > 0.0 ? std::format(" {}", range) : "");
    case MeasurementMode::Diode: return "CONF:DIOD";
    case MeasurementMode::Continuity: return "CONF:CONT";
    case MeasurementMode::Capacitance: return "CONF:CAP" + rangeValue + resolutionValue;
    }
    return {};
}

std::string integrationCommand(MeasurementMode mode, double integration)
{
    switch (mode) {
    case MeasurementMode::Dcv: return std::format("SENS:VOLT:DC:NPLC {}", integration);
    case MeasurementMode::Dci: return std::format("SENS:CURR:DC:NPLC {}", integration);
    case MeasurementMode::Resistance2W: return std::format("SENS:RES:NPLC {}", integration);
    case MeasurementMode::Resistance4W: return std::format("SENS:FRES:NPLC {}", integration);
    case MeasurementMode::Frequency: return std::format("SENS:FREQ:APER {}", integration);
    case MeasurementMode::Period: return std::format("SENS:PER:APER {}", integration);
    // HDM3000 firmware (2.0.0.6) rejects SENS:VOLT:AC:NPLC and
    // SENS:CURR:AC:NPLC with -113 "Undefined header" (verified on the
    // instrument): AC integration time is not settable, so do not send
    // these commands (they only pollute the instrument error queue).
    case MeasurementMode::Acv:
    case MeasurementMode::Aci:
        return {};
    default: return {};
    }
}

} // namespace

class HantekAdapter::Impl
{
public:
    VisaScpiSession session;
};

HantekAdapter::HantekAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

HantekAdapter::~HantekAdapter() = default;

Result HantekAdapter::connect(const std::string& visaAddress, const Plugin::Json& options)
{
    return m_impl->session.connect(visaAddress, options);
}

void HantekAdapter::disconnect() noexcept
{
    m_impl->session.disconnect();
}

bool HantekAdapter::isConnected() const noexcept
{
    return m_impl->session.connected();
}

Result HantekAdapter::identity()
{
    return m_impl->session.query("*IDN?");
}

Result HantekAdapter::reset()
{
    return m_impl->session.write("*RST");
}

Result HantekAdapter::clear()
{
    return m_impl->session.clear();
}

Result HantekAdapter::configure(MeasurementMode mode, double range, double integration, double resolution)
{
    const auto configuration = commandFor(mode, range, resolution);
    if (configuration.empty()) return Result::failed("UnsupportedMeasurementMode", "Hantek DMM mode is not supported");
    auto result = m_impl->session.write(configuration);
    if (!result.success) return result;

    const auto integrationCmd = integrationCommand(mode, integration);
    if (!integrationCmd.empty()) {
        result = m_impl->session.write(integrationCmd);
        if (!result.success) return result;
    }
    PicoATE_Log("HANTEK_CONFIG command={} integration={}", configuration, integration);
    return Result::passed();
}

Result HantekAdapter::read()
{
    return m_impl->session.numericQuery("READ?");
}

Result HantekAdapter::query(const std::string& command)
{
    return m_impl->session.query(command);
}

Result HantekAdapter::write(const std::string& command)
{
    return m_impl->session.write(command);
}

std::unique_ptr<IDmmAdapter> createDmmAdapter()
{
    return std::make_unique<HantekAdapter>();
}


} // namespace PicoATE::Plugins::Dmm
