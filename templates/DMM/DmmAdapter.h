#pragma once

#include "PicoATE/Plugin/PluginAbi.h"

#include <memory>
#include <string>
#include <utility>

namespace PicoATE::Plugins::Dmm {

struct Result {
    bool success = true;
    std::string errorCode;
    std::string errorMessage;
    Plugin::Json value;

    static Result passed(Plugin::Json value = {})
    {
        return {true, {}, {}, std::move(value)};
    }

    static Result failed(std::string code, std::string message)
    {
        return {false, std::move(code), std::move(message), {}};
    }
};

enum class MeasurementMode {
    Dcv,
    Acv,
    Dci,
    Aci,
    Resistance2W,
    Resistance4W,
    Frequency,
    Period,
    Diode,
    Continuity,
    Capacitance,
};

class IDmmAdapter
{
public:
    virtual ~IDmmAdapter() = default;

    virtual Result connect(const std::string& visaAddress,
                           const Plugin::Json& options) = 0;
    virtual void disconnect() noexcept = 0;
    virtual bool isConnected() const noexcept = 0;
    virtual Result identity() = 0;
    virtual Result reset() = 0;
    virtual Result clear() = 0;
    virtual Result configure(MeasurementMode mode, double range, double integration) = 0;
    virtual Result read() = 0;
    virtual Result query(const std::string& command) = 0;
    virtual Result write(const std::string& command) = 0;
};

std::unique_ptr<IDmmAdapter> createDmmAdapter();
Plugin::Json pluginDescription();

} // namespace PicoATE::Plugins::Dmm