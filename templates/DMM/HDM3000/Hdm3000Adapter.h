#pragma once

#include "DmmAdapter.h"

#include <memory>

namespace PicoATE::Plugins::Dmm {

class Hdm3000Adapter final : public IDmmAdapter
{
public:
    Hdm3000Adapter();
    ~Hdm3000Adapter() override;

    Result connect(const std::string& visaAddress, const Plugin::Json& options) override;
    void disconnect() noexcept override;
    bool isConnected() const noexcept override;
    Result identity() override;
    Result reset() override;
    Result clear() override;
    Result configure(MeasurementMode mode, double range, double integration) override;
    Result read() override;
    Result query(const std::string& command) override;
    Result write(const std::string& command) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PicoATE::Plugins::Dmm