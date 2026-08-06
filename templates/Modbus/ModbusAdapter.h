#pragma once

#include "PicoATE/Plugin/PluginAbi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PicoATE::Plugins::Modbus {

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

class IModbusAdapter
{
public:
    virtual ~IModbusAdapter() = default;

    virtual Result connect(const std::string& endpoint,
                           int slaveId,
                           const Plugin::Json& options) = 0;
    virtual void disconnect() noexcept = 0;
    virtual bool isConnected() const noexcept = 0;
    virtual Result readCoils(std::uint8_t unitId,
                             std::uint16_t address,
                             std::uint16_t count) = 0;
    virtual Result readDiscreteInputs(std::uint8_t unitId,
                                      std::uint16_t address,
                                      std::uint16_t count) = 0;
    virtual Result readHoldingRegisters(std::uint8_t unitId,
                                        std::uint16_t address,
                                        std::uint16_t count) = 0;
    virtual Result readInputRegisters(std::uint8_t unitId,
                                      std::uint16_t address,
                                      std::uint16_t count) = 0;
    virtual Result writeSingleCoil(std::uint8_t unitId,
                                   std::uint16_t address,
                                   bool value) = 0;
    virtual Result writeSingleRegister(std::uint8_t unitId,
                                       std::uint16_t address,
                                       std::uint16_t value) = 0;
    virtual Result writeMultipleCoils(std::uint8_t unitId,
                                      std::uint16_t address,
                                      const std::vector<bool>& values) = 0;
    virtual Result writeMultipleRegisters(std::uint8_t unitId,
                                          std::uint16_t address,
                                          const std::vector<std::uint16_t>& values) = 0;
};

std::unique_ptr<IModbusAdapter> createModbusAdapter();
Plugin::Json pluginDescription();

} // namespace PicoATE::Plugins::Modbus
