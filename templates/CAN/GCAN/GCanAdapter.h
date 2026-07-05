#pragma once

#include "CanAdapter.h"

#include <memory>
#include <string>

namespace PicoATE::Plugins::Can {

class GCanAdapter final : public ICanAdapter
{
public:
    GCanAdapter();
    ~GCanAdapter() override;

    OperationResult open(const OpenOptions& options) override;
    void close() noexcept override;
    bool isOpen() const noexcept override;
    std::string deviceDescription() const override;
    OperationResult transmit(const Frame& frame) override;
    ReceiveResult receive(std::uint32_t filterId,
                          std::uint32_t filterMask,
                          int timeoutMs) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PicoATE::Plugins::Can
