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
    OperationResult close(const OpenOptions& options) override;
    bool isOpen(const OpenOptions& options) const noexcept override;
    std::string deviceDescription(const OpenOptions& options) const override;
    OperationResult transmit(const OpenOptions& options,
                             const Frame& frame) override;
    ReceiveResult receive(const OpenOptions& options,
                          std::uint32_t filterId,
                          std::uint32_t filterMask,
                          int timeoutMs) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PicoATE::Plugins::Can
