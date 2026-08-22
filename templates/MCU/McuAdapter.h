#pragma once

#include "PicoATE/Plugin/PluginAbi.h"

#include <cstdint>
#include <memory>
#include <string>

namespace PicoATE::Plugins::Mcu {

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

class IMcuAdapter
{
public:
    virtual ~IMcuAdapter() = default;

    virtual Result open(const std::string& endpoint,
                        std::uint8_t defaultUnitId,
                        int ioTimeoutMs) = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;

    virtual Result readAllIo(std::uint8_t unitId) = 0;
    virtual Result readIoBank(std::uint8_t unitId, std::uint8_t bank) = 0;
    virtual Result writeIoBank(std::uint8_t unitId,
                               std::uint8_t bank,
                               std::uint16_t value) = 0;
    virtual Result setIo(std::uint8_t unitId,
                         std::uint16_t channel,
                         bool state) = 0;
    virtual Result initializeAllIo(std::uint8_t initializationUnitId) = 0;
};

std::unique_ptr<IMcuAdapter> createMcuAdapter();
Plugin::Json pluginDescription();

} // namespace PicoATE::Plugins::Mcu
