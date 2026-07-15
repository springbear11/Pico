#pragma once

#include "CanAdapter.h"

#include <memory>

namespace PicoATE::Plugins::Can {

class CxCanAdapter final : public ICanAdapter
{
public:
    CxCanAdapter();
    ~CxCanAdapter() override;

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
