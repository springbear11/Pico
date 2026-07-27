#pragma once

#include "PicoATE/Plugin/PluginAbi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PicoATE::Plugins::Can {

struct OpenOptions {
    std::string libraryPath;
    int deviceType = 0;
    int deviceIndex = 0;
    int channelIndex = 0;
    int arbitrationBitrate = 500000;
    int dataBitrate = 2000000;
    bool canFd = false;
    bool listenOnly = false;
    bool selfTest = false;
    Plugin::Json vendorOptions = Plugin::Json::object();
};

struct Frame {
    std::uint32_t id = 0;
    std::vector<std::uint8_t> data;
    bool extended = false;
    bool remote = false;
    bool canFd = false;
    bool bitrateSwitch = false;
    std::uint64_t timestampUs = 0;
};

struct OperationResult {
    bool success = true;
    std::string errorCode;
    std::string errorMessage;

    static OperationResult passed() { return {}; }
    static OperationResult failed(std::string code, std::string message)
    {
        return {false, std::move(code), std::move(message)};
    }
};

struct DiscoveryOptions {
    std::string libraryPath;
    int deviceType = 0;
    int maximumDeviceIndex = 15;
};

struct DiscoveredCanDevice {
    std::string serialNumber;
    std::string model;
    int deviceType = 0;
    int deviceIndex = 0;
    int channelCount = 0;
};

struct DiscoveryResult {
    OperationResult status;
    std::vector<DiscoveredCanDevice> devices;
};

enum class ReceiveStatus {
    Received,
    Timeout,
    Error
};

struct ReceiveResult {
    ReceiveStatus status = ReceiveStatus::Error;
    Frame frame;
    std::string errorCode;
    std::string errorMessage;
};

class ICanAdapter
{
public:
    virtual ~ICanAdapter() = default;

    virtual DiscoveryResult findDevices(const DiscoveryOptions& options) = 0;
    virtual OperationResult open(const OpenOptions& options) = 0;
    virtual OperationResult close(const OpenOptions& options) = 0;
    virtual bool isOpen(const OpenOptions& options) const noexcept = 0;
    virtual std::string deviceDescription(const OpenOptions& options) const = 0;
    virtual OperationResult transmit(const OpenOptions& options,
                                     const Frame& frame) = 0;
    virtual ReceiveResult receive(const OpenOptions& options,
                                  std::uint32_t filterId,
                                  std::uint32_t filterMask,
                                  int timeoutMs) = 0;
};

// Each CAN/<Vendor> implementation provides this one factory function.
std::unique_ptr<ICanAdapter> createCanAdapter();

// Each concrete vendor plugin describes only its own functions and parameters.
Plugin::Json pluginDescription();

} // namespace PicoATE::Plugins::Can
