#include "RigolScopeAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace PicoATE::Plugins::Scope {

Plugin::Json pluginDescription()
{
    return Plugin::Json::parse(R"json(
{
  "schema": "picoate.plugin",
  "schemaVersion": 1,
  "pluginId": "picoate.scope.rigol",
  "moduleId": "plugin.scope.rigol",
  "name": "Rigol VISA Oscilloscope",
  "category": "SCOPE",
  "connectionKinds": ["visa"],
  "vendor": "Rigol",
  "version": "1.0.0",
  "functions": [
    {
      "id": "open",
      "name": "Open Oscilloscope",
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
      "name": "Reset Oscilloscope",
      "description": "Send *RST",
      "timeoutMs": 10000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "clear",
      "name": "Clear Oscilloscope Status",
      "description": "Clear VISA/SCPI status",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "lockFrontPanel",
      "name": "Lock Front Panel",
      "description": ":SYSTem:LOCKed ON/OFF",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "lock",
          "name": "Lock",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Unlock", "value": 0},
            {"label": "Lock", "value": 1}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "run",
      "name": "Run Acquisition",
      "description": "Send :RUN",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "stop",
      "name": "Stop Acquisition",
      "description": "Send :STOP",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "single",
      "name": "Single Acquisition",
      "description": "Send :SINGle",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "forceTrigger",
      "name": "Force Trigger",
      "description": "Send :TFORce",
      "timeoutMs": 5000,
      "inputs": [],
      "outputs": []
    },
    {
      "id": "enableChannel",
      "name": "Enable Channel",
      "description": ":CHANnel<n>:DISPlay ON/OFF",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "enable",
          "name": "Enable",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "ON", "value": 1},
            {"label": "OFF", "value": 0}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setChannelCoupling",
      "name": "Set Channel Coupling",
      "description": ":CHANnel<n>:COUPling 0=AC 1=DC 2=GND",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "coupling",
          "name": "Coupling",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "AC", "value": 0},
            {"label": "DC", "value": 1},
            {"label": "GND", "value": 2}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setProbe",
      "name": "Set Probe Attenuation",
      "description": ":CHANnel<n>:PROBe 0.01~1000",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "attenuation",
          "name": "Attenuation",
          "type": "number",
          "default": 10.0,
          "minimum": 0.01,
          "maximum": 1000.0
        }
      ],
      "outputs": []
    },
    {
      "id": "setVoltageDiv",
      "name": "Set Voltage Per Division",
      "description": ":CHANnel<n>:SCALe (V/div)",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "voltsPerDiv",
          "name": "Volts Per Division",
          "type": "number",
          "default": 1.0,
          "minimum": 0.001,
          "unit": "V"
        }
      ],
      "outputs": []
    },
    {
      "id": "setChannelOffset",
      "name": "Set Channel Offset",
      "description": ":CHANnel<n>:OFFSet (V)",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "offset",
          "name": "Offset",
          "type": "number",
          "default": 0.0,
          "unit": "V"
        }
      ],
      "outputs": []
    },
    {
      "id": "setInvert",
      "name": "Set Channel Invert",
      "description": ":CHANnel<n>:INVert 0=OFF 1=ON",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "invert",
          "name": "Invert",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "OFF", "value": 0},
            {"label": "ON", "value": 1}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setBandwidthLimit",
      "name": "Set Bandwidth Limit",
      "description": ":CHANnel<n>:BWLimit 0=OFF 1=20M",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "limit",
          "name": "Limit",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "OFF (Full Bandwidth)", "value": 0},
            {"label": "20 MHz", "value": 1}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setTimebaseScale",
      "name": "Set Timebase Scale",
      "description": ":TIMebase:MAIN:SCALe (s/div)",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "secondsPerDiv",
          "name": "Seconds Per Division",
          "type": "number",
          "default": 0.001,
          "minimum": 0.000000005,
          "unit": "s"
        }
      ],
      "outputs": []
    },
    {
      "id": "setTriggerMode",
      "name": "Set Trigger Mode",
      "description": ":TRIGger:MODE 0=EDGE 1=PULSe ... 13=SPI",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "mode",
          "name": "Mode",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "EDGE", "value": 0},
            {"label": "PULSe", "value": 1},
            {"label": "RUNT", "value": 2},
            {"label": "WINDow", "value": 3},
            {"label": "SLOPe", "value": 4},
            {"label": "VIDeo", "value": 5},
            {"label": "PATTern", "value": 6},
            {"label": "DELay", "value": 7},
            {"label": "TIMeout", "value": 8},
            {"label": "DURation", "value": 9},
            {"label": "SHOLd", "value": 10},
            {"label": "RS232", "value": 11},
            {"label": "IIC", "value": 12},
            {"label": "SPI", "value": 13}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setTriggerSweep",
      "name": "Set Trigger Sweep",
      "description": ":TRIGger:SWEep 0=AUTO 1=NORMal 2=SINGle",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "sweep",
          "name": "Sweep",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "AUTO", "value": 0},
            {"label": "NORMal", "value": 1},
            {"label": "SINGle", "value": 2}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setTriggerCoupling",
      "name": "Set Trigger Coupling",
      "description": ":TRIGger:COUPling 0=AC 1=DC 2=LFReject 3=HFReject",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "coupling",
          "name": "Coupling",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "AC", "value": 0},
            {"label": "DC", "value": 1},
            {"label": "LFReject", "value": 2},
            {"label": "HFReject", "value": 3}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setEdgeTriggerSource",
      "name": "Set Edge Trigger Source",
      "description": ":TRIGger:EDGe:SOURce CHANnel<n>",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "channel",
          "name": "Channel",
          "type": "enum",
          "default": 1,
          "options": [
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "setEdgeTriggerLevel",
      "name": "Set Edge Trigger Level",
      "description": ":TRIGger:EDGe:LEVel (V)",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "level",
          "name": "Level",
          "type": "number",
          "default": 0.0,
          "unit": "V"
        }
      ],
      "outputs": []
    },
    {
      "id": "setEdgeTriggerSlope",
      "name": "Set Edge Trigger Slope",
      "description": ":TRIGger:EDGe:SLOPe 0=POSitive 1=NEGative 2=RFALl",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "slope",
          "name": "Slope",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Rising (POSitive)", "value": 0},
            {"label": "Falling (NEGative)", "value": 1},
            {"label": "Either (RFALl)", "value": 2}
          ]
        }
      ],
      "outputs": []
    },
    {
      "id": "measure",
      "name": "Measure Item",
      "description": ":MEASure:ITEM? <item>,CHANnel<n> (VPP/VMAX/VRMS/FREQuency/...)",
      "timeoutMs": 10000,
      "inputs": [
        {
          "key": "item",
          "name": "Measure Item",
          "type": "enum",
          "required": true,
          "options": [
            {"label": "VPP (Peak-to-Peak)", "value": "VPP"},
            {"label": "VMAX (Maximum)", "value": "VMAX"},
            {"label": "VMIN (Minimum)", "value": "VMIN"},
            {"label": "VTOP (Top)", "value": "VTOP"},
            {"label": "VBASE (Base)", "value": "VBASe"},
            {"label": "VAVG (Average)", "value": "VAVG"},
            {"label": "VRMS", "value": "VRMS"},
            {"label": "Frequency", "value": "FREQuency"},
            {"label": "Period", "value": "PERiod"},
            {"label": "Rise Time", "value": "RTIMe"},
            {"label": "Fall Time", "value": "FTIMe"},
            {"label": "Positive Width", "value": "PWIDth"},
            {"label": "Negative Width", "value": "NWIDth"},
            {"label": "Positive Duty Cycle", "value": "PDUTy"},
            {"label": "Negative Duty Cycle", "value": "NDUTy"},
            {"label": "Delay", "value": "DELay"},
            {"label": "Phase", "value": "PHASe"}
          ]
        },
        {
          "key": "source",
          "name": "Source Channel",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Default", "value": 0},
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        },
        {
          "key": "source2",
          "name": "Second Source Channel",
          "type": "enum",
          "default": 0,
          "options": [
            {"label": "Not Used", "value": 0},
            {"label": "CH1", "value": 1},
            {"label": "CH2", "value": 2},
            {"label": "CH3", "value": 3},
            {"label": "CH4", "value": 4}
          ]
        }
      ],
      "outputs": [
        {
          "key": "value",
          "name": "Measurement",
          "type": "number"
        }
      ]
    },
    {
      "id": "autoScale",
      "name": "Auto Scale",
      "description": "Send :AUToscale",
      "timeoutMs": 10000,
      "inputs": [],
      "outputs": []
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
      "name": "Close Oscilloscope",
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
// VISA session (embedded in this file, PSU/KORAD style: each plugin carries
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
        PicoATE_Log("SCOPE_VISA connect address={}", address);
        PicoATE_Log("SCOPE_VISA options visaLibrary={} ioTimeoutMs={} commandDelayMs={} readBufferSize={} writeTermination={} readTermination={}",
                    Plugin::stringValue(options, "visaLibrary", "visa64.dll"),
                    Plugin::numberValue(options, "ioTimeoutMs", 5000),
                    Plugin::numberValue(options, "commandDelayMs", 20),
                    Plugin::numberValue(options, "readBufferSize", 4096),
                    Plugin::stringValue(options, "writeTermination", "\n"),
                    Plugin::stringValue(options, "readTermination", "\n"));
        disconnect();
        if (address.empty()) return Result::failed("VisaAddressRequired", "Set the oscilloscope VISA resource in Station address");
        const auto loaded = load(options);
        if (!loaded.success) return loaded;

        m_ioTimeoutMs = std::clamp(Plugin::numberValue(options, "ioTimeoutMs", 5000), 1, 600000);
        m_commandDelayMs = std::clamp(Plugin::numberValue(options, "commandDelayMs", 20), 0, 10000);
        m_readBufferSize = std::clamp(Plugin::numberValue(options, "readBufferSize", 4096), 64, 65536);
        m_writeTermination = decodeTermination(Plugin::stringValue(options, "writeTermination", "\n"));
        m_readTermination = decodeTermination(Plugin::stringValue(options, "readTermination", "\n"));

        auto status = m_openDefaultRm(&m_resourceManager);
        PicoATE_Log("SCOPE_VISA viOpenDefaultRM status=0x{:08X} rm={}", static_cast<unsigned long>(status), m_resourceManager);
        if (!succeeded(status)) return Result::failed("VisaResourceManagerOpenFailed", error(status));
        status = m_open(m_resourceManager, address.c_str(), VisaNoLock, static_cast<ViUInt32>(m_ioTimeoutMs), &m_instrument);
        PicoATE_Log("SCOPE_VISA viOpen status=0x{:08X} session={}", static_cast<unsigned long>(status), m_instrument);
        if (!succeeded(status)) {
            const auto message = error(status);
            disconnect();
            return Result::failed("VisaInstrumentOpenFailed", message);
        }
        status = m_setAttribute(m_instrument, VisaAttrTimeoutValue, static_cast<ViAttrState>(m_ioTimeoutMs));
        PicoATE_Log("SCOPE_VISA setAttribute(Timeout={}ms) status=0x{:08X}", m_ioTimeoutMs, static_cast<unsigned long>(status));
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
        // Note: no viClear here. On USBTMC it resets the bulk endpoints and
        // makes the next query time out; use the SCPI *CLS via clear() instead.
        PicoATE_Log("SCOPE_VISA connected");
        return Result::passed({{"connected", true}, {"address", address}});
    }

    void disconnect() noexcept
    {
        if (m_instrument && m_close) m_close(m_instrument);
        if (m_instrument || m_resourceManager) {
            PicoATE_Log("SCOPE_VISA disconnect instrument={} rm={}", m_instrument, m_resourceManager);
        }
        m_instrument = 0;
        if (m_resourceManager && m_close) m_close(m_resourceManager);
        m_resourceManager = 0;
    }

    bool connected() const noexcept { return m_instrument != 0; }

    Result write(const std::string& command)
    {
        if (!connected()) return Result::failed("ScopeNotConnected", "VISA session is not open");
        if (command.empty()) return Result::failed("ScpiCommandRequired", "SCPI command is empty");
        const auto payload = command + m_writeTermination;
        ViUInt32 written = 0;
        const auto status = m_write(m_instrument, reinterpret_cast<const ViByte*>(payload.data()),
                                    static_cast<ViUInt32>(payload.size()), &written);
        if (!succeeded(status)) {
            const auto message = error(status);
            PicoATE_Log("SCOPE_VISA write '{}' status=0x{:08X} failed: {}", command, static_cast<unsigned long>(status), message);
            return Result::failed("VisaWriteFailed", message);
        }
        PicoATE_Log("SCOPE_VISA write '{}' bytes={} written={} status=0x{:08X}",
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
                PicoATE_Log("SCOPE_VISA read#{0} status=0x{1:08X} received=0 -> timeout, draining pending",
                            readCount, static_cast<unsigned long>(status));
                drainPending(buffer);
                return Result::failed("VisaReadFailed", error(status));
            }
            response.append(reinterpret_cast<const char*>(buffer.data()), received);
            PicoATE_Log("SCOPE_VISA read#{0} status=0x{1:08X} received={2} total={3}",
                        readCount, static_cast<unsigned long>(status), received, response.size());
            if (status == VisaErrorTimeout || status != VisaSuccessMaxCount || received < buffer.size()) break;
        }
        response = toValidUtf8(trim(sanitize(std::move(response))));
        if (response.empty()) {
            PicoATE_Log("SCOPE_VISA query '{}' -> EMPTY (no data)", command);
            return Result::failed("EmptyInstrumentResponse", command + " returned no data");
        }
        PicoATE_Log("SCOPE_VISA query '{}' -> '{}'", command, response);
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
        if (!end || end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
            PicoATE_Log("SCOPE_VISA numericQuery '{}' -> INVALID '{}'", command, text);
            return Result::failed("InvalidNumericResponse", command + " returned invalid numeric data: " + text);
        }
        PicoATE_Log("SCOPE_VISA numericQuery '{}' -> {}", command, value);
        return Result::passed(value);
    }

    Result clear()
    {
        // Always use the SCPI *CLS (clear error queue / status registers).
        // Do NOT use viClear (VISA device clear): on USBTMC it aborts pending
        // transfers and resets the bulk endpoints, which makes the next query
        // time out on Rigol oscilloscopes (verified: *IDN? after viClear
        // times out, moving clear after the query makes it pass).
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
            PicoATE_Log("SCOPE_VISA load '{}' FAILED (error={})", libraryName, static_cast<unsigned long>(GetLastError()));
            return Result::failed("VisaRuntimeLoadFailed", "Failed to load " + libraryName);
        }
        PicoATE_Log("SCOPE_VISA load '{}' OK", libraryName);
        std::string message;
        if (!(resolve(m_openDefaultRm, "viOpenDefaultRM", message) && resolve(m_open, "viOpen", message) &&
              resolve(m_close, "viClose", message) && resolve(m_setAttribute, "viSetAttribute", message) &&
              resolve(m_write, "viWrite", message) && resolve(m_read, "viRead", message))) {
            PicoATE_Log("SCOPE_VISA symbol resolve FAILED: {}", message);
            FreeLibrary(m_library);
            m_library = nullptr;
            return Result::failed("VisaSymbolMissing", message);
        }
        m_statusDesc = reinterpret_cast<ViStatusDesc>(GetProcAddress(m_library, "viStatusDesc"));
        PicoATE_Log("SCOPE_VISA symbols resolved (viStatusDesc={})",
                    m_statusDesc ? "yes" : "no");
        return Result::passed();
    }

    std::string error(ViStatus status) const
    {
        // NOTE: viStatusDesc returns locale-dependent text (a Chinese NI-VISA
        // build returns GBK-encoded Chinese), which is invalid UTF-8 and makes
        // nlohmann::json::dump throw (PicoATE_Execute returns 4 with an empty
        // response). Use a fixed English description plus the numeric status
        // instead, so errorMessage is always valid UTF-8.
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
                PicoATE_Log("SCOPE_VISA drain finished after {} bytes (attempt {})", flushedTotal, attempt);
                return;
            }
            flushedTotal += static_cast<int>(received);
        }
        PicoATE_Log("SCOPE_VISA drain stopped after {} bytes (attempt limit)", flushedTotal);
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
    ViStatusDesc m_statusDesc = nullptr;
    int m_ioTimeoutMs = 5000;
    int m_commandDelayMs = 20;
    int m_readBufferSize = 4096;
    std::string m_writeTermination = "\n";
    std::string m_readTermination = "\n";
};

// ============================================================================
// SCPI parameter mapping and validation
// ============================================================================

Result validateChannel(int channel)
{
    if (channel < 1 || channel > 4) {
        return Result::failed("InvalidChannel", "Channel must be 1~4");
    }
    return Result::passed();
}

std::string channelToken(int channel)
{
    return std::format("CHANnel{}", channel);
}

// Number formatting: SCPI accepts decimal or scientific notation; %g prints the shortest form
std::string formatNumber(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

Result invalidMode(const char* code, const char* message)
{
    return Result::failed(code, message);
}

std::string_view couplingToken(int coupling)
{
    switch (coupling) {
    case CouplingAc: return "AC";
    case CouplingDc: return "DC";
    case CouplingGnd: return "GND";
    }
    return {};
}

std::string_view triggerCouplingToken(int coupling)
{
    switch (coupling) {
    case TriggerCouplingAc: return "AC";
    case TriggerCouplingDc: return "DC";
    case TriggerCouplingLfReject: return "LFReject";
    case TriggerCouplingHfReject: return "HFReject";
    }
    return {};
}

std::string_view slopeToken(int slope)
{
    switch (slope) {
    case EdgeRising: return "POSitive";
    case EdgeFalling: return "NEGative";
    case EdgeEither: return "RFALl";
    }
    return {};
}

std::string_view sweepToken(int sweep)
{
    switch (sweep) {
    case SweepAuto: return "AUTO";
    case SweepNormal: return "NORMal";
    case SweepSingle: return "SINGle";
    }
    return {};
}

std::string_view triggerModeToken(int mode)
{
    switch (mode) {
    case TriggerEdge: return "EDGE";
    case TriggerPulse: return "PULSe";
    case TriggerRunt: return "RUNT";
    case TriggerWindow: return "WIND";
    case TriggerSlope: return "SLOPe";
    case TriggerVideo: return "VIDeo";
    case TriggerPattern: return "PATTern";
    case TriggerDelay: return "DELay";
    case TriggerTimeout: return "TIMeout";
    case TriggerDuration: return "DURation";
    case TriggerHold: return "SHOLd";
    case TriggerRs232: return "RS232";
    case TriggerI2C: return "IIC";
    case TriggerSpi: return "SPI";
    }
    return {};
}

std::string_view bandwidthToken(int limit)
{
    switch (limit) {
    case BandwidthOff: return "OFF";
    case Bandwidth20M: return "20M";
    }
    return {};
}

} // namespace

class RigolScopeAdapter::Impl
{
public:
    VisaScpiSession session;
};

RigolScopeAdapter::RigolScopeAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

RigolScopeAdapter::~RigolScopeAdapter() = default;

Result RigolScopeAdapter::connect(const std::string& visaAddress, const Plugin::Json& options)
{
    return m_impl->session.connect(visaAddress, options);
}

void RigolScopeAdapter::disconnect() noexcept
{
    m_impl->session.disconnect();
}

bool RigolScopeAdapter::isConnected() const noexcept
{
    return m_impl->session.connected();
}

Result RigolScopeAdapter::identity()
{
    return m_impl->session.query("*IDN?");
}

Result RigolScopeAdapter::reset()
{
    return m_impl->session.write("*RST");
}

Result RigolScopeAdapter::clear()
{
    return m_impl->session.clear();
}

Result RigolScopeAdapter::lockFrontPanel(int lock)
{
    if (lock != 0 && lock != 1) {
        return invalidMode("InvalidLockValue", "lock must be 0 (unlock) or 1 (lock)");
    }
    return m_impl->session.write(lock ? ":SYSTem:LOCKed ON" : ":SYSTem:LOCKed OFF");
}

Result RigolScopeAdapter::run()
{
    return m_impl->session.write(":RUN");
}

Result RigolScopeAdapter::stop()
{
    return m_impl->session.write(":STOP");
}

Result RigolScopeAdapter::single()
{
    return m_impl->session.write(":SINGle");
}

Result RigolScopeAdapter::forceTrigger()
{
    return m_impl->session.write(":TFORce");
}

Result RigolScopeAdapter::enableChannel(int channel, int enable)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    if (enable != 0 && enable != 1) {
        return invalidMode("InvalidEnableValue", "enable must be 0 or 1");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:DISPlay {}", channel, enable ? "ON" : "OFF"));
}

Result RigolScopeAdapter::setChannelCoupling(int channel, int coupling)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    const auto token = couplingToken(coupling);
    if (token.empty()) {
        return invalidMode("InvalidCoupling", "coupling must be 0=AC 1=DC 2=GND");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:COUPling {}", channel, token));
}

Result RigolScopeAdapter::setProbe(int channel, double attenuation)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    if (!std::isfinite(attenuation) || attenuation < 0.01 || attenuation > 1000.0) {
        return invalidMode("InvalidProbeAttenuation", "attenuation must be 0.01~1000");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:PROBe {}", channel, formatNumber(attenuation)));
}

Result RigolScopeAdapter::setVoltageDiv(int channel, double voltsPerDiv)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    if (!std::isfinite(voltsPerDiv) || voltsPerDiv <= 0.0) {
        return invalidMode("InvalidVoltageDiv", "voltsPerDiv must be positive");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:SCALe {}", channel, formatNumber(voltsPerDiv)));
}

Result RigolScopeAdapter::setChannelOffset(int channel, double offset)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    if (!std::isfinite(offset)) {
        return invalidMode("InvalidOffset", "offset must be a finite number");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:OFFSet {}", channel, formatNumber(offset)));
}

Result RigolScopeAdapter::setInvert(int channel, int invert)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    if (invert != 0 && invert != 1) {
        return invalidMode("InvalidInvertValue", "invert must be 0 or 1");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:INVert {}", channel, invert ? "ON" : "OFF"));
}

Result RigolScopeAdapter::setBandwidthLimit(int channel, int limit)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    const auto token = bandwidthToken(limit);
    if (token.empty()) {
        return invalidMode("InvalidBandwidthLimit", "limit must be 0=OFF 1=20M");
    }
    return m_impl->session.write(
        std::format(":CHANnel{}:BWLimit {}", channel, token));
}

Result RigolScopeAdapter::setTimebaseScale(double secondsPerDiv)
{
    if (!std::isfinite(secondsPerDiv) || secondsPerDiv <= 0.0) {
        return invalidMode("InvalidTimebaseScale", "secondsPerDiv must be positive");
    }
    return m_impl->session.write(
        std::format(":TIMebase:MAIN:SCALe {}", formatNumber(secondsPerDiv)));
}

Result RigolScopeAdapter::setTriggerMode(int mode)
{
    const auto token = triggerModeToken(mode);
    if (token.empty()) {
        return invalidMode("InvalidTriggerMode", "mode must be 0~13 (0=EDGE)");
    }
    return m_impl->session.write(std::format(":TRIGger:MODE {}", token));
}

Result RigolScopeAdapter::setTriggerSweep(int sweep)
{
    const auto token = sweepToken(sweep);
    if (token.empty()) {
        return invalidMode("InvalidTriggerSweep", "sweep must be 0=AUTO 1=NORMal 2=SINGle");
    }
    return m_impl->session.write(std::format(":TRIGger:SWEep {}", token));
}

Result RigolScopeAdapter::setTriggerCoupling(int coupling)
{
    const auto token = triggerCouplingToken(coupling);
    if (token.empty()) {
        return invalidMode("InvalidTriggerCoupling", "coupling must be 0=AC 1=DC 2=LFReject 3=HFReject");
    }
    return m_impl->session.write(std::format(":TRIGger:COUPling {}", token));
}

Result RigolScopeAdapter::setEdgeTriggerSource(int channel)
{
    if (const auto check = validateChannel(channel); !check.success) return check;
    return m_impl->session.write(
        std::format(":TRIGger:EDGe:SOURce {}", channelToken(channel)));
}

Result RigolScopeAdapter::setEdgeTriggerLevel(double level)
{
    if (!std::isfinite(level)) {
        return invalidMode("InvalidTriggerLevel", "level must be a finite number");
    }
    return m_impl->session.write(
        std::format(":TRIGger:EDGe:LEVel {}", formatNumber(level)));
}

Result RigolScopeAdapter::setEdgeTriggerSlope(int slope)
{
    const auto token = slopeToken(slope);
    if (token.empty()) {
        return invalidMode("InvalidTriggerSlope", "slope must be 0=Rising 1=Falling 2=Either");
    }
    return m_impl->session.write(std::format(":TRIGger:EDGe:SLOPe {}", token));
}

Result RigolScopeAdapter::measure(const std::string& item, int source, int source2)
{
    if (item.empty()) {
        return Result::failed("MeasureItemRequired", "measure item is empty");
    }
    if (source > 0) {
        if (const auto check = validateChannel(source); !check.success) return check;
    }
    if (source2 > 0) {
        if (const auto check = validateChannel(source2); !check.success) return check;
    }

    std::string command = ":MEASure:ITEM? ";
    command += item;
    if (source > 0) {
        command += "," + channelToken(source);
        if (source2 > 0) {
            command += "," + channelToken(source2);
        }
    }
    return m_impl->session.numericQuery(command);
}

Result RigolScopeAdapter::autoScale()
{
    return m_impl->session.write(":AUToscale");
}

Result RigolScopeAdapter::query(const std::string& command)
{
    return m_impl->session.query(command);
}

Result RigolScopeAdapter::write(const std::string& command)
{
    return m_impl->session.write(command);
}

std::unique_ptr<IScopeAdapter> createScopeAdapter()
{
    return std::make_unique<RigolScopeAdapter>();
}

} // namespace PicoATE::Plugins::Scope
