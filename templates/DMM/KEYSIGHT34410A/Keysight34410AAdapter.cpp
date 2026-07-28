#include "Keysight34410AAdapter.h"

#include "PicoATE/Plugin/PluginLog.h"
#include "ScpiVisaSession.h"

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace PicoATE::Plugins::Dmm {

Plugin::Json pluginDescription()
{
    return Plugin::Json::parse(R"json(
{
  "schema": "picoate.plugin",
  "schemaVersion": 1,
  "pluginId": "picoate.dmm.keysight34410a",
  "moduleId": "plugin.dmm.keysight34410a",
  "name": "Keysight KEYSIGHT34410A VISA DMM",
  "category": "DMM",
  "connectionKinds": ["visa"],
  "vendor": "Keysight",
  "version": "1.0.0",
  "functions": [
    {
      "id": "open",
      "name": "Open Keysight 34410A",
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
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "V"
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "number",
          "default": 10.0,
          "minimum": 0.02,
          "maximum": 100.0
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
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "V"
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "number",
          "default": 10.0,
          "minimum": 0.02,
          "maximum": 100.0
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
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "A"
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "number",
          "default": 10.0,
          "minimum": 0.02,
          "maximum": 100.0
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
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "A"
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "number",
          "default": 10.0,
          "minimum": 0.02,
          "maximum": 100.0
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
      "id": "configureResistance2w",
      "name": "Configure 2-Wire Resistance",
      "description": "CONF:RES and SENS:RES:NPLC",
      "timeoutMs": 5000,
      "inputs": [
        {
          "key": "range",
          "name": "Range",
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "Ohm"
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "number",
          "default": 10.0,
          "minimum": 0.02,
          "maximum": 100.0
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
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "Ohm"
        },
        {
          "key": "nplc",
          "name": "NPLC",
          "type": "number",
          "default": 10.0,
          "minimum": 0.02,
          "maximum": 100.0
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
          "key": "range",
          "name": "Range",
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "Hz"
        },
        {
          "key": "aperture",
          "name": "Aperture",
          "type": "number",
          "default": 0.1,
          "minimum": 0.01,
          "maximum": 1.0,
          "unit": "s"
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
          "key": "range",
          "name": "Range",
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "s"
        },
        {
          "key": "aperture",
          "name": "Aperture",
          "type": "number",
          "default": 0.1,
          "minimum": 0.01,
          "maximum": 1.0,
          "unit": "s"
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
          "type": "number",
          "default": 0.0,
          "minimum": 0.0,
          "unit": "F"
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
      "name": "Close Keysight 34410A",
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

class Keysight34410AAdapter::Impl
{
public:
    Detail::VisaScpiSession session;
};

Keysight34410AAdapter::Keysight34410AAdapter()
    : m_impl(std::make_unique<Impl>())
{
}

Keysight34410AAdapter::~Keysight34410AAdapter() = default;

Result Keysight34410AAdapter::connect(const std::string& visaAddress, const Plugin::Json& options)
{
    return m_impl->session.connect(visaAddress, options);
}

void Keysight34410AAdapter::disconnect() noexcept
{
    m_impl->session.disconnect();
}

bool Keysight34410AAdapter::isConnected() const noexcept
{
    return m_impl->session.connected();
}

Result Keysight34410AAdapter::identity()
{
    return m_impl->session.query("*IDN?");
}

Result Keysight34410AAdapter::reset()
{
    return m_impl->session.write("*RST");
}

Result Keysight34410AAdapter::clear()
{
    return m_impl->session.clear();
}

Result Keysight34410AAdapter::configure(MeasurementMode mode, double range, double integration)
{
    const auto configuration = Detail::commandFor(mode, range);
    if (configuration.empty()) return Result::failed("UnsupportedMeasurementMode", "Keysight 34410A mode is not supported");
    auto result = m_impl->session.write(configuration);
    if (!result.success) return result;

    const auto integrationCommand = Detail::integrationCommand(mode, integration);
    if (!integrationCommand.empty()) {
        result = m_impl->session.write(integrationCommand);
        if (!result.success) return result;
    }
    PicoATE_Log("KEYSIGHT34410A_CONFIG command={} integration={}", configuration, integration);
    return Result::passed();
}

Result Keysight34410AAdapter::read()
{
    return m_impl->session.numericQuery("READ?");
}

Result Keysight34410AAdapter::query(const std::string& command)
{
    return m_impl->session.query(command);
}

Result Keysight34410AAdapter::write(const std::string& command)
{
    return m_impl->session.write(command);
}

std::unique_ptr<IDmmAdapter> createDmmAdapter()
{
    return std::make_unique<Keysight34410AAdapter>();
}


} // namespace PicoATE::Plugins::Dmm
