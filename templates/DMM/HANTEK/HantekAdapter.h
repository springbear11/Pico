#pragma once

#include "DmmAdapter.h"

#include <memory>

namespace PicoATE::Plugins::Dmm {

class HantekAdapter final : public IDmmAdapter
{
public:
    HantekAdapter();
    ~HantekAdapter() override;

    Result connect(const std::string& visaAddress, const Plugin::Json& options) override;
    void disconnect() noexcept override;
    bool isConnected() const noexcept override;
    Result identity() override;
    Result reset() override;
    Result clear() override;
    Result configure(MeasurementMode mode, double range, double integration,
                     double resolution = 0.0) override;
    Result read() override;
    Result query(const std::string& command) override;
    Result write(const std::string& command) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PicoATE::Plugins::Dmm