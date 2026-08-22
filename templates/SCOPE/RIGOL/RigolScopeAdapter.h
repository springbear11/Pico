#pragma once

#include "ScopeAdapter.h"

#include <memory>

namespace PicoATE::Plugins::Scope {

// Rigol DS1000Z series (DS1104Z Plus / DS1074Z Plus / DS1054Z) implementation.
// Uses SCPI over VISA, per the Rigol DS1000Z series programming manual.
class RigolScopeAdapter final : public IScopeAdapter
{
public:
    RigolScopeAdapter();
    ~RigolScopeAdapter() override;

    Result connect(const std::string& visaAddress, const Plugin::Json& options) override;
    void disconnect() noexcept override;
    bool isConnected() const noexcept override;

    Result identity() override;
    Result reset() override;
    Result clear() override;
    Result lockFrontPanel(int lock) override;

    Result run() override;
    Result stop() override;
    Result single() override;
    Result forceTrigger() override;

    Result enableChannel(int channel, int enable) override;
    Result setChannelCoupling(int channel, int coupling) override;
    Result setProbe(int channel, double attenuation) override;
    Result setVoltageDiv(int channel, double voltsPerDiv) override;
    Result setChannelOffset(int channel, double offset) override;
    Result setInvert(int channel, int invert) override;
    Result setBandwidthLimit(int channel, int limit) override;

    Result setTimebaseScale(double secondsPerDiv) override;

    Result setTriggerMode(int mode) override;
    Result setTriggerSweep(int sweep) override;
    Result setTriggerCoupling(int coupling) override;
    Result setEdgeTriggerSource(int channel) override;
    Result setEdgeTriggerLevel(double level) override;
    Result setEdgeTriggerSlope(int slope) override;

    Result measure(const std::string& item, int source, int source2) override;

    Result autoScale() override;

    Result query(const std::string& command) override;
    Result write(const std::string& command) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PicoATE::Plugins::Scope
