#pragma once

#include "PicoATE/Plugin/PluginAbi.h"

#include <memory>
#include <string>
#include <utility>

namespace PicoATE::Plugins::PowerSupply {

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

class IPowerSupplyAdapter
{
public:
    virtual ~IPowerSupplyAdapter() = default;
    virtual Result connect(const std::string& visaAddress,
                           const Plugin::Json& options) = 0;
    virtual void disconnect() noexcept = 0;
    virtual bool isConnected() const noexcept = 0;
    virtual Result identity() = 0;
    virtual Result setCurrent(int channel, double amperes) = 0;
    virtual Result currentSetpoint(int channel) = 0;
    virtual Result setVoltage(int channel, double volts) = 0;
    virtual Result voltageSetpoint(int channel) = 0;
    virtual Result outputCurrent(int channel) = 0;
    virtual Result outputVoltage(int channel) = 0;
    virtual Result setBeep(bool enabled) = 0;
    virtual Result setOutput(bool enabled) = 0;
    virtual Result status() = 0;
    virtual Result recall(int slot) = 0;
    virtual Result save(int slot) = 0;
    virtual Result setOcp(bool enabled) = 0;
    virtual Result setOvp(bool enabled) = 0;
    virtual Result setLock(bool enabled) = 0;
};

std::unique_ptr<IPowerSupplyAdapter> createPowerSupplyAdapter();
Plugin::Json pluginDescription();

} // namespace PicoATE::Plugins::PowerSupply
