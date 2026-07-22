#pragma once

#include "PowerSupplyAdapter.h"

#include <memory>

namespace PicoATE::Plugins::PowerSupply {

class KoradPowerSupplyAdapter final : public IPowerSupplyAdapter
{
public:
    KoradPowerSupplyAdapter();
    ~KoradPowerSupplyAdapter() override;

    Result connect(const std::string& visaAddress,
                   const Plugin::Json& options) override;
    void disconnect() noexcept override;
    bool isConnected() const noexcept override;
    Result identity() override;
    Result setCurrent(int channel, double amperes) override;
    Result currentSetpoint(int channel) override;
    Result setVoltage(int channel, double volts) override;
    Result voltageSetpoint(int channel) override;
    Result outputCurrent(int channel) override;
    Result outputVoltage(int channel) override;
    Result setBeep(bool enabled) override;
    Result setOutput(bool enabled) override;
    Result status() override;
    Result recall(int slot) override;
    Result save(int slot) override;
    Result setOcp(bool enabled) override;
    Result setOvp(bool enabled) override;
    Result setLock(bool enabled) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PicoATE::Plugins::PowerSupply
